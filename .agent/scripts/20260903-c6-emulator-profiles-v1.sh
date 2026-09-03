#!/usr/bin/env bash
set -euo pipefail

git fetch origin
git checkout integration/c6-s3-i2c-20260903
git reset --hard origin/integration/c6-s3-i2c-20260903
rm -rf tests/zigbee_device_emulator/build tests/zigbee_device_emulator/sdkconfig tests/zigbee_device_emulator/sdkconfig.old
[ -z "$(git status --short)" ]
[ "$(git rev-parse HEAD)" = "6b1abf3746168e27ee01272fa9a1267fd306d6bd" ]

cat > tests/zigbee_device_emulator/main/Kconfig.projbuild <<'EOF'
menu "Second-C6 Zigbee emulator"

choice EMULATOR_PROFILE
    prompt "Emulated Zigbee profile"
    default EMULATOR_PROFILE_MIXED

config EMULATOR_PROFILE_TEMP_HUM
    bool "Temperature + humidity"

config EMULATOR_PROFILE_OCCUPANCY
    bool "Occupancy"

config EMULATOR_PROFILE_ONOFF_LEVEL
    bool "On/Off + Level"

config EMULATOR_PROFILE_MIXED
    bool "Mixed multi-endpoint"

endchoice

endmenu
EOF

cat > tests/zigbee_device_emulator/main/emulator_main.c <<'EOF'
#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_zigbee.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include <ezbee/af.h>
#include <ezbee/app_signals.h>
#include <ezbee/bdb.h>
#include <ezbee/zcl/cluster/basic_desc.h>
#include <ezbee/zcl/cluster/level_desc.h>
#include <ezbee/zcl/cluster/occupancy_sensing_desc.h>
#include <ezbee/zcl/cluster/on_off_desc.h>
#include <ezbee/zcl/cluster/rel_humidity_measurement_desc.h>
#include <ezbee/zcl/cluster/temperature_measurement_desc.h>
#include <ezbee/zcl/zcl_common.h>
#include <ezbee/zcl/zcl_core.h>

#define EMULATOR_PROFILE_ID 0x0104U
#define EMULATOR_DEVICE_ID_GENERIC 0x0000U
#define EMULATOR_DEVICE_ID_TEMP_SENSOR 0x0302U
#define EMULATOR_CHANNEL_MASK 0x07fff800UL
#define EMULATOR_STD_MANUF_CODE 0x0000U

#define EMULATOR_CLUSTER_TEMPERATURE 0x0402U
#define EMULATOR_CLUSTER_HUMIDITY 0x0405U
#define EMULATOR_CLUSTER_OCCUPANCY 0x0406U
#define EMULATOR_CLUSTER_ON_OFF 0x0006U
#define EMULATOR_CLUSTER_LEVEL 0x0008U
#define EMULATOR_ATTR_MEASURED_VALUE 0x0000U
#define EMULATOR_ATTR_OCCUPANCY 0x0000U

#if CONFIG_EMULATOR_PROFILE_MIXED
#define EMULATOR_ENV_ENDPOINT 1U
#define EMULATOR_OCC_ENDPOINT 2U
#define EMULATOR_LIGHT_ENDPOINT 3U
#elif CONFIG_EMULATOR_PROFILE_TEMP_HUM
#define EMULATOR_ENV_ENDPOINT 1U
#elif CONFIG_EMULATOR_PROFILE_OCCUPANCY
#define EMULATOR_OCC_ENDPOINT 1U
#elif CONFIG_EMULATOR_PROFILE_ONOFF_LEVEL
#define EMULATOR_LIGHT_ENDPOINT 1U
#endif

static const char *TAG = "zb_emulator";
static const uint8_t s_manufacturer[] = {5U, 'C', '6', 'L', 'a', 'b'};
static const uint8_t s_model[] = {8U, 'C', '6', 'E', 'm', 'u', 'V', '2', '0'};

static bool app_signal_handler(const ezb_app_signal_t *signal)
{
    const ezb_app_signal_type_t type = ezb_app_signal_get_type(signal);
    const void *params = ezb_app_signal_get_params(signal);
    if (type == EZB_BDB_SIGNAL_STEERING) {
        const ezb_bdb_signal_simple_params_t *steering = params;
        ESP_LOGI(TAG, "network steering status=%u",
                 steering == NULL ? 0xffU : (unsigned)steering->status);
    } else if (type == EZB_BDB_SIGNAL_DEVICE_FIRST_START) {
        ESP_LOGI(TAG, "device first start");
    } else if (type == EZB_BDB_SIGNAL_DEVICE_REBOOT) {
        ESP_LOGI(TAG, "device reboot");
    }
    return false;
}

