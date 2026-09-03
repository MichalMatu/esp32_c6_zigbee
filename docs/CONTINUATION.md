# C6 Zigbee continuation handoff

This is the living handoff for continuing work across ChatGPT context windows. It belongs on the active source branch, not on a separate handoff branch.

Use it together with:

- `AGENTS.md` for stable repository, Local Agent, resource, and execution rules;
- `docs/ARCHITECTURE.md` for module ownership and architectural invariants;
- `docs/GATEWAY_LINK_V1.md` for the GatewayLink protocol contract;
- `docs/VERIFIED_BASELINE.md` for frozen hardware/test evidence and recovery points.

`CONTINUATION.md` is intentionally mutable. Update it whenever the active goal, branch, hardware state, or next milestone changes materially.

## Current work context

Repository:

- GitHub: `MichalMatu/esp32_c6_zigbee`
- Local Agent repository id: `esp32-c6-zigbee`
- Local Agent binding: `64877d7d-af3f-4312-a511-699c44aa42dd`
- control branch: `agent-control`
- default branch: `main`
- active integration branch: `integration/c6-s3-i2c-20260903`

Current stable source state before S3 integration:

- `main`: `2b26f355f78d869789d719c1c14a98e87289286e`
- verified GatewayLink firmware commit: `a4b1f629c1286d631ac208515b71aeeaa7c44b23`
- stable tag: `c6-gatewaylink-stable-2026-09-03`
- earlier Zigbee-only stable tag: `c6-sonoff-stable-2026-09-02`

Always inspect the live active-branch HEAD instead of assuming this document's commit is still current.

## Project split

ESP32-C6 owns deterministic low-level work:

- Zigbee coordinator and paired Zigbee devices;
- local wired sensors/I/O;
- bounded event handling;
- GatewayLink production of normalized external events/measurements.

ESP32-S3 owns application-side work:

- Wi-Fi/BLE;
- application/web/LiteGraph integration;
- the opposite side of GatewayLink.

Do not move application responsibilities onto C6 merely to simplify an integration test.

## Completed before the S3 I2C integration phase

The GatewayLink refactor is complete on the C6 side:

1. Introduced a physical-transport facade instead of binding GatewayLink directly to UART.
2. Extracted common GatewayLink runtime ownership from the UART backend.
3. Added runtime observability and the `link status` console command.
4. Added a selectable I2C backend while keeping UART as the default known-working backend.
5. Added/ran host tests for protocol, stream framing, virtual-S3 E2E, and the I2C mailbox.
6. Verified complete ESP-IDF builds for both UART-default and I2C configurations.
7. Performed a physical C6 UART smoke test with the S3 absent.
8. Performed a 5 h 20 min physical UART soak with the paired SONOFF and local SCD4x active.
9. Froze the verified code with `c6-gatewaylink-stable-2026-09-03`.
10. Cleaned obsolete source branches; only `main`, `integration/c6-s3-i2c-20260903`, and `agent-control` remain.

Important commit sequence before the freeze:

- `fff7b242f8a713c009232b1f3a0f7da17ae13ddd` — GatewayLink transport facade
- `9c748e18835342aa7f66cd14566a5b487214b38a` — runtime extraction
- `53a6a85e4de2ff011b91c7c4d2a1f82604ebb566` — observability
- `a4b1f629c1286d631ac208515b71aeeaa7c44b23` — selectable I2C backend
- `2b26f355f78d869789d719c1c14a98e87289286e` — verified-baseline documentation

## Verified physical baseline

Hardware present during the final C6 soak:

- ESP32-C6 coordinator;
- local SCD4x on the C6 I2C bus;
- paired SONOFF SNZB-02D, IEEE `881a14fffeef6bd9`;
- S3 intentionally absent.

Final 5 h 20 min validation summary:

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

The final gate found no Guru Meditation, abort, panic, watchdog, Zigbee leave-reset, Zigbee security failure, GatewayLink queue drops, or gateway-event queue drops.

