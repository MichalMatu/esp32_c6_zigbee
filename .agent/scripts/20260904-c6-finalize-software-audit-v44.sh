#!/usr/bin/env bash
set -euo pipefail
BRANCH=integration/c6-s3-i2c-20260903
BASE=5bde32fba74dce2e7e9fba07884209efee8fb026
CP=/Users/michal/agent-workspace/repos/esp32-c6-zigbee/checkpoints/20260904-c6-restore-refactor-v42/1788537556452185000-task-exit

git fetch origin "$BRANCH"
test "$(git rev-parse origin/$BRANCH)" = "$BASE"
git checkout -B "$BRANCH" "$BASE"
git reset --hard "$BASE"
git clean -fd
git apply "$CP/tracked.patch"
cp -R "$CP/untracked/." .

mkdir -p scripts
cat > scripts/run_host_tests.sh <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
CC=${CC:-cc}
COMMON=(-std=c11 -Wall -Wextra -Werror -pedantic -Imain)
run() { echo "[host] $1"; shift; "$@"; }
run source-hygiene python3 tests/host/test_no_embedded_nul.py
run discovery-claim-source python3 tests/host/test_discovery_claim_source.py
run zcl "$CC" "${COMMON[@]}" -DGATEWAY_ZCL_HOST_TEST tests/host/test_gateway_zcl_value.c main/gateway_zcl_value.c -lm -o /tmp/test_gateway_zcl_value
/tmp/test_gateway_zcl_value
run device-state "$CC" "${COMMON[@]}" tests/host/test_gateway_device_state.c main/gateway_device_state.c -o /tmp/test_gateway_device_state
/tmp/test_gateway_device_state
run inputs "$CC" "${COMMON[@]}" tests/host/test_gateway_inputs.c main/gateway_inputs.c -o /tmp/test_gateway_inputs
/tmp/test_gateway_inputs
run zigbee-input "$CC" "${COMMON[@]}" -DGATEWAY_ZCL_HOST_TEST tests/host/test_gateway_zigbee_input.c main/gateway_zigbee_input.c main/gateway_zcl_value.c main/gateway_reporting_policy.c main/gateway_inputs.c -lm -o /tmp/test_gateway_zigbee_input
/tmp/test_gateway_zigbee_input
run command-policy "$CC" "${COMMON[@]}" tests/host/test_gateway_command_policy.c main/gateway_command_policy.c -lm -o /tmp/test_gateway_command_policy
/tmp/test_gateway_command_policy
run link-protocol "$CC" "${COMMON[@]}" tests/host/test_gateway_link_protocol.c main/gateway_link_protocol.c main/gateway_link_frame.c -lm -o /tmp/test_gateway_link_protocol
/tmp/test_gateway_link_protocol
run link-event-adapter "$CC" "${COMMON[@]}" tests/host/test_gateway_link_event_adapter.c main/gateway_link_event_adapter.c main/gateway_link_protocol.c main/gateway_link_frame.c -lm -o /tmp/test_gateway_link_event_adapter
/tmp/test_gateway_link_event_adapter
run link-stream "$CC" "${COMMON[@]}" tests/host/test_gateway_link_stream.c main/gateway_link_stream.c main/gateway_link_protocol.c main/gateway_link_frame.c -lm -o /tmp/test_gateway_link_stream
/tmp/test_gateway_link_stream
run link-snapshot-cache "$CC" "${COMMON[@]}" tests/host/test_gateway_link_snapshot_cache.c main/gateway_link_snapshot_cache.c -o /tmp/test_gateway_link_snapshot_cache
/tmp/test_gateway_link_snapshot_cache
run link-control "$CC" "${COMMON[@]}" tests/host/test_gateway_link_control.c main/gateway_link_control.c main/gateway_link_protocol.c main/gateway_link_frame.c -lm -o /tmp/test_gateway_link_control
/tmp/test_gateway_link_control
run link-e2e "$CC" "${COMMON[@]}" tests/host/test_gateway_link_e2e.c main/gateway_link_protocol.c main/gateway_link_frame.c main/gateway_link_stream.c main/gateway_link_control.c main/gateway_link_snapshot_cache.c main/gateway_inputs.c -lm -o /tmp/test_gateway_link_e2e
/tmp/test_gateway_link_e2e
run reporting-policy "$CC" "${COMMON[@]}" tests/host/test_gateway_reporting_policy.c main/gateway_reporting_policy.c -o /tmp/test_gateway_reporting_policy
/tmp/test_gateway_reporting_policy
run i2c-mailbox "$CC" "${COMMON[@]}" tests/host/test_gateway_i2c_mailbox.c main/gateway_i2c_mailbox.c -o /tmp/test_gateway_i2c_mailbox
/tmp/test_gateway_i2c_mailbox
echo '[host] all tests passed'
EOF
chmod +x scripts/run_host_tests.sh

