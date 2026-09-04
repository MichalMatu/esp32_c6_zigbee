#!/usr/bin/env bash
set -euo pipefail

BRANCH='integration/c6-s3-i2c-20260903'
BASE='5ce963d6ee3b03b9b788f9d02bd9acb4910acead'
CHECKPOINT='/Users/michal/agent-workspace/repos/esp32-c6-zigbee/checkpoints/20260904-c6-final-critical-reaudit-recover-v57/1788552652003432000-task-exit'
TAG='c6-litegraph-migration-ready-2026-09-04'
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
    'docs/FINAL_REAUDIT_2026-09-04.md',
]:
    p=Path(name)
    p.write_text(p.read_text().rstrip()+'\n')

p=Path('docs/README.md')
s=p.read_text()
entry='- `FINAL_REAUDIT_2026-09-04.md` — final critical pre-LiteGraph software/hardware audit and residual-risk statement.\n'
if entry not in s:
    marker='- `VERIFIED_BASELINE.md` — physical hardware evidence and recovery checkpoints.\n'
    if marker in s:
        s=s.replace(marker, marker+entry, 1)
    else:
        marker='## Verification and handoff\n'
        if marker not in s:
            raise SystemExit('documentation index section missing')
        s=s.replace(marker, marker+entry, 1)
p.write_text(s.rstrip()+'\n')
PY

# Runtime/build/test inputs must remain identical to the physically proven source.
changed_runtime="$(git diff --name-only "$BASE" -- CMakeLists.txt dependencies.lock main partitions.csv sdkconfig.defaults tests/host tests/zigbee_device_emulator scripts/run_host_tests.sh .github/workflows/quality.yml)"
if [[ -n "$changed_runtime" ]]; then
  echo 'unexpected runtime/build/test change during final closure:' >&2
  echo "$changed_runtime" >&2
  exit 10
fi

echo '=== FINAL REAUDIT V58: repository + contract audit ==='
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
print('manifest_contract=PASS')
PY

grep -q '^#define GATEWAY_LINK_PROTOCOL_VERSION 2U' main/gateway_link_protocol.h
if grep -RIn --exclude='GATEWAY_LINK_V1.md' --exclude='FINAL_REAUDIT_2026-09-04.md' -E 'GatewayLink v1|protocol version 1|GATEWAY_LINK_PROTOCOL_VERSION[[:space:]]+1' main docs README.md module.json; then
  echo 'active v1 compatibility/reference leaked into current contract' >&2
  exit 12
fi

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

echo '=== FINAL REAUDIT V58: canonical software gates ==='
./scripts/verify_migration_ready.sh
./scripts/run_host_tests.sh

. /Users/michal/esp/esp-idf/export.sh >/dev/null

echo '=== FINAL REAUDIT V58: UART build ==='
rm -rf build sdkconfig sdkconfig.old managed_components
idf.py set-target esp32c6 >/dev/null
idf.py build >/dev/null
echo UART_BUILD=PASS

echo '=== FINAL REAUDIT V58: I2C build ==='
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

echo '=== FINAL REAUDIT V58: emulator build ==='
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

git add docs/CONTINUATION.md docs/LITEGRAPH_MIGRATION.md docs/VERIFIED_BASELINE.md docs/README.md docs/FINAL_REAUDIT_2026-09-04.md module.json
git diff --cached --check

git commit -m 'Finalize C6 LiteGraph migration freeze'
FINAL_HEAD="$(git rev-parse HEAD)"
git push origin HEAD:"$BRANCH"

if git ls-remote --exit-code --tags origin "refs/tags/$TAG" >/dev/null 2>&1; then
  echo "tag already exists: $TAG" >&2
  exit 14
fi
git tag -a "$TAG" -m 'C6 LiteGraph migration-ready freeze'
git push origin "$TAG"

test -z "$(git status --short)"
echo FINAL_REAUDIT=PASS
echo FINAL_FREEZE_HEAD="$FINAL_HEAD"
echo FINAL_MIGRATION_TAG="$TAG"
echo HARDWARE_PROVEN_RUNTIME_SOURCE="$BASE"
