#!/usr/bin/env bash
set -euo pipefail

git fetch origin agent-control
git show origin/agent-control:.agent/scripts/20260903-c6-emulator-fault-report-v1.sh > /tmp/c6-emulator-fault-report-v2-base.sh

python3 - <<'PY'
from pathlib import Path
p = Path('/tmp/c6-emulator-fault-report-v2-base.sh')
s = p.read_text()
old = '''        ++tick;
        const uint8_t iterations = CONFIG_EMULATOR_BURST_UPDATES ? EMULATOR_BURST_COUNT : 1U;
        if (!esp_zigbee_lock_acquire(pdMS_TO_TICKS(100))) {
'''
new = '''        ++tick;
#ifdef CONFIG_EMULATOR_BURST_UPDATES
        const uint8_t iterations = EMULATOR_BURST_COUNT;
#else
        const uint8_t iterations = 1U;
#endif
#ifdef CONFIG_EMULATOR_INJECT_INVALID_VALUES
        const bool inject_invalid = (tick % 6U) == 0U;
#else
        const bool inject_invalid = false;
#endif
        if (!esp_zigbee_lock_acquire(pdMS_TO_TICKS(100))) {
'''
if old not in s:
    raise SystemExit('iterations marker not found')
s = s.replace(old, new, 1)
s = s.replace('CONFIG_EMULATOR_INJECT_INVALID_VALUES && (tick % 6U) == 0U', 'inject_invalid')
p.write_text(s)
PY

bash /tmp/c6-emulator-fault-report-v2-base.sh
