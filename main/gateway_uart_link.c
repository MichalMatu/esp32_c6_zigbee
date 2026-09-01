#include "gateway_uart_link.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "gateway_link_control.h"
#include "gateway_link_event_adapter.h"
#include "gateway_link_protocol.h"
#include "gateway_link_snapshot_cache.h"
#include "gateway_link_stream.h"
#include "zigbee_gateway.h"

#define LINK_UART UART_NUM_1
#define LINK_TX_QUEUE_DEPTH 16U
#define LINK_UART_RX_BUFFER_BYTES 1024
#define LINK_UART_TX_BUFFER_BYTES 1024
#define LINK_TX_TASK_STACK_BYTES 4096U
#define LINK_TX_TASK_PRIORITY 4U
#define LINK_RX_TASK_STACK_BYTES 4096U
#define LINK_RX_TASK_PRIORITY 4U
#define LINK_RX_READ_BYTES 64U

static const char *TAG = "gateway_uart_link";

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

uint32_t gateway_uart_link_take_dropped(void)
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

void gateway_uart_link_publish_event(const gateway_event_t *event)
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
        const int count = uart_read_bytes(
            LINK_UART, bytes, sizeof(bytes), pdMS_TO_TICKS(100));
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
    const int written = uart_write_bytes(LINK_UART, encoded, encoded_length);
    if (written != (int)encoded_length) {
        ESP_LOGW(TAG, "short UART write seq=%lu wrote=%d expected=%u",
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

esp_err_t gateway_uart_link_start(void)
{
    if (s_tx_queue != NULL) {
        return ESP_OK;
    }

    gateway_link_snapshot_cache_init(&s_snapshot_cache);

    s_tx_queue = xQueueCreateStatic(
        LINK_TX_QUEUE_DEPTH,
        sizeof(tx_item_t),
        s_tx_queue_storage,
        &s_tx_queue_buffer);
    if (s_tx_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const uart_config_t config = {
        .baud_rate = GATEWAY_UART_LINK_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t result = uart_driver_install(
        LINK_UART,
        LINK_UART_RX_BUFFER_BYTES,
        LINK_UART_TX_BUFFER_BYTES,
        0,
        NULL,
        0);
    if (result != ESP_OK) {
        s_tx_queue = NULL;
        return result;
    }
    result = uart_param_config(LINK_UART, &config);
    if (result != ESP_OK) {
        uart_driver_delete(LINK_UART);
        s_tx_queue = NULL;
        return result;
    }
    result = uart_set_pin(
        LINK_UART,
        GATEWAY_UART_LINK_TX_GPIO,
        GATEWAY_UART_LINK_RX_GPIO,
        UART_PIN_NO_CHANGE,
        UART_PIN_NO_CHANGE);
    if (result != ESP_OK) {
        uart_driver_delete(LINK_UART);
        s_tx_queue = NULL;
        return result;
    }
    (void)gpio_set_pull_mode((gpio_num_t)GATEWAY_UART_LINK_RX_GPIO, GPIO_PULLUP_ONLY);

    if (xTaskCreate(tx_task, "gateway_uart_tx", LINK_TX_TASK_STACK_BYTES, NULL,
                    LINK_TX_TASK_PRIORITY, &s_tx_task_handle) != pdPASS) {
        uart_driver_delete(LINK_UART);
        s_tx_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(rx_task, "gateway_uart_rx", LINK_RX_TASK_STACK_BYTES, NULL,
                    LINK_RX_TASK_PRIORITY, &s_rx_task_handle) != pdPASS) {
        vTaskDelete(s_tx_task_handle);
        s_tx_task_handle = NULL;
        uart_driver_delete(LINK_UART);
        s_tx_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    gateway_link_message_t hello;
    if (!gateway_link_make_hello_message(&hello) || !enqueue_message(&hello)) {
        ESP_LOGW(TAG, "failed to queue initial GatewayLink HELLO");
    }
    ESP_LOGI(TAG, "GatewayLink UART1 TX=%d RX=%d baud=%d",
             GATEWAY_UART_LINK_TX_GPIO, GATEWAY_UART_LINK_RX_GPIO,
             GATEWAY_UART_LINK_BAUD_RATE);
    return ESP_OK;
}
