import subprocess, threading, time, serial, sys
from serial.tools import list_ports

GW_SN = '40:4C:CA:5D:0A:00'
EMU_SN = '40:4C:CA:5D:01:D8'
PANIC = ['abort() was called', 'guru meditation', "panic'ed", 'assert failed', 'task watchdog got triggered']

def resolve(sn):
    end = time.time() + 15
    while time.time() < end:
        matches = [p for p in list_ports.comports() if p.serial_number == sn and p.vid == 0x303A]
        if len(matches) == 1:
            return matches[0].device
        time.sleep(0.15)
    raise RuntimeError('identity mismatch for ' + sn)

logs = {'GW': [], 'EMU': []}
stop = threading.Event()

def reader(name, port):
    with serial.Serial(port, 115200, timeout=0.1) as s:
        while not stop.is_set():
            b = s.readline()
            if b:
                t = b.decode('utf-8', 'replace').rstrip()
                logs[name].append(t)
                print(name + ': ' + t, flush=True)

gw = resolve(GW_SN)
emu_before = resolve(EMU_SN)
if gw == emu_before:
    raise SystemExit('gateway/emulator identity collision')
print(f'PRE_RESET_GATEWAY_PORT={gw} SERIAL={GW_SN}', flush=True)
print(f'PRE_RESET_EMULATOR_PORT={emu_before} SERIAL={EMU_SN}', flush=True)

tg = threading.Thread(target=reader, args=('GW', gw), daemon=True)
tg.start()
time.sleep(0.5)
print(f'RESET_EMU_PRESERVED_PORT={emu_before} SERIAL={EMU_SN}', flush=True)
subprocess.run([
    sys.executable, '-m', 'esptool', '--chip', 'esp32c6', '-p', emu_before,
    '--before', 'default_reset', '--after', 'hard_reset', 'chip_id'
], check=True, stdout=subprocess.DEVNULL)

emu = resolve(EMU_SN)
print(f'POST_RESET_EMULATOR_PORT={emu} SERIAL={EMU_SN}', flush=True)
te = threading.Thread(target=reader, args=('EMU', emu), daemon=True)
te.start()

deadline = time.time() + 100
while time.time() < deadline:
    gl = '\n'.join(logs['GW']).lower()
    el = '\n'.join(logs['EMU']).lower()
    if ('factory_new=0' in el and 'zigbee_device_authorized' in gl and
            'contact_open=0.000 bool' in gl and 'contact_open=1.000 bool' in gl):
        time.sleep(3)
        break
    time.sleep(0.2)

stop.set(); tg.join(timeout=1); te.join(timeout=1)
gl = '\n'.join(logs['GW']).lower(); el = '\n'.join(logs['EMU']).lower()
gp = [x for x in PANIC if x in gl]; ep = [x for x in PANIC if x in el]
non_factory = 'factory_new=0' in el
factory_new = 'factory_new=1' in el
auth = 'zigbee_device_authorized' in gl
contact_false = 'contact_open=0.000 bool' in gl
contact_true = 'contact_open=1.000 bool' in gl
cap = any('input available zigbee/' in x.lower() and 'read=0x00004000' in x.lower() for x in logs['GW'])
announce = any(k in gl for k in ['device announce', 'device_annce', 'announce'])

print('EVIDENCE_NON_FACTORY_NEW=' + str(non_factory))
print('EVIDENCE_FACTORY_NEW_TRUE=' + str(factory_new))
print('EVIDENCE_REJOIN_AUTH=' + str(auth))
print('EVIDENCE_REJOIN_ANNOUNCE=' + str(announce))
print('EVIDENCE_USABLE_CONTACT_CAPABILITY=' + str(cap))
print('EVIDENCE_CONTACT_OPEN_FALSE=' + str(contact_false))
print('EVIDENCE_CONTACT_OPEN_TRUE=' + str(contact_true))
print('EVIDENCE_GW_PANIC=' + str(gp))
print('EVIDENCE_EMU_PANIC=' + str(ep))

failed = []
for name, ok in [
    ('non-factory-new', non_factory and not factory_new),
    ('rejoin auth', auth),
    ('CONTACT_OPEN false', contact_false),
    ('CONTACT_OPEN true', contact_true),
    ('GW no panic', not gp),
    ('EMU no panic', not ep),
]:
    if not ok:
        failed.append(name)
if failed:
    print('=== GW REJOIN EVIDENCE ===')
    for x in logs['GW']:
        if any(k in x.lower() for k in ['authorized', 'announce', 'input available zigbee/', 'contact_open', 'ias ', 'panic', 'abort', 'guru']):
            print(x)
    print('=== EMU REJOIN EVIDENCE ===')
    for x in logs['EMU']:
        if any(k in x.lower() for k in ['factory_new', 'network steering', 'ias ', 'tick=', 'panic', 'abort', 'guru']):
            print(x)
    raise SystemExit('PRESERVED REJOIN FAILED: ' + ', '.join(failed))
print('PRESERVED_STORAGE_RESTART_REJOIN=PASS')