cat > tests/host/test_discovery_claim_source.py <<'EOF'
#!/usr/bin/env python3
from pathlib import Path
text = Path('main/zigbee_gateway_zcl.c').read_text()
if 'zigbee_gateway_schedule_active_discovery(slot)' not in text:
    raise SystemExit('ZCL callbacks no longer route discovery through the claim scheduler')
if 'DISCOVERY_ACTIVE_ENDPOINTS' in text:
    raise SystemExit('ZCL callback layer bypasses the private route-safe discovery scheduler')
print('discovery claim source invariant: PASS')
EOF
chmod +x tests/host/test_discovery_claim_source.py

cat > .github/workflows/quality.yml <<'EOF'
name: Quality

on:
  push:
    branches: [main]
  pull_request:

permissions:
  contents: read

jobs:
  host-tests:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Run strict host tests
        run: ./scripts/run_host_tests.sh

  firmware:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: espressif/esp-idf-ci-action@v1
        with:
          esp_idf_version: v5.5.4
          target: esp32c6
          command: idf.py build
EOF

cat > docs/AUDIT_2026-09-04.md <<'EOF'
# Pre-S3 repository audit — 2026-09-04

## Goal

Reduce structural risk before physical ESP32-C6 ↔ ESP32-S3 integration without reopening the verified Zigbee baseline or changing the GatewayLink v2 contract.

## Execution plan

1. Freeze the exact integration-branch starting point and verify the Local Agent binding.
2. Run strict host tests and UART/I2C firmware builds before edits.
3. Split genuine god-object responsibilities while preserving public APIs and bounded callback behavior.
4. Fix evidence-backed defects and add regression coverage.
5. Make host verification canonical and remove duplicated CI compile recipes.
6. Reconcile README/architecture/continuation docs with active GatewayLink v2 and the C6-master I2C milestone.
7. Run static/source hygiene, all host tests, UART build, I2C build and emulator build.
8. Flash both physical ESP32-C6 boards, preserve coordinator Zigbee storage unless a destructive test is explicitly required, and run bounded hardware regressions.
9. Record physical evidence and only then mark the refactor ready for S3 integration.

## Findings and changes

- `zigbee_gateway.c` had accumulated stack lifecycle, lifecycle signals, discovery scheduling, async ZDO work, ZCL/IAS handling, reporting configuration and command execution in one file. It is split into a small public/lifecycle shell plus cohesive internal work/discovery, ZCL/IAS and command modules.
- GatewayLink framing/COBS/CRC was mixed with payload codecs. Frame mechanics now live in `gateway_link_frame.c`; public protocol declarations and wire compatibility remain unchanged.
- Poll Control Check-In could bypass the per-route discovery claim by directly queueing Active Endpoint discovery. Check-In now goes through the same claim path as announce/report recovery, preventing overlapping discovery for one current route. Device-state tests cover repeated claims and route replacement.
- Active documentation incorrectly pointed new work at GatewayLink v1 even though the branch implements protocol v2 with no v1 compatibility shim. Active docs now point to v2; v1 remains historical recovery documentation only.
- CI repeated every host-test compiler command inline. `scripts/run_host_tests.sh` is now the canonical strict host gate used locally and by CI.

