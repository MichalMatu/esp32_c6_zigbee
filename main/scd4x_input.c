#include "scd4x_input.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "scd4x.h"

#include "gateway_events.h"
#include "gateway_inputs.h"
#include "local_i2c_bus.h"

#define SCD4X_INPUT_TASK_STACK 4096U
#define SCD4X_INPUT_TASK_PRIORITY 4U
#define SCD4X_INPUT_I2C_SPEED_HZ 100000U
#define SCD4X_INPUT_PROBE_TIMEOUT_MS 100U
#define SCD4X_INPUT_POLL_MS 1000U
#define SCD4X_INPUT_INIT_RETRY_MS 5000U
#define SCD4X_INPUT_WARNING_INTERVAL_MS 30000U
#define SCD4X_INPUT_ERROR_UNAVAILABLE_THRESHOLD 3U

static const gateway_input_capabilities_t SCD4X_CAPABILITIES =
    GATEWAY_INPUT_CAP_TEMPERATURE |
    GATEWAY_INPUT_CAP_HUMIDITY |
    GATEWAY_INPUT_CAP_CO2;

static const char *variant_model(scd4x_variant_t variant)
{
    switch (variant) {
    case SCD4X_VARIANT_SCD40: return "SCD40";
    case SCD4X_VARIANT_SCD41: return "SCD41";
    case SCD4X_VARIANT_SCD43: return "SCD43";
    default: return "SCD4x";
    }
}

static gateway_input_id_t fallback_input(void)
{
    return gateway_input_make(GATEWAY_SOURCE_LOCAL_I2C, "scd4x:0x62", 0U);
}

static gateway_input_id_t serial_input(const uint16_t serial[3])
{
    char id[GATEWAY_INPUT_ID_MAX_BYTES];
    snprintf(id, sizeof(id), "scd4x:%04x%04x%04x", serial[0], serial[1], serial[2]);
    return gateway_input_make(GATEWAY_SOURCE_LOCAL_I2C, id, 0U);
}

static void publish_input_state(
    const gateway_input_id_t *input, bool available, const char *model)
{
    gateway_event_t event = gateway_event_make_input(
        available ? GATEWAY_EVENT_INPUT_AVAILABLE : GATEWAY_EVENT_INPUT_UNAVAILABLE,
        input);
    event.data.input_desc.profile.readable = SCD4X_CAPABILITIES;
    if (model != NULL) {
        strncpy(
            event.data.input_desc.model,
            model,
            sizeof(event.data.input_desc.model) - 1U);
    }
    gateway_event_publish(&event);
}

static void publish_measurement(
    const gateway_input_id_t *input,
    gateway_measurement_kind_t kind,
    gateway_unit_t unit,
    double value)
{
    gateway_event_t event = gateway_event_make_input(GATEWAY_EVENT_MEASUREMENT, input);
    event.data.measurement = (gateway_measurement_t){
        .kind = kind,
        .unit = unit,
        .value = value,
    };
    gateway_event_publish(&event);
}

static void warning_throttled(
    const gateway_input_id_t *input,
    const char *text,
    uint32_t *last_warning_ms)
{
    const uint32_t now = gateway_uptime_ms();
    if (*last_warning_ms == 0U ||
        (uint32_t)(now - *last_warning_ms) >= SCD4X_INPUT_WARNING_INTERVAL_MS) {
        gateway_event_warning_input(input, text);
        *last_warning_ms = now;
    }
}

