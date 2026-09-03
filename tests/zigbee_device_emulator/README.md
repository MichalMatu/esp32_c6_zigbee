# Second-C6 Zigbee device emulator

This directory is an independent ESP-IDF application for a distinct second ESP32-C6. It does not link or include production `main/` sources.

## Build-time profiles

Select one under `Second-C6 Zigbee emulator` in menuconfig:

- `Temperature + humidity`: endpoint 1, Basic + Temperature Measurement + Relative Humidity Measurement.
- `Occupancy`: endpoint 1, Basic + Occupancy Sensing (PIR).
- `On/Off + Level`: endpoint 1, Basic + On/Off + Level Control server clusters.
- `Mixed multi-endpoint` (default): environment on endpoint 1, occupancy on endpoint 2, On/Off + Level on endpoint 3.

Environment and occupancy attributes change deterministically every 10 seconds. This is intended to exercise coordinator interview, Basic metadata, endpoint/cluster capability normalization and Configure Reporting. On/Off and Level are real server descriptors; stack-side attribute changes are logged through the ZCL core action callback so command behavior can be observed when hardware validation begins.

Hardware flashing remains a separate gate: verify two distinct Local Agent ESP32-C6 resource identities before any coordinator/emulator flash task.
