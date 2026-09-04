#!/usr/bin/env bash
set -euo pipefail
EMU_SERIAL='40:4C:CA:5D:01:D8'

resolve_port() {
  python3 - "$EMU_SERIAL" <<'PY'
import sys,time
from serial.tools import list_ports
serial=sys.argv[1]
deadline=time.time()+20
while time.time()<deadline:
    m=[p for p in list_ports.comports() if p.serial_number==serial and p.vid==0x303A]
    if len(m)==1:
        p=m[0]
        print(p.device)
        raise SystemExit(0)
    if len(m)>1:
        raise SystemExit(f'ambiguous serial {serial}: {[p.device for p in m]}')
    time.sleep(.5)
raise SystemExit(f'missing C6 serial {serial}')
PY
}

echo '=== PRE ==='
python3 - <<'PY'
from serial.tools import list_ports
for p in list_ports.comports():
    if p.vid==0x303A:
        print(f'DEVICE={p.device} SERIAL={p.serial_number} VIDPID={p.vid:04X}:{p.pid:04X} LOCATION={p.location} PRODUCT={p.product}')
PY
PORT="$(resolve_port)"
echo "EMU_PORT=$PORT"

. ~/esp/esp-idf-v5.5.4/export.sh >/dev/null

echo '=== ROM CHIP PROBE + HARD RESET ==='
python -m esptool --chip esp32c6 -p "$PORT" --before default_reset --after hard_reset chip_id

echo '=== ENUMERATION + SERIAL CAPTURE 25s ==='
python3 - "$EMU_SERIAL" <<'PY'
import sys,time
from serial.tools import list_ports
import serial
serial_no=sys.argv[1]
end=time.time()+25
last=None
ser=None
lines=[]
while time.time()<end:
    matches=[p for p in list_ports.comports() if p.serial_number==serial_no and p.vid==0x303A]
    state=matches[0].device if len(matches)==1 else ('AMBIGUOUS' if len(matches)>1 else 'MISSING')
    if state!=last:
        print(f'ENUM t={25-(end-time.time()):.2f}s state={state}')
        last=state
    if ser is None and len(matches)==1:
        try:
            ser=serial.Serial(matches[0].device,115200,timeout=.05)
            print(f'SERIAL_OPEN={matches[0].device}')
        except Exception as e:
            print(f'SERIAL_OPEN_ERROR={e!r}')
    if ser is not None:
        try:
            data=ser.readline()
            if data:
                text=data.decode('utf-8','replace').rstrip('\r\n')
                lines.append(text)
                print('EMU>',text)
        except Exception as e:
            print(f'SERIAL_READ_ERROR={e!r}')
            try: ser.close()
            except Exception: pass
            ser=None
    time.sleep(.05)
if ser is not None:
    ser.close()
print(f'EMU_CAPTURE_LINES={len(lines)}')
PY

echo '=== POST USB TOPOLOGY (TARGET ONLY) ==='
system_profiler SPUSBDataType 2>/dev/null | grep -A18 -B4 '40:4C:CA:5D:01:D8' || true
