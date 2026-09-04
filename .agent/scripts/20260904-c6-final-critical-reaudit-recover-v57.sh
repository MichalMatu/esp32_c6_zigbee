#!/usr/bin/env bash
set -euo pipefail

BRANCH='integration/c6-s3-i2c-20260903'
BASE='5ce963d6ee3b03b9b788f9d02bd9acb4910acead'
CHECKPOINT='/Users/michal/agent-workspace/repos/esp32-c6-zigbee/checkpoints/20260904-c6-final-critical-reaudit-v56/1788552181206270000-task-exit'
TAG='c6-litegraph-migration-ready-2026-09-04'
ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

git fetch origin "$BRANCH"
git checkout -B "$BRANCH" "origin/$BRANCH"
test "$(git rev-parse HEAD)" = "$BASE"
git reset --hard "$BASE"
git clean -fd

git apply "$CHECKPOINT/tracked.patch"

# Normalize trailing blank lines introduced by the failed v56 closure pass.
python3 - <<'PY'
from pathlib import Path
for name in ['docs/CONTINUATION.md', 'docs/VERIFIED_BASELINE.md', 'docs/LITEGRAPH_MIGRATION.md']:
    p=Path(name)
    s=p.read_text()
    p.write_text(s.rstrip()+'\n')
PY

cat > docs/FINAL_REAUDIT_2026-09-04.md <<'EOF'
# Final critical re-audit — 2026-09-04

## Scope

This is the final pre-LiteGraph re-audit of the ESP32-C6 Zigbee extension repository. The audited runtime/export source is `5ce963d6ee3b03b9b788f9d02bd9acb4910acead` (`Prepare C6 module for LiteGraph migration`). The purpose is to prove that the module is clean, reproducible, internally consistent, migration-ready, and backed by physical hardware evidence before absorption into the LiteGraph monorepo.

## Critical invariants checked

- native ESP-IDF C6 firmware; verified with ESP-IDF v5.5.4;
- `espressif/esp-zigbee-lib` v2.0.4;
- GatewayLink protocol version 2 only; v1 remains historical and no v1 compatibility shim is introduced;
- C6 remains Zigbee/local-I/O owner; S3 remains the future Wi-Fi/BLE/web/LiteGraph owner;
- IEEE-first Zigbee identity with mutable short address used only for current routing;
- bounded/nonblocking work/event boundaries are preserved;
- UART1 remains the verified/default fallback backend;
- I2C backend remains C6 master on I2C0, SDA GPIO1, SCL GPIO0, 400 kHz, planned S3 slave `0x42`, shared SCD4x `0x62`, bounded missing-peer behavior;
- no Arduino, Matter, Thread, Wi-Fi, BLE, MQTT or external RCP is added to the C6 module;
- persisted Zigbee/NVS expectations are preserved.

## Static and repository audit

The final audit checks:

- repository and generated-artifact hygiene;
- manifest/document/source contract consistency;
- stale migration-path references and historical-vs-active contract separation;
- shell syntax plus `shellcheck` when available;
- Python syntax for host checks;
- `cppcheck` portability/warning scan of `main/*.c` when available;
- `git diff --check`;
- canonical migration-readiness gate;
- canonical host-test suite;
- clean UART ESP-IDF build;
- clean I2C ESP-IDF build;
- clean Zigbee device emulator build;
- final tree contains no generated root `build/`, `sdkconfig`, `sdkconfig.old` or managed-component debris.

Two README occurrences using `/dev/cu.usbmodemXXXX` are intentional device-port placeholders, not hard-coded host dependencies. The documented `erase-flash` command is retained only as an explicit destructive recovery/factory-reset instruction; the migration/hardware validation path does not use it.

## Physical hardware evidence

Task `20260904-c6-migration-ready-hardware-v55` revalidated the exact runtime/export source `5ce963d6ee3b03b9b788f9d02bd9acb4910acead` on both physical ESP32-C6 boards without erasing Zigbee storage.

Verified results:

