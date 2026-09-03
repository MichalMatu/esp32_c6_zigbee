# Architecture

The ESP32-C6 firmware is intentionally split into a small SDK-facing shell and host-testable domain modules. Keep dependencies flowing toward the normalized event/state contracts rather than letting transport, device policy, or raw SDK callbacks grow into one central file.

## Module boundaries

- `app_main.c` is the composition root. It initializes NVS and starts the event bus, transport, Zigbee gateway, and console. It contains no protocol logic.
- `gateway_console.c/.h` owns the USB console command parser and delegates Zigbee actions through the public gateway API.
- `gateway_inputs.c/.h` defines the protocol-neutral input identity, measurement kinds/units, and capability bits. Zigbee endpoints and local buses must normalize into this contract before transport.
- `gateway_events.c/.h` defines the normalized internal event envelope, static event queue, drop accounting, timestamps, and common warning/event construction helpers. Zigbee lifecycle metadata may remain Zigbee-specific, while measurement events carry a protocol-neutral `gateway_input_id_t`.
- `gateway_transport.c/.h` consumes normalized events and renders the current serial/log transport. It must not own Zigbee state or interpretation policy.
- `gateway_link_protocol.c/.h` defines the hardware-independent GatewayLink v1 framing and payload codec for the future C6-to-S3 link. It uses COBS framing, CRC32, explicit little-endian wire values and fixed-size buffers; it never serializes raw C structs.
- `gateway_link_event_adapter.c/.h` is the pure mapping from normalized gateway input events to GatewayLink messages. Protocol-specific Zigbee lifecycle/raw events are intentionally not forwarded.
- `gateway_link_stream.c/.h` is the pure incremental COBS frame stream decoder. Oversize/corrupt frames are dropped at the delimiter so later frames resynchronize without dynamic allocation.
- `gateway_link_snapshot_cache.c/.h` owns a bounded cache of protocol-neutral input descriptors for reconnect resynchronization. The UART TX task is its sole runtime owner, so descriptor caching and snapshot replay have deterministic ordering without a cross-task lock. It is transport state only, not the application input registry; incremental events remain authoritative after a snapshot.
- `gateway_link_control.c/.h` owns link-control parsing, compatibility checks and response construction. It does not own UART I/O or Zigbee implementation.
- `gateway_link.c/.h` owns the transport-independent GatewayLink runtime: bounded TX queue, sequence allocation, snapshot replay/cache ownership, control dispatch, stream decoding and the RX/TX worker tasks. It talks to a narrow physical-backend contract and must not depend on UART driver details.
- `gateway_uart_link.c/.h` is the current physical backend for GatewayLink. It owns UART1 on TX GPIO18 / RX GPIO19 at 460800 8-N-1 and exposes bounded read/write/start/stop operations through `gateway_link_backend_t`. It does not own GatewayLink protocol, snapshot or Zigbee control policy.
- `gateway_i2c_link.c/.h` is the alternative C6-master I2C mailbox backend on the shared local I2C bus. It does not probe during startup; a missing S3 peer is handled with bounded transaction timeouts and one-second retry backoff so local SCD4x traffic can continue.
- `local_i2c_bus.c/.h` owns the reusable local I2C master bus on SCL GPIO0 / SDA GPIO1. Sensor adapters request devices from this bus instead of configuring I2C independently.
- `local_inputs.c/.h` is the composition point for board-local input adapters. Local-bus absence is reported but does not disable the Zigbee gateway.
- `scd4x_input.c/.h` adapts an SCD4x-family sensor into the protocol-neutral input contract. It probes before driver initialization, throttles absence/read warnings, and owns SCD4x polling/recovery policy, not transport or Zigbee behavior.
- `gateway_zcl_value.c/.h` is a pure, host-testable ZCL attribute normalization layer. Unsupported or scaling-dependent data stays raw instead of being guessed.
- `gateway_zigbee_input.c/.h` is the pure Zigbee-to-generic input adapter for capability aggregation and stable IEEE-based input identity. It deliberately refuses provisional short-address identities.
- `gateway_reporting_policy.c/.h` is the pure, host-testable table for standard binding requirements and per-attribute Configure Reporting parameters.
- `gateway_device_state.c/.h` owns the bounded IEEE-first device/endpoint registry, generation-safe references, short-address replacement, reclaim rules, and bounded per-device binding/reporting request records keyed by endpoint/cluster/attribute. It has no ESP Zigbee or FreeRTOS dependency.
- `zigbee_gateway.c/.h` is the SDK integration/orchestration boundary: stack lifecycle, app/ZCL signal dispatch, discovery jobs, bounded async callback contexts, binding/reporting submission, and permit-join control.

## Zigbee join lifecycle boundary

`DEVICE_UPDATE` is lifecycle/security evidence, not a blanket discovery trigger. In particular, `EZB_ZDO_UPDDEV_UNSECURE_JOIN` occurs before Trust Center authorization has completed and must not start Active Endpoint discovery. Normal device announce and supported rejoin paths start discovery. Active Endpoint discovery is claimed per current short address so duplicate lifecycle signals cannot launch overlapping commissioning for the same route; a route change or leave resets that claim according to the bounded device-state lifecycle.

