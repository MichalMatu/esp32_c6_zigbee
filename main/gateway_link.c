#include "gateway_link.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "gateway_link_backend.h"
#include "gateway_link_control.h"
#include "gateway_link_event_adapter.h"
#include "gateway_link_protocol.h"
#include "gateway_link_snapshot_cache.h"
#include "gateway_link_stream.h"
#include "gateway_uart_link.h"
#include "zigbee_gateway.h"

#define LINK_TX_QUEUE_DEPTH 16U
#define LINK_TX_TASK_STACK_BYTES 4096U
#define LINK_TX_TASK_PRIORITY 4U
#define LINK_RX_TASK_STACK_BYTES 4096U
#define LINK_RX_TASK_PRIORITY 4U
#define LINK_RX_READ_BYTES 64U

static const char *TAG = "gateway_link";

typedef enum {
    LINK_TX_ITEM_MESSAGE = 0,
    LINK_TX_ITEM_SNAPSHOT,
} tx_item_kind_t;

typedef struct {
    tx_item_kind_t kind;
    uint32_t sequence;
    uint32_t snapshot_token;
    gateway_link_message_t message;
} tx_item_t;

static StaticQueue_t s_tx_queue_buffer;
static uint8_t s_tx_queue_storage[LINK_TX_QUEUE_DEPTH * sizeof(tx_item_t)];
static QueueHandle_t s_tx_queue;
static uint32_t s_next_sequence = 1U;
static uint32_t s_dropped;
static TaskHandle_t s_tx_task_handle;
static TaskHandle_t s_rx_task_handle;
static portMUX_TYPE s_state_lock = portMUX_INITIALIZER_UNLOCKED;
static gateway_link_snapshot_cache_t s_snapshot_cache;
static const gateway_link_backend_t *s_backend;

static uint32_t allocate_sequence(void)
{
    portENTER_CRITICAL(&s_state_lock);
    const uint32_t value = s_next_sequence++;
    portEXIT_CRITICAL(&s_state_lock);
    return value;
}

static void note_drop(void)
{
    portENTER_CRITICAL(&s_state_lock);
    ++s_dropped;
    portEXIT_CRITICAL(&s_state_lock);
}

uint32_t gateway_link_take_dropped(void)
{
    portENTER_CRITICAL(&s_state_lock);
    const uint32_t value = s_dropped;
    s_dropped = 0U;
    portEXIT_CRITICAL(&s_state_lock);
    return value;
}

static bool enqueue_message(const gateway_link_message_t *message)
{
    if (message == NULL || s_tx_queue == NULL) {
        return false;
    }
    const tx_item_t item = {
        .kind = LINK_TX_ITEM_MESSAGE,
        .sequence = allocate_sequence(),
        .message = *message,
    };
    if (xQueueSend(s_tx_queue, &item, 0U) != pdPASS) {
        note_drop();
        return false;
    }
    return true;
}

static bool enqueue_snapshot(uint32_t token)
{
    if (s_tx_queue == NULL) {
        return false;
    }
    const tx_item_t item = {
        .kind = LINK_TX_ITEM_SNAPSHOT,
        .snapshot_token = token,
    };
    if (xQueueSend(s_tx_queue, &item, 0U) != pdPASS) {
        note_drop();
        return false;
    }
    return true;
}

void gateway_link_publish_event(const gateway_event_t *event)
{
    gateway_link_message_t message;
    if (gateway_link_message_from_event(event, &message)) {
        (void)enqueue_message(&message);
    }
}