- preserved-storage UART dual-C6 IAS/rejoin/contact regression: PASS;
- emulator remained `factory_new=0`;
- gateway rejoin + announce: PASS;
- IAS CIE write + enroll: PASS;
- contact false/true reports: PASS;
- gateway/emulator panic/watchdog checks: PASS;
- no gateway event-queue drop: PASS;
- C6 I2C0 mailbox backend with S3 intentionally absent: PASS;
- expected `peer=0` while missing S3: PASS;
- shared SCD41/SCD4x CO2/temperature/humidity remained available: PASS;
- Zigbee contact traffic remained active on the shared-bus gate: PASS;
- no event/link queue drop and no panic: PASS;
- final UART1 restoration smoke: PASS.

Hardware identities used:

- gateway C6: `40:4C:CA:5D:0A:00`;
- emulator C6: `40:4C:CA:5D:01:D8`.

## Residual risks / deliberately unproven items

No critical or high-severity C6-only blocker remains before migration.

The following is intentionally **not** claimed as verified yet:

- physical C6↔S3 I2C traffic;
- S3 mailbox/slave implementation at `0x42`;
- GatewayLink v2 HELLO/ACK with `peer=1` over real C6↔S3 I2C;
- end-to-end C6→S3 measurements and S3→C6 control over the physical link;
- disconnect/reconnect recovery with a real S3 peer;
- post-integration soak across both MCUs.

Those belong to the LiteGraph-bound integration phase, after this module is imported unchanged and its nested build/test gates pass.

## Final disposition

The C6 repository is frozen for migration. No additional C6-only feature work or refactor is justified before import. Import first with no behavior redesign, verify the nested module, then implement the S3 side and perform the first true cross-MCU hardware gate.
EOF

# Ensure the machine-readable manifest records the exact hardware-proven export source.
python3 - <<'PY'
import json
from pathlib import Path
p=Path('module.json')
m=json.loads(p.read_text())
assert m['verification']['hardware_tested_source']=='5ce963d6ee3b03b9b788f9d02bd9acb4910acead'
m['verification']['final_hardware_gate']='20260904-c6-migration-ready-hardware-v55'
m['verification']['final_hardware_gate_result']='PASS'
m['verification']['final_reaudit']='docs/FINAL_REAUDIT_2026-09-04.md'
m['migration']['export_source']='5ce963d6ee3b03b9b788f9d02bd9acb4910acead'
p.write_text(json.dumps(m, indent=2)+'\n')
PY

# Keep documentation index authoritative.
python3 - <<'PY'
from pathlib import Path
p=Path('docs/README.md')
s=p.read_text()
needle='- [`VERIFIED_BASELINE.md`](VERIFIED_BASELINE.md) — frozen physical hardware evidence and recovery checkpoints.\n'
add='- [`FINAL_REAUDIT_2026-09-04.md`](FINAL_REAUDIT_2026-09-04.md) — final critical pre-LiteGraph software/hardware audit and residual-risk statement.\n'
if add not in s:
    if needle not in s: raise SystemExit('documentation index insertion point missing')
    s=s.replace(needle, needle+add, 1)
p.write_text(s.rstrip()+'\n')
PY

# Runtime/build inputs must remain byte-for-byte unchanged from the hardware-proven source.
changed_runtime="$(git diff --name-only "$BASE" -- CMakeLists.txt dependencies.lock main partitions.csv sdkconfig.defaults tests/host tests/zigbee_device_emulator scripts/run_host_tests.sh .github/workflows/quality.yml)"
if [[ -n "$changed_runtime" ]]; then
  echo 'unexpected runtime/build/test change during final closure:' >&2
  echo "$changed_runtime" >&2
  exit 10
fi

echo '=== FINAL REAUDIT: diff/hygiene ==='
git diff --check
if git ls-files | grep -Eq '(^|/)(build/|managed_components/|sdkconfig$|sdkconfig\.old$)'; then
  echo 'tracked generated artifact detected' >&2
  exit 11
fi

python3 - <<'PY'
import json
from pathlib import Path
m=json.loads(Path('module.json').read_text())
assert m['module_id']=='zigbee-c6'
assert m['target']=='esp32c6'
assert m['framework']['verified_version']=='5.5.4'
assert m['dependencies']['espressif/esp-zigbee-lib']=='2.0.4'
assert m['gatewaylink']['protocol_version']==2
assert m['gatewaylink']['default_backend']=='uart1'
assert m['gatewaylink']['i2c']['controller']=='I2C0'
assert m['gatewaylink']['i2c']['sda_gpio']==1
assert m['gatewaylink']['i2c']['scl_gpio']==0
assert m['gatewaylink']['i2c']['planned_s3_slave_address']=='0x42'
assert m['gatewaylink']['i2c']['shared_scd4x_address']=='0x62'
assert m['verification']['hardware_tested_source']=='5ce963d6ee3b03b9b788f9d02bd9acb4910acead'
assert m['verification']['final_hardware_gate_result']=='PASS'
assert m['migration']['suggested_destination']=='firmware/extensions/zigbee-c6/'
assert Path(m['verification']['final_reaudit']).exists()
print('manifest_contract=PASS')
PY

