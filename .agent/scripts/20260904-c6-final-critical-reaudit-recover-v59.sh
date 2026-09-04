#!/usr/bin/env bash
set -euo pipefail

BRANCH='integration/c6-s3-i2c-20260903'
BASE='5ce963d6ee3b03b9b788f9d02bd9acb4910acead'
CHECKPOINT='/Users/michal/agent-workspace/repos/esp32-c6-zigbee/checkpoints/20260904-c6-final-critical-reaudit-recover-v58/1788552915632754000-task-exit'
ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

git fetch origin "$BRANCH"
git checkout -B "$BRANCH" "origin/$BRANCH"
test "$(git rev-parse HEAD)" = "$BASE"
git reset --hard "$BASE"
git clean -fd

git apply "$CHECKPOINT/tracked.patch"
cp "$CHECKPOINT/untracked/docs/FINAL_REAUDIT_2026-09-04.md" docs/FINAL_REAUDIT_2026-09-04.md

python3 - <<'PY'
from pathlib import Path
for name in [
    'docs/CONTINUATION.md',
    'docs/VERIFIED_BASELINE.md',
    'docs/LITEGRAPH_MIGRATION.md',
    'docs/README.md',
    'docs/FINAL_REAUDIT_2026-09-04.md',
]:
    p=Path(name)
    p.write_text(p.read_text().rstrip()+'\n')
PY

# Runtime/build/test inputs must remain byte-for-byte identical to the physically proven source.
changed_runtime="$(git diff --name-only "$BASE" -- CMakeLists.txt dependencies.lock main partitions.csv sdkconfig.defaults tests/host tests/zigbee_device_emulator scripts/run_host_tests.sh .github/workflows/quality.yml)"
if [[ -n "$changed_runtime" ]]; then
  echo 'unexpected runtime/build/test change during final closure:' >&2
  echo "$changed_runtime" >&2
  exit 10
fi

echo '=== FINAL REAUDIT V59: repository + manifest + active-contract audit ==='
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
assert m['gatewaylink']['contract']=='docs/GATEWAY_LINK_V2.md'
assert m['gatewaylink']['default_backend']=='uart1'
assert m['gatewaylink']['verified_fallback_backend']=='uart1'
i=m['gatewaylink']['i2c']
assert i['controller']=='I2C0' and i['role']=='master'
assert i['sda_gpio']==1 and i['scl_gpio']==0 and i['frequency_hz']==400000
assert i['planned_s3_slave_address']=='0x42'
assert i['shared_scd4x_address']=='0x62'
assert i['transaction_timeout_ms']==20 and i['absent_peer_backoff_ms']==1000
v=m['verification']
assert v['hardware_tested_source']=='5ce963d6ee3b03b9b788f9d02bd9acb4910acead'
assert v['final_hardware_gate']=='20260904-c6-migration-ready-hardware-v55'
assert v['final_hardware_gate_result']=='PASS'
assert v['final_reaudit']=='docs/FINAL_REAUDIT_2026-09-04.md'
assert m['migration']['suggested_destination']=='firmware/extensions/zigbee-c6/'
assert m['migration']['export_source']=='5ce963d6ee3b03b9b788f9d02bd9acb4910acead'
assert Path(v['final_reaudit']).exists()

# Active documentation must select v2; historical v1 references are explicitly allowed.
idx=Path('docs/README.md').read_text()
assert 'GATEWAY_LINK_V2.md' in idx and 'active C6↔S3 wire contract' in idx
assert 'GATEWAY_LINK_V1.md' in idx and 'Historical' in idx
mig=Path('docs/LITEGRAPH_MIGRATION.md').read_text()
assert 'GatewayLink v2 is the only active contract' in mig
assert 'Do not create two hand-maintained protocol copies and do not add a v1 compatibility shim' in mig
v2=Path('docs/GATEWAY_LINK_V2.md').read_text()
assert 'GatewayLink v1 remains documented' in v2
print('manifest_and_active_contract=PASS')
PY

# No v1 protocol implementation may exist in current runtime sources.
grep -q '^#define GATEWAY_LINK_PROTOCOL_VERSION 2U' main/gateway_link_protocol.h
if grep -RIn -E 'GATEWAY_LINK_PROTOCOL_VERSION[[:space:]]+1|gateway_link_v1|protocol_v1|compat.*v1|v1.*compat' main; then
  echo 'v1 implementation/shim leaked into runtime' >&2
  exit 12
fi

echo ACTIVE_GATEWAYLINK_V2_ONLY=PASS

for f in scripts/*.sh; do bash -n "$f"; done
python3 -m py_compile tests/host/*.py
if command -v shellcheck >/dev/null 2>&1; then
  shellcheck scripts/*.sh
  echo SHELLCHECK=PASS
fi
if command -v cppcheck >/dev/null 2>&1; then
  cppcheck --error-exitcode=1 --enable=warning,performance,portability --std=c11 --quiet main/*.c
  echo CPPCHECK=PASS
fi

echo '=== FINAL REAUDIT V59: canonical software gates ==='
./scripts/verify_migration_ready.sh
./scripts/run_host_tests.sh

. /Users/michal/esp/esp-idf/export.sh >/dev/null

echo '=== FINAL REAUDIT V59: UART build ==='
rm -rf build sdkconfig sdkconfig.old managed_components
idf.py set-target esp32c6 >/dev/null
idf.py build >/dev/null
echo UART_BUILD=PASS

echo '=== FINAL REAUDIT V59: I2C build ==='
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

echo '=== FINAL REAUDIT V59: emulator build ==='
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

BAD="$(git status --short | awk '{print $2}' | grep -Ev '^(docs/(CONTINUATION|LITEGRAPH_MIGRATION|VERIFIED_BASELINE|README|FINAL_REAUDIT_2026-09-04)\.md|module\.json)$' || true)"
if [[ -n "$BAD" ]]; then
  echo 'unexpected final closure files:' >&2
  echo "$BAD" >&2
  exit 13
fi

# Re-prove runtime/build/test inputs did not change during the closure/gates.
changed_runtime="$(git diff --name-only "$BASE" -- CMakeLists.txt dependencies.lock main partitions.csv sdkconfig.defaults tests/host tests/zigbee_device_emulator scripts/run_host_tests.sh .github/workflows/quality.yml)"
test -z "$changed_runtime"

git add docs/CONTINUATION.md docs/LITEGRAPH_MIGRATION.md docs/VERIFIED_BASELINE.md docs/README.md docs/FINAL_REAUDIT_2026-09-04.md module.json
git diff --cached --check

git commit -m 'Finalize C6 LiteGraph migration freeze'
FINAL_HEAD="$(git rev-parse HEAD)"
git push origin HEAD:"$BRANCH"

test -z "$(git status --short)"
echo FINAL_REAUDIT_SOFTWARE=PASS
echo FINAL_FREEZE_HEAD="$FINAL_HEAD"
echo HARDWARE_PROVEN_RUNTIME_SOURCE="$BASE"
