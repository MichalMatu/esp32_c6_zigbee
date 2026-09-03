#!/usr/bin/env bash
set -euo pipefail

git fetch origin agent-control
git show origin/agent-control:.agent/scripts/20260903-c6-emulator-skeleton-v1.sh > /tmp/c6-emulator-skeleton-v2-base.sh
python3 - <<'PY'
from pathlib import Path
p = Path('/tmp/c6-emulator-skeleton-v2-base.sh')
s = p.read_text()
needle = "mkdir -p tests/zigbee_device_emulator/main\n"
replacement = """mkdir -p tests/zigbee_device_emulator/main
cat > tests/zigbee_device_emulator/.gitignore <<'EOF'
build/
managed_components/
sdkconfig
sdkconfig.old
EOF
"""
if s.count(needle) != 1:
    raise SystemExit('emulator mkdir marker mismatch')
s = s.replace(needle, replacement, 1)
p.write_text(s)
PY
bash /tmp/c6-emulator-skeleton-v2-base.sh
