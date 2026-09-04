# C6 Zigbee continuation handoff

> **Status:** C6-side work is frozen and ready for LiteGraph monorepo import. Do not start speculative C6 refactors before migration.

This file is the concise current-state handoff. Historical implementation details and physical evidence live in [Verified baseline](VERIFIED_BASELINE.md), [Pre-S3 repository audit](AUDIT_2026-09-04.md), and [Final critical re-audit](FINAL_REAUDIT_2026-09-04.md).

## Start here

Read these documents in order when resuming work:

1. [Architecture](ARCHITECTURE.md)
2. [GatewayLink v2](GATEWAY_LINK_V2.md)
3. [LiteGraph migration guide](LITEGRAPH_MIGRATION.md)
4. [Verified baseline](VERIFIED_BASELINE.md)
5. [Final critical re-audit](FINAL_REAUDIT_2026-09-04.md)

## Current repository state

- repository: `MichalMatu/esp32_c6_zigbee`
- source branch: `main`
- Local Agent control branch: `agent-control`
- migration-ready tag: `c6-litegraph-migration-ready-2026-09-04`
- migration-ready freeze commit: `067fa6f744f5e28233eb5b267edd7d6daa262c2c`
- exact hardware-proven runtime source: `5ce963d6ee3b03b9b788f9d02bd9acb4910acead`
- active protocol: GatewayLink v2 only
- default physical backend: UART1
- selectable C6-side alternate backend: I2C0 mailbox

`main` may contain documentation-only cleanup commits after the migration-ready tag. The runtime/build/test inputs must remain identical to the hardware-proven runtime source until the module is imported and revalidated in the LiteGraph monorepo.

## What this firmware is

The ESP32-C6 is the deterministic hardware and field-I/O extension for the LiteGraph controller architecture. It owns Zigbee and local low-level I/O, normalizes source-specific data into a common input/capability/measurement model, and exposes that model to the ESP32-S3 through GatewayLink.

The ESP32-S3 remains the application processor. It owns Wi-Fi, BLE, web/UI, configuration, LiteGraph automation, and the application-facing input registry.

## C6 responsibilities

- Zigbee coordinator lifecycle and persisted Zigbee/NVS state;
- IEEE-first device identity and route-safe short-address handling;
- bounded discovery, ZDO work, binding, reporting, Poll Control, and IAS handling;
- normalized measurement and command translation;
- local I2C input adapters such as SCD4x;
- GatewayLink v2 runtime, snapshot/resync, and physical backends;
- C6-specific console, host tests, and two-C6 emulator tests.

Do not move Wi-Fi, BLE, web/UI, LiteGraph application logic, Matter, Thread, MQTT, or external-RCP responsibilities onto the C6 during migration.

## Verified software state

The final software re-audit passed:

- repository/source hygiene;
- manifest/document/source-contract consistency;
- active GatewayLink v2-only checks;
- shell syntax and `shellcheck`;
- C portability/warning scan with `cppcheck`;
- canonical strict host tests;
- migration-readiness verification;
- UART-default ESP-IDF build;
- I2C-backend ESP-IDF build;
- Zigbee device-emulator ESP-IDF build.

See [Final critical re-audit](FINAL_REAUDIT_2026-09-04.md) for the exact evidence.

## Verified hardware state

The exact runtime source `5ce963d6ee3b03b9b788f9d02bd9acb4910acead` passed the final two-C6 hardware gate without erasing persisted Zigbee storage.

Verified behaviors include:

- preserved-storage UART rejoin and device announce;
- IAS CIE write, enrollment, and contact false/true reporting;
- no gateway or emulator panic/watchdog failure;
- no gateway-event queue drop;
- C6 I2C0 backend selected with the S3 intentionally absent;
- expected missing-peer state (`peer=0`) with bounded retry behavior;
- SCD4x CO2/temperature/humidity remaining healthy on the shared I2C bus;
- Zigbee contact traffic remaining healthy while the I2C backend is selected;
- no GatewayLink event/link queue drops;
- successful final restoration to the UART1 fallback.

Hardware identities:

- gateway/coordinator C6: `40:4C:CA:5D:0A:00`
- emulator C6: `40:4C:CA:5D:01:D8`

Always rediscover the live serial ports from these identities before hardware access.

## Deliberately unverified

Do **not** claim any of the following until a real S3 peer has been implemented and physically tested:

- physical C6↔S3 I2C traffic;
- S3 mailbox/slave implementation at address `0x42`;
- GatewayLink v2 HELLO/ACK with `peer=1` over physical I2C;
- C6→S3 measurement delivery over the physical I2C link;
- S3→C6 normalized command/control over the physical I2C link;
- disconnect/reconnect recovery with a real S3 peer;
- post-integration two-MCU soak stability.

## Next action

The next implementation step belongs in the LiteGraph/S3 repository context, not in this standalone C6 repository:

1. Import this C6 module unchanged into `firmware/extensions/zigbee-c6/`.
2. Re-run the nested host/readiness/UART/I2C/emulator gates.
3. Re-run the preserved-NVS two-C6 smoke after import.
4. Implement the S3 I2C slave/mailbox at `0x42`.
5. Run the first true C6↔S3 GatewayLink v2 hardware E2E gate.
6. Keep UART as fallback until the physical I2C E2E and bounded soak pass.

Follow [LiteGraph migration guide](LITEGRAPH_MIGRATION.md) for the canonical sequence.

## Local Agent boundary

For this standalone repository only:

- Local Agent repository id: `esp32-c6-zigbee`
- agent binding: `64877d7d-af3f-4312-a511-699c44aa42dd`
- control branch: `agent-control`
- source branch: `main`
- hardware resource: `board:zigbee-c6`

Before queueing a task, require fresh `agent-control:.agent/status/daemon.json` evidence for this exact repository/binding and `state=idle`. Every executable Local Agent task must contain exactly the bound `agent_binding`. Never route work through another repository's control branch.

These Local Agent details are operational metadata for the standalone repository. Rewrite them when the module is absorbed into a different monorepo; do not copy them verbatim as architecture.

## Recovery points

- `c6-litegraph-migration-ready-2026-09-04` — final migration-ready repository freeze
- `c6-gatewaylink-stable-2026-09-03` — pre-v2-integration GatewayLink recovery point
- `c6-sonoff-stable-2026-09-02` — verified SONOFF Zigbee baseline

Do not move historical tags. Create a new tag after any future independently verified runtime change.

## Maintenance rule

Keep this handoff short and current. Do not append chronological task logs here. Put durable hardware evidence in `VERIFIED_BASELINE.md`, architecture rules in `ARCHITECTURE.md`, migration instructions in `LITEGRAPH_MIGRATION.md`, and dated audit reports in their dedicated files.