## Deliberately unchanged

- The emulator remains a single test-program translation unit for now. It is large, but its responsibilities are tightly coupled to synthetic Zigbee endpoint/profile behavior and production risk is lower than introducing another abstraction during this pre-hardware cleanup. Revisit only if profile growth makes independent units materially testable.
- The `main/` component remains one ESP-IDF component. Moving every source into directory trees would create include/CMake churn without reducing runtime coupling; module ownership is expressed by file boundaries and architecture docs instead.
- UART remains the verified fallback. The I2C backend is not promoted over UART until physical C6↔S3 validation succeeds.
EOF

python3 - <<'PY'
from pathlib import Path
p=Path('docs/CONTINUATION.md')
s=p.read_text()
s=s.replace('- `docs/GATEWAY_LINK_V1.md` for the GatewayLink protocol contract;', '- `docs/GATEWAY_LINK_V2.md` for the active GatewayLink protocol contract; `docs/GATEWAY_LINK_V1.md` is historical recovery documentation only;', 1)
s=s.replace('then `AGENTS.md`, `docs/VERIFIED_BASELINE.md`, `docs/ARCHITECTURE.md`, and `docs/GATEWAY_LINK_V1.md`.', 'then `AGENTS.md`, `docs/VERIFIED_BASELINE.md`, `docs/ARCHITECTURE.md`, and `docs/GATEWAY_LINK_V2.md`.', 1)
s=s.replace('A later cleanup may split logical packet encoding from UART-specific COBS stream framing more strictly, but do not perform that refactor merely as a prerequisite for the first physical I2C validation unless evidence shows it is necessary.', 'GatewayLink frame mechanics (COBS/CRC/frame envelope) are now split from payload codecs in `gateway_link_frame.c`; the public v2 wire contract is unchanged. UART and I2C continue to carry the same complete encoded frame.')
marker='## Current work context\n'
insert='''## 2026-09-04 pre-S3 structural audit

A behavior-preserving structural audit is being closed before S3 integration. The oversized Zigbee SDK boundary was split into lifecycle/public API, work/discovery, ZCL/IAS, and command modules; GatewayLink frame mechanics were split from payload codecs. Poll Control Check-In now uses the same per-route discovery claim as other discovery triggers, preventing duplicate Active Endpoint discovery for one current route. `scripts/run_host_tests.sh` is the canonical strict host-test gate. See `docs/AUDIT_2026-09-04.md` for scope and evidence.

Active development uses GatewayLink v2. v1 is retained only as a frozen recovery contract and must not be implemented by a new S3 peer.

'''
if marker not in s:
    raise SystemExit('CONTINUATION marker missing')
s=s.replace(marker, insert+marker, 1)
p.write_text(s)

p=Path('docs/ARCHITECTURE.md')
s=p.read_text()
s=s.replace('- `gateway_link_protocol.c/.h` defines the hardware-independent GatewayLink v1 framing and payload codec for the future C6-to-S3 link. It uses COBS framing, CRC32, explicit little-endian wire values and fixed-size buffers; it never serializes raw C structs.', '- `gateway_link_frame.c` owns GatewayLink v2 frame-envelope mechanics: COBS encode/decode and CRC32 over fixed-size buffers. `gateway_link_protocol.c/.h` owns typed v2 payload codecs and the public protocol API. Neither serializes raw C structs; both are physical-backend independent.')
s=s.replace('- `zigbee_gateway.c/.h` is the SDK integration/orchestration boundary: stack lifecycle, app/ZCL signal dispatch, discovery jobs, bounded async callback contexts, binding/reporting submission, and permit-join control.', '- `zigbee_gateway.c/.h` is the small public/lifecycle shell for stack start, app signals, permit-join and normalized public submissions. `zigbee_gateway_work.c` owns the bounded discovery/work queue, async ZDO contexts, binding/reporting submission and route-safe scheduling. `zigbee_gateway_zcl.c` owns ZCL report/read/config/Poll-Control/IAS callbacks and normalization. `zigbee_gateway_commands.c` owns bounded outbound command confirmation contexts and ZCL command translation. `zigbee_gateway_internal.h` is private glue only; no application code may depend on it.')
s=s.replace('`gateway_inputs`, `gateway_zcl_value`, `gateway_reporting_policy`, and `gateway_device_state` have strict C11 host tests compiled with `-Wall -Wextra -Werror -pedantic`. GitHub CI also builds the complete firmware with the pinned ESP-IDF/ESP Zigbee versions.', '`scripts/run_host_tests.sh` is the canonical strict C11 host-test gate and compiles all host-tested modules with `-Wall -Wextra -Werror -pedantic`. GitHub CI invokes that runner and builds the complete firmware with the pinned ESP-IDF/ESP Zigbee versions. Local release gates additionally build both UART-default and I2C GatewayLink configurations.')
p.write_text(s)

