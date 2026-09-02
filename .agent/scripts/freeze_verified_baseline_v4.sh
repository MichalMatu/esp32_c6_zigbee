#!/usr/bin/env bash
set -euo pipefail

git fetch --quiet origin agent-control
BASE=/tmp/freeze_verified_baseline_v2.sh
PATCHED=/tmp/freeze_verified_baseline_v4_patched.sh
git show origin/agent-control:.agent/scripts/freeze_verified_baseline_v2.sh > "$BASE"
python3 - "$BASE" "$PATCHED" <<'PY'
from pathlib import Path
import sys
src = Path(sys.argv[1]).read_text()
old_consumers = '''test "$(grep -R -l 'gateway_event_receive(' main --include='*.c' | wc -l | tr -d ' ')" = "1"
grep -q 'gateway_transport.c' <(grep -R -l 'gateway_event_receive(' main --include='*.c')
'''
new_consumers = '''CONSUMERS=$(grep -R -l 'gateway_event_receive(' main --include='*.c' | grep -v '^main/gateway_events.c$' || true)
test "$(printf '%s\\n' "$CONSUMERS" | sed '/^$/d' | wc -l | tr -d ' ')" = "1"
test "$CONSUMERS" = "main/gateway_transport.c"
'''
old_unsecure = "grep -q 'EZB_ZDO_UPDDEV_UNSECURE_JOIN' main/zigbee_gateway.c\n"
new_unsecure = '''python3 - <<'PYCHECK'
from pathlib import Path
s = Path('main/zigbee_gateway.c').read_text()
start = s.index('static bool app_signal_handler(')
end = s.index('static void fail_zigbee_task', start)
h = s[start:end]
assert 'p->status == EZB_ZDO_UPDDEV_SECURE_REJOIN' in h
assert 'p->status == EZB_ZDO_UPDDEV_TC_REJOIN' in h
assert 'publish_device_update(p);' in h
assert 'GATEWAY_EVENT_DEVICE_UPDATE, p->short_addr' not in h
PYCHECK
'''
if old_consumers not in src:
    raise SystemExit('expected v2 consumer invariant block not found')
if old_unsecure not in src:
    raise SystemExit('expected v2 unsecure invariant line not found')
src = src.replace(old_consumers, new_consumers, 1)
src = src.replace(old_unsecure, new_unsecure, 1)
Path(sys.argv[2]).write_text(src)
PY
chmod +x "$PATCHED"
exec "$PATCHED"
