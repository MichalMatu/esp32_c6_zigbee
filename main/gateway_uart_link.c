#include "gateway_uart_link.h"

#include "driver/gpio.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"

#define LINK_UART UART_NUM_1
#define LINK_UART_RX_BUFFER_BYTES 1024
#define LINK_UART_TX_BUFFER_BYTES 1024

static bool s_started;

static esp_err_t uart_backend_start(void)
{
    if (s_started) {
        return ESP_OK;
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
        return result;
    }
    result = uart_param_config(LINK_UART, &config);
    if (result != ESP_OK) {
        uart_driver_delete(LINK_UART);
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
        return result;
    }
    (void)gpio_set_pull_mode((gpio_num_t)GATEWAY_UART_LINK_RX_GPIO, GPIO_PULLUP_ONLY);
    s_started = true;
    return ESP_OK;
}

static void uart_backend_stop(void)
{
    if (!s_started) {
        return;
    }
    (void)uart_driver_delete(LINK_UART);
    s_started = false;
}

static int uart_backend_read(uint8_t *buffer, size_t capacity, uint32_t timeout_ms)
{
    if (!s_started || buffer == NULL || capacity == 0U) {
        return -1;
    }
    return uart_read_bytes(LINK_UART, buffer, capacity, pdMS_TO_TICKS(timeout_ms));
}

static int uart_backend_write(const uint8_t *buffer, size_t length)
{
    if (!s_started || buffer == NULL || length == 0U) {
        return -1;
    }
    return uart_write_bytes(LINK_UART, buffer, length);
}

const gateway_link_backend_t *gateway_uart_link_backend(void)
{
    static const gateway_link_backend_t backend = {
        .name = "uart1",
        .start = uart_backend_start,
        .stop = uart_backend_stop,
        .read = uart_backend_read,
        .write = uart_backend_write,
    };
    return &backend;
}