p=Path('README.md')
s=p.read_text()
needle='The firmware keeps ESP Zigbee SDK integration separate from a protocol-neutral input contract, normalized events, transport, value decoding, reporting policy, device state, and console handling.'
repl=needle+' The SDK-facing gateway itself is split into lifecycle/public API, bounded discovery/work scheduling, ZCL/IAS handling, and outbound command execution so no single translation unit owns the whole coordinator.'
if needle not in s:
    raise SystemExit('README architecture sentence missing')
s=s.replace(needle,repl,1)
p.write_text(s)
PY

echo '=== SOURCE METRICS ==='
wc -l main/zigbee_gateway*.c main/gateway_link_protocol.c main/gateway_link_frame.c tests/zigbee_device_emulator/main/emulator_main.c

echo '=== STRICT HOST GATE ==='
./scripts/run_host_tests.sh

echo '=== STATIC CHECK ==='
if command -v cppcheck >/dev/null 2>&1; then
  cppcheck --std=c11 --enable=warning,performance,portability --error-exitcode=1 --suppress=missingIncludeSystem --suppress=unusedFunction \
    main/gateway_inputs.c main/gateway_device_state.c main/gateway_reporting_policy.c main/gateway_command_policy.c \
    main/gateway_link_frame.c main/gateway_link_protocol.c main/gateway_link_stream.c main/gateway_link_snapshot_cache.c
else
  echo 'cppcheck unavailable; skipped'
fi

git diff --check
. ~/esp/esp-idf-v5.5.4/export.sh >/dev/null

echo '=== UART BUILD ==='
rm -rf build sdkconfig sdkconfig.old
idf.py set-target esp32c6 >/dev/null
idf.py build

echo '=== I2C BUILD ==='
python3 - <<'PY'
from pathlib import Path
p=Path('sdkconfig')
s=p.read_text()
old='CONFIG_GATEWAY_LINK_BACKEND_UART=y'
new='# CONFIG_GATEWAY_LINK_BACKEND_UART is not set\nCONFIG_GATEWAY_LINK_BACKEND_I2C=y'
if old not in s:
    raise SystemExit('UART backend config missing')
p.write_text(s.replace(old,new,1))
PY
idf.py reconfigure >/dev/null
idf.py build

echo '=== EMULATOR BUILD ==='
cd tests/zigbee_device_emulator
rm -rf build sdkconfig sdkconfig.old
idf.py set-target esp32c6 >/dev/null
idf.py build
cd ../..
git checkout -- tests/zigbee_device_emulator/dependencies.lock 2>/dev/null || true

./scripts/run_host_tests.sh
git diff --check

echo '=== COMMIT ==='
git status --short
git add main tests/host scripts .github/workflows/quality.yml README.md docs/ARCHITECTURE.md docs/CONTINUATION.md docs/AUDIT_2026-09-04.md
git diff --cached --check
git commit -m 'Refactor Zigbee gateway before S3 integration'
git push origin HEAD:"$BRANCH"
echo SOFTWARE_AUDIT_HEAD=$(git rev-parse HEAD)
