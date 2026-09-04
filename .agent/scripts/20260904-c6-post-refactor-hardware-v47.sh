#!/usr/bin/env bash
set -euo pipefail
BRANCH='integration/c6-s3-i2c-20260903'
EXPECTED='f13b293be2de6b1601d179568424e0046d6219a7'
GW_SERIAL='40:4C:CA:5D:0A:00'
EMU_SERIAL='40:4C:CA:5D:01:D8'

resolve_port() { python3 - "$1" <<'PY'
import sys,time
from serial.tools import list_ports
sn=sys.argv[1]; end=time.time()+20
while time.time()<end:
    m=[p for p in list_ports.comports() if p.serial_number==sn and p.vid==0x303A]
    if len(m)==1:
        print(m[0].device); raise SystemExit(0)
    time.sleep(.2)
raise SystemExit('identity mismatch for '+sn)
PY
}

check_ports() {
  local gw emu
  gw=$(resolve_port "$GW_SERIAL"); emu=$(resolve_port "$EMU_SERIAL")
  test "$gw" != "$emu"
  echo "GATEWAY_PORT=$gw SERIAL=$GW_SERIAL"
  echo "EMULATOR_PORT=$emu SERIAL=$EMU_SERIAL"
  for p in "$gw" "$emu"; do
    if command -v lsof >/dev/null 2>&1 && lsof "$p" >/tmp/c6_v47_holders 2>/dev/null; then
      echo "PORT_BUSY=$p"; cat /tmp/c6_v47_holders; exit 42
    fi
  done
}

git fetch origin "$BRANCH"
test "$(git rev-parse origin/$BRANCH)" = "$EXPECTED"
git checkout -B "$BRANCH" "$EXPECTED"
git reset --hard "$EXPECTED"
git clean -fd
. ~/esp/esp-idf-v5.5.4/export.sh >/dev/null
check_ports

# Build and flash the refactored production gateway with the known-good UART backend.
rm -rf build sdkconfig sdkconfig.old
idf.py set-target esp32c6 >/dev/null
idf.py build >/dev/null
GW_PORT=$(resolve_port "$GW_SERIAL")
idf.py -p "$GW_PORT" flash >/dev/null

# Build and flash the IAS Contact emulator without erasing Zigbee storage.
pushd tests/zigbee_device_emulator >/dev/null
rm -rf build sdkconfig sdkconfig.old
idf.py set-target esp32c6 >/dev/null
python3 - <<'PY'
from pathlib import Path
p=Path('sdkconfig'); s=p.read_text()
old='CONFIG_EMULATOR_PROFILE_MIXED=y'
new='# CONFIG_EMULATOR_PROFILE_MIXED is not set\nCONFIG_EMULATOR_PROFILE_IAS_CONTACT=y'
if old not in s: raise SystemExit('emulator mixed-profile default missing')
p.write_text(s.replace(old,new,1))
PY
idf.py reconfigure >/dev/null
idf.py build >/dev/null
EMU_PORT=$(resolve_port "$EMU_SERIAL")
idf.py -p "$EMU_PORT" flash >/dev/null
popd >/dev/null
git checkout -- tests/zigbee_device_emulator/dependencies.lock 2>/dev/null || true

# UART regression: preserved network/rejoin + IAS contact end-to-end after refactor.
python3 - <<'PY'
import subprocess, threading, time, serial, sys, re
from serial.tools import list_ports
GW_SN='40:4C:CA:5D:0A:00'; EMU_SN='40:4C:CA:5D:01:D8'
PANIC=['abort() was called','guru meditation',"panic'ed",'assert failed','task watchdog got triggered']
def resolve(sn):
    end=time.time()+20
    while time.time()<end:
        m=[p for p in list_ports.comports() if p.serial_number==sn and p.vid==0x303A]
        if len(m)==1:return m[0].device
        time.sleep(.15)
    raise RuntimeError('cannot resolve '+sn)
def reset(sn):
    p=resolve(sn)
    subprocess.run([sys.executable,'-m','esptool','--chip','esp32c6','-p',p,'--before','default_reset','--after','hard_reset','chip_id'],check=True,stdout=subprocess.DEVNULL)
    return resolve(sn)