`DEVICE_AUTHORIZED` is logged separately so hardware tests can distinguish successful authorization from early unsecure-join updates. This rule prevents discovery/bind/reporting traffic from racing the security handshake on sleepy end devices while retaining IEEE-first identity and generation-safe route replacement.

## Invariants

IEEE identity is authoritative; 16-bit Zigbee short addresses are mutable routes. Async work must use generation-safe device references so a recycled slot or reused short address cannot be mistaken for the old device.

Zigbee SDK callbacks must remain bounded and non-blocking. They may copy/normalize payloads, update bounded per-device request state when a response determines that state, publish normalized events, and enqueue follow-up discovery/retry work. They must not wait indefinitely for locks, run blocking discovery loops, or perform transport I/O.

The event bus is the internal transport boundary. Input adapters normalize measurements before publishing them. GatewayLink is the external MCU boundary and serializes only the normalized contract. Input adapters normalize measurements before publishing them. `gateway_transport` must consume `gateway_input_id_t` plus normalized measurements without branching on Zigbee cluster IDs or local sensor register formats. A later UART/SPI link to another MCU should serialize this normalized input contract; the ESP32-S3 can then own the current input list/state used by LiteGraph.

Stable input identity belongs to the adapter boundary. Zigbee uses IEEE identity plus endpoint as the logical channel; short addresses are routing-only and are never emitted as normalized input identities. The SCD4x adapter uses the sensor 48-bit serial number and channel 0, with `scd4x:0x62` only as a fallback when the serial cannot be read.

The ESP32-C6 is an input gateway. Zigbee and local I2C are peer input adapters. The future UART/SPI transport to the ESP32-S3 must serialize input identity, availability/capabilities, and normalized measurements; the ESP32-S3 owns the application-facing current input registry used by LiteGraph. A later link resynchronization message may send a snapshot, but transport must not reinterpret source protocols.

All fixed-capacity structures must fail visibly rather than allocate without bounds or silently overwrite live state. Startup/task creation failures must return an error to the composition root or publish a warning before a task terminates.

## Verification

`gateway_inputs`, `gateway_zcl_value`, `gateway_reporting_policy`, and `gateway_device_state` have strict C11 host tests compiled with `-Wall -Wextra -Werror -pedantic`. GitHub CI also builds the complete firmware with the pinned ESP-IDF/ESP Zigbee versions. Hardware/RF behavior remains a separate validation gate after source/CI validation.

## Future hardware-offload direction

This section is a non-binding long-term direction, not a current migration plan. The ESP32-S3 currently remains a standalone, working application controller and must not be weakened or made dependent on unfinished C6 features. Any responsibility moved to the C6 should be introduced incrementally, with the existing S3 behavior retained until the replacement path is independently verified.

The preferred long-term split is to evolve the ESP32-C6 into a deterministic hardware and field-I/O processor while the ESP32-S3 remains the application processor. The C6 should concentrate hardware-facing work that does not compete with its Zigbee radio: Zigbee coordination, local I2C sensors, additional wired sensor buses and GPIO, ADC, 1-Wire, SPI peripherals, UART/RS485/Modbus, counters, PWM/relay/output drivers, and other bounded local I/O adapters. These adapters should normalize their data into the same protocol-neutral input/event contract already used by Zigbee and SCD4x.

Where useful, the C6 may also own lightweight preprocessing close to the hardware: calibration, validation, debouncing, filtering, deadbands, rate limiting, availability/freshness tracking, and simple fail-safe output behavior when the S3 link is unavailable. Application semantics, automation graphs, UI policy, user configuration, and high-level control decisions should remain on the S3 unless a later design explicitly proves a better boundary.

Radio responsibilities should stay intentionally separated. The C6 should remain dedicated to Zigbee/IEEE 802.15.4 plus wired I/O; Wi-Fi and BLE scanning should stay on the ESP32-S3. Do not add a second 2.4 GHz workload to the C6 merely because the silicon supports it if doing so can reduce Zigbee coordinator reliability.

A small persistent store-and-forward buffer on the C6 may be considered later so short S3 outages do not lose normalized measurements. Prefer a bounded ring buffer and simple failure isolation over a full filesystem. FRAM or another simple persistent memory may be a better fit than placing a full microSD/FAT data logger inside the Zigbee coordinator process. Full history/archive ownership should remain on the S3 unless later measurements show a clear reason to move it.

Possible future C6 expansion areas, subject to separate design and hardware validation, therefore include:

- additional I2C sensors and reusable local-bus adapters;
- 1-Wire sensors such as DS18B20;
- GPIO digital inputs, pulse counters, and local actuator outputs;
- ADC and external ADC adapters;
- UART and RS485/Modbus field devices;
- bounded preprocessing and freshness/availability policy;
- local fail-safe output behavior;
- a small persistent measurement backlog for GatewayLink outages.

This direction should be revisited before implementation. Pin allocation, memory budget, task/queue ownership, failure isolation, electrical interfaces, and the exact S3/C6 responsibility boundary still need deliberate design. The goal is not to maximize C6 utilization; it is to reduce S3 hardware workload without making the Zigbee coordinator less reliable or coupling the currently standalone S3 to unfinished infrastructure.

## Growth rule

Prefer extending the pure policy/state modules when adding supported clusters or device lifecycle behavior. Keep SDK-specific request/callback lifetime management in the gateway integration layer unless a future extraction creates a smaller coherent discovery API rather than merely moving functions between files.
