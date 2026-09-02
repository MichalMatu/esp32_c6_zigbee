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
