#!/usr/bin/env bash
set -euo pipefail
BRANCH=integration/c6-s3-i2c-20260903
EXPECTED=68a066f546eed8307447a3ee59562b88a19823ca
GW_SERIAL='40:4C:CA:5D:0A:00'
EMU_SERIAL='40:4C:CA:5D:01:D8'
ZB_OFFSET=0x16e000
ZB_SIZE=0x8000

git fetch origin "$BRANCH"
test "$(git rev-parse origin/$BRANCH)" = "$EXPECTED"
git checkout -B "$BRANCH" "$EXPECTED"
git reset --hard "$EXPECTED"

python3 - <<'PY'
from pathlib import Path
p=Path('main/zigbee_gateway.c')
s=p.read_text()
old='''    gateway_event_publish(&event);\n    if (slot->discovery_short_addr != slot->device.short_addr &&\n        !schedule_active_discovery(slot)) {\n'''
new='''    gateway_event_publish(&event);\n    if (kind == GATEWAY_EVENT_DEVICE_REJOIN) {\n        for (size_t i = 0U; i < GATEWAY_MAX_ENDPOINTS_PER_DEVICE; ++i) {\n            endpoint_state_t *state = &slot->endpoints[i];\n            if (state->in_use && state->ias_zone_type_known &&\n                !queue_job(\n                    DISCOVERY_WRITE_IAS_CIE, slot, state->endpoint,\n                    ZCL_CLUSTER_IAS_ZONE, 0U)) {\n                gateway_event_warning(&slot->device, "IAS CIE rejoin refresh queue full");\n            }\n        }\n    }\n    if (slot->discovery_short_addr != slot->device.short_addr &&\n        !schedule_active_discovery(slot)) {\n'''
if s.count(old) != 1:
    raise SystemExit(f'announce insertion count={s.count(old)}')
p.write_text(s.replace(old,new,1))
PY

git diff --check
python3 tests/host/test_no_embedded_nul.py
. ~/esp/esp-idf-v5.5.4/export.sh >/dev/null
idf.py build >/dev/null

git add main/zigbee_gateway.c
git diff --cached --check
git commit -m 'Refresh IAS CIE after device rejoin'
FIX_HEAD=$(git rev-parse HEAD)
git push origin HEAD:refs/heads/$BRANCH
echo IAS_REJOIN_CIE_REFRESH_HEAD=$FIX_HEAD

resolve_port() { python3 - "$1" <<'PY'
import sys,time
from serial.tools import list_ports
sn=sys.argv[1]; end=time.time()+15
while time.time()<end:
    m=[p for p in list_ports.comports() if p.serial_number==sn and p.vid==0x303A]
    if len(m)==1:
        print(m[0].device); raise SystemExit(0)
    time.sleep(.2)
raise SystemExit('identity mismatch for '+sn)
PY
}

GW_PORT=$(resolve_port "$GW_SERIAL"); EMU_PORT=$(resolve_port "$EMU_SERIAL")
test "$GW_PORT" != "$EMU_PORT"
echo FRESH_GATEWAY_PORT=$GW_PORT SERIAL=$GW_SERIAL
echo FRESH_EMULATOR_PORT=$EMU_PORT SERIAL=$EMU_SERIAL
for P in "$GW_PORT" "$EMU_PORT"; do
  if command -v lsof >/dev/null 2>&1 && lsof "$P" >/tmp/c6_v24_holders 2>/dev/null; then
    echo PORT_BUSY=$P; cat /tmp/c6_v24_holders; exit 42
  fi
done

GW_PORT=$(resolve_port "$GW_SERIAL"); idf.py -p "$GW_PORT" flash >/dev/null
cd tests/zigbee_device_emulator
rm -f sdkconfig sdkconfig.old; rm -rf build
idf.py set-target esp32c6 >/dev/null
python3 - <<'PY'
from pathlib import Path
p=Path('sdkconfig'); s=p.read_text(); old='CONFIG_EMULATOR_PROFILE_MIXED=y'; new='# CONFIG_EMULATOR_PROFILE_MIXED is not set\nCONFIG_EMULATOR_PROFILE_IAS_CONTACT=y'
if s.count(old)!=1: raise SystemExit(f'mixed profile default count={s.count(old)}')
p.write_text(s.replace(old,new,1))
PY
idf.py reconfigure >/dev/null
idf.py build >/dev/null
EMU_PORT=$(resolve_port "$EMU_SERIAL"); idf.py -p "$EMU_PORT" flash >/dev/null
cd ../..
git checkout -- tests/zigbee_device_emulator/dependencies.lock 2>/dev/null || true

GW_PORT=$(resolve_port "$GW_SERIAL"); python -m esptool --chip esp32c6 -p "$GW_PORT" --before default_reset --after no_reset erase_region "$ZB_OFFSET" "$ZB_SIZE" >/dev/null
EMU_PORT=$(resolve_port "$EMU_SERIAL"); python -m esptool --chip esp32c6 -p "$EMU_PORT" --before default_reset --after no_reset erase_region "$ZB_OFFSET" "$ZB_SIZE" >/dev/null

python3 - <<'PY'
import subprocess,threading,time,serial,sys
from serial.tools import list_ports
GW_SN='40:4C:CA:5D:0A:00'; EMU_SN='40:4C:CA:5D:01:D8'
PANIC=['abort() was called','guru meditation',"panic'ed",'assert failed','task watchdog got triggered']
def resolve(sn):
    end=time.time()+15
    while time.time()<end:
        m=[p for p in list_ports.comports() if p.serial_number==sn and p.vid==0x303A]
        if len(m)==1:return m[0].device
        time.sleep(.15)
    raise RuntimeError('cannot resolve '+sn)
