#!/usr/bin/env bash
set -euo pipefail

git fetch origin
git checkout integration/c6-s3-i2c-20260903
git reset --hard origin/integration/c6-s3-i2c-20260903
rm -rf tests/zigbee_device_emulator/build tests/zigbee_device_emulator/build-sleepy \
       tests/zigbee_device_emulator/sdkconfig tests/zigbee_device_emulator/sdkconfig.old
[ -z "$(git status --short)" ]
[ "$(git rev-parse HEAD)" = "7d2616c4490691ff354f871e2f17d804a6d94162" ]

python3 - <<'PY'
from pathlib import Path

kconfig = Path('tests/zigbee_device_emulator/main/Kconfig.projbuild')
s = kconfig.read_text()
s = s.replace(
'''config EMULATOR_PROFILE_ONOFF_LEVEL
    bool "On/Off + Level"

config EMULATOR_PROFILE_MIXED
''',
'''config EMULATOR_PROFILE_ONOFF_LEVEL
    bool "On/Off + Level"

config EMULATOR_PROFILE_SLEEPY_ENV_BATTERY
    bool "Sleepy temperature + humidity + battery + Poll Control"

config EMULATOR_PROFILE_MIXED
''', 1)
s = s.replace(
'''config EMULATOR_TICK_MS
    int "Emulator tick interval (ms)"
    range 1000 60000
    default 10000

endmenu
''',
'''config EMULATOR_TICK_MS
    int "Emulator tick interval (ms)"
    range 1000 60000
    default 30000 if EMULATOR_PROFILE_SLEEPY_ENV_BATTERY
    default 10000

config EMULATOR_SLEEPY_KEEP_ALIVE_MS
    int "Sleepy end-device keep-alive (ms)"
    depends on EMULATOR_PROFILE_SLEEPY_ENV_BATTERY
    range 1000 60000
    default 10000
    help
        Parent poll/keep-alive timing used by the Zigbee end-device stack. This
        emulates a sleepy protocol surface; it is not a deep-sleep power test.

endmenu
''', 1)
kconfig.write_text(s)

main = Path('tests/zigbee_device_emulator/main/emulator_main.c')
s = main.read_text()
s = s.replace(
'#include <ezbee/zcl/cluster/on_off_desc.h>\n',
'#include <ezbee/zcl/cluster/on_off_desc.h>\n#include <ezbee/zcl/cluster/poll_control_desc.h>\n#include <ezbee/zcl/cluster/power_config_desc.h>\n', 1)
s = s.replace(
'''#define EMULATOR_CLUSTER_TEMPERATURE 0x0402U
#define EMULATOR_CLUSTER_HUMIDITY 0x0405U
''',
'''#define EMULATOR_CLUSTER_POWER_CONFIG 0x0001U
#define EMULATOR_CLUSTER_POLL_CONTROL 0x0020U
#define EMULATOR_CLUSTER_TEMPERATURE 0x0402U
#define EMULATOR_CLUSTER_HUMIDITY 0x0405U
''', 1)
s = s.replace(
'''#define EMULATOR_ATTR_MEASURED_VALUE 0x0000U
#define EMULATOR_ATTR_OCCUPANCY 0x0000U
''',
'''#define EMULATOR_ATTR_MEASURED_VALUE 0x0000U
#define EMULATOR_ATTR_OCCUPANCY 0x0000U
#define EMULATOR_ATTR_BATTERY_VOLTAGE 0x0020U
#define EMULATOR_ATTR_BATTERY_PERCENTAGE_REMAINING 0x0021U

#if CONFIG_EMULATOR_PROFILE_SLEEPY_ENV_BATTERY
#define EMULATOR_KEEP_ALIVE_MS CONFIG_EMULATOR_SLEEPY_KEEP_ALIVE_MS
#else
#define EMULATOR_KEEP_ALIVE_MS 3000U
#endif
''', 1)
s = s.replace(
'''#elif CONFIG_EMULATOR_PROFILE_ONOFF_LEVEL
#define EMULATOR_LIGHT_ENDPOINT 1U
#endif
''',
'''#elif CONFIG_EMULATOR_PROFILE_ONOFF_LEVEL
#define EMULATOR_LIGHT_ENDPOINT 1U
#elif CONFIG_EMULATOR_PROFILE_SLEEPY_ENV_BATTERY
#define EMULATOR_ENV_ENDPOINT 1U
#endif
''', 1)

