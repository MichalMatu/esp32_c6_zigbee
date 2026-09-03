#!/usr/bin/env bash
set -euo pipefail

git fetch origin
git checkout integration/c6-s3-i2c-20260903
git reset --hard origin/integration/c6-s3-i2c-20260903
rm -rf tests/zigbee_device_emulator/build tests/zigbee_device_emulator/sdkconfig tests/zigbee_device_emulator/sdkconfig.old
[ -z "$(git status --short)" ]
[ "$(git rev-parse HEAD)" = "6bb5022e3ea8185ce10f9497c9e557df46df4b4c" ]

python3 - <<'PY'
from pathlib import Path
p = Path('tests/zigbee_device_emulator/main/emulator_main.c')
s = p.read_text()

s = s.replace('#include "freertos/FreeRTOS.h"\n#include "freertos/task.h"\n', '#include "freertos/FreeRTOS.h"\n#include "freertos/queue.h"\n#include "freertos/task.h"\n', 1)
s = s.replace('#define EMULATOR_RESERVED_OCCUPANCY 0x02U\n', '#define EMULATOR_RESERVED_OCCUPANCY 0x02U\n#define EMULATOR_ATTR_ON_OFF 0x0000U\n#define EMULATOR_ATTR_CURRENT_LEVEL 0x0000U\n#define EMULATOR_ROUNDTRIP_QUEUE_DEPTH 8U\n#define EMULATOR_ROUNDTRIP_REPORT_DELAY_MS 20U\n', 1)

marker = 'static const uint8_t s_model[] = {8U, \'C\', \'6\', \'E\', \'m\', \'u\', \'V\', \'2\', \'0\'};\n'
insert = '''static const uint8_t s_model[] = {8U, 'C', '6', 'E', 'm', 'u', 'V', '2', '0'};

typedef struct {
    uint8_t endpoint;
    uint16_t cluster;
    uint16_t attribute;
} emulator_roundtrip_event_t;

static StaticQueue_t s_roundtrip_queue_storage;
static uint8_t s_roundtrip_queue_buffer[
    EMULATOR_ROUNDTRIP_QUEUE_DEPTH * sizeof(emulator_roundtrip_event_t)
];
static QueueHandle_t s_roundtrip_queue;
'''
if marker not in s:
    raise SystemExit('model marker not found')
s = s.replace(marker, insert, 1)

old_handler = '''static void zcl_core_action_handler(
    ezb_zcl_core_action_callback_id_t callback_id, void *message)
{
    (void)message;
    if (callback_id == EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID) {
        ESP_LOGI(TAG, "ZCL server attribute changed");
    }
}
'''
new_handler = '''static void zcl_core_action_handler(
    ezb_zcl_core_action_callback_id_t callback_id, void *message)
{
    if (callback_id != EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID ||
        message == NULL || s_roundtrip_queue == NULL) {
        return;
    }
    const ezb_zcl_set_attr_value_message_t *changed = message;
    if (changed->info.cluster_role != EZB_ZCL_CLUSTER_SERVER) {
        return;
    }

    emulator_roundtrip_event_t event = {
        .endpoint = changed->info.dst_ep,
        .cluster = changed->info.cluster_id,
        .attribute = 0U,
    };
    if (event.cluster == EMULATOR_CLUSTER_ON_OFF) {
        event.attribute = EMULATOR_ATTR_ON_OFF;
    } else if (event.cluster == EMULATOR_CLUSTER_LEVEL) {
        event.attribute = EMULATOR_ATTR_CURRENT_LEVEL;
    } else {
        return;
    }

    if (xQueueSend(s_roundtrip_queue, &event, 0) != pdPASS) {
        ESP_LOGW(TAG, "roundtrip report queue full ep=%u cluster=0x%04x",
                 event.endpoint, event.cluster);
    } else {
        ESP_LOGI(TAG, "writable state changed ep=%u cluster=0x%04x; report queued",
                 event.endpoint, event.cluster);
    }
}
'''
if old_handler not in s:
    raise SystemExit('handler block not found')
s = s.replace(old_handler, new_handler, 1)

