#!/usr/bin/env bash
set -euo pipefail

BRANCH='integration/c6-s3-i2c-20260903'
EXPECTED_BASE='5ce963d6ee3b03b9b788f9d02bd9acb4910acead'
ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

git fetch origin "$BRANCH"
git checkout -B "$BRANCH" "origin/$BRANCH"
git reset --hard "origin/$BRANCH"
if [[ "$(git rev-parse HEAD)" != "$EXPECTED_BASE" ]]; then
  echo "unexpected source HEAD=$(git rev-parse HEAD) expected=$EXPECTED_BASE" >&2
  exit 2
fi
if [[ -n "$(git status --short)" ]]; then
  echo 'workspace not clean before final audit' >&2
  git status --short
  exit 3
fi

rm -rf build sdkconfig sdkconfig.old tests/zigbee_device_emulator/build tests/zigbee_device_emulator/sdkconfig tests/zigbee_device_emulator/sdkconfig.old

echo '=== FINAL CRITICAL AUDIT: repository hygiene ==='
if git ls-files | grep -E '(^|/)(build/|managed_components/|sdkconfig$|sdkconfig\.old$|\.DS_Store$)' >/tmp/c6_tracked_debris; then
  echo 'tracked generated debris found:' >&2
  cat /tmp/c6_tracked_debris >&2
  exit 10
fi

git diff --check

python3 - <<'PY'
from pathlib import Path
import json, re, sys
root=Path.cwd()

# Parse manifest and compare hard contract against source/docs.
m=json.loads((root/'module.json').read_text())
assert m['schema_version']==1
assert m['module_id']=='zigbee-c6'
assert m['target']=='esp32c6'
assert m['framework']=={'name':'ESP-IDF','verified_version':'5.5.4'}
assert m['dependencies']['espressif/esp-zigbee-lib']=='2.0.4'
assert m['dependencies']['jef-sure/scd4x']=='0.0.3'
assert m['gatewaylink']['protocol_version']==2
assert m['gatewaylink']['default_backend']=='uart1'
assert set(m['gatewaylink']['supported_backends'])=={'uart1','i2c0-mailbox'}
assert m['gatewaylink']['i2c']['controller']=='I2C0'
assert m['gatewaylink']['i2c']['role']=='master'
assert m['gatewaylink']['i2c']['sda_gpio']==1
assert m['gatewaylink']['i2c']['scl_gpio']==0
assert m['gatewaylink']['i2c']['frequency_hz']==400000
assert m['gatewaylink']['i2c']['planned_s3_slave_address']=='0x42'
assert m['gatewaylink']['i2c']['shared_scd4x_address']=='0x62'
assert m['gatewaylink']['i2c']['transaction_timeout_ms']==20
assert m['gatewaylink']['i2c']['absent_peer_backoff_ms']==1000
assert m['verification']['emulator_path']=='tests/zigbee_device_emulator'
assert m['migration']['suggested_destination']=='firmware/extensions/zigbee-c6/'

proto=(root/'main/gateway_link_protocol.h').read_text()
if not re.search(r'#define\s+GATEWAY_LINK_PROTOCOL_VERSION\s+2U\b', proto):
    raise SystemExit('source protocol version is not exactly v2')
i2c=(root/'main/gateway_i2c_link.c').read_text()
mail=(root/'main/gateway_i2c_mailbox.h').read_text()
for needle,label in [('0x42','S3 peer address'),('20','I2C timeout/backoff source')]:
    if needle not in i2c and needle not in mail:
        raise SystemExit(f'missing expected {label} evidence in source')

# Active docs may mention v1 only as frozen/historical; reject compatibility language implying support.
active=[root/'README.md', root/'docs/ARCHITECTURE.md', root/'docs/GATEWAY_LINK_V2.md', root/'docs/LITEGRAPH_MIGRATION.md', root/'docs/CONTINUATION.md']
for p in active:
    s=p.read_text().lower()
    bad=['v1 compatibility shim is supported','gatewaylink v1 is active','support gatewaylink v1 and v2']
    for b in bad:
        if b in s:
            raise SystemExit(f'stale active v1 compatibility claim in {p}: {b}')

# Reject the known stale emulator path everywhere except immutable Local-Agent evidence under .agent (not tracked here).
stale=[]
for p in root.rglob('*'):
    if not p.is_file() or '.git' in p.parts or 'build' in p.parts:
        continue
    try: s=p.read_text(errors='strict')
    except Exception: continue
    if 'tools/zigbee_device_emulator' in s:
        stale.append(str(p.relative_to(root)))
if stale:
    raise SystemExit('stale emulator path(s): '+', '.join(stale))