static void handle_control_frame(const gateway_link_frame_t *frame)
{
    const gateway_link_control_action_t action = gateway_link_control_parse(frame);
    gateway_link_message_t response;
    switch (action.kind) {
    case GATEWAY_LINK_CONTROL_HELLO:
        if (!gateway_link_control_peer_compatible(&action.peer_hello)) {
            ESP_LOGW(TAG, "incompatible GatewayLink HELLO from peer");
            return;
        }
        ESP_LOGI(TAG, "GatewayLink S3 peer compatible (HELLO)");
        if (gateway_link_make_hello_ack_message(&response)) {
            (void)enqueue_message(&response);
        }
        break;
    case GATEWAY_LINK_CONTROL_HELLO_ACK:
        if (gateway_link_control_peer_compatible(&action.peer_hello)) {
            ESP_LOGI(TAG, "GatewayLink S3 peer ready");
        } else {
            ESP_LOGW(TAG, "incompatible GatewayLink HELLO_ACK from peer");
        }
        break;
    case GATEWAY_LINK_CONTROL_PING:
        if (gateway_link_make_pong_message(action.token, &response)) {
            (void)enqueue_message(&response);
        }
        break;
    case GATEWAY_LINK_CONTROL_SNAPSHOT_REQUEST:
        if (!enqueue_snapshot(action.token)) {
            ESP_LOGW(TAG, "failed to queue GatewayLink snapshot token=%lu",
                     (unsigned long)action.token);
        }
        break;
    case GATEWAY_LINK_CONTROL_PERMIT_JOIN:
    {
        const esp_err_t result = zigbee_gateway_set_permit_join(action.permit_join_seconds);
        const gateway_link_config_status_t status =
            result == ESP_OK ? GATEWAY_LINK_CONFIG_APPLIED : GATEWAY_LINK_CONFIG_ERROR;
        if (gateway_link_make_config_result_message(action.request_id, status, &response)) {
            (void)enqueue_message(&response);
        }
        break;
    }
    case GATEWAY_LINK_CONTROL_MEASUREMENT_POLICY_UNSUPPORTED:
        if (gateway_link_make_config_result_message(
                action.request_id, GATEWAY_LINK_CONFIG_UNSUPPORTED, &response)) {
            (void)enqueue_message(&response);
        }
        break;
    case GATEWAY_LINK_CONTROL_INVALID:
        ESP_LOGW(TAG, "invalid GatewayLink control payload type=0x%02x", frame->type);
        break;
    case GATEWAY_LINK_CONTROL_IGNORE:
    default:
        break;
    }
}

static void rx_task(void *arg)
{
    (void)arg;
    gateway_link_stream_decoder_t decoder;
    gateway_link_stream_init(&decoder);
    uint8_t bytes[LINK_RX_READ_BYTES];
    for (;;) {
        const int count = s_backend->read(bytes, sizeof(bytes), 100U);
        if (count <= 0) {
            continue;
        }
        for (int i = 0; i < count; ++i) {
            gateway_link_frame_t frame = {0};
            gateway_link_result_t decode_result = GATEWAY_LINK_OK;
            const gateway_link_stream_event_t event = gateway_link_stream_feed(
                &decoder, bytes[i], &frame, &decode_result);
            if (event == GATEWAY_LINK_STREAM_FRAME) {
                handle_control_frame(&frame);
            } else if (event == GATEWAY_LINK_STREAM_DROPPED) {
                ESP_LOGW(TAG, "dropped invalid GatewayLink RX frame error=%u",
                         (unsigned)decode_result);
            }
        }
    }
}

static void write_message(const gateway_link_message_t *message, uint32_t sequence)
{
    if (message == NULL) {
        return;
    }
    gateway_link_frame_t frame = {
        .type = message->type,
        .flags = message->flags,
        .sequence = sequence,
        .payload_length = message->payload_length,
    };
    if (frame.payload_length != 0U) {
        memcpy(frame.payload, message->payload, frame.payload_length);
    }
    uint8_t encoded[GATEWAY_LINK_MAX_FRAME_BYTES];
    size_t encoded_length = 0U;
    if (gateway_link_encode_frame(
            &frame, encoded, sizeof(encoded), &encoded_length) != GATEWAY_LINK_OK) {
        ESP_LOGW(TAG, "failed to encode GatewayLink frame type=0x%02x seq=%lu",
                 frame.type, (unsigned long)frame.sequence);
        return;
    }
    const int written = s_backend->write(encoded, encoded_length);
    if (written != (int)encoded_length) {
        ESP_LOGW(TAG, "short backend write seq=%lu wrote=%d expected=%u",
                 (unsigned long)frame.sequence, written, (unsigned)encoded_length);
    }
}

