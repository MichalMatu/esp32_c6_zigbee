# Final critical re-audit — 2026-09-04

## Scope

This is the final pre-LiteGraph re-audit of the ESP32-C6 Zigbee extension repository. The audited runtime/export source is `5ce963d6ee3b03b9b788f9d02bd9acb4910acead` (`Prepare C6 module for LiteGraph migration`). The purpose is to prove that the module is clean, reproducible, internally consistent, migration-ready, and backed by physical hardware evidence before absorption into the LiteGraph monorepo.

## Critical invariants checked

- native ESP-IDF C6 firmware; verified with ESP-IDF v5.5.4;
- `espressif/esp-zigbee-lib` v2.0.4;
- GatewayLink protocol version 2 only; v1 remains historical and no v1 compatibility shim is introduced;
- C6 remains Zigbee/local-I/O owner; S3 remains the future Wi-Fi/BLE/web/LiteGraph owner;
- IEEE-first Zigbee identity with mutable short address used only for current routing;
- bounded/nonblocking work/event boundaries are preserved;
- UART1 remains the verified/default fallback backend;
- I2C backend remains C6 master on I2C0, SDA GPIO1, SCL GPIO0, 400 kHz, planned S3 slave `0x42`, shared SCD4x `0x62`, bounded missing-peer behavior;
- no Arduino, Matter, Thread, Wi-Fi, BLE, MQTT or external RCP is added to the C6 module;
- persisted Zigbee/NVS expectations are preserved.

## Static and repository audit

The final audit checks:

- repository and generated-artifact hygiene;
- manifest/document/source contract consistency;
- stale migration-path references and historical-vs-active contract separation;
- shell syntax plus `shellcheck` when available;
- Python syntax for host checks;
- `cppcheck` portability/warning scan of `main/*.c` when available;
- `git diff --check`;
- canonical migration-readiness gate;
- canonical host-test suite;
- clean UART ESP-IDF build;
- clean I2C ESP-IDF build;
- clean Zigbee device emulator build;
- final tree contains no generated root `build/`, `sdkconfig`, `sdkconfig.old` or managed-component debris.

Two README occurrences using `/dev/cu.usbmodemXXXX` are intentional device-port placeholders, not hard-coded host dependencies. The documented `erase-flash` command is retained only as an explicit destructive recovery/factory-reset instruction; the migration/hardware validation path does not use it.

## Physical hardware evidence

Task `20260904-c6-migration-ready-hardware-v55` revalidated the exact runtime/export source `5ce963d6ee3b03b9b788f9d02bd9acb4910acead` on both physical ESP32-C6 boards without erasing Zigbee storage.

Verified results:

- preserved-storage UART dual-C6 IAS/rejoin/contact regression: PASS;
- emulator remained `factory_new=0`;
- gateway rejoin + announce: PASS;
- IAS CIE write + enroll: PASS;
- contact false/true reports: PASS;
- gateway/emulator panic/watchdog checks: PASS;
- no gateway event-queue drop: PASS;
- C6 I2C0 mailbox backend with S3 intentionally absent: PASS;
- expected `peer=0` while missing S3: PASS;
- shared SCD41/SCD4x CO2/temperature/humidity remained available: PASS;
- Zigbee contact traffic remained active on the shared-bus gate: PASS;
- no event/link queue drop and no panic: PASS;
- final UART1 restoration smoke: PASS.

Hardware identities used:

- gateway C6: `40:4C:CA:5D:0A:00`;
- emulator C6: `40:4C:CA:5D:01:D8`.

## Residual risks / deliberately unproven items

No critical or high-severity C6-only blocker remains before migration.

The following is intentionally **not** claimed as verified yet:

- physical C6↔S3 I2C traffic;
- S3 mailbox/slave implementation at `0x42`;
- GatewayLink v2 HELLO/ACK with `peer=1` over real C6↔S3 I2C;
- end-to-end C6→S3 measurements and S3→C6 control over the physical link;
- disconnect/reconnect recovery with a real S3 peer;
- post-integration soak across both MCUs.

Those belong to the LiteGraph-bound integration phase, after this module is imported unchanged and its nested build/test gates pass.

## Final disposition

The C6 repository is frozen for migration. No additional C6-only feature work or refactor is justified before import. Import first with no behavior redesign, verify the nested module, then implement the S3 side and perform the first true cross-MCU hardware gate.
