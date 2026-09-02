#!/usr/bin/env bash
set -euo pipefail

EXPECTED=0d64fb03164d3bcb9f5cddd639977b4027bc581f
TAG=c6-sonoff-stable-2026-09-02

git fetch --quiet origin main --tags
test "$(git rev-parse HEAD)" = "$EXPECTED"
test "$(git rev-parse origin/main)" = "$EXPECTED"
test -z "$(git status --porcelain --untracked-files=normal)"

echo '=== ARCHITECTURE INVARIANTS ==='
test "$(grep -R -l 'gateway_event_receive(' main --include='*.c' | wc -l | tr -d ' ')" = "1"
grep -q 'gateway_transport.c' <(grep -R -l 'gateway_event_receive(' main --include='*.c')
! grep -R -n -E '\b(malloc|calloc|realloc|free)\s*\(' main --include='*.c' --include='*.h'
! grep -R -n -E '\bxQueueCreate\s*\(' main --include='*.c' --include='*.h'
grep -q 'EZB_ZDO_SIGNAL_DEVICE_AUTHORIZED' main/zigbee_gateway.c
grep -q 'EZB_ZDO_UPDDEV_UNSECURE_JOIN' main/zigbee_gateway.c
grep -q 'gateway_device_claim_discovery' main/zigbee_gateway.c

echo '=== HOST TESTS ==='
cc -std=c11 -Wall -Wextra -Werror -pedantic -DGATEWAY_ZCL_HOST_TEST -Imain tests/host/test_gateway_zcl_value.c main/gateway_zcl_value.c -lm -o /tmp/test_gateway_zcl_value
/tmp/test_gateway_zcl_value
cc -std=c11 -Wall -Wextra -Werror -pedantic -Imain tests/host/test_gateway_device_state.c main/gateway_device_state.c -o /tmp/test_gateway_device_state
/tmp/test_gateway_device_state
cc -std=c11 -Wall -Wextra -Werror -pedantic -Imain tests/host/test_gateway_inputs.c main/gateway_inputs.c -o /tmp/test_gateway_inputs
/tmp/test_gateway_inputs
cc -std=c11 -Wall -Wextra -Werror -pedantic -DGATEWAY_ZCL_HOST_TEST -Imain tests/host/test_gateway_zigbee_input.c main/gateway_zigbee_input.c main/gateway_zcl_value.c main/gateway_inputs.c -lm -o /tmp/test_gateway_zigbee_input
/tmp/test_gateway_zigbee_input
cc -std=c11 -Wall -Wextra -Werror -pedantic -Imain tests/host/test_gateway_link_protocol.c main/gateway_link_protocol.c -lm -o /tmp/test_gateway_link_protocol
/tmp/test_gateway_link_protocol
cc -std=c11 -Wall -Wextra -Werror -pedantic -Imain tests/host/test_gateway_link_event_adapter.c main/gateway_link_event_adapter.c main/gateway_link_protocol.c -lm -o /tmp/test_gateway_link_event_adapter
/tmp/test_gateway_link_event_adapter
cc -std=c11 -Wall -Wextra -Werror -pedantic -Imain tests/host/test_gateway_link_stream.c main/gateway_link_stream.c main/gateway_link_protocol.c -lm -o /tmp/test_gateway_link_stream
/tmp/test_gateway_link_stream
cc -std=c11 -Wall -Wextra -Werror -pedantic -Imain tests/host/test_gateway_link_snapshot_cache.c main/gateway_link_snapshot_cache.c -o /tmp/test_gateway_link_snapshot_cache
/tmp/test_gateway_link_snapshot_cache
cc -std=c11 -Wall -Wextra -Werror -pedantic -Imain tests/host/test_gateway_link_control.c main/gateway_link_control.c main/gateway_link_protocol.c -lm -o /tmp/test_gateway_link_control
/tmp/test_gateway_link_control
cc -std=c11 -Wall -Wextra -Werror -pedantic -Imain tests/host/test_gateway_link_e2e.c main/gateway_link_protocol.c main/gateway_link_stream.c main/gateway_link_control.c main/gateway_link_snapshot_cache.c main/gateway_inputs.c -lm -o /tmp/test_gateway_link_e2e
/tmp/test_gateway_link_e2e
cc -std=c11 -Wall -Wextra -Werror -pedantic -Imain tests/host/test_gateway_reporting_policy.c main/gateway_reporting_policy.c -o /tmp/test_gateway_reporting_policy
/tmp/test_gateway_reporting_policy