static void transmit_snapshot(uint32_t token)
{
    gateway_link_message_t message;
    if (!gateway_link_make_snapshot_marker_message(
            GATEWAY_LINK_MSG_SNAPSHOT_BEGIN, token, &message)) {
        return;
    }
    write_message(&message, allocate_sequence());

    size_t sent = 0U;
    for (size_t slot = 0U; slot < GATEWAY_LINK_SNAPSHOT_CACHE_CAPACITY; ++slot) {
        gateway_link_input_descriptor_t descriptor;
        const bool present = gateway_link_snapshot_cache_copy_slot(
            &s_snapshot_cache, slot, &descriptor);
        if (!present) {
            continue;
        }
        memset(&message, 0, sizeof(message));
        message.type = GATEWAY_LINK_MSG_INPUT_DESCRIPTOR;
        if (gateway_link_encode_input_descriptor_payload(
                &descriptor, message.payload, sizeof(message.payload),
                &message.payload_length) != GATEWAY_LINK_OK) {
            ESP_LOGW(TAG, "failed to encode snapshot descriptor slot=%u", (unsigned)slot);
            continue;
        }
        write_message(&message, allocate_sequence());
        ++sent;
    }

    if (gateway_link_make_snapshot_marker_message(
            GATEWAY_LINK_MSG_SNAPSHOT_END, token, &message)) {
        write_message(&message, allocate_sequence());
    }
    ESP_LOGI(TAG, "GatewayLink snapshot token=%lu descriptors=%u",
             (unsigned long)token, (unsigned)sent);
}

static void tx_task(void *arg)
{
    (void)arg;
    tx_item_t item;
    for (;;) {
        if (xQueueReceive(s_tx_queue, &item, portMAX_DELAY) != pdPASS) {
            continue;
        }
        if (item.kind == LINK_TX_ITEM_SNAPSHOT) {
            transmit_snapshot(item.snapshot_token);
        } else {
            if (item.message.type == GATEWAY_LINK_MSG_INPUT_DESCRIPTOR) {
                gateway_link_input_descriptor_t descriptor;
                if (gateway_link_decode_input_descriptor_payload(
                        item.message.payload, item.message.payload_length,
                        &descriptor) != GATEWAY_LINK_OK ||
                    !gateway_link_snapshot_cache_update(&s_snapshot_cache, &descriptor)) {
                    note_drop();
                }
            }
            write_message(&item.message, item.sequence);
        }
    }
}

esp_err_t gateway_link_start(void)
{
    if (s_tx_queue != NULL) {
        return ESP_OK;
    }

    s_backend = gateway_uart_link_backend();
    if (s_backend == NULL || s_backend->start == NULL || s_backend->stop == NULL ||
        s_backend->read == NULL || s_backend->write == NULL) {
        s_backend = NULL;
        return ESP_ERR_INVALID_STATE;
    }

    gateway_link_snapshot_cache_init(&s_snapshot_cache);
    s_tx_queue = xQueueCreateStatic(
        LINK_TX_QUEUE_DEPTH,
        sizeof(tx_item_t),
        s_tx_queue_storage,
        &s_tx_queue_buffer);
    if (s_tx_queue == NULL) {
        s_backend = NULL;
        return ESP_ERR_NO_MEM;
    }

    const esp_err_t backend_result = s_backend->start();
    if (backend_result != ESP_OK) {
        s_tx_queue = NULL;
        s_backend = NULL;
        return backend_result;
    }

    if (xTaskCreate(tx_task, "gateway_link_tx", LINK_TX_TASK_STACK_BYTES, NULL,
                    LINK_TX_TASK_PRIORITY, &s_tx_task_handle) != pdPASS) {
        s_backend->stop();
        s_tx_queue = NULL;
        s_backend = NULL;
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(rx_task, "gateway_link_rx", LINK_RX_TASK_STACK_BYTES, NULL,
                    LINK_RX_TASK_PRIORITY, &s_rx_task_handle) != pdPASS) {
        vTaskDelete(s_tx_task_handle);
        s_tx_task_handle = NULL;
        s_backend->stop();
        s_tx_queue = NULL;
        s_backend = NULL;
        return ESP_ERR_NO_MEM;
    }

    gateway_link_message_t hello;
    if (!gateway_link_make_hello_message(&hello) || !enqueue_message(&hello)) {
        ESP_LOGW(TAG, "failed to queue initial GatewayLink HELLO");
    }
    ESP_LOGI(TAG, "GatewayLink backend=%s", s_backend->name != NULL ? s_backend->name : "unknown");
    return ESP_OK;
}
