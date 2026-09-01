#include "gateway_uart_link.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "gateway_link_event_adapter.h"
#include "gateway_link_protocol.h"

#define LINK_UART UART_NUM_1
#define LINK_TX_QUEUE_DEPTH 16U
#define LINK_UART_RX_BUFFER_BYTES 1024
#define LINK_UART_TX_BUFFER_BYTES 1024
#define LINK_TX_TASK_STACK_BYTES 4096U
#define LINK_TX_TASK_PRIORITY 4U

static const char *TAG = "gateway_uart_link";

typedef struct {
    uint32_t sequence;
    gateway_link_message_t message;
} tx_item_t;

static StaticQueue_t s_tx_queue_buffer;
static uint8_t s_tx_queue_storage[LINK_TX_QUEUE_DEPTH * sizeof(tx_item_t)];
static QueueHandle_t s_tx_queue;
static uint32_t s_next_sequence = 1U;
static uint32_t s_dropped;
static portMUX_TYPE s_state_lock = portMUX_INITIALIZER_UNLOCKED;

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
        .sequence = allocate_sequence(),
        .message = *message,
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

static void tx_task(void *arg)
{
    (void)arg;
    tx_item_t item;
    uint8_t encoded[GATEWAY_LINK_MAX_FRAME_BYTES];
    for (;;) {
        if (xQueueReceive(s_tx_queue, &item, portMAX_DELAY) != pdPASS) {
            continue;
        }
        gateway_link_frame_t frame = {
            .type = item.message.type,
            .flags = item.message.flags,
            .sequence = item.sequence,
            .payload_length = item.message.payload_length,
        };
        if (frame.payload_length != 0U) {
            memcpy(frame.payload, item.message.payload, frame.payload_length);
        }
        size_t encoded_length = 0U;
        if (gateway_link_encode_frame(
                &frame, encoded, sizeof(encoded), &encoded_length) != GATEWAY_LINK_OK) {
            ESP_LOGW(TAG, "failed to encode GatewayLink frame type=0x%02x seq=%lu",
                     frame.type, (unsigned long)frame.sequence);
            continue;
        }
        const int written = uart_write_bytes(LINK_UART, encoded, encoded_length);
        if (written != (int)encoded_length) {
            ESP_LOGW(TAG, "short UART write seq=%lu wrote=%d expected=%u",
                     (unsigned long)frame.sequence, written, (unsigned)encoded_length);
        }
    }
}

esp_err_t gateway_uart_link_start(void)
{
    if (s_tx_queue != NULL) {
        return ESP_OK;
    }

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
                    LINK_TX_TASK_PRIORITY, NULL) != pdPASS) {
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