echo '=== FULL ESP-IDF BUILD ==='
. ~/esp/esp-idf-v5.5.4/export.sh >/dev/null
grep -q '^CONFIG_IDF_TARGET="esp32c6"' sdkconfig
idf.py build
idf.py size
git diff --check
test "$(git rev-parse HEAD)" = "$EXPECTED"
test -z "$(git status --porcelain --untracked-files=normal)"
echo "BASELINE_REAUDIT=PASS SHA=$EXPECTED"

echo '=== FREEZE CODE TAG ==='
git fetch --quiet origin main --tags
if git rev-parse -q --verify "refs/tags/$TAG" >/dev/null; then
    test "$(git rev-list -n1 "$TAG")" = "$EXPECTED"
else
    git tag -a "$TAG" "$EXPECTED" -m "Verified ESP32-C6 Sonoff SNZB-02D stable baseline"
    git push origin "refs/tags/$TAG"
fi
test "$(git rev-list -n1 "$TAG")" = "$EXPECTED"
echo "FROZEN_TAG=$TAG SHA=$(git rev-list -n1 "$TAG")"

echo '=== DOCUMENT BASELINE ==='
python3 - <<'PY'
from pathlib import Path

baseline = '''# Verified stable baseline

This file records the hardware-verified ESP32-C6 Zigbee baseline frozen on 2026-09-02.

## Frozen firmware

- firmware commit: `0d64fb03164d3bcb9f5cddd639977b4027bc581f`
- annotated tag: `c6-sonoff-stable-2026-09-02`
- ESP-IDF: `v5.5.4`
- `espressif/esp-zigbee-lib`: `v2.0.4`
- target: ESP32-C6
- tested sleepy device: SONOFF SNZB-02D, IEEE `881a14fffeef6bd9`

The tag points to the firmware commit itself. Documentation describing the verification is committed after the tagged code so the recovery point cannot move when documentation changes.

## Failure that was fixed

Before the frozen baseline, the gateway treated `EZB_ZDO_UPDDEV_UNSECURE_JOIN` (`DEVICE_UPDATE status=0x01`) as if the device were already ready for normal discovery. One physical join could therefore trigger multiple overlapping Active Endpoint / descriptor / bind / Configure Reporting sequences while the Trust Center authorization flow was still in progress.

On the SNZB-02D this correlated with repeated `LEAVE_RESET`, changing short addresses and later `APS ... security failed 0x13` messages.

The fix in `0d64fb03164d3bcb9f5cddd639977b4027bc581f` keeps unsecure-join updates as lifecycle evidence instead of a discovery trigger, logs `DEVICE_AUTHORIZED`, and claims Active Endpoint discovery per current short address so duplicate lifecycle signals do not start duplicate discovery for the same route. Normal announce/rejoin remains the commissioning trigger.

This preserves the existing architecture: IEEE identity remains authoritative, short addresses remain mutable routes, `gateway_transport` remains the sole event consumer, and the ESP32-C6 remains a bounded protocol-neutral input gateway rather than an application registry.

## Hardware evidence

### Controlled pairing test

Task: `20260902-c6-sonoff-lifecycle-fix-hw-v1`

The exact frozen SHA was built and flashed to the ESP32-C6 without erasing Zigbee storage. The C6 USB identity was validated before access and the S3 serial device was excluded.

Observed summary:

```text
update=2
authorized=1
announce=1
rejoin=0
leave_reset=0
descriptor=1
binding=4
reporting=5
temp=1
humidity=2
battery=0
security_failed=0
```

Authorization completed with `status=0x00`. Temperature and humidity reports were received after commissioning.

### Read-only stability soak

Task: `20260902-c6-sonoff-postfix-soak-v1`

A 20-minute serial-only soak followed the successful pairing. It performed no flash, no permit-join operation and no re-pairing.

Observed summary:

```text
update=0
authorized=0
announce=0
rejoin=0
leave_reset=0
unavailable=0
temp=5
humidity=4
battery=0
security_failed=0
queue_drop=0
panic=0
watchdog=0
```

The absence of battery reports in this 20-minute window is expected because the configured battery reporting interval is measured in hours, not minutes.

## Verification gate

Before creating the stable tag, the frozen firmware is re-audited with:

- architecture invariant checks;
- all strict host tests used by the repository Quality workflow, including GatewayLink virtual-S3 E2E;
- a complete ESP-IDF v5.5.4 build for ESP32-C6;
- firmware size reporting;
- clean-tree and exact-SHA checks.

Hardware behavior remains proven by the two tasks above; the freeze task does not re-pair or mutate the Zigbee network.

## Recovery

To inspect or restore the known-good firmware code:

```sh
git fetch --tags
git checkout c6-sonoff-stable-2026-09-02
```

Do not move or recreate this tag onto a later commit. Future firmware changes should use a new commit and, after independent verification, a new stable tag.
'''
Path('docs/VERIFIED_BASELINE.md').write_text(baseline)

