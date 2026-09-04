#!/bin/bash
set -euo pipefail
BRANCH=integration/c6-s3-i2c-20260903
EXPECTED=5bde32fba74dce2e7e9fba07884209efee8fb026

git fetch origin "$BRANCH" agent-control
test "$(git rev-parse origin/$BRANCH)" = "$EXPECTED"
git checkout -B "$BRANCH" "$EXPECTED"
git reset --hard "$EXPECTED"
git show origin/agent-control:.agent/scripts/20260904-c6-structural-refactor-v33.prompt.txt > /tmp/c6_refactor_prompt.txt

codex exec --full-auto -C "$PWD" --output-last-message /tmp/c6_refactor_last.txt - < /tmp/c6_refactor_prompt.txt

echo '=== CODEX SUMMARY ==='
cat /tmp/c6_refactor_last.txt || true
git diff --check

if [ -x tests/host/run_all.sh ]; then
  tests/host/run_all.sh
elif [ -x tools/run_host_tests.sh ]; then
  tools/run_host_tests.sh
else
  echo 'Canonical host test runner missing after refactor' >&2
  exit 2
fi

. ~/esp/esp-idf-v5.5.4/export.sh >/dev/null
rm -rf build managed_components sdkconfig sdkconfig.old
idf.py set-target esp32c6 >/dev/null
idf.py build

python3 - <<'PY'
from pathlib import Path
p=Path('sdkconfig')
s=p.read_text()
old='CONFIG_GATEWAY_LINK_BACKEND_UART=y'
if old not in s:
    raise SystemExit('UART backend selection missing')
p.write_text(s.replace(old, '# CONFIG_GATEWAY_LINK_BACKEND_UART is not set\nCONFIG_GATEWAY_LINK_BACKEND_I2C=y', 1))
PY
idf.py reconfigure >/dev/null
idf.py build

cd tests/zigbee_device_emulator
rm -rf build managed_components sdkconfig sdkconfig.old
idf.py set-target esp32c6 >/dev/null
idf.py build
cd ../..

git checkout -- dependencies.lock tests/zigbee_device_emulator/dependencies.lock 2>/dev/null || true
rm -rf build managed_components sdkconfig sdkconfig.old \
  tests/zigbee_device_emulator/build tests/zigbee_device_emulator/managed_components \
  tests/zigbee_device_emulator/sdkconfig tests/zigbee_device_emulator/sdkconfig.old
python3 tests/host/test_no_embedded_nul.py
git diff --check

echo '=== FINAL DIFF ==='
git status --short
git diff --stat
test -n "$(git status --short)"
git add -A
git diff --cached --check
git commit -m 'Refactor C6 gateway structure before S3 integration'
git push origin HEAD:"$BRANCH"
echo REFACTOR_HEAD=$(git rev-parse HEAD)
