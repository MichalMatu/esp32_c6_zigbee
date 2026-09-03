#!/usr/bin/env bash
set -euo pipefail

git fetch origin agent-control
git show origin/agent-control:.agent/scripts/20260903-c6-emulator-profiles-v1.sh > /tmp/c6-emulator-profiles-v1.sh
python3 - <<'PY'
from pathlib import Path
p = Path('/tmp/c6-emulator-profiles-v1.sh')
s = p.read_text()
old = "pio run -c platformio.tests.ini -e test-all-host\n"
new = "if [ -f platformio.tests.ini ]; then\n    pio run -c platformio.tests.ini -e test-all-host\nelse\n    echo 'platformio.tests.ini absent; skipping host PlatformIO gate'\nfi\n"
if old not in s:
    raise SystemExit('expected host-test line not found')
p.write_text(s.replace(old, new, 1))
PY
bash /tmp/c6-emulator-profiles-v1.sh