`peer=0`, `rx=0`, and `last_rx_ms=0` are expected in that baseline because no S3 peer was connected.

The failed status of the original long soak task was only a Local Agent total-budget issue: all four 80-minute capture chunks completed with exit code 0, but the final log-analysis command was not started. A separate read-only validation task analyzed the complete log and passed.

Canonical detailed evidence remains in `docs/VERIFIED_BASELINE.md`.

## C6 I2C backend currently implemented

The I2C backend is an alternative physical backend, not a UART replacement.

C6 side:

- C6 is I2C master;
- controller: I2C0;
- SDA: GPIO1;
- SCL: GPIO0;
- existing local SCD4x shares the same bus at address `0x62`;
- planned S3 GatewayLink slave address: `0x42`;
- default speed: 400 kHz.

Mailbox operations carry the existing encoded GatewayLink frame:

- `0x01 WRITE_FRAME`: C6 writes opcode + LE16 frame length + one complete encoded GatewayLink frame;
- `0x02 PENDING_LENGTH`: C6 write-read obtains a two-byte LE pending S3-to-C6 frame length;
- `0x03 READ_FRAME`: C6 write-read obtains exactly the pending encoded frame;
- pending length `0` means no frame;
- frames larger than `GATEWAY_LINK_MAX_FRAME_BYTES` are rejected.

Missing-peer behavior is intentionally bounded:

- backend startup does not probe the S3 peer;
- the device handle can be created while S3 is absent;
- transaction timeout is short (20 ms);
- transaction/NACK errors back off for approximately 1 s;
- backend logging is not allowed to flood;
- common short-write warnings are throttled while counters still record failures.

The I2C backend is host-tested and firmware-build-tested. It has **not** yet been physically validated against an S3 slave.

## Active goal

Next milestone: physical C6 ↔ S3 GatewayLink over I2C while the local SCD4x continues to share the C6 bus safely.

Required end-to-end behaviors:

1. Implement the matching GatewayLink I2C slave/mailbox side on S3 in the S3 repository.
2. Connect C6 and S3 physically with common ground, SDA, and SCL using the agreed pins/voltage domain.
3. Build/flash the C6 I2C configuration without erasing the persisted Zigbee network.
4. Verify HELLO/ACK and `peer=1` over I2C.
5. Verify C6 → S3 delivery of local SCD4x measurements and SONOFF measurements.
6. Verify S3 → C6 control traffic through the same mailbox.
7. Verify SCD4x continues reporting normally while GatewayLink shares the bus.
8. Power-cycle or disconnect the S3 peer and verify missing-peer timeout/backoff does not destabilize SCD4x or Zigbee.
9. Reconnect S3 and verify recovery without requiring C6 flash/NVS erase.
10. Run a bounded physical soak after functional validation.

Do not delete the UART backend. UART remains the known-working fallback/diagnostic transport until I2C is independently hardware-verified.

A later cleanup may split logical packet encoding from UART-specific COBS stream framing more strictly, but do not perform that refactor merely as a prerequisite for the first physical I2C validation unless evidence shows it is necessary.

## Planned next major milestone — Generic Zigbee Device Interview & Capability Discovery

After physical C6↔S3 transport is proven, the next major C6 feature is **Generic Zigbee Device Interview & Capability Discovery**. The goal is to stop treating supported devices as model-specific special cases wherever standard ZCL metadata is sufficient.

The C6 should generically discover and normalize device capabilities from the Zigbee network:

1. permit join and complete the existing secure join lifecycle;
2. resolve active endpoints;
3. read Simple Descriptors for each relevant endpoint;
4. enumerate input/output clusters;
5. map known standard clusters/attributes to normalized capabilities such as temperature, humidity, battery, occupancy, on/off, level, and other supported classes;
6. apply standard bind and Configure Reporting policy where appropriate;
7. expose discovered capabilities, reporting configuration, measurements, state, and failures through GatewayLink;
8. use explicit per-device quirks only when standard ZCL behavior is insufficient;
9. keep manufacturer-specific/Tuya-style datapoint support as a separate explicit extension rather than mixing it into the generic ZCL path.

