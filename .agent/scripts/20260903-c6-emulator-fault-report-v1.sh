#!/usr/bin/env bash
set -euo pipefail

git fetch origin
git checkout integration/c6-s3-i2c-20260903
git reset --hard origin/integration/c6-s3-i2c-20260903
rm -rf tests/zigbee_device_emulator/build tests/zigbee_device_emulator/sdkconfig tests/zigbee_device_emulator/sdkconfig.old
[ -z "$(git status --short)" ]
[ "$(git rev-parse HEAD)" = "e4b9305224d823ccdbe679db125491337f60fc56" ]

python3 - <<'PY'
from pathlib import Path

kconfig = Path('tests/zigbee_device_emulator/main/Kconfig.projbuild')
s = kconfig.read_text()
needle = 'endchoice\n\nendmenu\n'
replacement = '''endchoice

config EMULATOR_EXPLICIT_REPORTS
    bool "Explicitly request reports after value changes"
    default y
    help
        Calls the stack report-attribute request after deterministic value updates.
        The stack still applies its configured-reporting rules; this makes report
        generation explicit for emulator burst/fault exercises.

config EMULATOR_BURST_UPDATES
    bool "Burst value updates"
    default n
    help
        Apply 24 deterministic update/report requests per emulator tick to stress
        coordinator queues without requiring hardware-side timing tricks.

config EMULATOR_INJECT_INVALID_VALUES
    bool "Inject invalid/reserved measurement values"
    default n
    help
        Every sixth tick emits Zigbee invalid sentinels for temperature and
        humidity plus a reserved occupancy bitmap value.

config EMULATOR_TICK_MS
    int "Emulator tick interval (ms)"
    range 1000 60000
    default 10000

endmenu
'''
if needle not in s:
    raise SystemExit('Kconfig marker not found')
kconfig.write_text(s.replace(needle, replacement, 1))