marker = '''static esp_err_t add_occupancy_endpoint(ezb_af_device_desc_t device, uint8_t endpoint_id)
'''
sleepy_fn = r'''static esp_err_t add_sleepy_environment_endpoint(
    ezb_af_device_desc_t device, uint8_t endpoint_id)
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

    static const uint8_t initial_battery_voltage = 30U;      /* 3.0 V */
    static const uint8_t initial_battery_percentage = 180U; /* 90%, half-percent units */
    ezb_zcl_cluster_desc_t power = ezb_zcl_power_config_create_cluster_desc(
        NULL, EZB_ZCL_CLUSTER_SERVER);
    if (power != EZB_INVALID_ZCL_CLUSTER_DESC &&
        (ezb_zcl_power_config_cluster_desc_add_attr(
             power, EMULATOR_ATTR_BATTERY_VOLTAGE, &initial_battery_voltage) != EZB_ERR_NONE ||
         ezb_zcl_power_config_cluster_desc_add_attr(
             power, EMULATOR_ATTR_BATTERY_PERCENTAGE_REMAINING,
             &initial_battery_percentage) != EZB_ERR_NONE)) {
        return ESP_FAIL;
    }

    const ezb_zcl_poll_control_cluster_server_config_t poll_config = {
        .check_in_interval = 120U,  /* 30 s, quarter-second units */
        .long_poll_interval = 20U, /* 5 s */
        .short_poll_interval = 4U, /* 1 s */
        .fast_poll_timeout = 20U,  /* 5 s */
    };
    ezb_zcl_cluster_desc_t poll = ezb_zcl_poll_control_create_cluster_desc(
        &poll_config, EZB_ZCL_CLUSTER_SERVER);

    if (temp == EZB_INVALID_ZCL_CLUSTER_DESC ||
        humidity == EZB_INVALID_ZCL_CLUSTER_DESC ||
        power == EZB_INVALID_ZCL_CLUSTER_DESC ||
        poll == EZB_INVALID_ZCL_CLUSTER_DESC ||
        ezb_af_endpoint_add_cluster_desc(endpoint, temp) != EZB_ERR_NONE ||
        ezb_af_endpoint_add_cluster_desc(endpoint, humidity) != EZB_ERR_NONE ||
        ezb_af_endpoint_add_cluster_desc(endpoint, power) != EZB_ERR_NONE ||
        ezb_af_endpoint_add_cluster_desc(endpoint, poll) != EZB_ERR_NONE ||
        ezb_af_device_add_endpoint_desc(device, endpoint) != EZB_ERR_NONE) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

'''
if marker not in s:
    raise SystemExit('occupancy function marker not found')
s = s.replace(marker, sleepy_fn + marker, 1)

s = s.replace(
'''#elif CONFIG_EMULATOR_PROFILE_ONOFF_LEVEL
    if (add_light_endpoint(device, EMULATOR_LIGHT_ENDPOINT) != ESP_OK) {
        return ESP_FAIL;
    }
#elif CONFIG_EMULATOR_PROFILE_MIXED
''',
'''#elif CONFIG_EMULATOR_PROFILE_ONOFF_LEVEL
    if (add_light_endpoint(device, EMULATOR_LIGHT_ENDPOINT) != ESP_OK) {
        return ESP_FAIL;
    }
#elif CONFIG_EMULATOR_PROFILE_SLEEPY_ENV_BATTERY
    if (add_sleepy_environment_endpoint(device, EMULATOR_ENV_ENDPOINT) != ESP_OK) {
        return ESP_FAIL;
    }
#elif CONFIG_EMULATOR_PROFILE_MIXED
''', 1)

