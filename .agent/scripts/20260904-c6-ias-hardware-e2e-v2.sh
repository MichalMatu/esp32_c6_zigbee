#!/usr/bin/env bash
set -euo pipefail

GW_SERIAL='40:4C:CA:5D:0A:00'
EMU_SERIAL='40:4C:CA:5D:01:D8'
EXPECTED_HEAD='85869b350d77d244993812bfec3787e2a9e38e58'
BRANCH='integration/c6-s3-i2c-20260903'

resolve_port() {
  python3 - "$1" <<'PY'
import sys, time
from serial.tools import list_ports
serial = sys.argv[1]
deadline = time.time() + 30
while time.time() < deadline:
    matches = [p for p in list_ports.comports() if p.serial_number == serial and p.vid == 0x303A]
    if len(matches) == 1:
        p = matches[0]
        print(p.device)
        raise SystemExit(0)
    if len(matches) > 1:
        raise SystemExit(f'ambiguous serial {serial}: {[p.device for p in matches]}')
    time.sleep(1)
raise SystemExit(f'missing C6 serial {serial}')
PY
}

show_identity() {
  python3 - "$1" <<'PY'
import sys
from serial.tools import list_ports
serial = sys.argv[1]
for p in list_ports.comports():
    if p.serial_number == serial and p.vid == 0x303A:
        print(f'IDENTITY_OK serial={serial} device={p.device} location={p.location} vidpid={p.vid:04X}:{p.pid:04X} product={p.product}')
        raise SystemExit(0)
raise SystemExit(f'identity not present for serial {serial}')
PY
}

echo '=== VERIFY PHYSICAL IDENTITIES ==='
show_identity "$GW_SERIAL"
show_identity "$EMU_SERIAL"
GW_PORT="$(resolve_port "$GW_SERIAL")"
EMU_PORT="$(resolve_port "$EMU_SERIAL")"
[ "$GW_PORT" != "$EMU_PORT" ]
echo "GATEWAY_PORT=$GW_PORT"
echo "EMULATOR_PORT=$EMU_PORT"

echo '=== RESET SOURCE ==='
git fetch origin "$BRANCH"
git checkout "$BRANCH"
git reset --hard "origin/$BRANCH"
rm -rf build sdkconfig sdkconfig.old tests/zigbee_device_emulator/build tests/zigbee_device_emulator/sdkconfig tests/zigbee_device_emulator/sdkconfig.old
[ -z "$(git status --short)" ]
ACTUAL_HEAD="$(git rev-parse HEAD)"
echo "SOURCE_HEAD=$ACTUAL_HEAD"
[ "$ACTUAL_HEAD" = "$EXPECTED_HEAD" ]

. ~/esp/esp-idf-v5.5.4/export.sh >/dev/null

echo '=== BUILD + FLASH GATEWAY ==='
idf.py set-target esp32c6
idf.py build
GW_PORT="$(resolve_port "$GW_SERIAL")"
show_identity "$GW_SERIAL"
echo "FLASH_GATEWAY_PORT=$GW_PORT"
idf.py -p "$GW_PORT" erase-flash flash
sleep 5
show_identity "$GW_SERIAL"

echo '=== BUILD IAS CONTACT EMULATOR ==='
cd tests/zigbee_device_emulator
idf.py set-target esp32c6
python3 - <<'PY'
from pathlib import Path
p = Path('sdkconfig')
s = p.read_text()
if 'CONFIG_EMULATOR_PROFILE_MIXED=y' not in s:
    raise SystemExit('default mixed profile selection not found')
s = s.replace('CONFIG_EMULATOR_PROFILE_MIXED=y', '# CONFIG_EMULATOR_PROFILE_MIXED is not set\nCONFIG_EMULATOR_PROFILE_IAS_CONTACT=y', 1)
p.write_text(s)
PY
idf.py reconfigure
idf.py build
EMU_PORT="$(resolve_port "$EMU_SERIAL")"
show_identity "$EMU_SERIAL"
echo "FLASH_EMULATOR_PORT=$EMU_PORT"
idf.py -p "$EMU_PORT" erase-flash flash
cd ../..
sleep 8
show_identity "$GW_SERIAL"
show_identity "$EMU_SERIAL"

echo '=== CAPTURE BOTH SERIAL LOGS FOR 90s ==='
GW_PORT="$(resolve_port "$GW_SERIAL")"
EMU_PORT="$(resolve_port "$EMU_SERIAL")"
python3 - "$GW_PORT" "$EMU_PORT" <<'PY'
import sys, time, threading
import serial

gw_port, emu_port = sys.argv[1], sys.argv[2]
stop = time.time() + 90
outputs = {'GW': [], 'EMU': []}
errors = []

def reader(name, port):
    try:
        with serial.Serial(port, 115200, timeout=0.25) as s:
            while time.time() < stop:
                data = s.readline()
                if data:
                    text = data.decode('utf-8', 'replace').rstrip('\r\n')
                    outputs[name].append(text)
    except Exception as e:
        errors.append(f'{name}:{e!r}')

threads = [threading.Thread(target=reader, args=('GW', gw_port)), threading.Thread(target=reader, args=('EMU', emu_port))]
for t in threads: t.start()
for t in threads: t.join()
for name in ('GW','EMU'):
    with open(f'/tmp/c6_{name.lower()}_e2e.log','w',encoding='utf-8') as f:
        f.write('\n'.join(outputs[name]))
        f.write('\n')
    print(f'{name}_LINES={len(outputs[name])}')
if errors:
    raise SystemExit('; '.join(errors))
PY

echo '=== GATEWAY RELEVANT LOG ==='
grep -Eai 'zigbee|ias|zone|contact|alarm|join|announce|endpoint|cluster|report|input|measure|device|commission|network|permit' /tmp/c6_gw_e2e.log | tail -300 || true
echo '=== EMULATOR RELEVANT LOG ==='
grep -Eai 'zigbee|ias|zone|contact|alarm|join|announce|endpoint|cluster|report|commission|network' /tmp/c6_emu_e2e.log | tail -300 || true
echo '=== GATEWAY TAIL ==='
tail -120 /tmp/c6_gw_e2e.log || true
echo '=== EMULATOR TAIL ==='
tail -120 /tmp/c6_emu_e2e.log || true

echo '=== E2E COLLECTION COMPLETE ==='
git status --short