request_marker = '''static bool set_server_attr(
    uint8_t endpoint, uint16_t cluster, uint16_t attribute, void *value)
'''
roundtrip_task = '''static void roundtrip_report_task(void *arg)
{
    (void)arg;
    emulator_roundtrip_event_t event;
    for (;;) {
        if (xQueueReceive(s_roundtrip_queue, &event, portMAX_DELAY) != pdPASS) {
            continue;
        }
        vTaskDelay(pdMS_TO_TICKS(EMULATOR_ROUNDTRIP_REPORT_DELAY_MS));
        if (!esp_zigbee_lock_acquire(pdMS_TO_TICKS(100))) {
            ESP_LOGW(TAG, "roundtrip report lock timeout ep=%u cluster=0x%04x",
                     event.endpoint, event.cluster);
            continue;
        }
        const bool sent = request_report(event.endpoint, event.cluster, event.attribute);
        esp_zigbee_lock_release();
        ESP_LOGI(TAG, "roundtrip report ep=%u cluster=0x%04x sent=%u",
                 event.endpoint, event.cluster, (unsigned)sent);
    }
}

static bool set_server_attr(
    uint8_t endpoint, uint16_t cluster, uint16_t attribute, void *value)
'''
if request_marker not in s:
    raise SystemExit('set_server_attr marker not found')
s = s.replace(request_marker, roundtrip_task, 1)

start_marker = '''    ezb_app_signal_add_handler(app_signal_handler);
    ezb_zcl_core_action_handler_register(zcl_core_action_handler);
    if (esp_zigbee_start(true) != ESP_OK) {
'''
start_repl = '''    s_roundtrip_queue = xQueueCreateStatic(
        EMULATOR_ROUNDTRIP_QUEUE_DEPTH,
        sizeof(emulator_roundtrip_event_t),
        s_roundtrip_queue_buffer,
        &s_roundtrip_queue_storage);
    if (s_roundtrip_queue == NULL) {
        ESP_LOGE(TAG, "roundtrip queue creation failed");
        vTaskDelete(NULL);
        return;
    }
    ezb_app_signal_add_handler(app_signal_handler);
    ezb_zcl_core_action_handler_register(zcl_core_action_handler);
    if (esp_zigbee_start(true) != ESP_OK) {
'''
if start_marker not in s:
    raise SystemExit('start marker not found')
s = s.replace(start_marker, start_repl, 1)

create_marker = '''    if (xTaskCreate(emulation_task, "emu_values", 3072, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "emulation task creation failed");
    }
    ESP_LOGI(TAG, "selected Zigbee emulator profile started");
'''
create_repl = '''    if (xTaskCreate(emulation_task, "emu_values", 3072, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "emulation task creation failed");
    }
#if CONFIG_EMULATOR_PROFILE_ONOFF_LEVEL || CONFIG_EMULATOR_PROFILE_MIXED
    if (xTaskCreate(roundtrip_report_task, "emu_roundtrip", 3072, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "roundtrip report task creation failed");
    }
#endif
    ESP_LOGI(TAG, "selected Zigbee emulator profile started");
'''
if create_marker not in s:
    raise SystemExit('task creation marker not found')
s = s.replace(create_marker, create_repl, 1)
p.write_text(s)

readme = Path('tests/zigbee_device_emulator/README.md')
r = readme.read_text()
r += '''\n## Writable command round-trip\n\nFor `On/Off + Level` and `Mixed multi-endpoint`, standard server-side On/Off and Level commands are handled by the Zigbee stack. The emulator watches `EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID`; when the server OnOff or CurrentLevel attribute changes, it queues a report request after a short delay so the coordinator can observe authoritative post-command state rather than treating AF transmission confirmation as application state.\n'''
readme.write_text(r)
PY

python3 tests/host/test_no_embedded_nul.py
git diff --check

. ~/esp/esp-idf-v5.5.4/export.sh >/dev/null
cd tests/zigbee_device_emulator
idf.py set-target esp32c6
idf.py build
idf.py size
rm -rf build sdkconfig sdkconfig.old
cd ../..

python3 tests/host/test_no_embedded_nul.py
if [ -f platformio.tests.ini ]; then
    pio run -c platformio.tests.ini -e test-all-host
else
    printf '%s\n' 'platformio.tests.ini absent; emulator/full ESP-IDF build is the applicable compile gate'
fi
git diff --check

git add tests/zigbee_device_emulator/main/emulator_main.c tests/zigbee_device_emulator/README.md
git diff --cached --check
git commit -m 'Report writable emulator state after commands'
git push origin HEAD:integration/c6-s3-i2c-20260903
printf 'EMULATOR_ROUNDTRIP_HEAD=%s\n' "$(git rev-parse HEAD)"
git status --short