def reset(sn,label):
    p=resolve(sn); print(f'{label}_PORT={p} SERIAL={sn}',flush=True)
    subprocess.run([sys.executable,'-m','esptool','--chip','esp32c6','-p',p,'--before','default_reset','--after','hard_reset','chip_id'],check=True,stdout=subprocess.DEVNULL)
    return resolve(sn)
def run_readers(reset_gateway, label, timeout):
    logs={'GW':[],'EMU':[]}; stop=threading.Event()
    def reader(name,port):
        with serial.Serial(port,115200,timeout=.1) as s:
            while not stop.is_set():
                b=s.readline()
                if b:
                    t=b.decode('utf-8','replace').rstrip(); logs[name].append(t); print(name+': '+t,flush=True)
    if reset_gateway:
        gw=reset(GW_SN,label+'_GW'); tg=threading.Thread(target=reader,args=('GW',gw),daemon=True); tg.start()
        deadline=time.time()+30
        while time.time()<deadline:
            gl='\n'.join(logs['GW']).lower()
            if 'network formed' in gl and 'permit join duration=180' in gl: break
            time.sleep(.2)
        else:
            stop.set(); tg.join(timeout=1); raise SystemExit('fresh gateway formation/permit timeout')
    else:
        gw=resolve(GW_SN); print(f'{label}_GW_PORT={gw} SERIAL={GW_SN}',flush=True)
        tg=threading.Thread(target=reader,args=('GW',gw),daemon=True); tg.start(); time.sleep(.5)
    emu=reset(EMU_SN,label+'_EMU'); te=threading.Thread(target=reader,args=('EMU',emu),daemon=True); te.start()
    deadline=time.time()+timeout
    while time.time()<deadline:
        gl='\n'.join(logs['GW']).lower(); el='\n'.join(logs['EMU']).lower()
        if 'contact_open=0.000 bool' in gl and 'contact_open=1.000 bool' in gl:
            time.sleep(3); break
        time.sleep(.2)
    stop.set(); tg.join(timeout=1); te.join(timeout=1)
    return logs

fresh=run_readers(True,'FRESH',100)
g='\n'.join(fresh['GW']).lower(); e='\n'.join(fresh['EMU']).lower()
gp=[x for x in PANIC if x in g]; ep=[x for x in PANIC if x in e]
checks={
 'remote_zonetype': any('input available zigbee/' in x.lower() and 'read=0x00004000' in x.lower() for x in fresh['GW']) and 'ias zonetype read failed' not in g,
 'cie_queued': 'writable state changed ep=1 cluster=0x0500 attr=0x0010; queued' in e,
 'cie_action': 'roundtrip action ep=1 cluster=0x0500 attr=0x0010 sent=1' in e,
 'enroll_req': 'ias enroll request ep=1 error=0' in e,
 'enroll_rsp': 'ias enroll response success' in e,
 'false': 'contact_open=0.000 bool' in g,
 'true': 'contact_open=1.000 bool' in g,
 'no_error5': 'cluster=0x0500 attr=0x0002 error=5' not in e,
 'gw_no_panic': not gp,
 'emu_no_panic': not ep,
}
for k,v in checks.items(): print('FRESH_'+k.upper()+'='+str(v))
if not all(checks.values()): raise SystemExit('FRESH E2E FAILED: '+','.join(k for k,v in checks.items() if not v))
print('FRESH_NETWORK_DUAL_C6_IAS_E2E=PASS')

# Preserved-storage phase: intentionally NO flash and NO erase here.
time.sleep(1)
preserved=run_readers(False,'PRESERVED',100)
g='\n'.join(preserved['GW']).lower(); e='\n'.join(preserved['EMU']).lower()
gp=[x for x in PANIC if x in g]; ep=[x for x in PANIC if x in e]
checks={
 'non_factory_new': 'factory_new=0' in e and 'factory_new=1' not in e,
 'rejoin': 'zigbee_device_rejoin' in g,
 'announce': 'zigbee_device_announce' in g,
 'cie_queued': 'writable state changed ep=1 cluster=0x0500 attr=0x0010; queued' in e,
 'cie_action': 'roundtrip action ep=1 cluster=0x0500 attr=0x0010 sent=1' in e,
 'enroll_req': 'ias enroll request ep=1 error=0' in e,
 'enroll_rsp': 'ias enroll response success' in e,
 'false': 'contact_open=0.000 bool' in g,
 'true': 'contact_open=1.000 bool' in g,
 'gw_no_panic': not gp,
 'emu_no_panic': not ep,
}
for k,v in checks.items(): print('PRESERVED_'+k.upper()+'='+str(v))
if not all(checks.values()):
    print('=== PRESERVED GW EVIDENCE ===')
    for x in preserved['GW']:
        if any(k in x.lower() for k in ['rejoin','announce','contact_open','ias ','panic','abort','guru']): print(x)
    print('=== PRESERVED EMU EVIDENCE ===')
    for x in preserved['EMU']:
        if any(k in x.lower() for k in ['factory_new','ias ','roundtrip','tick=','panic','abort','guru']): print(x)
    raise SystemExit('PRESERVED REJOIN FAILED: '+','.join(k for k,v in checks.items() if not v))
print('PRESERVED_STORAGE_RESTART_REJOIN=PASS')
PY

git status --short
