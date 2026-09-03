#!/usr/bin/env bash
set -euo pipefail

git fetch origin agent-control
git show origin/agent-control:.agent/scripts/20260903-c6-generic-zigbee-phase6-level-v3.sh > /tmp/c6-phase6-level-v3.sh
sed '/^\. ~\/esp\/esp-idf-v5.5.4\/export.sh/,$d' /tmp/c6-phase6-level-v3.sh > /tmp/c6-phase6-level-v4-pre.sh
bash /tmp/c6-phase6-level-v4-pre.sh

python3 - <<'PY'
from pathlib import Path
p = Path('main/gateway_zcl_value.c')
s = p.read_text()
marker = '#include <string.h>\n\n'
if marker not in s:
    raise SystemExit('gateway_zcl_value include marker missing')
if '#define ZCL_CLUSTER_LEVEL_CONTROL 0x0008U\n' not in s:
    s = s.replace(marker, marker + '#define ZCL_CLUSTER_LEVEL_CONTROL 0x0008U\n\n', 1)
s = s.replace('EZB_ZCL_CLUSTER_ID_LEVEL_CONTROL', 'ZCL_CLUSTER_LEVEL_CONTROL')
p.write_text(s)
PY

python3 tests/host/test_no_embedded_nul.py
git diff --check
cc -std=c11 -Wall -Wextra -Werror -pedantic -DGATEWAY_ZCL_HOST_TEST -Imain tests/host/test_gateway_zcl_value.c main/gateway_zcl_value.c -lm -o /tmp/test_gateway_zcl_value && /tmp/test_gateway_zcl_value

. ~/esp/esp-idf-v5.5.4/export.sh >/dev/null
idf.py set-target esp32c6
idf.py build
idf.py size
rm -f sdkconfig.old
python3 tests/host/test_no_embedded_nul.py
git diff --check

git add main tests docs
git diff --cached --check
git commit -m 'Add normalized Zigbee Level Control'
git push origin HEAD:integration/c6-s3-i2c-20260903
printf 'PHASE6_HEAD=%s\n' "$(git rev-parse HEAD)"
git status --short
