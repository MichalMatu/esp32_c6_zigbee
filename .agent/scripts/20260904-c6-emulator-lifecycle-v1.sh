#!/usr/bin/env bash
set -euo pipefail

git fetch origin
git checkout integration/c6-s3-i2c-20260903
git reset --hard origin/integration/c6-s3-i2c-20260903
rm -rf tests/zigbee_device_emulator/build tests/zigbee_device_emulator/sdkconfig tests/zigbee_device_emulator/sdkconfig.old
[ -z "$(git status --short)" ]
[ "$(git rev-parse HEAD)" = "700880d383b0d50a27d9f195ed2cc24325feff98" ]

python3 - <<'PY'
from pathlib import Path

kconfig = Path('tests/zigbee_device_emulator/main/Kconfig.projbuild')
s = kconfig.read_text()
needle = '''config EMULATOR_SLEEPY_KEEP_ALIVE_MS
    int "Sleepy end-device keep-alive (ms)"
    depends on EMULATOR_PROFILE_SLEEPY_ENV_BATTERY
    range 1000 60000
    default 10000
    help
        Parent poll/keep-alive timing used by the Zigbee end-device stack. This
        emulates a sleepy protocol surface; it is not a deep-sleep power test.

endmenu
'''
replacement = '''config EMULATOR_SLEEPY_KEEP_ALIVE_MS
    int "Sleepy end-device keep-alive (ms)"
    depends on EMULATOR_PROFILE_SLEEPY_ENV_BATTERY
    range 1000 60000
    default 10000
    help
        Parent poll/keep-alive timing used by the Zigbee end-device stack. This
        emulates a sleepy protocol surface; it is not a deep-sleep power test.

config EMULATOR_LIFECYCLE_REBOOT
    bool "Periodic lifecycle reboot/rejoin exercise"
    default n
    help
        Periodically reboots the emulator without erasing Zigbee storage. This
        exercises persisted end-device reboot/rejoin lifecycle handling without
        relying on ezbee test-only leave APIs.

config EMULATOR_LIFECYCLE_REBOOT_MS
    int "Lifecycle reboot interval (ms)"
    depends on EMULATOR_LIFECYCLE_REBOOT
    range 10000 600000
    default 60000

endmenu
'''
if needle not in s:
    raise SystemExit('Kconfig lifecycle marker not found')
kconfig.write_text(s.replace(needle, replacement, 1))

main = Path('tests/zigbee_device_emulator/main/emulator_main.c')
s = main.read_text()
s = s.replace('#include "esp_log.h"\n#include "esp_zigbee.h"\n', '#include "esp_log.h"\n#include "esp_system.h"\n#include "esp_zigbee.h"\n', 1)
needle = '''static void zigbee_task(void *arg)
{
'''
insert = '''#ifdef CONFIG_EMULATOR_LIFECYCLE_REBOOT
static void lifecycle_reboot_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(CONFIG_EMULATOR_LIFECYCLE_REBOOT_MS));
    ESP_LOGW(TAG,
             "lifecycle fault: rebooting after %u ms with Zigbee storage preserved",
             (unsigned)CONFIG_EMULATOR_LIFECYCLE_REBOOT_MS);
    esp_restart();
}
#endif

static void zigbee_task(void *arg)
{
'''
if needle not in s:
    raise SystemExit('zigbee task marker not found')
s = s.replace(needle, insert, 1)
needle = '''    if (xTaskCreate(emulation_task, "emu_values", 3072, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "emulation task creation failed");
    }
'''
replacement = '''    if (xTaskCreate(emulation_task, "emu_values", 3072, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "emulation task creation failed");
    }
#ifdef CONFIG_EMULATOR_LIFECYCLE_REBOOT
    if (xTaskCreate(lifecycle_reboot_task, "emu_lifecycle", 2048, NULL, 3, NULL) != pdPASS) {
        ESP_LOGE(TAG, "lifecycle reboot task creation failed");
    }
#endif
'''
if needle not in s:
    raise SystemExit('emulation task creation marker not found')
s = s.replace(needle, replacement, 1)
main.write_text(s)

readme = Path('tests/zigbee_device_emulator/README.md')
s = readme.read_text()
s += '''\n## Lifecycle reboot/rejoin fault\n\nAn optional menuconfig fault periodically calls `esp_restart()` while preserving the Zigbee storage partition. On the next boot the end device starts with its persisted network state, exercising coordinator duplicate lifecycle/reboot/rejoin handling without depending on ezbee's test-only `ezb_nwk_leave_request()` API. The default interval is 60 s and the mode is disabled by default.\n\nThis mode deliberately does not promise a new NWK short address or exact Device Announce ordering; those are stack/network outcomes and remain part of the later two-board hardware validation.\n'''
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
old='# CONFIG_EMULATOR_LIFECYCLE_REBOOT is not set'
if old not in s:
    raise SystemExit('lifecycle config line not found in sdkconfig')
p.write_text(s.replace(old, 'CONFIG_EMULATOR_LIFECYCLE_REBOOT=y', 1))
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
git commit -m 'Add Zigbee emulator lifecycle reboot fault'
git push origin HEAD:integration/c6-s3-i2c-20260903
printf 'EMULATOR_LIFECYCLE_HEAD=%s\n' "$(git rev-parse HEAD)"
git status --short