main = Path('tests/zigbee_device_emulator/main/emulator_main.c')
s = main.read_text()
s = s.replace('#include <ezbee/zcl/zcl_core.h>\n', '#include <ezbee/zcl/zcl_core.h>\n#include <ezbee/zcl/zcl_general_cmd.h>\n', 1)
s = s.replace('#define EMULATOR_STD_MANUF_CODE 0x0000U\n', '''#define EMULATOR_STD_MANUF_CODE 0x0000U
#define EMULATOR_COORDINATOR_SHORT 0x0000U
#define EMULATOR_COORDINATOR_ENDPOINT 1U
#define EMULATOR_BURST_COUNT 24U
#define EMULATOR_INVALID_TEMPERATURE ((int16_t)0x8000)
#define EMULATOR_INVALID_HUMIDITY 0xffffU
#define EMULATOR_RESERVED_OCCUPANCY 0x02U
''', 1)
s = s.replace('static void set_server_attr(\n    uint8_t endpoint, uint16_t cluster, uint16_t attribute, void *value)\n{\n    const ezb_zcl_status_t status = ezb_zcl_set_attr_value(\n        endpoint, cluster, EZB_ZCL_CLUSTER_SERVER, attribute,\n        EMULATOR_STD_MANUF_CODE, value, false);\n    if (status != 0U) {\n        ESP_LOGW(TAG, "set attr ep=%u cluster=0x%04x attr=0x%04x status=0x%02x",\n                 endpoint, cluster, attribute, (unsigned)status);\n    }\n}\n', '''static bool request_report(uint8_t endpoint, uint16_t cluster, uint16_t attribute)
{
    const ezb_zcl_report_attr_cmd_t request = {
        .cmd_ctrl = {
            .dst_addr = EZB_ADDRESS_SHORT(EMULATOR_COORDINATOR_SHORT),
            .dst_ep = EMULATOR_COORDINATOR_ENDPOINT,
            .src_ep = endpoint,
            .cluster_id = cluster,
            .manuf_code = EMULATOR_STD_MANUF_CODE,
        },
        .payload = {.attr_id = attribute},
    };
    const ezb_err_t error = ezb_zcl_report_attr_cmd_req(&request);
    if (error != EZB_ERR_NONE) {
        ESP_LOGW(TAG, "report request ep=%u cluster=0x%04x attr=0x%04x error=%u",
                 endpoint, cluster, attribute, (unsigned)error);
        return false;
    }
    return true;
}

static bool set_server_attr(
    uint8_t endpoint, uint16_t cluster, uint16_t attribute, void *value)
{
    const ezb_zcl_status_t status = ezb_zcl_set_attr_value(
        endpoint, cluster, EZB_ZCL_CLUSTER_SERVER, attribute,
        EMULATOR_STD_MANUF_CODE, value, false);
    if (status != 0U) {
        ESP_LOGW(TAG, "set attr ep=%u cluster=0x%04x attr=0x%04x status=0x%02x",
                 endpoint, cluster, attribute, (unsigned)status);
        return false;
    }
#if CONFIG_EMULATOR_EXPLICIT_REPORTS
    (void)request_report(endpoint, cluster, attribute);
#endif
    return true;
}
''', 1)
old = '''    uint8_t occupancy = 0U;

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
'''
new = '''    uint8_t occupancy = 0U;
    uint32_t tick = 0U;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(CONFIG_EMULATOR_TICK_MS));
        ++tick;
        const uint8_t iterations = CONFIG_EMULATOR_BURST_UPDATES ? EMULATOR_BURST_COUNT : 1U;
        if (!esp_zigbee_lock_acquire(pdMS_TO_TICKS(100))) {
            ESP_LOGW(TAG, "emulation update lock timeout");
            continue;
        }

        for (uint8_t i = 0U; i < iterations; ++i) {
#if CONFIG_EMULATOR_PROFILE_TEMP_HUM || CONFIG_EMULATOR_PROFILE_MIXED
            temperature = (int16_t)(temperature + temperature_step);
            if (temperature >= 2350 || temperature <= 1950) {
                temperature_step = (int16_t)-temperature_step;
            }
            humidity = (uint16_t)((int32_t)humidity + humidity_step);
            if (humidity >= 6500U || humidity <= 4500U) {
                humidity_step = (int16_t)-humidity_step;
            }
            int16_t temperature_out = temperature;
            uint16_t humidity_out = humidity;
            if (CONFIG_EMULATOR_INJECT_INVALID_VALUES && (tick % 6U) == 0U) {
                temperature_out = EMULATOR_INVALID_TEMPERATURE;
                humidity_out = EMULATOR_INVALID_HUMIDITY;
            }
            (void)set_server_attr(
                EMULATOR_ENV_ENDPOINT, EMULATOR_CLUSTER_TEMPERATURE,
                EMULATOR_ATTR_MEASURED_VALUE, &temperature_out);
            (void)set_server_attr(
                EMULATOR_ENV_ENDPOINT, EMULATOR_CLUSTER_HUMIDITY,
                EMULATOR_ATTR_MEASURED_VALUE, &humidity_out);
#endif

#if CONFIG_EMULATOR_PROFILE_OCCUPANCY || CONFIG_EMULATOR_PROFILE_MIXED
            occupancy = occupancy == 0U ? 1U : 0U;
            uint8_t occupancy_out = occupancy;
            if (CONFIG_EMULATOR_INJECT_INVALID_VALUES && (tick % 6U) == 0U) {
                occupancy_out = EMULATOR_RESERVED_OCCUPANCY;
            }
            (void)set_server_attr(
                EMULATOR_OCC_ENDPOINT, EMULATOR_CLUSTER_OCCUPANCY,
                EMULATOR_ATTR_OCCUPANCY, &occupancy_out);
#endif
        }

        esp_zigbee_lock_release();
        ESP_LOGI(TAG, "emulator tick=%lu iterations=%u invalid=%u",
                 (unsigned long)tick, (unsigned)iterations,
                 (unsigned)(CONFIG_EMULATOR_INJECT_INVALID_VALUES && (tick % 6U) == 0U));
    }
'''
if old not in s:
    raise SystemExit('emulation task block not found')
s = s.replace(old, new, 1)
main.write_text(s)

readme = Path('tests/zigbee_device_emulator/README.md')
s = readme.read_text()
s += '''\n## Deterministic fault/report modes\n\nMenuconfig also exposes explicit report requests, a 24-update burst mode, invalid-value injection, and the tick interval. Invalid injection uses the Zigbee Temperature Measurement invalid sentinel `0x8000`, Relative Humidity invalid sentinel `0xffff`, and reserved Occupancy bitmap `0x02` every sixth tick. These modes are compile-time deterministic and require no production firmware coupling.\n\nExplicit report requests target coordinator short address `0x0000`, endpoint 1, which matches the Zigbee coordinator role and this repository's production gateway endpoint. The Zigbee stack still applies its reporting configuration rules.\n'''
readme.write_text(s)
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

git add tests/zigbee_device_emulator/main/Kconfig.projbuild \
        tests/zigbee_device_emulator/main/emulator_main.c \
        tests/zigbee_device_emulator/README.md
git diff --cached --check
git commit -m 'Add deterministic Zigbee emulator fault modes'
git push origin HEAD:integration/c6-s3-i2c-20260903
printf 'EMULATOR_FAULT_HEAD=%s\n' "$(git rev-parse HEAD)"
git status --short
