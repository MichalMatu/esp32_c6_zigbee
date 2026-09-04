#!/usr/bin/env bash
set -euo pipefail
ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"
git fetch origin agent-control
BASE_SCRIPT=/tmp/c6_migration_ready_hardware_v55.base.sh
git show FETCH_HEAD:.agent/scripts/20260904-c6-post-refactor-hardware-v47.sh > "$BASE_SCRIPT"
sed \
  -e 's/f13b293be2de6b1601d179568424e0046d6219a7/5ce963d6ee3b03b9b788f9d02bd9acb4910acead/g' \
  -e 's/POST_REFACTOR_HARDWARE_GATE=PASS/POST_MIGRATION_READY_HARDWARE_GATE=PASS/g' \
  -e 's/UART_POST_REFACTOR_DUAL_C6=PASS/UART_MIGRATION_READY_DUAL_C6=PASS/g' \
  "$BASE_SCRIPT" > /tmp/c6_migration_ready_hardware_v55.run.sh
chmod +x /tmp/c6_migration_ready_hardware_v55.run.sh
/tmp/c6_migration_ready_hardware_v55.run.sh
