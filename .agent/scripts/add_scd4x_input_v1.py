from pathlib import Path

Path('main/local_i2c_bus.h').write_text(r'''#pragma once

#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#define LOCAL_I2C_SCL_GPIO 0
#define LOCAL_I2C_SDA_GPIO 1

esp_err_t local_i2c_bus_init(void);
esp_err_t local_i2c_bus_add_device(
    uint16_t address,
    uint32_t scl_speed_hz,
    i2c_master_dev_handle_t *device);
''')

Path('main/local_i2c_bus.c').write_text(r'''#include "local_i2c_bus.h"

#include "driver/gpio.h"

static i2c_master_bus_handle_t s_bus;

esp_err_t local_i2c_bus_init(void)
{
    if (s_bus != NULL) {
        return ESP_OK;
    }

    const i2c_master_bus_config_t config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = GPIO_NUM_1,
        .scl_io_num = GPIO_NUM_0,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7U,
        .flags.enable_internal_pullup = true,
    };
    return i2c_new_master_bus(&config, &s_bus);
}

esp_err_t local_i2c_bus_add_device(
    uint16_t address,
    uint32_t scl_speed_hz,
    i2c_master_dev_handle_t *device)
{
    if (s_bus == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (device == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = scl_speed_hz,
    };
    return i2c_master_bus_add_device(s_bus, &config, device);
}
''')

Path('main/scd4x_input.h').write_text(r'''#pragma once

#include "esp_err.h"

esp_err_t scd4x_input_start(void);
''')

Path('main/scd4x_input.c').write_text(r'''#include "scd4x_input.h"

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
    event.data.input_desc.capabilities = SCD4X_CAPABILITIES;
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
    const esp_err_t add_result = local_i2c_bus_add_device(
        SCD4X_I2C_ADDR, SCD4X_INPUT_I2C_SPEED_HZ, &i2c_device);
    if (add_result != ESP_OK) {
        gateway_event_warning_input(&input, "SCD4x I2C device registration failed");
        vTaskDelete(NULL);
        return;
    }

    scd4x_t *sensor = NULL;
    const char *model = "SCD4x";
    bool measuring = false;
    bool available = false;
    uint8_t consecutive_errors = 0U;
    uint32_t last_warning_ms = 0U;

    for (;;) {
        if (sensor == NULL) {
            sensor = scd4x_init(i2c_device);
            if (sensor == NULL) {
                warning_throttled(
                    &input,
                    "SCD4x not responding on local I2C bus",
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
''')

Path('main/local_inputs.h').write_text(r'''#pragma once

#include "esp_err.h"

esp_err_t local_inputs_start(void);
''')

Path('main/local_inputs.c').write_text(r'''#include "local_inputs.h"

#include "gateway_events.h"
#include "gateway_inputs.h"
#include "local_i2c_bus.h"
#include "scd4x_input.h"

esp_err_t local_inputs_start(void)
{
    const gateway_input_id_t bus = gateway_input_make(
        GATEWAY_SOURCE_LOCAL_I2C, "i2c-bus:0", 0U);
    const esp_err_t bus_result = local_i2c_bus_init();
    if (bus_result != ESP_OK) {
        gateway_event_warning_input(&bus, "local I2C bus initialization failed");
        return ESP_OK;
    }

    return scd4x_input_start();
}
''')

p = Path('main/app_main.c')
s = p.read_text()
s = s.replace('#include "gateway_transport.h"\n', '#include "gateway_transport.h"\n#include "local_inputs.h"\n', 1)
s = s.replace(
    '    ESP_ERROR_CHECK(gateway_transport_start());\n    ESP_ERROR_CHECK(zigbee_gateway_start());\n',
    '    ESP_ERROR_CHECK(gateway_transport_start());\n    ESP_ERROR_CHECK(local_inputs_start());\n    ESP_ERROR_CHECK(zigbee_gateway_start());\n', 1)
p.write_text(s)

p = Path('main/CMakeLists.txt')
s = p.read_text()
s = s.replace(
    '        "gateway_zcl_value.c"\n        "zigbee_gateway.c"\n',
    '        "gateway_zcl_value.c"\n        "local_i2c_bus.c"\n        "local_inputs.c"\n        "scd4x_input.c"\n        "zigbee_gateway.c"\n', 1)
