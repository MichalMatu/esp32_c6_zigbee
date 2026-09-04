#!/usr/bin/env bash
set -euo pipefail

BRANCH=integration/c6-s3-i2c-20260903
EXPECTED=109a01f32d3bbc5c2ce2799ccbc8946a717b0e7a

git fetch origin "$BRANCH"
test "$(git rev-parse origin/$BRANCH)" = "$EXPECTED"
git checkout -B "$BRANCH" "$EXPECTED"
git reset --hard "$EXPECTED"

python3 - <<'PY'
from pathlib import Path

# VERIFIED_BASELINE: replace the obsolete failed-development checkpoint at EOF
# with the exact hardware-verified dual-C6 IAS baseline.
p = Path('docs/VERIFIED_BASELINE.md')
s = p.read_text()
marker = '## Dual-C6 Zigbee laboratory development checkpoint — 2026-09-04\n'
pos = s.find(marker)
if pos < 0:
    raise SystemExit('VERIFIED_BASELINE dual-C6 checkpoint marker not found')
verified = '''## Dual-C6 Zigbee laboratory verified baseline — 2026-09-04

This section freezes the verified dual-C6 IAS Contact laboratory baseline on the active integration branch. It is a hardware-verified source checkpoint, not a new stable tag.

Verified source commit before this documentation commit:

- `109a01f32d3bbc5c2ce2799ccbc8946a717b0e7a` — refresh IAS CIE enrollment state after a device rejoin while preserving IEEE-first identity and the existing bounded discovery architecture.

The IAS Contact emulator also includes the previously verified fixes that expose ZoneType as ENUM16, complete IAS enrollment after the coordinator writes its CIE address, preserve RestoreNotify while toggling Alarm1, and run the roundtrip worker for the IAS profile.

### Fresh-network hardware gate

Task: `20260904-c6-ias-rejoin-cie-refresh-v24`.

The test rebuilt and flashed the exact source, erased only the `zb_storage` regions intentionally required for a factory-new Zigbee test, re-resolved both USB ports by immutable serial identity, formed a fresh coordinator network, opened permit-join, then started the emulator.

Observed evidence:

```text
FRESH_REMOTE_ZONETYPE=True
FRESH_CIE_QUEUED=True
FRESH_CIE_ACTION=True
FRESH_ENROLL_REQ=True
FRESH_ENROLL_RSP=True
FRESH_FALSE=True
FRESH_TRUE=True
FRESH_NO_ERROR5=True
FRESH_GW_NO_PANIC=True
FRESH_EMU_NO_PANIC=True
FRESH_NETWORK_DUAL_C6_IAS_E2E=PASS
```

This proves remote IAS ZoneType `0x0015`, standard CIE/enrollment handshake, normalized `CONTACT_OPEN=false` and `CONTACT_OPEN=true`, no generic IAS ZoneStatus `error=5`, and no gateway/emulator panic.

### Preserved-storage restart/rejoin gate

The second phase of the same task restarted the emulator without erasing Zigbee storage and without reflashing either board. Ports were re-resolved from USB serial identity before hardware access.

Observed evidence:

```text
PRESERVED_NON_FACTORY_NEW=True
PRESERVED_REJOIN=True
PRESERVED_ANNOUNCE=True
PRESERVED_CIE_QUEUED=True
PRESERVED_CIE_ACTION=True
PRESERVED_ENROLL_REQ=True
PRESERVED_ENROLL_RSP=True
PRESERVED_FALSE=True
PRESERVED_TRUE=True
PRESERVED_GW_NO_PANIC=True
PRESERVED_EMU_NO_PANIC=True
PRESERVED_STORAGE_RESTART_REJOIN=PASS
```

The emulator explicitly reported `factory_new=0`; the coordinator observed rejoin/announce and refreshed the IAS CIE handshake even when the network short address was unchanged. Contact-open false/true notifications resumed after restart with no storage erase and no panic.

Verified board identities:

- coordinator C6 USB serial `40:4C:CA:5D:0A:00`;
- emulator C6 USB serial `40:4C:CA:5D:01:D8`.

This closes the IAS Contact fresh-network plus persisted-rejoin blocker for the current Zigbee laboratory baseline. Future hardware changes must preserve these gates rather than re-opening the already resolved storage/autostart/ZoneType/enrollment investigation without new regression evidence.
'''
p.write_text(s[:pos] + verified)

# CONTINUATION: replace the obsolete blocker section with the completed baseline.
p = Path('docs/CONTINUATION.md')
s = p.read_text()
start = s.find('## 2026-09-04 dual-C6 Zigbee laboratory checkpoint\n')
end = s.find('## Project split\n', start)
if start < 0 or end < 0:
    raise SystemExit('CONTINUATION checkpoint boundaries not found')