def capture(timeout=120, send_status=False):
    reset(GW_SN); time.sleep(.25)
    gp=resolve(GW_SN); ep=resolve(EMU_SN)
    logs={'GW':[],'EMU':[]}; stop=threading.Event()
    handles={}
    def reader(name,port):
        with serial.Serial(port,115200,timeout=.1) as s:
            handles[name]=s
            while not stop.is_set():
                b=s.readline()
                if b:
                    t=b.decode('utf-8','replace').rstrip(); logs[name].append(t); print(name+': '+t,flush=True)
    tg=threading.Thread(target=reader,args=('GW',gp),daemon=True); tg.start()
    time.sleep(2)
    reset(EMU_SN); time.sleep(.2); ep=resolve(EMU_SN)
    te=threading.Thread(target=reader,args=('EMU',ep),daemon=True); te.start()
    status_sent=False; deadline=time.time()+timeout
    while time.time()<deadline:
        gl='\n'.join(logs['GW']).lower(); el='\n'.join(logs['EMU']).lower()
        if send_status and not status_sent and time.time()+0 > deadline-timeout+8 and 'GW' in handles:
            handles['GW'].write(b'link status\n'); handles['GW'].flush(); status_sent=True
        if ('contact_open=0.000 bool' in gl and 'contact_open=1.000 bool' in gl and
            ('factory_new=0' in el or 'factory_new=0' in gl)):
            time.sleep(3); break
        time.sleep(.2)
    stop.set(); tg.join(timeout=1); te.join(timeout=1)
    return logs
logs=capture()
g='\n'.join(logs['GW']).lower(); e='\n'.join(logs['EMU']).lower()
checks={
 'emulator_preserved_storage': 'factory_new=0' in e and 'factory_new=1' not in e,
 'gateway_rejoin': 'zigbee_device_rejoin' in g,
 'gateway_announce': 'zigbee_device_announce' in g,
 'ias_cie_write': 'writable state changed ep=1 cluster=0x0500 attr=0x0010; queued' in e,
 'ias_enroll': 'ias enroll response success' in e,
 'contact_false': 'contact_open=0.000 bool' in g,
 'contact_true': 'contact_open=1.000 bool' in g,
 'gw_no_panic': not any(x in g for x in PANIC),
 'emu_no_panic': not any(x in e for x in PANIC),
 'no_gateway_event_drop': 'gateway events because the 16-entry queue was full' not in g,
}
for k,v in checks.items(): print('UART_'+k.upper()+'='+str(v))
if not all(checks.values()): raise SystemExit('UART preserved regression failed: '+','.join(k for k,v in checks.items() if not v))
print('UART_POST_REFACTOR_DUAL_C6=PASS')
PY

# Build/flash the I2C backend on the coordinator only. S3 is intentionally absent.
rm -rf build sdkconfig sdkconfig.old
idf.py set-target esp32c6 >/dev/null
python3 - <<'PY'
from pathlib import Path
p=Path('sdkconfig'); s=p.read_text(); old='CONFIG_GATEWAY_LINK_BACKEND_UART=y'
new='# CONFIG_GATEWAY_LINK_BACKEND_UART is not set\nCONFIG_GATEWAY_LINK_BACKEND_I2C=y'
if old not in s: raise SystemExit('UART backend config missing')
p.write_text(s.replace(old,new,1))
PY
idf.py reconfigure >/dev/null
idf.py build >/dev/null
GW_PORT=$(resolve_port "$GW_SERIAL")
idf.py -p "$GW_PORT" flash >/dev/null

# I2C missing-S3 gate: shared-bus SCD4x and Zigbee must remain alive, no queue drops/panic.
python3 - <<'PY'
import subprocess, threading, time, serial, sys, re
from serial.tools import list_ports
GW_SN='40:4C:CA:5D:0A:00'; EMU_SN='40:4C:CA:5D:01:D8'
PANIC=['abort() was called','guru meditation',"panic'ed",'assert failed','task watchdog got triggered']
def resolve(sn):
    end=time.time()+20
    while time.time()<end:
        m=[p for p in list_ports.comports() if p.serial_number==sn and p.vid==0x303A]
        if len(m)==1:return m[0].device
        time.sleep(.15)
    raise RuntimeError('cannot resolve '+sn)
def reset(sn):
    p=resolve(sn)
    subprocess.run([sys.executable,'-m','esptool','--chip','esp32c6','-p',p,'--before','default_reset','--after','hard_reset','chip_id'],check=True,stdout=subprocess.DEVNULL)
    return resolve(sn)
reset(GW_SN); time.sleep(.25)
gp=resolve(GW_SN); logs={'GW':[],'EMU':[]}; stop=threading.Event(); handles={}
def reader(name,port):
    with serial.Serial(port,115200,timeout=.1) as s:
        handles[name]=s
        while not stop.is_set():
            b=s.readline()
            if b:
                t=b.decode('utf-8','replace').rstrip(); logs[name].append(t); print(name+': '+t,flush=True)
