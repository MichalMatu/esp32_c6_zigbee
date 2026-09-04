#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
echo "[migration] root=$ROOT"
python3 - <<'PY'
from pathlib import Path
import json
r=Path.cwd()
required=['module.json','docs/LITEGRAPH_MIGRATION.md','docs/README.md','docs/GATEWAY_LINK_V2.md','docs/ARCHITECTURE.md','docs/VERIFIED_BASELINE.md','scripts/run_host_tests.sh']
missing=[p for p in required if not (r/p).exists()]
if missing: raise SystemExit('missing: '+', '.join(missing))
m=json.loads((r/'module.json').read_text())
assert m['module_id']=='zigbee-c6'
assert m['target']=='esp32c6'
assert m['framework']['verified_version']=='5.5.4'
assert m['dependencies']['espressif/esp-zigbee-lib']=='2.0.4'
assert m['gatewaylink']['protocol_version']==2
assert m['gatewaylink']['default_backend']=='uart1'
assert 'i2c0-mailbox' in m['gatewaylink']['supported_backends']
assert m['gatewaylink']['i2c']['sda_gpio']==1 and m['gatewaylink']['i2c']['scl_gpio']==0
assert m['gatewaylink']['i2c']['planned_s3_slave_address']=='0x42'
assert m['gatewaylink']['i2c']['shared_scd4x_address']=='0x62'
assert m['migration']['suggested_destination']=='firmware/extensions/zigbee-c6/'
print('[migration] manifest PASS')
PY
tracked="$(git ls-files | grep -E '(^|/)(build/|managed_components/|sdkconfig$|sdkconfig\.old$)' || true)"
[[ -z "$tracked" ]] || { echo "$tracked" >&2; exit 2; }
git diff --check
./scripts/run_host_tests.sh
echo "[migration] readiness PASS"
