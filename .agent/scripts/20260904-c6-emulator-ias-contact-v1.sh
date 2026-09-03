#!/usr/bin/env bash
set -euo pipefail

git fetch origin
git checkout integration/c6-s3-i2c-20260903
git reset --hard origin/integration/c6-s3-i2c-20260903
rm -rf tests/zigbee_device_emulator/build tests/zigbee_device_emulator/sdkconfig tests/zigbee_device_emulator/sdkconfig.old
[ -z "$(git status --short)" ]
[ "$(git rev-parse HEAD)" = "d2a7bc285e2b67c342c7ea80ffaca00e6e8c5e0e" ]

python3 - <<'PY'
from pathlib import Path

kconfig = Path('tests/zigbee_device_emulator/main/Kconfig.projbuild')
s = kconfig.read_text()
needle = '''config EMULATOR_PROFILE_SLEEPY_ENV_BATTERY
    bool "Sleepy temperature + humidity + battery + Poll Control"

config EMULATOR_PROFILE_MIXED
'''
replacement = '''config EMULATOR_PROFILE_SLEEPY_ENV_BATTERY
    bool "Sleepy temperature + humidity + battery + Poll Control"

config EMULATOR_PROFILE_IAS_CONTACT
    bool "IAS Zone contact switch"

config EMULATOR_PROFILE_MIXED
'''
if needle not in s:
    raise SystemExit('Kconfig IAS profile marker not found')
kconfig.write_text(s.replace(needle, replacement, 1))

main = Path('tests/zigbee_device_emulator/main/emulator_main.c')
s = main.read_text()

needle = '#include <ezbee/zcl/cluster/basic_desc.h>\n'
replacement = '#include <ezbee/zcl/cluster/basic_desc.h>\n#include <ezbee/zcl/cluster/ias_zone_desc.h>\n'
if needle not in s:
    raise SystemExit('IAS include marker not found')
s = s.replace(needle, replacement, 1)

needle = '''#define EMULATOR_CLUSTER_LEVEL 0x0008U
#define EMULATOR_ATTR_MEASURED_VALUE 0x0000U
'''
replacement = '''#define EMULATOR_CLUSTER_LEVEL 0x0008U
#define EMULATOR_CLUSTER_IAS_ZONE 0x0500U
#define EMULATOR_ATTR_MEASURED_VALUE 0x0000U
#define EMULATOR_ATTR_IAS_ZONE_TYPE 0x0001U
#define EMULATOR_ATTR_IAS_ZONE_STATUS 0x0002U
'''
if needle not in s:
    raise SystemExit('IAS constants marker not found')
s = s.replace(needle, replacement, 1)

needle = '''#elif CONFIG_EMULATOR_PROFILE_SLEEPY_ENV_BATTERY
#define EMULATOR_ENV_ENDPOINT 1U
#endif
'''
replacement = '''#elif CONFIG_EMULATOR_PROFILE_SLEEPY_ENV_BATTERY
#define EMULATOR_ENV_ENDPOINT 1U
#elif CONFIG_EMULATOR_PROFILE_IAS_CONTACT
#define EMULATOR_IAS_ENDPOINT 1U
#endif
'''
if needle not in s:
    raise SystemExit('IAS endpoint marker not found')
s = s.replace(needle, replacement, 1)

needle = '''static esp_err_t add_light_endpoint(ezb_af_device_desc_t device, uint8_t endpoint_id)
{
'''
insert = '''static esp_err_t add_ias_contact_endpoint(ezb_af_device_desc_t device, uint8_t endpoint_id)
{
    ezb_af_ep_desc_t endpoint = create_endpoint(endpoint_id, EMULATOR_DEVICE_ID_GENERIC);
    if (endpoint == EZB_INVALID_AF_EP_DESC || add_basic(endpoint) != ESP_OK) {
        return ESP_FAIL;
    }

    ezb_zcl_cluster_desc_t ias_zone = ezb_zcl_ias_zone_create_cluster_desc(
        NULL, EZB_ZCL_CLUSTER_SERVER);
    if (ias_zone == EZB_INVALID_ZCL_CLUSTER_DESC ||
        ezb_af_endpoint_add_cluster_desc(endpoint, ias_zone) != EZB_ERR_NONE ||
        ezb_af_device_add_endpoint_desc(device, endpoint) != EZB_ERR_NONE) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t add_light_endpoint(ezb_af_device_desc_t device, uint8_t endpoint_id)
{
'''
if needle not in s:
    raise SystemExit('IAS endpoint function marker not found')