tg=threading.Thread(target=reader,args=('GW',gp),daemon=True); tg.start()
time.sleep(3)
reset(EMU_SN); time.sleep(.2); ep=resolve(EMU_SN)
te=threading.Thread(target=reader,args=('EMU',ep),daemon=True); te.start()
start=time.time(); status_sent=0
while time.time()-start < 120:
    elapsed=time.time()-start
    if elapsed > 10 and status_sent < 1 and 'GW' in handles:
        handles['GW'].write(b'link status\n'); handles['GW'].flush(); status_sent=1
    if elapsed > 70 and status_sent < 2 and 'GW' in handles:
        handles['GW'].write(b'link status\n'); handles['GW'].flush(); status_sent=2
    gl='\n'.join(logs['GW']).lower()
    if ('measurement local_i2c/scd4x:' in gl and ' co2=' in gl and
        'contact_open=0.000 bool' in gl and 'contact_open=1.000 bool' in gl and
        'link backend=i2c0-mailbox peer=0' in gl and status_sent >= 2):
        time.sleep(3); break
    time.sleep(.2)
stop.set(); tg.join(timeout=1); te.join(timeout=1)
g='\n'.join(logs['GW']).lower(); e='\n'.join(logs['EMU']).lower()
status=[x for x in logs['GW'] if 'link backend=i2c0-mailbox' in x.lower()]
print('I2C_STATUS_SAMPLES='+str(len(status)))
for x in status: print('I2C_STATUS='+x)
checks={
 'backend_selected': bool(status),
 'peer_absent_expected': bool(status) and all('peer=0' in x.lower() for x in status),
 'scd4x_available': 'input available local_i2c/scd4x:' in g,
 'scd4x_co2': 'measurement local_i2c/scd4x:' in g and ' co2=' in g,
 'scd4x_temperature': 'measurement local_i2c/scd4x:' in g and ' temperature=' in g,
 'scd4x_humidity': 'measurement local_i2c/scd4x:' in g and ' humidity=' in g,
 'zigbee_rejoin': 'zigbee_device_rejoin' in g,
 'contact_false': 'contact_open=0.000 bool' in g,
 'contact_true': 'contact_open=1.000 bool' in g,
 'gw_no_panic': not any(x in g for x in PANIC),
 'emu_no_panic': not any(x in e for x in PANIC),
 'no_event_drop': 'gateway events because the 16-entry queue was full' not in g,
 'no_link_queue_drop': 'gatewaylink messages because the tx queue was full' not in g,
 'scd4x_not_marked_unavailable': 'input unavailable local_i2c/scd4x:' not in g,
}
for k,v in checks.items(): print('I2C_'+k.upper()+'='+str(v))
if not all(checks.values()): raise SystemExit('I2C missing-S3 regression failed: '+','.join(k for k,v in checks.items() if not v))
print('I2C_MISSING_S3_SHARED_BUS=PASS')
PY

# Restore the known-good UART backend before leaving the lab state.
rm -rf build sdkconfig sdkconfig.old
idf.py set-target esp32c6 >/dev/null
idf.py build >/dev/null
GW_PORT=$(resolve_port "$GW_SERIAL")
idf.py -p "$GW_PORT" flash >/dev/null

# Final bounded smoke after UART restoration; do not erase NVS.
python3 - <<'PY'
import subprocess,time,serial,sys
from serial.tools import list_ports
SN='40:4C:CA:5D:0A:00'
def resolve():
    end=time.time()+15
    while time.time()<end:
        m=[p for p in list_ports.comports() if p.serial_number==SN and p.vid==0x303A]
        if len(m)==1:return m[0].device
        time.sleep(.2)
    raise SystemExit('gateway port unresolved')
p=resolve(); subprocess.run([sys.executable,'-m','esptool','--chip','esp32c6','-p',p,'--before','default_reset','--after','hard_reset','chip_id'],check=True,stdout=subprocess.DEVNULL)
time.sleep(.3); p=resolve(); lines=[]
with serial.Serial(p,115200,timeout=.2) as s:
    start=time.time(); sent=False
    while time.time()-start<30:
        if not sent and time.time()-start>8:
            s.write(b'link status\n'); s.flush(); sent=True
        b=s.readline()
        if b:
            t=b.decode('utf-8','replace').rstrip(); lines.append(t); print('FINAL_GW: '+t,flush=True)
        if any('link backend=uart1' in x.lower() for x in lines): break
text='\n'.join(lines).lower()
if 'link backend=uart1' not in text: raise SystemExit('UART restore status missing')
if any(x in text for x in ['guru meditation','abort() was called',"panic'ed",'task watchdog got triggered']): raise SystemExit('panic after UART restore')
print('UART_RESTORED_FINAL_SMOKE=PASS')
PY

# Leave checkout clean at the exact published refactor commit.
git reset --hard "$EXPECTED" >/dev/null
git clean -fd >/dev/null
echo HARDWARE_GATE_HEAD="$EXPECTED"
echo POST_REFACTOR_HARDWARE_GATE=PASS
