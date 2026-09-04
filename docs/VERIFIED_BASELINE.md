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

### Historical integration branch

Physical-S3 preparation originally continued on `integration/c6-s3-i2c-20260903` after this baseline was recorded. That branch was later fast-forwarded into `main` and deleted during the final repository cleanup. Its history remains reachable from `main` and the immutable recovery tags.

The remaining physical milestone is C6↔S3 I²C validation with a real S3 slave/mailbox peer. That work belongs after the C6 module is imported into the LiteGraph monorepo; UART remains the verified fallback until the cross-MCU gate passes.

### Recovery

To restore the verified C6 firmware before S3 integration:

```sh
git fetch --tags
git checkout c6-gatewaylink-stable-2026-09-03
```

Do not move or recreate this tag onto a later commit.

## Dual-C6 Zigbee laboratory verified baseline — 2026-09-04

This section freezes the verified dual-C6 IAS Contact laboratory baseline on the active integration branch. It is a hardware-verified source checkpoint, not a new stable tag.

Verified source commit before this documentation commit:

- `109a01f32d3bbc5c2ce2799ccbc8946a717b0e7a` — refresh IAS CIE enrollment state after a device rejoin while preserving IEEE-first identity and the existing bounded discovery architecture.

The IAS Contact emulator also includes the previously verified fixes that expose ZoneType as ENUM16, complete IAS enrollment after the coordinator writes its CIE address, preserve RestoreNotify while toggling Alarm1, and run the roundtrip worker for the IAS profile.

### Fresh-network hardware gate

Task: `20260904-c6-ias-rejoin-cie-refresh-v24`.

The test rebuilt and flashed the exact source, erased only the `zb_storage` regions intentionally required for a factory-new Zigbee test, re-resolved both USB ports by immutable serial identity, formed a fresh coordinator network, opened permit-join, then started the emulator.

Observed evidence:

```text
FRESH_REMOTE_ZONETYPE=True
FRESH_CIE_QUEUED=True
FRESH_CIE_ACTION=True
FRESH_ENROLL_REQ=True
FRESH_ENROLL_RSP=True
FRESH_FALSE=True
FRESH_TRUE=True
FRESH_NO_ERROR5=True
FRESH_GW_NO_PANIC=True
FRESH_EMU_NO_PANIC=True
FRESH_NETWORK_DUAL_C6_IAS_E2E=PASS
```

This proves remote IAS ZoneType `0x0015`, standard CIE/enrollment handshake, normalized `CONTACT_OPEN=false` and `CONTACT_OPEN=true`, no generic IAS ZoneStatus `error=5`, and no gateway/emulator panic.

### Preserved-storage restart/rejoin gate

The second phase of the same task restarted the emulator without erasing Zigbee storage and without reflashing either board. Ports were re-resolved from USB serial identity before hardware access.

Observed evidence:

```text
PRESERVED_NON_FACTORY_NEW=True
PRESERVED_REJOIN=True
PRESERVED_ANNOUNCE=True
PRESERVED_CIE_QUEUED=True
PRESERVED_CIE_ACTION=True
PRESERVED_ENROLL_REQ=True
PRESERVED_ENROLL_RSP=True
PRESERVED_FALSE=True
PRESERVED_TRUE=True
PRESERVED_GW_NO_PANIC=True
PRESERVED_EMU_NO_PANIC=True
PRESERVED_STORAGE_RESTART_REJOIN=PASS
```

The emulator explicitly reported `factory_new=0`; the coordinator observed rejoin/announce and refreshed the IAS CIE handshake even when the network short address was unchanged. Contact-open false/true notifications resumed after restart with no storage erase and no panic.

Verified board identities:

- coordinator C6 USB serial `40:4C:CA:5D:0A:00`;
- emulator C6 USB serial `40:4C:CA:5D:01:D8`.

This closes the IAS Contact fresh-network plus persisted-rejoin blocker for the current Zigbee laboratory baseline. Future hardware changes must preserve these gates rather than re-opening the already resolved storage/autostart/ZoneType/enrollment investigation without new regression evidence.


## Post-refactor dual-C6 hardware regression — 2026-09-04

Task: `20260904-c6-post-refactor-hardware-v47`.

Exact tested source:

- `f13b293be2de6b1601d179568424e0046d6219a7` — `Refactor Zigbee gateway before S3 integration`.

The task re-resolved both boards from their immutable USB serial identities and did not erase coordinator or emulator Zigbee storage. It first flashed/ran the UART-default coordinator and IAS Contact emulator, then rebuilt/flashed the coordinator with the I2C GatewayLink backend while the S3 peer remained intentionally absent, and finally restored the UART-default firmware.

UART preserved-storage regression:

```text
UART_EMULATOR_PRESERVED_STORAGE=True
UART_GATEWAY_REJOIN=True
UART_GATEWAY_ANNOUNCE=True
UART_IAS_CIE_WRITE=True
UART_IAS_ENROLL=True
UART_CONTACT_FALSE=True
UART_CONTACT_TRUE=True
UART_GW_NO_PANIC=True
UART_EMU_NO_PANIC=True
UART_NO_GATEWAY_EVENT_DROP=True
UART_POST_REFACTOR_DUAL_C6=PASS
```

I2C missing-S3/shared-bus regression:

```text
I2C_BACKEND_SELECTED=True
I2C_PEER_ABSENT_EXPECTED=True
I2C_SCD4X_AVAILABLE=True
I2C_SCD4X_CO2=True
I2C_SCD4X_TEMPERATURE=True
I2C_SCD4X_HUMIDITY=True
I2C_ZIGBEE_REJOIN=True
I2C_CONTACT_FALSE=True
I2C_CONTACT_TRUE=True
I2C_GW_NO_PANIC=True
I2C_EMU_NO_PANIC=True
I2C_NO_EVENT_DROP=True
I2C_NO_LINK_QUEUE_DROP=True
I2C_SCD4X_NOT_MARKED_UNAVAILABLE=True
I2C_MISSING_S3_SHARED_BUS=PASS
```

During the I2C phase `peer=0` was expected. Missing-peer write failures increased the short-write counter, but bounded backoff/logging prevented queue loss or disruption of the shared SCD41 bus. Local CO2/temperature/humidity measurements and IAS Contact false/true measurements continued throughout the phase.

Final restoration check:

```text
UART_RESTORED_FINAL_SMOKE=PASS
HARDWARE_GATE_HEAD=f13b293be2de6b1601d179568424e0046d6219a7
POST_REFACTOR_HARDWARE_GATE=PASS
```

This hardware gate closes the C6-side structural-refactor regression. Physical GatewayLink I2C communication with a real S3 slave remains a separate future integration gate.

## LiteGraph migration-ready hardware closure — 2026-09-04

The migration-ready source checkpoint `5ce963d6ee3b03b9b788f9d02bd9acb4910acead` (`Prepare C6 module for LiteGraph migration`) was revalidated on both physical ESP32-C6 boards without erasing Zigbee storage. The preserved-storage UART IAS regression passed (rejoin, announce, CIE write, enroll, contact true/false, no panic/event drop). The C6 I2C0 mailbox backend then passed with the S3 intentionally absent while the shared SCD41 and Zigbee traffic remained healthy (`peer=0` expected, no event/link queue drop, no panic, no SCD4x unavailable transition). The gateway was finally restored to the UART1 fallback and the bounded final smoke passed.

This proves the exported C6 checkpoint itself is hardware-safe before monorepo absorption. It does **not** prove physical C6↔S3 I2C; that remains the first true cross-MCU hardware gate after the S3 slave/mailbox exists.
