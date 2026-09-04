# Second-C6 Zigbee device emulator

This directory is an independent ESP-IDF application for a distinct second ESP32-C6. It does not link or include production `main/` sources.

## Build

From the repository root:

```sh
. ~/esp/esp-idf-v5.5.4/export.sh
idf.py -C tests/zigbee_device_emulator set-target esp32c6
idf.py -C tests/zigbee_device_emulator build
```

Use `idf.py -C tests/zigbee_device_emulator menuconfig` to select a profile before building when the default mixed profile is not desired.

## Build-time profiles

Select one under `Second-C6 Zigbee emulator` in menuconfig:

- `Temperature + humidity`: endpoint 1, Basic + Temperature Measurement + Relative Humidity Measurement.
- `Occupancy`: endpoint 1, Basic + Occupancy Sensing (PIR).
- `On/Off + Level`: endpoint 1, Basic + On/Off + Level Control server clusters.
- `Sleepy temperature + humidity + battery + Poll Control`: endpoint 1, Basic + Temperature + Humidity + Power Configuration + Poll Control.
- `IAS Zone contact switch`: endpoint 1, Basic + IAS Zone server with ZoneType `ContactSwitch`; ZoneStatus toggles Alarm1 deterministically.
- `Mixed multi-endpoint` (default): environment on endpoint 1, occupancy on endpoint 2, On/Off + Level on endpoint 3.

Environment and occupancy attributes change deterministically every 10 seconds. This is intended to exercise coordinator interview, Basic metadata, endpoint/cluster capability normalization and Configure Reporting. On/Off and Level are real server descriptors; stack-side attribute changes are logged through the ZCL core action callback so command behavior can be observed when hardware validation begins.

Hardware flashing remains a separate gate: verify two distinct Local Agent ESP32-C6 resource identities before any coordinator/emulator flash task.

## Deterministic fault/report modes

Menuconfig also exposes explicit report requests, a 24-update burst mode, invalid-value injection, and the tick interval. Invalid injection uses the Zigbee Temperature Measurement invalid sentinel `0x8000`, Relative Humidity invalid sentinel `0xffff`, and reserved Occupancy bitmap `0x02` every sixth tick. These modes are compile-time deterministic and require no production firmware coupling.

Explicit report requests target coordinator short address `0x0000`, endpoint 1, which matches the Zigbee coordinator role and this repository's production gateway endpoint. The Zigbee stack still applies its reporting configuration rules.

## Writable command round-trip

For `On/Off + Level` and `Mixed multi-endpoint`, standard server-side On/Off and Level commands are handled by the Zigbee stack. The emulator watches `EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID`; when the server OnOff or CurrentLevel attribute changes, it queues a report request after a short delay so the coordinator can observe authoritative post-command state rather than treating AF transmission confirmation as application state.

## Sleepy end-device profile

The sleepy profile exposes battery voltage (`0x0020`) and battery percentage remaining (`0x0021`) on Power Configuration plus a Poll Control server. Its defaults are a 30 s Check-In interval, 5 s long poll, 1 s short poll, 5 s fast-poll timeout, 10 s Zigbee keep-alive, and a 30 s emulator value tick. Battery percentage changes deterministically in Zigbee half-percent units.

This profile emulates sleepy Zigbee protocol timing and Poll Control behavior. It intentionally does not claim MCU deep-sleep/current-consumption behavior; that requires the later two-board hardware validation gate.

## Lifecycle reboot/rejoin fault

An optional menuconfig fault periodically calls `esp_restart()` while preserving the Zigbee storage partition. On the next boot the end device starts with its persisted network state, exercising coordinator duplicate lifecycle/reboot/rejoin handling without depending on ezbee's test-only `ezb_nwk_leave_request()` API. The default interval is 60 s and the mode is disabled by default.

This mode deliberately does not promise a new NWK short address or exact Device Announce ordering; those are stack/network outcomes and remain part of the later two-board hardware validation.

## IAS contact profile

The IAS contact profile uses the standard IAS Zone cluster (`0x0500`) with `ZoneType=ContactSwitch` (`0x0015`). It toggles the `ZoneStatus.Alarm1` bit on each emulator tick. The ezbee IAS API documents that changing ZoneStatus can trigger the standard Zone Status Change Notification; the emulator also exposes the authoritative ZoneStatus attribute. Enrollment/CIE behavior is left to the Zigbee stack and later two-board validation rather than being faked in application code.