readme = Path('README.md')
text = readme.read_text()
anchor = '## Architecture\n'
block = '''## Verified stable baseline

The hardware-verified SONOFF SNZB-02D baseline is frozen at tag `c6-sonoff-stable-2026-09-02`, pointing to firmware commit `0d64fb03164d3bcb9f5cddd639977b4027bc581f`. The controlled pairing test completed with successful authorization, no `LEAVE_RESET` and no APS security failures; a following 20-minute read-only soak received repeated temperature/humidity reports with zero rejoin, leave, queue-drop, panic or watchdog events. See [docs/VERIFIED_BASELINE.md](docs/VERIFIED_BASELINE.md) for the exact root cause, fix and evidence.

'''
if block not in text:
    if anchor not in text:
        raise SystemExit('README architecture anchor missing')
    text = text.replace(anchor, block + anchor, 1)
readme.write_text(text)

arch = Path('docs/ARCHITECTURE.md')
text = arch.read_text()
anchor = '## Invariants\n'
block = '''## Zigbee join lifecycle boundary

`DEVICE_UPDATE` is lifecycle/security evidence, not a blanket discovery trigger. In particular, `EZB_ZDO_UPDDEV_UNSECURE_JOIN` occurs before Trust Center authorization has completed and must not start Active Endpoint discovery. Normal device announce and supported rejoin paths start discovery. Active Endpoint discovery is claimed per current short address so duplicate lifecycle signals cannot launch overlapping commissioning for the same route; a route change or leave resets that claim according to the bounded device-state lifecycle.

`DEVICE_AUTHORIZED` is logged separately so hardware tests can distinguish successful authorization from early unsecure-join updates. This rule prevents discovery/bind/reporting traffic from racing the security handshake on sleepy end devices while retaining IEEE-first identity and generation-safe route replacement.

'''
if block not in text:
    if anchor not in text:
        raise SystemExit('ARCHITECTURE invariants anchor missing')
    text = text.replace(anchor, block + anchor, 1)
arch.write_text(text)
PY

git diff --check
git add README.md docs/ARCHITECTURE.md docs/VERIFIED_BASELINE.md
git diff --cached --check
git commit -m "Document verified C6 Zigbee baseline"
git push origin HEAD:main

echo '=== FINAL FREEZE CHECK ==='
git fetch --quiet origin main --tags
test "$(git rev-parse HEAD)" = "$(git rev-parse origin/main)"
test "$(git rev-list -n1 "$TAG")" = "$EXPECTED"
test -f docs/VERIFIED_BASELINE.md
grep -q "$EXPECTED" docs/VERIFIED_BASELINE.md
grep -q "$TAG" docs/VERIFIED_BASELINE.md
grep -q '20-minute' docs/VERIFIED_BASELINE.md
grep -q 'leave_reset=0' docs/VERIFIED_BASELINE.md
grep -q 'security_failed=0' docs/VERIFIED_BASELINE.md
git diff --check
test -z "$(git status --porcelain --untracked-files=normal)"
echo "FREEZE_FINAL=PASS TAG=$TAG CODE_SHA=$EXPECTED DOCS_SHA=$(git rev-parse HEAD)"
