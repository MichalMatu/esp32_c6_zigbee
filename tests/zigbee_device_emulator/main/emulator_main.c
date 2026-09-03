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
#include <ezbee/zcl/cluster/temperature_measurement_desc.h>
#include <ezbee/zcl/zcl_common.h>

#define EMULATOR_ENDPOINT 1U
#define EMULATOR_PROFILE_ID 0x0104U
#define EMULATOR_DEVICE_ID 0x0302U
#define EMULATOR_CHANNEL_MASK 0x07fff800UL
#define EMULATOR_TEMPERATURE_CLUSTER_ID 0x0402U
#define EMULATOR_MEASURED_VALUE_ATTR_ID 0x0000U
#define EMULATOR_STD_MANUF_CODE 0x0000U

static const char *TAG = "zb_emulator";

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

static esp_err_t register_temperature_profile(void)
{
    const ezb_af_ep_config_t endpoint_config = {
        .ep_id = EMULATOR_ENDPOINT,
        .app_profile_id = EMULATOR_PROFILE_ID,
        .app_device_id = EMULATOR_DEVICE_ID,
        .app_device_version = 1U,
    };
    ezb_af_device_desc_t device = ezb_af_create_device_desc();
    ezb_af_ep_desc_t endpoint = ezb_af_create_endpoint_desc(&endpoint_config);
    if (device == EZB_INVALID_AF_DEVICE_DESC || endpoint == EZB_INVALID_AF_EP_DESC) {
        return ESP_ERR_NO_MEM;
    }

    const ezb_zcl_basic_cluster_server_config_t basic_config = {
        .zcl_version = 3U,
        .power_source = 0x01U,
    };
    ezb_zcl_cluster_desc_t basic = ezb_zcl_basic_create_cluster_desc(
        &basic_config, EZB_ZCL_CLUSTER_SERVER);
    if (basic == EZB_INVALID_ZCL_CLUSTER_DESC) {
        return ESP_ERR_NO_MEM;
    }
    static const uint8_t manufacturer[] = {5U, 'C', '6', 'L', 'a', 'b'};
    static const uint8_t model[] = {8U, 'T', 'e', 'm', 'p', 'E', 'm', 'u', '1'};
    if (ezb_zcl_basic_cluster_desc_add_attr(basic, 0x0004U, manufacturer) != EZB_ERR_NONE ||
        ezb_zcl_basic_cluster_desc_add_attr(basic, 0x0005U, model) != EZB_ERR_NONE) {
        return ESP_FAIL;
    }

    const ezb_zcl_temperature_measurement_cluster_server_config_t temperature_config = {
        .measured_value = 2150,
        .min_measured_value = -4000,
        .max_measured_value = 12500,
    };
    ezb_zcl_cluster_desc_t temperature =
        ezb_zcl_temperature_measurement_create_cluster_desc(
            &temperature_config, EZB_ZCL_CLUSTER_SERVER);
    if (temperature == EZB_INVALID_ZCL_CLUSTER_DESC) {
        return ESP_ERR_NO_MEM;
    }

    if (ezb_af_endpoint_add_cluster_desc(endpoint, basic) != EZB_ERR_NONE ||
        ezb_af_endpoint_add_cluster_desc(endpoint, temperature) != EZB_ERR_NONE ||
        ezb_af_device_add_endpoint_desc(device, endpoint) != EZB_ERR_NONE ||
        ezb_af_device_desc_register(device) != EZB_ERR_NONE) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

static void temperature_task(void *arg)
{
    (void)arg;
    int16_t temperature = 2150;
    int16_t step = 25;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        temperature = (int16_t)(temperature + step);
        if (temperature >= 2350 || temperature <= 1950) {
            step = (int16_t)-step;
        }
        if (esp_zigbee_lock_acquire(pdMS_TO_TICKS(100))) {
            const ezb_zcl_status_t status = ezb_zcl_set_attr_value(
                EMULATOR_ENDPOINT,
                EMULATOR_TEMPERATURE_CLUSTER_ID,
                EZB_ZCL_CLUSTER_SERVER,
                EMULATOR_MEASURED_VALUE_ATTR_ID,
                EMULATOR_STD_MANUF_CODE,
                &temperature,
                false);
            esp_zigbee_lock_release();
            ESP_LOGI(TAG, "temperature=%0.2fC set_status=0x%02x",
                     (double)temperature / 100.0, (unsigned)status);
        }
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
    if (register_temperature_profile() != ESP_OK) {
        ESP_LOGE(TAG, "temperature profile registration failed");
        vTaskDelete(NULL);
        return;
    }
    ezb_app_signal_add_handler(app_signal_handler);
    if (esp_zigbee_start(true) != ESP_OK) {
        ESP_LOGE(TAG, "esp_zigbee_start failed");
        vTaskDelete(NULL);
        return;
    }
    if (xTaskCreate(temperature_task, "emu_temperature", 3072, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "temperature task creation failed");
    }
    ESP_LOGI(TAG, "temperature emulator profile started");
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