for f in scripts/*.sh; do bash -n "$f"; done
python3 -m py_compile tests/host/*.py
if command -v shellcheck >/dev/null 2>&1; then shellcheck scripts/*.sh; echo SHELLCHECK=PASS; fi
if command -v cppcheck >/dev/null 2>&1; then
  cppcheck --error-exitcode=1 --enable=warning,performance,portability --std=c11 --quiet main/*.c
  echo CPPCHECK=PASS
fi

echo '=== FINAL REAUDIT: canonical software gates ==='
./scripts/verify_migration_ready.sh
./scripts/run_host_tests.sh

. /Users/michal/esp/esp-idf/export.sh >/dev/null

echo '=== FINAL REAUDIT: UART build ==='
rm -rf build sdkconfig sdkconfig.old
idf.py set-target esp32c6 >/dev/null
idf.py build >/dev/null
echo UART_BUILD=PASS

echo '=== FINAL REAUDIT: I2C build ==='
python3 - <<'PY'
from pathlib import Path
p=Path('sdkconfig'); s=p.read_text(); old='CONFIG_GATEWAY_LINK_BACKEND_UART=y'
new='# CONFIG_GATEWAY_LINK_BACKEND_UART is not set\nCONFIG_GATEWAY_LINK_BACKEND_I2C=y'
if old not in s: raise SystemExit('UART backend config missing')
p.write_text(s.replace(old,new,1))
PY
idf.py reconfigure >/dev/null
idf.py build >/dev/null
echo I2C_BUILD=PASS

echo '=== FINAL REAUDIT: emulator build ==='
pushd tests/zigbee_device_emulator >/dev/null
rm -rf build sdkconfig sdkconfig.old managed_components
idf.py set-target esp32c6 >/dev/null
idf.py build >/dev/null
rm -rf build sdkconfig sdkconfig.old managed_components
popd >/dev/null
echo EMULATOR_BUILD=PASS

rm -rf build sdkconfig sdkconfig.old managed_components tests/host/__pycache__
git checkout -- dependencies.lock tests/zigbee_device_emulator/dependencies.lock 2>/dev/null || true

./scripts/verify_migration_ready.sh
git diff --check

# Only final documentation/metadata closure files may differ from the hardware-proven source.
BAD="$(git diff --name-only "$BASE" | grep -Ev '^(docs/(CONTINUATION|LITEGRAPH_MIGRATION|VERIFIED_BASELINE|README|FINAL_REAUDIT_2026-09-04)\.md|module\.json)$' || true)"
if [[ -n "$BAD" ]]; then
  echo 'unexpected final closure files:' >&2
  echo "$BAD" >&2
  exit 12
fi

# Commit and publish the closure.
git add docs/CONTINUATION.md docs/LITEGRAPH_MIGRATION.md docs/VERIFIED_BASELINE.md docs/README.md docs/FINAL_REAUDIT_2026-09-04.md module.json
git diff --cached --check
git commit -m 'Finalize C6 LiteGraph migration freeze'
FINAL_HEAD="$(git rev-parse HEAD)"
git push origin HEAD:"$BRANCH"

if git ls-remote --exit-code --tags origin "refs/tags/$TAG" >/dev/null 2>&1; then
  echo "tag already exists: $TAG" >&2
  exit 13
fi
git tag -a "$TAG" -m 'C6 LiteGraph migration-ready freeze'
git push origin "$TAG"

test -z "$(git status --short)"
echo FINAL_REAUDIT=PASS
echo FINAL_FREEZE_HEAD="$FINAL_HEAD"
echo FINAL_MIGRATION_TAG="$TAG"
echo HARDWARE_PROVEN_RUNTIME_SOURCE="$BASE"
