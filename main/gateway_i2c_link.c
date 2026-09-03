#include "gateway_i2c_link.h"

#include <stdbool.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gateway_i2c_mailbox.h"
#include "gateway_link_protocol.h"
#include "local_i2c_bus.h"
#include "sdkconfig.h"

#define LINK_I2C_IO_TIMEOUT_MS 20U
#define LINK_I2C_MISSING_PEER_BACKOFF_MS 1000U

#ifndef CONFIG_GATEWAY_LINK_I2C_ADDRESS
#define CONFIG_GATEWAY_LINK_I2C_ADDRESS 0x42
#endif
#ifndef CONFIG_GATEWAY_LINK_I2C_SPEED_HZ
#define CONFIG_GATEWAY_LINK_I2C_SPEED_HZ 400000
#endif

static i2c_master_dev_handle_t s_device;
static bool s_started;
static int64_t s_next_attempt_us;
static uint8_t s_rx_frame[GATEWAY_LINK_MAX_FRAME_BYTES];
static size_t s_rx_length;
static size_t s_rx_offset;

static int64_t now_us(void)
{
    return esp_timer_get_time();
}

static void note_failure(void)
{
    s_next_attempt_us = now_us() + (int64_t)LINK_I2C_MISSING_PEER_BACKOFF_MS * 1000LL;
}

static void note_success(void)
{
    s_next_attempt_us = 0;
}

static bool can_attempt(void)
{
    return s_next_attempt_us == 0 || now_us() >= s_next_attempt_us;
}

static esp_err_t i2c_backend_start(void)
{
    if (s_started) {
        return ESP_OK;
    }

    esp_err_t result = local_i2c_bus_init();
    if (result != ESP_OK) {
        return result;
    }
    result = local_i2c_bus_add_device(
        CONFIG_GATEWAY_LINK_I2C_ADDRESS,
        CONFIG_GATEWAY_LINK_I2C_SPEED_HZ,
        &s_device);
    if (result != ESP_OK) {
        s_device = NULL;
        return result;
    }

    s_rx_length = 0U;
    s_rx_offset = 0U;
    s_next_attempt_us = 0;
    s_started = true;
    return ESP_OK;
}

static void i2c_backend_stop(void)
{
    if (!s_started) {
        return;
    }
    if (s_device != NULL) {
        (void)i2c_master_bus_rm_device(s_device);
    }
    s_device = NULL;
    s_rx_length = 0U;
    s_rx_offset = 0U;
    s_next_attempt_us = 0;
    s_started = false;
}

static int copy_cached_rx(uint8_t *buffer, size_t capacity)
{
    if (s_rx_offset >= s_rx_length) {
        s_rx_offset = 0U;
        s_rx_length = 0U;
        return 0;
    }
    const size_t available = s_rx_length - s_rx_offset;
    const size_t count = available < capacity ? available : capacity;
    memcpy(buffer, &s_rx_frame[s_rx_offset], count);
    s_rx_offset += count;
    if (s_rx_offset == s_rx_length) {
        s_rx_offset = 0U;
        s_rx_length = 0U;
    }
    return (int)count;
}

static int i2c_backend_read(uint8_t *buffer, size_t capacity, uint32_t timeout_ms)
{
    if (!s_started || s_device == NULL || buffer == NULL || capacity == 0U) {
        return -1;
    }

    const int cached = copy_cached_rx(buffer, capacity);
    if (cached > 0) {
        return cached;
    }

    if (!can_attempt()) {
        if (timeout_ms != 0U) {
            const uint32_t sleep_ms = timeout_ms < 50U ? timeout_ms : 50U;
            vTaskDelay(pdMS_TO_TICKS(sleep_ms));
        }
        return 0;
    }

    const uint8_t pending_command = GATEWAY_I2C_MAILBOX_OP_PENDING_LENGTH;
    uint8_t pending_reply[2] = {0};
    esp_err_t result = i2c_master_transmit_receive(
        s_device,
        &pending_command,
        sizeof(pending_command),
        pending_reply,
        sizeof(pending_reply),
        LINK_I2C_IO_TIMEOUT_MS);
    if (result != ESP_OK) {
        note_failure();
        return -1;
    }

    size_t pending_length = 0U;
    if (!gateway_i2c_mailbox_parse_pending_length(pending_reply, &pending_length)) {
        note_failure();
        return -1;
    }
    if (pending_length == 0U) {
        note_success();
        if (timeout_ms != 0U) {
            const uint32_t sleep_ms = timeout_ms < 20U ? timeout_ms : 20U;
            vTaskDelay(pdMS_TO_TICKS(sleep_ms));
        }
        return 0;
    }

    const uint8_t read_command = GATEWAY_I2C_MAILBOX_OP_READ_FRAME;
    result = i2c_master_transmit_receive(
        s_device,
        &read_command,
        sizeof(read_command),
        s_rx_frame,
        pending_length,
        LINK_I2C_IO_TIMEOUT_MS);
    if (result != ESP_OK) {
        note_failure();
        return -1;
    }

    s_rx_length = pending_length;
    s_rx_offset = 0U;
    note_success();
    return copy_cached_rx(buffer, capacity);
}

static int i2c_backend_write(const uint8_t *buffer, size_t length)
{
    if (!s_started || s_device == NULL || buffer == NULL || length == 0U || !can_attempt()) {
        return -1;
    }

    uint8_t request[GATEWAY_LINK_MAX_FRAME_BYTES + GATEWAY_I2C_MAILBOX_WRITE_OVERHEAD];
    size_t request_length = 0U;
    if (!gateway_i2c_mailbox_build_write(
            buffer, length, request, sizeof(request), &request_length)) {
        return -1;
    }

    const esp_err_t result = i2c_master_transmit(
        s_device, request, request_length, LINK_I2C_IO_TIMEOUT_MS);
    if (result != ESP_OK) {
        note_failure();
        return -1;
    }
    note_success();
    return (int)length;
}

const gateway_link_backend_t *gateway_i2c_link_backend(void)
{
    static const gateway_link_backend_t backend = {
        .name = "i2c0-mailbox",
        .start = i2c_backend_start,
        .stop = i2c_backend_stop,
        .read = i2c_backend_read,
        .write = i2c_backend_write,
    };
    return &backend;
}