checkpoint = '''## 2026-09-04 dual-C6 Zigbee laboratory baseline complete

Verified source checkpoint before this documentation commit:

- `109a01f32d3bbc5c2ce2799ccbc8946a717b0e7a` — IAS CIE refresh after rejoin.

Hardware task `20260904-c6-ias-rejoin-cie-refresh-v24` passed both required gates on the two bound C6 boards.

Fresh network proved remote IAS ZoneType `0x0015`, CIE write + enrollment, normalized `CONTACT_OPEN=false` and `CONTACT_OPEN=true`, absence of generic IAS ZoneStatus `error=5`, and no panic on either board.

Preserved-storage restart/rejoin then restarted the emulator with no storage erase and no reflash. The emulator reported `factory_new=0`; the coordinator observed rejoin + announce, refreshed the IAS CIE/enrollment path, and again received `CONTACT_OPEN=false` and `CONTACT_OPEN=true` with no panic.

The earlier `v23` failure was diagnostic evidence, not a storage/rejoin failure: rejoin itself succeeded with `factory_new=0`, but the coordinator did not refresh IAS CIE state when the device returned with the same short address. Commit `109a01f32d3bbc5c2ce2799ccbc8946a717b0e7a` fixes that exact lifecycle gap without resetting the whole discovery registry.

Do not repeat investigation of `zb_storage`, `esp_zigbee_start(false)`, ZoneType datatype, enrollment API, RestoreNotify semantics, or same-short rejoin unless a later code change creates new regression evidence.

Verified hardware identities; always re-resolve live ports from serial before hardware access:

- coordinator C6 serial `40:4C:CA:5D:0A:00`;
- emulator C6 serial `40:4C:CA:5D:01:D8`.

'''
s = s[:start] + checkpoint + s[end:]

# Replace the now-completed active-goal block while preserving the separately
# documented deferred physical integration requirements below it.
start = s.find('## Active goal\n')
end = s.find('### Deferred physical C6 ↔ S3 milestone\n', start)
if start < 0 or end < 0:
    raise SystemExit('CONTINUATION active-goal boundaries not found')
active = '''## Active goal

The C6-only generic Zigbee/dual-C6 IAS laboratory blocker is closed and frozen by hardware evidence. The next C6-side milestone is preparation for physical C6 ↔ S3 GatewayLink I2C validation while preserving the verified Zigbee baseline and the existing UART fallback.

Do not modify an S3 repository from this bound C6 context. Any S3 implementation requires its own correctly bound repository session. C6-side work must continue to preserve local SCD4x operation, Zigbee coordinator reliability, bounded missing-peer behavior, and the fresh-network/preserved-rejoin IAS gates above.

'''
s = s[:start] + active + s[end:]

heading = '## Active milestone — Generic Zigbee Device Interview & Capability Discovery\n'
if heading in s:
    s = s.replace(
        heading,
        '## Completed milestone — Generic Zigbee Device Interview & Capability Discovery\n\nThe current dual-C6 baseline closes the IAS Contact blocker required for this milestone. The detailed design notes below remain as the capability/discovery contract and regression scope.\n',
        1,
    )
p.write_text(s)

# ARCHITECTURE: document the exact same-short rejoin invariant that v24 proved.
p = Path('docs/ARCHITECTURE.md')
s = p.read_text()
needle = ('`DEVICE_AUTHORIZED` is logged separately so hardware tests can distinguish successful authorization from early unsecure-join updates. '
          'This rule prevents discovery/bind/reporting traffic from racing the security handshake on sleepy end devices while retaining IEEE-first identity and generation-safe route replacement.\n')
if needle not in s:
    raise SystemExit('ARCHITECTURE join lifecycle paragraph not found')
addition = needle + ('\nA secure/Trust-Center rejoin may legitimately return with the same 16-bit short address. That must still be treated as a lifecycle transition for protocol state that the end device reconstructs at boot. For a previously identified IAS Contact endpoint, the gateway refreshes the IAS CIE write/enrollment path on `DEVICE_REJOIN` even when full endpoint discovery is already claimed for the unchanged route. This is a bounded targeted refresh: it does not discard IEEE identity, duplicate the complete discovery flow, or make the short address an application identity.\n')
s = s.replace(needle, addition, 1)
p.write_text(s)
PY

python3 tests/host/test_no_embedded_nul.py
git diff --check

test "$(git diff --name-only | sort | tr '\n' ' ')" = "docs/ARCHITECTURE.md docs/CONTINUATION.md docs/VERIFIED_BASELINE.md "
grep -q '109a01f32d3bbc5c2ce2799ccbc8946a717b0e7a' docs/VERIFIED_BASELINE.md
grep -q 'FRESH_NETWORK_DUAL_C6_IAS_E2E=PASS' docs/VERIFIED_BASELINE.md
grep -q 'PRESERVED_STORAGE_RESTART_REJOIN=PASS' docs/VERIFIED_BASELINE.md
grep -q 'same 16-bit short address' docs/ARCHITECTURE.md
grep -q 'dual-C6 Zigbee laboratory baseline complete' docs/CONTINUATION.md

. ~/esp/esp-idf-v5.5.4/export.sh >/dev/null
idf.py build >/dev/null
python3 tests/host/test_no_embedded_nul.py
git diff --check

test "$(git diff --name-only | sort | tr '\n' ' ')" = "docs/ARCHITECTURE.md docs/CONTINUATION.md docs/VERIFIED_BASELINE.md "

git add docs/VERIFIED_BASELINE.md docs/CONTINUATION.md docs/ARCHITECTURE.md
test "$(git diff --cached --name-only | sort | tr '\n' ' ')" = "docs/ARCHITECTURE.md docs/CONTINUATION.md docs/VERIFIED_BASELINE.md "
git diff --cached --check
git commit -m 'Freeze verified dual-C6 Zigbee lab baseline'
git push origin HEAD:"$BRANCH"
echo ZIGBEE_LAB_BASELINE_DOC_HEAD=$(git rev-parse HEAD)
git status --short
