# Verified stable baseline

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

## GatewayLink baseline — 2026-09-03

This baseline freezes the verified ESP32-C6 GatewayLink refactor before physical C6↔S3 I²C integration work.

### Frozen firmware

- firmware commit: `a4b1f629c1286d631ac208515b71aeeaa7c44b23`
- annotated tag: `c6-gatewaylink-stable-2026-09-03`
- source branch used for verification: `night/link-backend-refactor-20260903`
- ESP-IDF: `v5.5.4`
- target: ESP32-C6
- default physical GatewayLink backend: UART1
- selectable alternative backend present: I²C master mailbox

The tag points to the firmware commit itself. This documentation is committed after the tag, so the recovery point cannot move when documentation changes. The earlier `c6-sonoff-stable-2026-09-02` tag remains untouched.

### Verified scope

The frozen firmware includes:

- GatewayLink transport facade;
- runtime ownership extracted from the UART backend;
- link, queue, heap and stack observability plus `link status`;
- selectable UART and I²C physical backends;
- host coverage for the I²C mailbox, protocol, stream framing and virtual-S3 E2E path;
- successful complete firmware builds for both default UART and alternate I²C configurations.

The I²C backend is software-verified on the C6 side only. Physical C6↔S3 I²C communication has not yet been validated and is intentionally outside this frozen baseline. UART remains the known-working physical link.

### Physical UART verification

Physical C6 smoke validation task: `20260903-c6-uart-smoke-validate-v2`.

The ESP32-C6 remained connected to the paired SONOFF SNZB-02D and local SCD4x. The S3 was absent, so `peer=0` and `rx=0` are expected. UART transmission, local sensing, Zigbee sensing and GatewayLink telemetry were all observed healthy.

### 5 h 20 min soak

Capture task: `20260903-c6-uart-soak-v1`. The four 80-minute capture chunks all completed successfully. The original task exhausted its total task budget before the final analysis command could start; this was a harness-budget outcome, not a firmware failure.

Final read-only validation task: `20260903-c6-uart-soak-validate-v2`.

Observed full-soak summary:

```text
C6 UART SOAK PASS
local_measurements=12125
sonoff_measurements=132
status_samples=68
first_link_status peer=0 tx=643 rx=0 invalid=0 queue=0/16 high=2 drop=0 short=0
last_link_status  peer=0 tx=12901 rx=0 invalid=0 queue=0/16 high=2 drop=0 short=0
first_resources min_heap=329212 tx_stack_hwm=2796 rx_stack_hwm=3128
last_resources  min_heap=329212 tx_stack_hwm=2796 rx_stack_hwm=3128
min_heap=329212
tx_stack_hwm_min=2796
rx_stack_hwm_min=3128
```

The final gate also found no Guru Meditation, abort, panic, watchdog, Zigbee leave-reset, Zigbee security failure, GatewayLink queue drops or gateway-event queue drops. Heap and task stack high-water values were unchanged between the first and last resource samples.

### Next integration branch

Further C6-side work for physical S3 integration starts from:

`integration/c6-s3-i2c-20260903`

That branch is created after this verification note is committed, while the immutable firmware recovery tag remains on `a4b1f629c1286d631ac208515b71aeeaa7c44b23`.

The next bench milestone is physical C6↔S3 I²C validation, including coexistence with the existing local SCD4x on the shared C6 I²C bus and recovery when the S3 peer is absent or unpowered. S3-side implementation belongs in its own correctly bound repository context.

### Recovery

To restore the verified C6 firmware before S3 integration:

```sh
git fetch --tags
git checkout c6-gatewaylink-stable-2026-09-03
```

Do not move or recreate this tag onto a later commit.

## Dual-C6 Zigbee laboratory development checkpoint — 2026-09-04

This is a hardware-verified development checkpoint, not a frozen stable tag.

Physical task `20260904-c6-ias-fresh-network-e2e-v3` used emulator firmware `1821f6d95c0a1c1481031ecf42e35e586006146d` and proved fresh coordinator formation, 180 s permit-join, emulator factory-new steering success, authorization, announce, endpoint 1 discovery, IAS cluster `0x0500`, Basic metadata, and no emulator abort/panic.

It did not complete IAS Contact semantic E2E: the coordinator logged `IAS ZoneType read failed`, no normalized `CONTACT_OPEN` was observed, and emulator ZoneStatus report requests returned `error=5`.

Therefore no stable Zigbee-lab tag should be created yet. The next gate is successful remote ZoneType `0x0015` read, observed Contact Open false/true normalization, then restart/rejoin resilience.

Verified boards:
- coordinator serial `40:4C:CA:5D:0A:00`;
- emulator serial `40:4C:CA:5D:01:D8`.