s = s.replace(needle, insert, 1)

needle = '''#elif CONFIG_EMULATOR_PROFILE_SLEEPY_ENV_BATTERY
    if (add_sleepy_environment_endpoint(device, EMULATOR_ENV_ENDPOINT) != ESP_OK) {
        return ESP_FAIL;
    }
#elif CONFIG_EMULATOR_PROFILE_MIXED
'''
replacement = '''#elif CONFIG_EMULATOR_PROFILE_SLEEPY_ENV_BATTERY
    if (add_sleepy_environment_endpoint(device, EMULATOR_ENV_ENDPOINT) != ESP_OK) {
        return ESP_FAIL;
    }
#elif CONFIG_EMULATOR_PROFILE_IAS_CONTACT
    if (add_ias_contact_endpoint(device, EMULATOR_IAS_ENDPOINT) != ESP_OK) {
        return ESP_FAIL;
    }
#elif CONFIG_EMULATOR_PROFILE_MIXED
'''
if needle not in s:
    raise SystemExit('IAS profile registration marker not found')
s = s.replace(needle, replacement, 1)

needle = '''static bool request_report(uint8_t endpoint, uint16_t cluster, uint16_t attribute)
{
'''
insert = '''#if CONFIG_EMULATOR_PROFILE_IAS_CONTACT
static bool initialize_ias_contact_attributes(void)
{
    uint16_t zone_type = EZB_ZCL_IAS_ZONE_ZONE_TYPE_CONTACT_SWITCH;
    uint16_t zone_status = 0U;
    const ezb_zcl_status_t type_status = ezb_zcl_set_attr_value(
        EMULATOR_IAS_ENDPOINT, EMULATOR_CLUSTER_IAS_ZONE,
        EZB_ZCL_CLUSTER_SERVER, EMULATOR_ATTR_IAS_ZONE_TYPE,
        EMULATOR_STD_MANUF_CODE, &zone_type, false);
    const ezb_zcl_status_t status_status = ezb_zcl_set_attr_value(
        EMULATOR_IAS_ENDPOINT, EMULATOR_CLUSTER_IAS_ZONE,
        EZB_ZCL_CLUSTER_SERVER, EMULATOR_ATTR_IAS_ZONE_STATUS,
        EMULATOR_STD_MANUF_CODE, &zone_status, false);
    if (type_status != 0U || status_status != 0U) {
        ESP_LOGE(TAG, "IAS contact init failed type=0x%02x status=0x%02x",
                 (unsigned)type_status, (unsigned)status_status);
        return false;
    }
    ESP_LOGI(TAG, "IAS contact initialized ZoneType=0x%04x ZoneStatus=0x%04x",
             (unsigned)zone_type, (unsigned)zone_status);
    return true;
}
#endif

static bool request_report(uint8_t endpoint, uint16_t cluster, uint16_t attribute)
{
'''
if needle not in s:
    raise SystemExit('IAS init marker not found')
s = s.replace(needle, insert, 1)

needle = '''    uint8_t battery_voltage = 30U;
    uint8_t battery_percentage = 180U;
    uint32_t tick = 0U;
'''
replacement = '''    uint8_t battery_voltage = 30U;
    uint8_t battery_percentage = 180U;
    uint16_t ias_zone_status = 0U;
    uint32_t tick = 0U;
'''
if needle not in s:
    raise SystemExit('IAS emulation state marker not found')
s = s.replace(needle, replacement, 1)