The intended production data path is:

```text
Zigbee device
    ↓
C6 interview / ZCL decoding / reporting policy
    ↓
normalized GatewayLink descriptors + measurements + state
    ↓
UART or I2C physical backend
    ↓
ESP32-S3
    ↓
frontend + automation engine / LiteGraph
```

The S3/frontend should not need to know the raw Zigbee wire format for normal standard devices. It should receive normalized device identity, capabilities, current values, configuration/reporting state, availability, and control/configuration results. Commands and configuration requests travel in the opposite direction through GatewayLink and are translated by the C6 into Zigbee/ZCL operations.

### Second-C6 Zigbee device emulator / self-check loop

Use a second ESP32-C6 as a programmable Zigbee end-device emulator and test peer. This should become a deterministic regression harness for the coordinator firmware rather than depending only on a collection of real commercial devices.

The emulator should be able to expose selectable standard device profiles and controlled behaviors, initially including representative classes such as:

- temperature sensor;
- temperature + humidity sensor;
- battery-powered sleepy sensor;
- occupancy/contact-style sensor;
- on/off actuator;
- dimmable/level-control actuator;
- multi-endpoint device;
- device with multiple standard measurement clusters.

Later profiles can deliberately exercise quirks and failure paths, for example:

- delayed or missing descriptor responses;
- unsupported Configure Reporting values;
- changed short address after rejoin while preserving IEEE identity;
- duplicate announce/rejoin lifecycle signals;
- malformed or unexpected attribute values;
- reporting bursts and queue pressure;
- sleepy-device timing;
- endpoint/cluster combinations that are valid but previously unseen;
- manufacturer-specific behavior in a separate explicit test profile.

The coordinator-side test loop should verify automatically that:

```text
emulated device profile
    ↓ join/interview/bind/reporting
C6 coordinator
    ↓ normalized GatewayLink output
host/S3 test observer
    ↓ assertions
expected capabilities + measurements + configuration behavior
```

For writable/controllable profiles, the loop should also verify the reverse path:

```text
frontend/automation-style command
    ↓ GatewayLink
C6 coordinator
    ↓ Zigbee/ZCL command or Configure Reporting
emulated second C6
    ↓ deterministic response/report
C6 + GatewayLink
    ↓ assertion
```

This emulator is a test harness, not a replacement for physical validation with real devices. Keep at least the existing SONOFF as a real-world regression device, and add selected commercial devices only where they expose behavior the emulator cannot faithfully represent.

### Reporting-policy goal

For standard reportable ZCL attributes, the system should eventually expose configuration such as minimum interval, maximum interval, and reportable change through the S3 frontend/automation layer. The C6 remains responsible for validating/translating that normalized policy into the correct Zigbee Configure Reporting operation and reporting the actual result back to S3.

Do not assume every real device honors requested reporting parameters exactly. The generic path should record success/failure and observed behavior; model-specific exceptions belong in explicit quirk handling rather than hidden fallbacks.

## Repository-boundary rule for S3 work

This C6 repository has a hard Local Agent binding. A C6-bound Chat Bridge wake must not inspect, modify, queue, cancel, or execute work in the S3 repository.

When S3 implementation is required:

- start or use a separately correctly bound S3 conversation/agent context;
- provide that conversation with the mailbox contract above and `docs/GATEWAY_LINK_V1.md`;
- do not reuse the C6 `agent_binding` for S3;
- return to this C6 context for C6-side changes and physical C6 verification.

## Local Agent operating loop

`AGENTS.md` is the canonical repository-local contract. The following is the short operational loop a new chat should follow.

### Before any task

1. Read `AGENTS.md`, this file, `README.md`, and `docs/ARCHITECTURE.md`.
2. Verify the requested source branch and current HEAD.
3. Fetch `.agent/binding.json` and `.agent/status/daemon.json` from `agent-control`.
4. Confirm repository id, repository name, and `agent_binding` match this repository.
5. If a relevant task is active, follow it instead of queueing another task.

