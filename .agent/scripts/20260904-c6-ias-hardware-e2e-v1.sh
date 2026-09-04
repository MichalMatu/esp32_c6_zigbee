#!/usr/bin/env bash
set -euo pipefail

BRANCH=integration/c6-s3-i2c-20260903
EXPECTED_HEAD=85869b350d77d244993812bfec3787e2a9e38e58
GW_PORT=/dev/cu.usbmodem101
GW_SERIAL='40:4C:CA:5D:0A:00'
EMU_PORT=/dev/cu.usbmodem11101
EMU_SERIAL='40:4C:CA:5D:01:D8'

echo '=== VERIFY PHYSICAL C6 IDENTITIES ==='
python3 - <<'PY'
from serial.tools import list_ports
expected = {
    '/dev/cu.usbmodem101': '40:4C:CA:5D:0A:00',
    '/dev/cu.usbmodem11101': '40:4C:CA:5D:01:D8',
}
ports = {p.device: p for p in list_ports.comports()}
for device, serial in expected.items():
    p = ports.get(device)
    if p is None:
        raise SystemExit(f'missing required C6 port: {device}')
    if p.vid != 0x303A or p.pid != 0x1001:
        raise SystemExit(f'wrong VID:PID on {device}: {p.vid}:{p.pid}')
    if p.serial_number != serial:
        raise SystemExit(f'wrong C6 serial on {device}: expected {serial}, got {p.serial_number}')
    print(f'IDENTITY_OK device={device} serial={p.serial_number} location={p.location} product={p.product}')
PY

echo '=== RESET SOURCE TO EXACT PRODUCTION HEAD ==='
git fetch origin "$BRANCH"
git checkout "$BRANCH"
git reset --hard "origin/$BRANCH"
[ "$(git rev-parse HEAD)" = "$EXPECTED_HEAD" ]
[ -z "$(git status --short)" ]

. ~/esp/esp-idf-v5.5.4/export.sh >/dev/null

echo '=== BUILD GATEWAY ==='
rm -rf build sdkconfig sdkconfig.old
idf.py set-target esp32c6
idf.py build
idf.py size

echo '=== REVERIFY GATEWAY BEFORE ERASE/FLASH ==='
python3 - <<'PY'
from serial.tools import list_ports
p = next((p for p in list_ports.comports() if p.device == '/dev/cu.usbmodem101'), None)
assert p is not None, 'gateway port disappeared'
assert p.vid == 0x303A and p.pid == 0x1001, 'gateway VID/PID changed'
assert p.serial_number == '40:4C:CA:5D:0A:00', f'gateway serial changed: {p.serial_number}'
print('GATEWAY_REVERIFY_OK')
PY
idf.py -p "$GW_PORT" erase-flash
idf.py -p "$GW_PORT" flash

echo '=== BUILD IAS CONTACT EMULATOR ==='
cd tests/zigbee_device_emulator
rm -rf build sdkconfig sdkconfig.old
idf.py set-target esp32c6
python3 - <<'PY'
from pathlib import Path
p = Path('sdkconfig')
s = p.read_text()
if 'CONFIG_EMULATOR_PROFILE_MIXED=y' not in s:
    raise SystemExit('default mixed emulator profile not found')
s = s.replace(
    'CONFIG_EMULATOR_PROFILE_MIXED=y',
    '# CONFIG_EMULATOR_PROFILE_MIXED is not set\nCONFIG_EMULATOR_PROFILE_IAS_CONTACT=y',
    1,
)
p.write_text(s)
PY
idf.py reconfigure
grep -E 'CONFIG_EMULATOR_PROFILE_(IAS_CONTACT|MIXED)' sdkconfig
idf.py build
idf.py size

echo '=== REVERIFY EMULATOR BEFORE ERASE/FLASH ==='
python3 - <<'PY'
from serial.tools import list_ports
p = next((p for p in list_ports.comports() if p.device == '/dev/cu.usbmodem11101'), None)
assert p is not None, 'emulator port disappeared'
assert p.vid == 0x303A and p.pid == 0x1001, 'emulator VID/PID changed'
assert p.serial_number == '40:4C:CA:5D:01:D8', f'emulator serial changed: {p.serial_number}'
print('EMULATOR_REVERIFY_OK')
PY
idf.py -p "$EMU_PORT" erase-flash
idf.py -p "$EMU_PORT" flash
cd ../..

echo '=== CAPTURE BOTH SERIAL LOGS FOR 90 SECONDS ==='
python3 - <<'PY'
import queue
import serial
import threading
import time

ports = {
    'GW': '/dev/cu.usbmodem101',
    'EMU': '/dev/cu.usbmodem11101',
}
q = queue.Queue()
handles = {}
stop = threading.Event()

def reader(label, handle):
    while not stop.is_set():
        try:
            raw = handle.readline()
        except Exception as exc:
            q.put((label, f'<SERIAL_ERROR {exc!r}>'))
            return
        if raw:
            q.put((label, raw.decode('utf-8', errors='replace').rstrip()))

for label, port in ports.items():
    h = serial.Serial(port, 115200, timeout=0.2)
    handles[label] = h
    threading.Thread(target=reader, args=(label, h), daemon=True).start()

end = time.time() + 90
lines = []
while time.time() < end:
    try:
        label, line = q.get(timeout=0.5)
    except queue.Empty:
        continue
    stamped = f'[{label}] {line}'
    lines.append(stamped)
    print(stamped, flush=True)

stop.set()
for h in handles.values():
    h.close()

print('=== E2E KEY LINES ===')
needles = (
    'zigbee', 'commission', 'steer', 'join', 'announce', 'ias', 'zone',
    'contact', '0x0500', '0x0015', 'alarm', 'input', 'report', 'error', 'fail'
)
for line in lines:
    low = line.lower()
    if any(n in low for n in needles):
        print(line)
PY

echo '=== FINAL SOURCE INVARIANTS ==='
[ "$(git rev-parse HEAD)" = "$EXPECTED_HEAD" ]
python3 tests/host/test_no_embedded_nul.py
git diff --check
printf 'IAS_HARDWARE_E2E_SOURCE_HEAD=%s\n' "$(git rev-parse HEAD)"
