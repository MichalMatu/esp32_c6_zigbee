#!/usr/bin/env bash
set -euo pipefail
BRANCH='integration/c6-s3-i2c-20260903'
EXPECTED='f13b293be2de6b1601d179568424e0046d6219a7'

git fetch origin "$BRANCH"
test "$(git rev-parse origin/$BRANCH)" = "$EXPECTED"
git checkout -B "$BRANCH" "$EXPECTED"
git reset --hard "$EXPECTED"

python3 - <<'PY'
from pathlib import Path

# CONTINUATION
p = Path('docs/CONTINUATION.md')
s = p.read_text()
s = s.replace(
    'A behavior-preserving structural audit is being closed before S3 integration. The oversized Zigbee SDK boundary was split into lifecycle/public API, work/discovery, ZCL/IAS, and command modules; GatewayLink frame mechanics were split from payload codecs. Poll Control Check-In now uses the same per-route discovery claim as other discovery triggers, preventing duplicate Active Endpoint discovery for one current route. `scripts/run_host_tests.sh` is the canonical strict host-test gate. See `docs/AUDIT_2026-09-04.md` for scope and evidence.',
    'A behavior-preserving structural audit was completed before S3 integration. The oversized Zigbee SDK boundary was split into lifecycle/public API, work/discovery, ZCL/IAS, and command modules; GatewayLink frame mechanics were split from payload codecs. Poll Control Check-In now uses the same per-route discovery claim as other discovery triggers, preventing duplicate Active Endpoint discovery for one current route. `scripts/run_host_tests.sh` is the canonical strict host-test gate. See `docs/AUDIT_2026-09-04.md` for scope and evidence.'
)
marker = 'Active development uses GatewayLink v2. v1 is retained only as a frozen recovery contract and must not be implemented by a new S3 peer.\n'
addition = '''\nPost-refactor physical regression task `20260904-c6-post-refactor-hardware-v47` validated exact source `f13b293be2de6b1601d179568424e0046d6219a7` on both connected ESP32-C6 boards. It preserved Zigbee storage, passed IAS Contact restart/rejoin on UART, selected the I2C backend with the S3 intentionally absent while SCD41 and Zigbee remained healthy, and then restored UART with a final smoke pass. No panic, gateway-event drop, GatewayLink queue drop, or SCD4x-unavailable transition was observed.\n\nThis closes the C6 structural-audit/regression phase. The remaining physical integration gap is specifically communication with a real S3 I2C slave/mailbox peer; that still cannot be closed from this C6-bound repository alone.\n'''
if '20260904-c6-post-refactor-hardware-v47' not in s:
    s = s.replace(marker, marker + addition, 1)
s = s.replace(
    'The I2C backend is host-tested and firmware-build-tested. It has **not** yet been physically validated against an S3 slave.',
    'The I2C backend is host-tested, firmware-build-tested, and physically regression-tested on the C6 with the S3 intentionally absent while sharing I2C0 with the SCD41. It has **not** yet been physically validated against an actual S3 slave.'
)
p.write_text(s)