static void zcl_core_action_handler(
    ezb_zcl_core_action_callback_id_t callback_id, void *message)
{
    (void)message;
    if (callback_id == EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID) {
        ESP_LOGI(TAG, "ZCL server attribute changed");
    }
}

static ezb_af_ep_desc_t create_endpoint(uint8_t endpoint_id, uint16_t device_id)
{
    const ezb_af_ep_config_t endpoint_config = {
        .ep_id = endpoint_id,
        .app_profile_id = EMULATOR_PROFILE_ID,
        .app_device_id = device_id,
        .app_device_version = 1U,
    };
    return ezb_af_create_endpoint_desc(&endpoint_config);
}

static esp_err_t add_basic(ezb_af_ep_desc_t endpoint)
{
    const ezb_zcl_basic_cluster_server_config_t config = {
        .zcl_version = 3U,
        .power_source = 0x01U,
    };
    ezb_zcl_cluster_desc_t cluster = ezb_zcl_basic_create_cluster_desc(
        &config, EZB_ZCL_CLUSTER_SERVER);
    if (cluster == EZB_INVALID_ZCL_CLUSTER_DESC) {
        return ESP_ERR_NO_MEM;
    }
    if (ezb_zcl_basic_cluster_desc_add_attr(cluster, 0x0004U, s_manufacturer) != EZB_ERR_NONE ||
        ezb_zcl_basic_cluster_desc_add_attr(cluster, 0x0005U, s_model) != EZB_ERR_NONE ||
        ezb_af_endpoint_add_cluster_desc(endpoint, cluster) != EZB_ERR_NONE) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t add_environment_endpoint(ezb_af_device_desc_t device, uint8_t endpoint_id)
{
    ezb_af_ep_desc_t endpoint = create_endpoint(endpoint_id, EMULATOR_DEVICE_ID_TEMP_SENSOR);
    if (endpoint == EZB_INVALID_AF_EP_DESC || add_basic(endpoint) != ESP_OK) {
        return ESP_FAIL;
    }

    const ezb_zcl_temperature_measurement_cluster_server_config_t temp_config = {
        .measured_value = 2150,
        .min_measured_value = -4000,
        .max_measured_value = 12500,
    };
    ezb_zcl_cluster_desc_t temp = ezb_zcl_temperature_measurement_create_cluster_desc(
        &temp_config, EZB_ZCL_CLUSTER_SERVER);

    const ezb_zcl_rel_humidity_measurement_cluster_server_config_t humidity_config = {
        .measured_value = 5500U,
        .min_measured_value = 0U,
        .max_measured_value = 10000U,
    };
    ezb_zcl_cluster_desc_t humidity = ezb_zcl_rel_humidity_measurement_create_cluster_desc(
        &humidity_config, EZB_ZCL_CLUSTER_SERVER);

    if (temp == EZB_INVALID_ZCL_CLUSTER_DESC || humidity == EZB_INVALID_ZCL_CLUSTER_DESC ||
        ezb_af_endpoint_add_cluster_desc(endpoint, temp) != EZB_ERR_NONE ||
        ezb_af_endpoint_add_cluster_desc(endpoint, humidity) != EZB_ERR_NONE ||
        ezb_af_device_add_endpoint_desc(device, endpoint) != EZB_ERR_NONE) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t add_occupancy_endpoint(ezb_af_device_desc_t device, uint8_t endpoint_id)
{
    ezb_af_ep_desc_t endpoint = create_endpoint(endpoint_id, EMULATOR_DEVICE_ID_GENERIC);
    if (endpoint == EZB_INVALID_AF_EP_DESC || add_basic(endpoint) != ESP_OK) {
        return ESP_FAIL;
    }

    const ezb_zcl_occupancy_sensing_cluster_server_config_t occupancy_config = {
        .occupancy = EZB_ZCL_OCCUPANCY_SENSING_OCCUPANCY_UNOCCUPIED,
        .occupancy_sensor_type = EZB_ZCL_OCCUPANCY_SENSING_OCCUPANCY_SENSOR_TYPE_PIR,
        .occupancy_sensor_type_bitmap = EZB_ZCL_OCCUPANCY_SENSING_OCCUPANCY_SENSOR_TYPE_BITMAP_PIR,
    };
    ezb_zcl_cluster_desc_t occupancy = ezb_zcl_occupancy_sensing_create_cluster_desc(
        &occupancy_config, EZB_ZCL_CLUSTER_SERVER);
    if (occupancy == EZB_INVALID_ZCL_CLUSTER_DESC ||
        ezb_af_endpoint_add_cluster_desc(endpoint, occupancy) != EZB_ERR_NONE ||
        ezb_af_device_add_endpoint_desc(device, endpoint) != EZB_ERR_NONE) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t add_light_endpoint(ezb_af_device_desc_t device, uint8_t endpoint_id)
{
    ezb_af_ep_desc_t endpoint = create_endpoint(endpoint_id, EMULATOR_DEVICE_ID_GENERIC);
    if (endpoint == EZB_INVALID_AF_EP_DESC || add_basic(endpoint) != ESP_OK) {
        return ESP_FAIL;
    }

    const ezb_zcl_on_off_cluster_server_config_t on_off_config = {.on_off = false};
    ezb_zcl_cluster_desc_t on_off = ezb_zcl_on_off_create_cluster_desc(
        &on_off_config, EZB_ZCL_CLUSTER_SERVER);

    const ezb_zcl_level_cluster_server_config_t level_config = {.current_level = 127U};
    ezb_zcl_cluster_desc_t level = ezb_zcl_level_create_cluster_desc(
        &level_config, EZB_ZCL_CLUSTER_SERVER);

    if (on_off == EZB_INVALID_ZCL_CLUSTER_DESC || level == EZB_INVALID_ZCL_CLUSTER_DESC ||
        ezb_af_endpoint_add_cluster_desc(endpoint, on_off) != EZB_ERR_NONE ||
        ezb_af_endpoint_add_cluster_desc(endpoint, level) != EZB_ERR_NONE ||
        ezb_af_device_add_endpoint_desc(device, endpoint) != EZB_ERR_NONE) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t register_selected_profile(void)
{
    ezb_af_device_desc_t device = ezb_af_create_device_desc();
    if (device == EZB_INVALID_AF_DEVICE_DESC) {
        return ESP_ERR_NO_MEM;
    }

#if CONFIG_EMULATOR_PROFILE_TEMP_HUM
    if (add_environment_endpoint(device, EMULATOR_ENV_ENDPOINT) != ESP_OK) {
        return ESP_FAIL;
    }
#elif CONFIG_EMULATOR_PROFILE_OCCUPANCY
    if (add_occupancy_endpoint(device, EMULATOR_OCC_ENDPOINT) != ESP_OK) {
        return ESP_FAIL;
    }
#elif CONFIG_EMULATOR_PROFILE_ONOFF_LEVEL
    if (add_light_endpoint(device, EMULATOR_LIGHT_ENDPOINT) != ESP_OK) {
        return ESP_FAIL;
    }
#elif CONFIG_EMULATOR_PROFILE_MIXED
    if (add_environment_endpoint(device, EMULATOR_ENV_ENDPOINT) != ESP_OK ||
        add_occupancy_endpoint(device, EMULATOR_OCC_ENDPOINT) != ESP_OK ||
        add_light_endpoint(device, EMULATOR_LIGHT_ENDPOINT) != ESP_OK) {
        return ESP_FAIL;
    }
#else
#error "Select exactly one emulator profile"
#endif

    return ezb_af_device_desc_register(device) == EZB_ERR_NONE ? ESP_OK : ESP_FAIL;
}

static void set_server_attr(
    uint8_t endpoint, uint16_t cluster, uint16_t attribute, void *value)
{
    const ezb_zcl_status_t status = ezb_zcl_set_attr_value(
        endpoint, cluster, EZB_ZCL_CLUSTER_SERVER, attribute,
        EMULATOR_STD_MANUF_CODE, value, false);
    if (status != 0U) {
        ESP_LOGW(TAG, "set attr ep=%u cluster=0x%04x attr=0x%04x status=0x%02x",
                 endpoint, cluster, attribute, (unsigned)status);
    }
}

static void emulation_task(void *arg)
{
    (void)arg;
    int16_t temperature = 2150;
    int16_t temperature_step = 25;
    uint16_t humidity = 5500U;
    int16_t humidity_step = 100;
    uint8_t occupancy = 0U;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        if (!esp_zigbee_lock_acquire(pdMS_TO_TICKS(100))) {
            ESP_LOGW(TAG, "emulation update lock timeout");
            continue;
        }

#if CONFIG_EMULATOR_PROFILE_TEMP_HUM || CONFIG_EMULATOR_PROFILE_MIXED
        temperature = (int16_t)(temperature + temperature_step);
        if (temperature >= 2350 || temperature <= 1950) {
            temperature_step = (int16_t)-temperature_step;
        }
        humidity = (uint16_t)((int32_t)humidity + humidity_step);
        if (humidity >= 6500U || humidity <= 4500U) {
            humidity_step = (int16_t)-humidity_step;
        }
        set_server_attr(
            EMULATOR_ENV_ENDPOINT, EMULATOR_CLUSTER_TEMPERATURE,
            EMULATOR_ATTR_MEASURED_VALUE, &temperature);
        set_server_attr(
            EMULATOR_ENV_ENDPOINT, EMULATOR_CLUSTER_HUMIDITY,
            EMULATOR_ATTR_MEASURED_VALUE, &humidity);
#endif

#if CONFIG_EMULATOR_PROFILE_OCCUPANCY || CONFIG_EMULATOR_PROFILE_MIXED
        occupancy = occupancy == 0U ? 1U : 0U;
        set_server_attr(
            EMULATOR_OCC_ENDPOINT, EMULATOR_CLUSTER_OCCUPANCY,
            EMULATOR_ATTR_OCCUPANCY, &occupancy);
#endif

        esp_zigbee_lock_release();
        ESP_LOGI(TAG, "deterministic emulator tick");
    }
}

static void zigbee_task(void *arg)
{
    (void)arg;
    const esp_zigbee_config_t config = {
        .device_config = {
            .device_type = EZB_NWK_DEVICE_TYPE_END_DEVICE,
            .install_code_policy = false,
            .zed_config = {.keep_alive = 3000U},
        },
        .platform_config = {
            .storage_partition_name = "zb_storage",
            .radio_config = {.radio_mode = ESP_ZIGBEE_RADIO_MODE_NATIVE},
        },
    };
    if (esp_zigbee_init(&config) != ESP_OK) {
        ESP_LOGE(TAG, "esp_zigbee_init failed");
        vTaskDelete(NULL);
        return;
    }
    ezb_bdb_set_primary_channel_set(EMULATOR_CHANNEL_MASK);
    if (register_selected_profile() != ESP_OK) {
        ESP_LOGE(TAG, "emulator profile registration failed");
        vTaskDelete(NULL);
        return;
    }
    ezb_app_signal_add_handler(app_signal_handler);
    ezb_zcl_core_action_handler_register(zcl_core_action_handler);
    if (esp_zigbee_start(true) != ESP_OK) {
        ESP_LOGE(TAG, "esp_zigbee_start failed");
        vTaskDelete(NULL);
        return;
    }
    if (xTaskCreate(emulation_task, "emu_values", 3072, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "emulation task creation failed");
    }
    ESP_LOGI(TAG, "selected Zigbee emulator profile started");
    esp_zigbee_launch_mainloop();
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    if (xTaskCreate(zigbee_task, "emu_zigbee", 6144, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "zigbee task creation failed");
    }
}
EOF

cat > tests/zigbee_device_emulator/README.md <<'EOF'
# Second-C6 Zigbee device emulator

This directory is an independent ESP-IDF application for a distinct second ESP32-C6. It does not link or include production `main/` sources.

## Build-time profiles

Select one under `Second-C6 Zigbee emulator` in menuconfig:

- `Temperature + humidity`: endpoint 1, Basic + Temperature Measurement + Relative Humidity Measurement.
- `Occupancy`: endpoint 1, Basic + Occupancy Sensing (PIR).
- `On/Off + Level`: endpoint 1, Basic + On/Off + Level Control server clusters.
- `Mixed multi-endpoint` (default): environment on endpoint 1, occupancy on endpoint 2, On/Off + Level on endpoint 3.

Environment and occupancy attributes change deterministically every 10 seconds. This is intended to exercise coordinator interview, Basic metadata, endpoint/cluster capability normalization and Configure Reporting. On/Off and Level are real server descriptors; stack-side attribute changes are logged through the ZCL core action callback so command behavior can be observed when hardware validation begins.

Hardware flashing remains a separate gate: verify two distinct Local Agent ESP32-C6 resource identities before any coordinator/emulator flash task.
EOF

python3 tests/host/test_no_embedded_nul.py
git diff --check

. ~/esp/esp-idf-v5.5.4/export.sh >/dev/null
cd tests/zigbee_device_emulator
rm -rf build sdkconfig sdkconfig.old
idf.py set-target esp32c6
idf.py build
idf.py size
rm -rf build sdkconfig sdkconfig.old
cd ../..

python3 tests/host/test_no_embedded_nul.py
pio run -c platformio.tests.ini -e test-all-host
git diff --check

git add tests/zigbee_device_emulator/main/Kconfig.projbuild \
        tests/zigbee_device_emulator/main/emulator_main.c \
        tests/zigbee_device_emulator/README.md
git diff --cached --check
git commit -m 'Add selectable second-C6 emulator profiles'
git push origin HEAD:integration/c6-s3-i2c-20260903
printf 'EMULATOR_PROFILES_HEAD=%s\n' "$(git rev-parse HEAD)"
git status --short