needle = '''#if CONFIG_EMULATOR_PROFILE_OCCUPANCY || CONFIG_EMULATOR_PROFILE_MIXED
            occupancy = occupancy == 0U ? 1U : 0U;
'''
insert = '''#if CONFIG_EMULATOR_PROFILE_IAS_CONTACT
            ias_zone_status = ias_zone_status == 0U ?
                EZB_ZCL_IAS_ZONE_ZONE_STATUS_ALARM1 : 0U;
            (void)set_server_attr(
                EMULATOR_IAS_ENDPOINT, EMULATOR_CLUSTER_IAS_ZONE,
                EMULATOR_ATTR_IAS_ZONE_STATUS, &ias_zone_status);
#endif

#if CONFIG_EMULATOR_PROFILE_OCCUPANCY || CONFIG_EMULATOR_PROFILE_MIXED
            occupancy = occupancy == 0U ? 1U : 0U;
'''
if needle not in s:
    raise SystemExit('IAS emulation update marker not found')
s = s.replace(needle, insert, 1)

needle = '''    if (esp_zigbee_start(true) != ESP_OK) {
        ESP_LOGE(TAG, "esp_zigbee_start failed");
        vTaskDelete(NULL);
        return;
    }
    if (xTaskCreate(emulation_task, "emu_values", 3072, NULL, 4, NULL) != pdPASS) {
'''
replacement = '''    if (esp_zigbee_start(true) != ESP_OK) {
        ESP_LOGE(TAG, "esp_zigbee_start failed");
        vTaskDelete(NULL);
        return;
    }
#if CONFIG_EMULATOR_PROFILE_IAS_CONTACT
    if (!initialize_ias_contact_attributes()) {
        vTaskDelete(NULL);
        return;
    }
#endif
    if (xTaskCreate(emulation_task, "emu_values", 3072, NULL, 4, NULL) != pdPASS) {
'''
if needle not in s:
    raise SystemExit('IAS startup marker not found')
s = s.replace(needle, replacement, 1)
main.write_text(s)

readme = Path('tests/zigbee_device_emulator/README.md')
s = readme.read_text()
needle = '- `Sleepy temperature + humidity + battery + Poll Control`: endpoint 1, Basic + Temperature + Humidity + Power Configuration + Poll Control.\n'
replacement = needle + '- `IAS Zone contact switch`: endpoint 1, Basic + IAS Zone server with ZoneType `ContactSwitch`; ZoneStatus toggles Alarm1 deterministically.\n'
if needle not in s:
    raise SystemExit('README profile marker not found')
s = s.replace(needle, replacement, 1)
s += '''\n## IAS contact profile\n\nThe IAS contact profile uses the standard IAS Zone cluster (`0x0500`) with `ZoneType=ContactSwitch` (`0x0015`). It toggles the `ZoneStatus.Alarm1` bit on each emulator tick. The ezbee IAS API documents that changing ZoneStatus can trigger the standard Zone Status Change Notification; the emulator also exposes the authoritative ZoneStatus attribute. Enrollment/CIE behavior is left to the Zigbee stack and later two-board validation rather than being faked in application code.\n'''
readme.write_text(s)
PY

python3 tests/host/test_no_embedded_nul.py
git diff --check

. ~/esp/esp-idf-v5.5.4/export.sh >/dev/null
cd tests/zigbee_device_emulator
idf.py set-target esp32c6
idf.py build
idf.py size
python3 - <<'PY'
from pathlib import Path
p=Path('sdkconfig')
s=p.read_text()
if 'CONFIG_EMULATOR_PROFILE_MIXED=y' not in s:
    raise SystemExit('mixed profile selection not found in sdkconfig')
s=s.replace('CONFIG_EMULATOR_PROFILE_MIXED=y', '# CONFIG_EMULATOR_PROFILE_MIXED is not set\nCONFIG_EMULATOR_PROFILE_IAS_CONTACT=y', 1)
p.write_text(s)
PY
idf.py reconfigure
idf.py build
idf.py size
rm -rf build sdkconfig sdkconfig.old
cd ../..

python3 tests/host/test_no_embedded_nul.py
git diff --check

git add tests/zigbee_device_emulator/main/Kconfig.projbuild \
        tests/zigbee_device_emulator/main/emulator_main.c \
        tests/zigbee_device_emulator/README.md
git diff --cached --check
git commit -m 'Add IAS contact Zigbee emulator profile'
git push origin HEAD:integration/c6-s3-i2c-20260903
printf 'EMULATOR_IAS_CONTACT_HEAD=%s\n' "$(git rev-parse HEAD)"
git status --short