### Task contract

Every task JSON created for this repository must contain exactly:

```json
"agent_binding": "64877d7d-af3f-4312-a511-699c44aa42dd"
```

Normal executable task mode is `"commands"`.

Use:

- `resources: []` for repository-local tests/builds/docs/static work;
- `resources: ["board:zigbee-c6"]` for C6 USB/serial/flash/Zigbee/hardware ownership;
- `resources: ["machine"]` only for genuine host-global maintenance.

Use unique immutable task ids. A retry with any payload change gets a new id.

### Evidence authority

For a task `<id>`:

1. read `.agent/runs/<id>.json` while it is active;
2. once terminal, read `.agent/results/<id>.json`;
3. treat the exact terminal result and command output as authority;
4. after a terminal task, fetch fresh daemon status again before queueing another task.

A `command_heartbeat` proves that the Local Agent process/capture is alive and has not hit a Local Agent timeout. It does **not** prove the firmware log is healthy. Firmware health must come from actual captured output or the final analysis gate.

Never queue a second relevant task while a previous relevant task is active.

## Chat Bridge operating loop

A Bridge wake for this repository should contain identifiers equivalent to:

```text
[LA_AGENT=64877d7d-af3f-4312-a511-699c44aa42dd]
[LA_REPO=esp32-c6-zigbee]
[LA_REPOSITORY=MichalMatu/esp32_c6_zigbee]
[LA_CHAT=<bridge-provided-chat-id>]
```

`LA_CHAT` is conversation/Bridge-session-specific. Do not treat an old chat id stored in historical notes as canonical for a new conversation.

Hard binding remains immutable for that wake. If the active goal requires a different repository, pause/rebind explicitly rather than guessing.

Bridge control tokens are placed on the final line of Bridge-oriented replies:

- `[LAB:STOP]` — active goal is complete;
- `[LAB:PAUSE]` — manual intervention is required;
- `[LAB:RESUME]` — resume after a pause when applicable;
- `[LAB:NEXT=30s]`, `[LAB:NEXT=5m]`, `[LAB:NEXT=10m]` — request the next wake after that interval;
- `[LAB:INTERVAL=30m]` — set a fixed recurring interval;
- `[LAB:INTERVAL=AUTO]` — return interval selection to automatic mode.

Do not use `STOP` merely because one task finished if the active goal still has unfinished steps.

## New-chat bootstrap

A new ChatGPT conversation should not need a pasted multi-page history. The minimal startup request can be:

```text
Continue the C6 Zigbee project from repository evidence. Work on
integration/c6-s3-i2c-20260903. Read AGENTS.md and docs/CONTINUATION.md first,
then inspect fresh Local Agent daemon/run/result evidence before acting.
Do not inspect or modify the S3 repository from the C6 binding.
```

If Chat Bridge is in use, let it supply the current hard-binding wake header rather than manually copying an old `LA_CHAT` value.

The new chat should then report, in a few lines only:

- active branch and current HEAD;
- whether Local Agent is idle or which exact task is active;
- the next concrete milestone from this document;
- any physical/manual dependency that blocks execution.

It should not retell the entire project history unless needed for a decision.

## How to keep this handoff useful

After each major milestone, update only the sections that changed:

- current branch/baseline;
- completed work;
- active goal;
- hardware state;
- exact next steps;
- any new failure/constraint that future chats must know.

Do not turn this into an append-only chat transcript. Old verification details belong in `docs/VERIFIED_BASELINE.md`; stable operating rules belong in `AGENTS.md`; protocol details belong in `docs/GATEWAY_LINK_V1.md`.

The intended documentation split is:

- `AGENTS.md` — **how to work safely**;
- `docs/VERIFIED_BASELINE.md` — **what is proven and recoverable**;
- `docs/CONTINUATION.md` — **where work is now and what happens next**.
