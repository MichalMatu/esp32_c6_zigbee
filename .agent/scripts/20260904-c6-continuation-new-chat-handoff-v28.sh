#!/usr/bin/env bash
set -euo pipefail

git fetch origin integration/c6-s3-i2c-20260903
git checkout integration/c6-s3-i2c-20260903
git reset --hard 34ba6a9e4a52d6a4d4fdab5010f8c4fdfb8713e3
test -z "$(git status --porcelain)"

python3 - <<'PY'
from pathlib import Path

p = Path('docs/CONTINUATION.md')
s = p.read_text()
marker = '## Current work context'
assert marker in s
assert '## START HERE — new ChatGPT window handoff' not in s

handoff = '''## START HERE — new ChatGPT window handoff (2026-09-04)

Read this section first, then `AGENTS.md`, `docs/VERIFIED_BASELINE.md`, `docs/ARCHITECTURE.md`, and `docs/GATEWAY_LINK_V1.md`. Do not reconstruct project state from old chat messages when repository evidence is available.

### Hard repository / Local Agent boundary

- Work only in `MichalMatu/esp32_c6_zigbee` / repository id `esp32-c6-zigbee`.
- Local Agent binding for this C6 context is `64877d7d-af3f-4312-a511-699c44aa42dd`; do not rebind, infer, inspect, queue, cancel, or execute another repository from a C6-bound wake.
- Local Agent control branch is `agent-control`; source work branch is `integration/c6-s3-i2c-20260903`.
- Before every Local Agent task, fetch fresh `.agent/status/daemon.json` from `agent-control` and require this exact repository/binding plus `state=idle`. One task at a time.
- If the next required implementation belongs to S3, pause. S3 must use its own correctly bound repository conversation/agent. Never modify an S3 repository from this C6 context.

### Exact handoff state

At handoff creation, the active source branch HEAD was `34ba6a9e4a52d6a4d4fdab5010f8c4fdfb8713e3` (`Freeze verified dual-C6 Zigbee lab baseline`), whose parent is the verified Zigbee code checkpoint `109a01f32d3bbc5c2ce2799ccbc8946a717b0e7a`. Always fetch the live branch HEAD first in the new chat because this continuation update itself will create a newer documentation-only commit.

The dual-C6 Zigbee laboratory blocker is CLOSED. Fresh-network IAS Contact and preserved-storage restart/rejoin both passed on real hardware. Generic Zigbee interview/capability-discovery work described later in this file is completed/regression context, not the next active feature. Do not restart investigation of `zb_storage`, `esp_zigbee_start(false)`, ZoneType datatype, IAS enrollment, RestoreNotify, or same-short rejoin unless new code creates regression evidence.

Verified C6 hardware identities (re-resolve live serial ports before every hardware operation):

- gateway/coordinator C6: `40:4C:CA:5D:0A:00`;
- emulator C6: `40:4C:CA:5D:01:D8`.

### What is implemented but not yet physically closed

The C6 GatewayLink I2C backend already exists and is host-tested/build-tested. C6 is I2C master on I2C0, SDA GPIO1, SCL GPIO0, planned S3 slave address `0x42`, 400 kHz; the local SCD4x remains on the same bus at `0x62`. Missing-peer timeout/backoff is intentionally bounded. UART remains the known-working fallback and must not be deleted before independent I2C hardware validation.

There has been no physical C6 ↔ S3 GatewayLink I2C validation yet because the matching S3 slave/mailbox side belongs in the S3 repository. That is the current cross-repository dependency.

### Next real milestone

The next project milestone is physical C6 ↔ S3 GatewayLink I2C validation, but its first implementation step is on S3: implement the matching I2C slave/mailbox in a separately bound S3 context. Once S3 is ready, return to this C6 repository and execute the deferred physical validation sequence already listed below: preserve Zigbee storage, flash/select the C6 I2C backend, verify HELLO/ACK + `peer=1`, C6→S3 measurements, S3→C6 control, shared-bus SCD4x stability, S3 disconnect/backoff, reconnect recovery without C6 flash/NVS erase, then a bounded soak.

Until the S3 side exists or new C6-specific evidence/task is supplied, an idle C6 Local Agent is expected. Do not invent filler work and do not rerun the completed dual-C6 laboratory goal.

### New-chat operating rule

On opening a new C6 chat: fetch the live active-branch HEAD and fresh `agent-control:.agent/status/daemon.json`, read this file plus the canonical docs above, then continue only from repository evidence. If daemon is idle and no new C6-side task is enabled by evidence, say so tersely rather than creating work.
'''

s = s.replace(marker, handoff + '\n\n' + marker, 1)

old = 'Before physical C6↔S3 wiring, the next major C6 feature is **Generic Zigbee Device Interview & Capability Discovery**. The goal is to stop treating supported devices as model-specific special cases wherever standard ZCL metadata is sufficient.'
new = 'This milestone is already complete in the frozen dual-C6 baseline. The design notes below are retained as historical capability/discovery contract and regression scope; **do not treat this section as the next active task**.'
assert old in s
s = s.replace(old, new, 1)

s = s.replace(
    'After the Zigbee-laboratory baseline is frozen, required C6↔S3 end-to-end behaviors are:',
    'With the Zigbee-laboratory baseline now frozen, required C6↔S3 end-to-end behaviors are:',
    1,
)

p.write_text(s)
PY

git diff --check
git diff -- docs/CONTINUATION.md
git add docs/CONTINUATION.md
test "$(git diff --cached --name-only)" = "docs/CONTINUATION.md"
git commit -m "Refresh continuation handoff for new chat"
git push origin integration/c6-s3-i2c-20260903
printf 'HANDOFF_HEAD='
git rev-parse HEAD
git status --short
