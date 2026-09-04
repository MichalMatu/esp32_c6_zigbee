#!/usr/bin/env bash
set -euo pipefail

BRANCH="integration/c6-s3-i2c-20260903"
EXPECTED_BASE="5801964144ccc8f825c4d4548daca4a1e526937c"
CHECKPOINT="/Users/michal/agent-workspace/repos/esp32-c6-zigbee/checkpoints/20260904-c6-migration-readiness-v53/1788549461137654000-task-exit"
ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

git fetch origin "$BRANCH"
git checkout -B "$BRANCH" "origin/$BRANCH"
git reset --hard "origin/$BRANCH"
if [[ "$(git rev-parse HEAD)" != "$EXPECTED_BASE" ]]; then
  echo "unexpected base HEAD: $(git rev-parse HEAD) expected $EXPECTED_BASE" >&2
  exit 2
fi
rm -rf build sdkconfig sdkconfig.old

if [[ ! -f "$CHECKPOINT/tracked.patch" || ! -d "$CHECKPOINT/untracked" ]]; then
  echo "v53 checkpoint missing: $CHECKPOINT" >&2
  exit 3
fi

git apply "$CHECKPOINT/tracked.patch"
cp -R "$CHECKPOINT/untracked/." "$ROOT/"

# v53 proved all gates through both firmware variants; only its emulator path was stale.
python3 - <<'PY'
from pathlib import Path
import json
root = Path.cwd()
manifest = root / 'module.json'
m = json.loads(manifest.read_text())
old = m['verification'].get('emulator_path')
if old not in ('tools/zigbee_device_emulator', 'tests/zigbee_device_emulator'):
    raise SystemExit(f'unexpected emulator_path {old!r}')
m['verification']['emulator_path'] = 'tests/zigbee_device_emulator'
m['verification']['emulator_build'] = 'idf.py -C tests/zigbee_device_emulator set-target esp32c6 && idf.py -C tests/zigbee_device_emulator build'
manifest.write_text(json.dumps(m, indent=2) + '\n')
for rel in ['docs/LITEGRAPH_MIGRATION.md', 'README.md', 'docs/CONTINUATION.md', 'docs/README.md']:
    p = root / rel
    if p.exists():
        s = p.read_text()
        s = s.replace('tools/zigbee_device_emulator/', 'tests/zigbee_device_emulator/')
        s = s.replace('tools/zigbee_device_emulator', 'tests/zigbee_device_emulator')
        p.write_text(s)
PY

chmod +x scripts/run_host_tests.sh scripts/verify_migration_ready.sh

echo "=== DIFF / READINESS ==="
git diff --check
./scripts/verify_migration_ready.sh

echo "=== ESP-IDF ==="
source /Users/michal/esp/esp-idf/export.sh >/dev/null

echo "=== UART BUILD ==="
rm -rf build sdkconfig sdkconfig.old
idf.py set-target esp32c6 >/dev/null
idf.py build >/tmp/c6-v54-uart-build.log 2>&1 || { cat /tmp/c6-v54-uart-build.log; exit 10; }
echo "UART_BUILD=PASS"

echo "=== I2C BUILD ==="
python3 - <<'PY'
from pathlib import Path
p=Path('sdkconfig')
s=p.read_text()
s=s.replace('CONFIG_GATEWAY_LINK_BACKEND_UART=y', '# CONFIG_GATEWAY_LINK_BACKEND_UART is not set')
if 'CONFIG_GATEWAY_LINK_BACKEND_I2C=y' not in s:
    s += '\nCONFIG_GATEWAY_LINK_BACKEND_I2C=y\n'
p.write_text(s)
PY
idf.py build >/tmp/c6-v54-i2c-build.log 2>&1 || { cat /tmp/c6-v54-i2c-build.log; exit 11; }
echo "I2C_BUILD=PASS"

echo "=== EMULATOR BUILD ==="
rm -rf /tmp/c6-emu-build-v54 /tmp/c6-emu-sdkconfig-v54 /tmp/c6-emu-sdkconfig-v54.old
idf.py -C tests/zigbee_device_emulator -B /tmp/c6-emu-build-v54 -D SDKCONFIG=/tmp/c6-emu-sdkconfig-v54 set-target esp32c6 >/dev/null
idf.py -C tests/zigbee_device_emulator -B /tmp/c6-emu-build-v54 -D SDKCONFIG=/tmp/c6-emu-sdkconfig-v54 build >/tmp/c6-v54-emulator-build.log 2>&1 || { cat /tmp/c6-v54-emulator-build.log; exit 12; }
echo "EMULATOR_BUILD=PASS"

rm -rf build sdkconfig sdkconfig.old
./scripts/verify_migration_ready.sh
git diff --check

if git status --short | grep -E '(^|/)(build/|sdkconfig\.old$|sdkconfig$|managed_components/)' >/dev/null; then
  echo "generated build/config debris remains" >&2
  git status --short
  exit 13
fi

git add -A
git diff --cached --check
if git diff --cached --quiet; then
  echo "no migration-readiness changes recovered" >&2
  exit 14
fi

git commit -m "Prepare C6 module for LiteGraph migration"
git push origin HEAD:"$BRANCH"
FINAL_HEAD=$(git rev-parse HEAD)
echo "MIGRATION_READY_SOFTWARE_HEAD=$FINAL_HEAD"
git status --short