# Ensure migration guide states the exact non-goals and true I2C status.
mig=(root/'docs/LITEGRAPH_MIGRATION.md').read_text()
required=[
  'firmware/extensions/zigbee-c6/', 'GatewayLink v2', 'UART',
  'True physical C6↔S3 I2C communication is still unverified',
  'tests/zigbee_device_emulator'
]
for x in required:
    if x not in mig:
        raise SystemExit('migration guide missing: '+x)
print('manifest/docs/source contract audit: PASS')
PY

echo '=== FINAL CRITICAL AUDIT: stale markers ==='
# Print markers for review, but fail only on dangerous migration blockers.
git grep -n -E 'TODO|FIXME|HACK|XXX' -- ':!docs/AUDIT_2026-09-04.md' ':!docs/CONTINUATION.md' || true
if git grep -n -E 'tools/zigbee_device_emulator|GATEWAY_LINK_PROTOCOL_VERSION[[:space:]]+1U' -- . ':!.agent' ; then
  echo 'migration-blocking stale path/protocol marker found' >&2
  exit 11
fi

echo '=== FINAL CRITICAL AUDIT: shell/python syntax ==='
while IFS= read -r f; do bash -n "$f"; done < <(git ls-files '*.sh')
while IFS= read -r f; do python3 -m py_compile "$f"; done < <(git ls-files '*.py')
find . -type d -name __pycache__ -prune -exec rm -rf {} +