# VERIFIED_BASELINE
p = Path('docs/VERIFIED_BASELINE.md')
s = p.read_text()
section = '''\n\n## Post-refactor dual-C6 hardware regression — 2026-09-04\n\nTask: `20260904-c6-post-refactor-hardware-v47`.\n\nExact tested source:\n\n- `f13b293be2de6b1601d179568424e0046d6219a7` — `Refactor Zigbee gateway before S3 integration`.\n\nThe task re-resolved both boards from their immutable USB serial identities and did not erase coordinator or emulator Zigbee storage. It first flashed/ran the UART-default coordinator and IAS Contact emulator, then rebuilt/flashed the coordinator with the I2C GatewayLink backend while the S3 peer remained intentionally absent, and finally restored the UART-default firmware.\n\nUART preserved-storage regression:\n\n```text\nUART_EMULATOR_PRESERVED_STORAGE=True\nUART_GATEWAY_REJOIN=True\nUART_GATEWAY_ANNOUNCE=True\nUART_IAS_CIE_WRITE=True\nUART_IAS_ENROLL=True\nUART_CONTACT_FALSE=True\nUART_CONTACT_TRUE=True\nUART_GW_NO_PANIC=True\nUART_EMU_NO_PANIC=True\nUART_NO_GATEWAY_EVENT_DROP=True\nUART_POST_REFACTOR_DUAL_C6=PASS\n```\n\nI2C missing-S3/shared-bus regression:\n\n```text\nI2C_BACKEND_SELECTED=True\nI2C_PEER_ABSENT_EXPECTED=True\nI2C_SCD4X_AVAILABLE=True\nI2C_SCD4X_CO2=True\nI2C_SCD4X_TEMPERATURE=True\nI2C_SCD4X_HUMIDITY=True\nI2C_ZIGBEE_REJOIN=True\nI2C_CONTACT_FALSE=True\nI2C_CONTACT_TRUE=True\nI2C_GW_NO_PANIC=True\nI2C_EMU_NO_PANIC=True\nI2C_NO_EVENT_DROP=True\nI2C_NO_LINK_QUEUE_DROP=True\nI2C_SCD4X_NOT_MARKED_UNAVAILABLE=True\nI2C_MISSING_S3_SHARED_BUS=PASS\n```\n\nDuring the I2C phase `peer=0` was expected. Missing-peer write failures increased the short-write counter, but bounded backoff/logging prevented queue loss or disruption of the shared SCD41 bus. Local CO2/temperature/humidity measurements and IAS Contact false/true measurements continued throughout the phase.\n\nFinal restoration check:\n\n```text\nUART_RESTORED_FINAL_SMOKE=PASS\nHARDWARE_GATE_HEAD=f13b293be2de6b1601d179568424e0046d6219a7\nPOST_REFACTOR_HARDWARE_GATE=PASS\n```\n\nThis hardware gate closes the C6-side structural-refactor regression. Physical GatewayLink I2C communication with a real S3 slave remains a separate future integration gate.\n'''
if '## Post-refactor dual-C6 hardware regression — 2026-09-04' not in s:
    s += section
p.write_text(s)

# AUDIT
p = Path('docs/AUDIT_2026-09-04.md')
s = p.read_text()
section = '''\n\n## Completion status\n\nThe audit is complete on source commit `f13b293be2de6b1601d179568424e0046d6219a7`.\n\nSoftware gates passed before hardware access:\n\n- canonical strict host tests;\n- UART-default ESP-IDF firmware build;\n- I2C-backend ESP-IDF firmware build;\n- Zigbee device emulator firmware build;\n- diff/source hygiene checks.\n\nPhysical gate `20260904-c6-post-refactor-hardware-v47` then passed on the two bound ESP32-C6 boards without erasing persisted Zigbee storage. It verified preserved-storage IAS rejoin/enrollment/contact reporting on UART, I2C-backend missing-peer behavior with the S3 absent while the local SCD41 shared the same bus, continued Zigbee IAS Contact traffic, no event/link queue drops, no panic on either C6, and successful final restoration to UART.\n\nResult: `POST_REFACTOR_HARDWARE_GATE=PASS`.\n\nNo additional C6 structural work is justified before the S3 slave/mailbox side exists. The next real integration gate remains physical C6↔S3 GatewayLink I2C communication; UART stays the verified fallback until that gate passes.\n'''
if '## Completion status' not in s:
    s += section
p.write_text(s)
PY

git diff --check
./scripts/run_host_tests.sh
. ~/esp/esp-idf-v5.5.4/export.sh >/dev/null
rm -rf build
idf.py set-target esp32c6 >/dev/null
idf.py build >/dev/null
cp sdkconfig /tmp/c6_v48_uart_sdkconfig
python3 - <<'PY'
from pathlib import Path
p=Path('sdkconfig')
s=p.read_text()
old='CONFIG_GATEWAY_LINK_BACKEND_UART=y'
new='# CONFIG_GATEWAY_LINK_BACKEND_UART is not set\nCONFIG_GATEWAY_LINK_BACKEND_I2C=y'
if s.count(old) != 1:
    raise SystemExit(f'UART backend selection count={s.count(old)}')
p.write_text(s.replace(old,new,1))
PY
idf.py reconfigure >/dev/null
idf.py build >/dev/null
cp /tmp/c6_v48_uart_sdkconfig sdkconfig
idf.py reconfigure >/dev/null

git add docs/CONTINUATION.md docs/VERIFIED_BASELINE.md docs/AUDIT_2026-09-04.md
git diff --cached --check
git commit -m 'Record post-refactor C6 hardware validation'
HEAD=$(git rev-parse HEAD)
git push origin HEAD:refs/heads/$BRANCH
echo AUDIT_CLOSURE_HEAD=$HEAD
git status --short