s = s.replace(
'''    uint8_t occupancy = 0U;
    uint32_t tick = 0U;
''',
'''    uint8_t occupancy = 0U;
    uint8_t battery_voltage = 30U;
    uint8_t battery_percentage = 180U;
    uint32_t tick = 0U;
''', 1)
s = s.replace(
'''#if CONFIG_EMULATOR_PROFILE_TEMP_HUM || CONFIG_EMULATOR_PROFILE_MIXED
''',
'''#if CONFIG_EMULATOR_PROFILE_TEMP_HUM || CONFIG_EMULATOR_PROFILE_MIXED || \
    CONFIG_EMULATOR_PROFILE_SLEEPY_ENV_BATTERY
''', 1)
needle = '''            (void)set_server_attr(
                EMULATOR_ENV_ENDPOINT, EMULATOR_CLUSTER_HUMIDITY,
                EMULATOR_ATTR_MEASURED_VALUE, &humidity_out);
#endif

#if CONFIG_EMULATOR_PROFILE_OCCUPANCY || CONFIG_EMULATOR_PROFILE_MIXED
'''
replacement = '''            (void)set_server_attr(
                EMULATOR_ENV_ENDPOINT, EMULATOR_CLUSTER_HUMIDITY,
                EMULATOR_ATTR_MEASURED_VALUE, &humidity_out);

#if CONFIG_EMULATOR_PROFILE_SLEEPY_ENV_BATTERY
            if (battery_percentage > 120U) {
                battery_percentage = (uint8_t)(battery_percentage - 2U);
            } else {
                battery_percentage = 180U;
            }
            battery_voltage = (uint8_t)(27U + ((battery_percentage - 120U) / 20U));
            (void)set_server_attr(
                EMULATOR_ENV_ENDPOINT, EMULATOR_CLUSTER_POWER_CONFIG,
                EMULATOR_ATTR_BATTERY_VOLTAGE, &battery_voltage);
            (void)set_server_attr(
                EMULATOR_ENV_ENDPOINT, EMULATOR_CLUSTER_POWER_CONFIG,
                EMULATOR_ATTR_BATTERY_PERCENTAGE_REMAINING, &battery_percentage);
#endif
#endif

#if CONFIG_EMULATOR_PROFILE_OCCUPANCY || CONFIG_EMULATOR_PROFILE_MIXED
'''
if needle not in s:
    raise SystemExit('environment update marker not found')
s = s.replace(needle, replacement, 1)
s = s.replace('.zed_config = {.keep_alive = 3000U},',
              '.zed_config = {.keep_alive = EMULATOR_KEEP_ALIVE_MS},', 1)
main.write_text(s)

readme = Path('tests/zigbee_device_emulator/README.md')
s = readme.read_text()
s = s.replace(
'- `On/Off + Level`: endpoint 1, Basic + On/Off + Level Control server clusters.\n',
'- `On/Off + Level`: endpoint 1, Basic + On/Off + Level Control server clusters.\n'
'- `Sleepy temperature + humidity + battery + Poll Control`: endpoint 1, Basic + Temperature + Humidity + Power Configuration + Poll Control.\n', 1)
s += '''\n## Sleepy end-device profile\n\nThe sleepy profile exposes battery voltage (`0x0020`) and battery percentage remaining (`0x0021`) on Power Configuration plus a Poll Control server. Its defaults are a 30 s Check-In interval, 5 s long poll, 1 s short poll, 5 s fast-poll timeout, 10 s Zigbee keep-alive, and a 30 s emulator value tick. Battery percentage changes deterministically in Zigbee half-percent units.\n\nThis profile emulates sleepy Zigbee protocol timing and Poll Control behavior. It intentionally does not claim MCU deep-sleep/current-consumption behavior; that requires the later two-board hardware validation gate.\n'''
readme.write_text(s)
PY

python3 tests/host/test_no_embedded_nul.py
git diff --check
python3 - <<'PY'
import subprocess
bad=[]
for p in subprocess.check_output(['git','diff','--name-only'], text=True).splitlines():
    if not p.startswith('tests/zigbee_device_emulator/'):
        bad.append(p)
if bad:
    raise SystemExit('unexpected changed paths: ' + ', '.join(bad))
print('emulator-only diff scope passed')
PY

. ~/esp/esp-idf-v5.5.4/export.sh >/dev/null
cd tests/zigbee_device_emulator
idf.py set-target esp32c6
idf.py build
idf.py size
rm -rf build sdkconfig sdkconfig.old

cat > /tmp/c6-emulator-sleepy.defaults <<'EOF'
CONFIG_EMULATOR_PROFILE_SLEEPY_ENV_BATTERY=y
CONFIG_EMULATOR_EXPLICIT_REPORTS=y
CONFIG_EMULATOR_TICK_MS=30000
CONFIG_EMULATOR_SLEEPY_KEEP_ALIVE_MS=10000
EOF
idf.py -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;/tmp/c6-emulator-sleepy.defaults' -B build-sleepy set-target esp32c6
idf.py -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;/tmp/c6-emulator-sleepy.defaults' -B build-sleepy build
idf.py -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;/tmp/c6-emulator-sleepy.defaults' -B build-sleepy size
rm -rf build-sleepy sdkconfig sdkconfig.old
cd ../..

python3 tests/host/test_no_embedded_nul.py
git diff --check

git add tests/zigbee_device_emulator/main/Kconfig.projbuild \
        tests/zigbee_device_emulator/main/emulator_main.c \
        tests/zigbee_device_emulator/README.md
git diff --cached --check
git commit -m 'Add sleepy battery Zigbee emulator profile'
git push origin HEAD:integration/c6-s3-i2c-20260903
printf 'EMULATOR_SLEEPY_HEAD=%s\n' "$(git rev-parse HEAD)"
git status --short
