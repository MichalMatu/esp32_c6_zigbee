#!/usr/bin/env bash
set -euo pipefail
BRANCH=integration/c6-s3-i2c-20260903
BASE=5bde32fba74dce2e7e9fba07884209efee8fb026
CP=/Users/michal/agent-workspace/repos/esp32-c6-zigbee/checkpoints/20260904-c6-finalize-software-audit-v44/1788538006242334000-task-exit

git fetch origin "$BRANCH"
test "$(git rev-parse origin/$BRANCH)" = "$BASE"
git checkout -B "$BRANCH" "$BASE"
git reset --hard "$BASE"
git clean -fd
git apply "$CP/tracked.patch"
cp -R "$CP/untracked/." .
python3 - <<'PY'
from pathlib import Path
p=Path('main/zigbee_gateway_commands.c')
s=p.read_text()
p.write_text(s.rstrip('\n')+'\n')
PY

git diff --check
./scripts/run_host_tests.sh
. ~/esp/esp-idf-v5.5.4/export.sh >/dev/null

rm -rf build sdkconfig sdkconfig.old
idf.py set-target esp32c6 >/dev/null
idf.py build
python3 - <<'PY'
from pathlib import Path
p=Path('sdkconfig')
s=p.read_text()
old='CONFIG_GATEWAY_LINK_BACKEND_UART=y'
new='# CONFIG_GATEWAY_LINK_BACKEND_UART is not set\nCONFIG_GATEWAY_LINK_BACKEND_I2C=y'
if old not in s:
    raise SystemExit('UART backend config missing')
p.write_text(s.replace(old,new,1))
PY
idf.py reconfigure >/dev/null
idf.py build

cd tests/zigbee_device_emulator
rm -rf build sdkconfig sdkconfig.old
idf.py set-target esp32c6 >/dev/null
idf.py build
cd ../..
git checkout -- tests/zigbee_device_emulator/dependencies.lock 2>/dev/null || true

git diff --check
git add main tests/host scripts .github/workflows/quality.yml README.md docs/ARCHITECTURE.md docs/CONTINUATION.md docs/AUDIT_2026-09-04.md
git diff --cached --check
git commit -m 'Refactor Zigbee gateway before S3 integration'
git push origin HEAD:"$BRANCH"
echo SOFTWARE_AUDIT_HEAD=$(git rev-parse HEAD)