if command -v shellcheck >/dev/null 2>&1; then
  echo '[audit] shellcheck available'
  shellcheck scripts/*.sh
else
  echo '[audit] shellcheck not installed; bash -n used'
fi
if command -v cppcheck >/dev/null 2>&1; then
  echo '[audit] cppcheck available'
  cppcheck --enable=warning,performance,portability --error-exitcode=1 --inline-suppr main 2>/tmp/c6_cppcheck.err || { cat /tmp/c6_cppcheck.err; exit 12; }
else
  echo '[audit] cppcheck not installed; host Werror suite remains canonical static gate'
fi

echo '=== FINAL CRITICAL AUDIT: migration readiness + host tests ==='
./scripts/verify_migration_ready.sh
./scripts/run_host_tests.sh

echo '=== FINAL CRITICAL AUDIT: ESP-IDF builds ==='
if [[ -f "$HOME/esp/esp-idf-v5.5.4/export.sh" ]]; then
  . "$HOME/esp/esp-idf-v5.5.4/export.sh" >/dev/null
elif [[ -f "$HOME/esp/esp-idf/export.sh" ]]; then
  . "$HOME/esp/esp-idf/export.sh" >/dev/null
else
  echo 'ESP-IDF export.sh not found' >&2
  exit 13
fi

# UART default
rm -rf build sdkconfig sdkconfig.old
idf.py set-target esp32c6 >/dev/null
idf.py build >/dev/null
echo 'UART_BUILD=PASS'

# I2C backend
python3 - <<'PY'
from pathlib import Path
p=Path('sdkconfig'); s=p.read_text()
old='CONFIG_GATEWAY_LINK_BACKEND_UART=y'
new='# CONFIG_GATEWAY_LINK_BACKEND_UART is not set\nCONFIG_GATEWAY_LINK_BACKEND_I2C=y'
if old not in s: raise SystemExit('UART backend default not found in sdkconfig')
p.write_text(s.replace(old,new,1))
PY
idf.py reconfigure >/dev/null
idf.py build >/dev/null
echo 'I2C_BUILD=PASS'

# Emulator
rm -rf tests/zigbee_device_emulator/build tests/zigbee_device_emulator/sdkconfig tests/zigbee_device_emulator/sdkconfig.old
idf.py -C tests/zigbee_device_emulator set-target esp32c6 >/dev/null
idf.py -C tests/zigbee_device_emulator build >/dev/null
echo 'EMULATOR_BUILD=PASS'

# Clean generated outputs before applying closure docs.
rm -rf build sdkconfig sdkconfig.old tests/zigbee_device_emulator/build tests/zigbee_device_emulator/sdkconfig tests/zigbee_device_emulator/sdkconfig.old
find . -type d -name __pycache__ -prune -exec rm -rf {} +

git checkout -- dependencies.lock tests/zigbee_device_emulator/dependencies.lock 2>/dev/null || true

# Final closure metadata. Runtime source remains unchanged by this task.
python3 - <<'PY'
from pathlib import Path
import json
root=Path.cwd()
base='5ce963d6ee3b03b9b788f9d02bd9acb4910acead'

p=root/'module.json'
m=json.loads(p.read_text())
v=m.setdefault('verification',{})
v['hardware_tested_source']=base
v['migration_ready_hardware_gate']=base
v['migration_ready_hardware_gate_result']='PASS'
v['migration_ready_hardware_gate_date']='2026-09-04'
m.setdefault('migration',{})['export_source_checkpoint']=base
p.write_text(json.dumps(m,indent=2)+'\n')

p=root/'docs/VERIFIED_BASELINE.md'
s=p.read_text()
section=f'''\n\n## LiteGraph migration-ready hardware closure — 2026-09-04\n\nThe migration-ready source checkpoint `{base}` (`Prepare C6 module for LiteGraph migration`) was revalidated on both physical ESP32-C6 boards without erasing Zigbee storage. The preserved-storage UART IAS regression passed (rejoin, announce, CIE write, enroll, contact true/false, no panic/event drop). The C6 I2C0 mailbox backend then passed with the S3 intentionally absent while the shared SCD41 and Zigbee traffic remained healthy (`peer=0` expected, no event/link queue drop, no panic, no SCD4x unavailable transition). The gateway was finally restored to the UART1 fallback and the bounded final smoke passed.\n\nThis proves the exported C6 checkpoint itself is hardware-safe before monorepo absorption. It does **not** prove physical C6↔S3 I2C; that remains the first true cross-MCU hardware gate after the S3 slave/mailbox exists.\n'''
if '## LiteGraph migration-ready hardware closure — 2026-09-04' not in s:
    p.write_text(s.rstrip()+section+'\n')

p=root/'docs/CONTINUATION.md'
s=p.read_text()
section=f'''\n\n## Final pre-LiteGraph freeze — 2026-09-04\n\nSource checkpoint `{base}` is the migration-ready C6 firmware proven by the final dual-C6 hardware gate. Software gates passed: migration readiness, canonical host tests, UART build, I2C build, emulator build, source hygiene and syntax/path/contract audit. Hardware gates passed without Zigbee/NVS erase: preserved-storage UART IAS/rejoin/contact regression, I2C missing-S3/shared-SCD41 stability, and final UART restore smoke.\n\nNo further C6-only feature/refactor work is justified before import into the LiteGraph monorepo. The next work belongs in a LiteGraph-bound context: import this module unchanged first, rerun its nested gates, then implement the S3 I2C slave/mailbox and perform true C6↔S3 GatewayLink v2 E2E validation.\n'''
if '## Final pre-LiteGraph freeze — 2026-09-04' not in s:
    p.write_text(s.rstrip()+section+'\n')

p=root/'docs/LITEGRAPH_MIGRATION.md'
s=p.read_text()
needle='## Recovery checkpoints\n'
line=f'- migration-ready hardware-proven export source → `{base}`\n'
if line not in s:
    s=s.replace(needle,needle+line,1)
p.write_text(s)
PY

./scripts/verify_migration_ready.sh
git diff --check

# Confirm task changed only metadata/docs, not firmware/runtime/test behavior.
changed="$(git diff --name-only)"
echo "$changed"
if echo "$changed" | grep -Ev '^(module\.json|docs/(VERIFIED_BASELINE|CONTINUATION|LITEGRAPH_MIGRATION)\.md)$' | grep -q .; then
  echo 'unexpected runtime/source change during closure task' >&2
  exit 14
fi

git add module.json docs/VERIFIED_BASELINE.md docs/CONTINUATION.md docs/LITEGRAPH_MIGRATION.md
git commit -m 'Freeze C6 migration-ready baseline'
git push origin HEAD:"$BRANCH"
FINAL_HEAD="$(git rev-parse HEAD)"

# Lightweight tag names the final docs/metadata closure; hardware-tested runtime source is recorded separately in module.json.
TAG='c6-litegraph-migration-ready-2026-09-04'
if git rev-parse -q --verify "refs/tags/$TAG" >/dev/null; then
  test "$(git rev-list -n1 "$TAG")" = "$FINAL_HEAD"
else
  git tag -a "$TAG" -m 'C6 LiteGraph migration-ready freeze' "$FINAL_HEAD"
  git push origin "$TAG"
fi

if [[ -n "$(git status --short)" ]]; then
  echo 'workspace dirty after final closure' >&2
  git status --short
  exit 15
fi

echo "FINAL_AUDIT_RUNTIME_SOURCE=$base"
echo "FINAL_AUDIT_CLOSURE_HEAD=$FINAL_HEAD"
echo "FINAL_AUDIT_TAG=$TAG"
echo 'FINAL_CRITICAL_REAUDIT_SOFTWARE=PASS'