static void scd4x_input_task(void *arg)
{
    (void)arg;

    i2c_master_dev_handle_t i2c_device = NULL;
    gateway_input_id_t input = fallback_input();
    scd4x_t *sensor = NULL;
    const char *model = "SCD4x";
    bool measuring = false;
    bool available = false;
    uint8_t consecutive_errors = 0U;
    uint32_t last_warning_ms = 0U;

    for (;;) {
        if (sensor == NULL) {
            if (local_i2c_bus_probe(SCD4X_I2C_ADDR, SCD4X_INPUT_PROBE_TIMEOUT_MS) != ESP_OK) {
                warning_throttled(
                    &input,
                    "SCD4x not present on local I2C bus",
                    &last_warning_ms);
                vTaskDelay(pdMS_TO_TICKS(SCD4X_INPUT_INIT_RETRY_MS));
                continue;
            }
            if (i2c_device == NULL) {
                const esp_err_t add_result = local_i2c_bus_add_device(
                    SCD4X_I2C_ADDR, SCD4X_INPUT_I2C_SPEED_HZ, &i2c_device);
                if (add_result != ESP_OK) {
                    gateway_event_warning_input(
                        &input, "SCD4x I2C device registration failed");
                    vTaskDelete(NULL);
                    return;
                }
            }

            sensor = scd4x_init(i2c_device);
            if (sensor == NULL) {
                warning_throttled(
                    &input,
                    "SCD4x initialization failed after successful I2C probe",
                    &last_warning_ms);
                vTaskDelay(pdMS_TO_TICKS(SCD4X_INPUT_INIT_RETRY_MS));
                continue;
            }

            uint16_t serial[3];
            if (scd4x_get_serial_number(sensor, serial) == ESP_OK) {
                input = serial_input(serial);
            } else {
                gateway_event_warning_input(
                    &input,
                    "SCD4x serial number unavailable; using bus fallback identity");
            }

            scd4x_variant_t variant = SCD4X_VARIANT_UNKNOWN;
            if (scd4x_get_sensor_variant(sensor, &variant) == ESP_OK) {
                model = variant_model(variant);
            } else {
                gateway_event_warning_input(
                    &input,
                    "SCD4x variant unavailable; using generic model");
            }
        }

        if (!measuring) {
            const esp_err_t start_result = scd4x_start_periodic_measurement(sensor);
            if (start_result != ESP_OK) {
                warning_throttled(
                    &input,
                    "SCD4x periodic measurement start failed",
                    &last_warning_ms);
                vTaskDelay(pdMS_TO_TICKS(SCD4X_INPUT_INIT_RETRY_MS));
                continue;
            }
            measuring = true;
            available = true;
            consecutive_errors = 0U;
            publish_input_state(&input, true, model);
        }

        scd4x_measurement_t measurement = {0};
        const esp_err_t read_result = scd4x_read_measurement(sensor, &measurement);
        if (read_result == ESP_ERR_NOT_FINISHED) {
            vTaskDelay(pdMS_TO_TICKS(SCD4X_INPUT_POLL_MS));
            continue;
        }
        if (read_result != ESP_OK) {
            if (consecutive_errors < UINT8_MAX) {
                ++consecutive_errors;
            }
            warning_throttled(
                &input,
                "SCD4x measurement read failed",
                &last_warning_ms);
            if (available &&
                consecutive_errors >= SCD4X_INPUT_ERROR_UNAVAILABLE_THRESHOLD) {
                available = false;
                publish_input_state(&input, false, model);
            }
            vTaskDelay(pdMS_TO_TICKS(SCD4X_INPUT_POLL_MS));
            continue;
        }

        consecutive_errors = 0U;
        if (!available) {
            available = true;
            publish_input_state(&input, true, model);
        }
        publish_measurement(
            &input,
            GATEWAY_MEAS_CO2,
            GATEWAY_UNIT_PPM,
            measurement.co2);
        publish_measurement(
            &input,
            GATEWAY_MEAS_TEMPERATURE,
            GATEWAY_UNIT_CELSIUS,
            measurement.temperature);
        publish_measurement(
            &input,
            GATEWAY_MEAS_HUMIDITY,
            GATEWAY_UNIT_PERCENT,
            measurement.humidity);

        vTaskDelay(pdMS_TO_TICKS(SCD4X_INPUT_POLL_MS));
    }
}

esp_err_t scd4x_input_start(void)
{
    return xTaskCreate(
        scd4x_input_task,
        "scd4x_input",
        SCD4X_INPUT_TASK_STACK,
        NULL,
        SCD4X_INPUT_TASK_PRIORITY,
        NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