s = s.replace('    REQUIRES nvs_flash esp_timer\n', '    REQUIRES nvs_flash esp_timer esp_driver_i2c\n', 1)
p.write_text(s)

p = Path('main/idf_component.yml')
s = p.read_text()
assert 'jef-sure/scd4x' not in s
s = s.rstrip() + '\n  jef-sure/scd4x: "0.0.3"\n'
p.write_text(s)

p = Path('docs/ARCHITECTURE.md')
s = p.read_text()
s = s.replace(
    '- `gateway_transport.c/.h` consumes normalized events and renders the current serial/log transport. It must not own Zigbee state or interpretation policy.\n',
    '- `gateway_transport.c/.h` consumes normalized events and renders the current serial/log transport. It must not own Zigbee state or interpretation policy.\n- `local_i2c_bus.c/.h` owns the reusable local I2C master bus on SCL GPIO0 / SDA GPIO1. Sensor adapters request devices from this bus instead of configuring I2C independently.\n- `local_inputs.c/.h` is the composition point for board-local input adapters. Local-bus absence is reported but does not disable the Zigbee gateway.\n- `scd4x_input.c/.h` adapts an SCD4x-family sensor into the protocol-neutral input contract. It owns SCD4x polling/recovery policy, not transport or Zigbee behavior.\n', 1)
s = s.replace(
    'Stable input identity belongs to the adapter boundary. Zigbee uses IEEE identity plus endpoint as the logical channel; short addresses are only a provisional fallback when IEEE recovery has not completed. Local sensors use a stable hardware identity such as the SCD4x serial number, with a board-local fallback only when the device cannot expose one.\n',
    'Stable input identity belongs to the adapter boundary. Zigbee uses IEEE identity plus endpoint as the logical channel; short addresses are only a provisional fallback when IEEE recovery has not completed. The SCD4x adapter uses the sensor 48-bit serial number and channel 0, with `scd4x:0x62` only as a fallback when the serial cannot be read.\n\nThe ESP32-C6 is an input gateway. Zigbee and local I2C are peer input adapters. The future UART/SPI transport to the ESP32-S3 must serialize input identity, availability/capabilities, and normalized measurements; the ESP32-S3 owns the application-facing current input registry used by LiteGraph. A later link resynchronization message may send a snapshot, but transport must not reinterpret source protocols.\n', 1)
p.write_text(s)

p = Path('README.md')
s = p.read_text()
s = s.replace(
    'This is SDK 2.x firmware: it uses the `esp_zigbee_*` and `ezb_*` APIs and one `ezb_af_create_gateway_endpoint()` gateway endpoint. It does not add a pretend client-cluster data model, nor does it use a ZBOSS/v1 compatibility API, Wi-Fi, BLE, Matter, Thread, MQTT, an external RCP, or an SCD41.\n',
    'This is SDK 2.x firmware: it uses the `esp_zigbee_*` and `ezb_*` APIs and one `ezb_af_create_gateway_endpoint()` gateway endpoint. It does not add a pretend client-cluster data model, nor does it use a ZBOSS/v1 compatibility API, Wi-Fi, BLE, Matter, Thread, MQTT, or an external RCP. An SCD4x-family sensor is supported as an independent local I2C input adapter.\n', 1)
insert = r'''## Local SCD4x input

The board-local SCD4x adapter uses the shared I2C bus with **SCL GPIO0**, **SDA GPIO1**, and the sensor's fixed address `0x62`. The managed dependency is pinned to `jef-sure/scd4x` v0.0.3.

On successful detection the adapter publishes one protocol-neutral input identity based on the sensor's 48-bit serial number, advertises temperature / humidity / CO2 capabilities, starts periodic measurement, and publishes normalized `temperature` (C), `humidity` (%), and `co2` (ppm) events. Sensor absence or read failures are reported without stopping Zigbee operation.

The local sensor does not enter `zigbee_gateway.c`: Zigbee reports and SCD4x readings converge only at `gateway_input_id_t` + `gateway_measurement_t`. This is the same boundary intended for the later C6 -> UART/SPI -> ESP32-S3 link and LiteGraph input registry.

'''
marker = '## Build and flash\n'
assert marker in s
s = s.replace(marker, insert + marker, 1)
p.write_text(s)
