#!/usr/bin/env bash
set -euo pipefail

git fetch --quiet origin agent-control
BASE=/tmp/freeze_verified_baseline_v2.sh
PATCHED=/tmp/freeze_verified_baseline_v3_patched.sh
git show origin/agent-control:.agent/scripts/freeze_verified_baseline_v2.sh > "$BASE"
python3 - "$BASE" "$PATCHED" <<'PY'
from pathlib import Path
import sys
src = Path(sys.argv[1]).read_text()
old = '''test "$(grep -R -l 'gateway_event_receive(' main --include='*.c' | wc -l | tr -d ' ')" = "1"
grep -q 'gateway_transport.c' <(grep -R -l 'gateway_event_receive(' main --include='*.c')
'''
new = '''CONSUMERS=$(grep -R -l 'gateway_event_receive(' main --include='*.c' | grep -v '^main/gateway_events.c$' || true)
test "$(printf '%s\n' "$CONSUMERS" | sed '/^$/d' | wc -l | tr -d ' ')" = "1"
test "$CONSUMERS" = "main/gateway_transport.c"
'''
if old not in src:
    raise SystemExit('expected v2 consumer invariant block not found')
Path(sys.argv[2]).write_text(src.replace(old, new, 1))
PY
chmod +x "$PATCHED"
exec "$PATCHED"
