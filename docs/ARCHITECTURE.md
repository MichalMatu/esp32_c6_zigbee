# Architecture

The ESP32-C6 firmware is intentionally split into a small SDK-facing shell and host-testable domain modules. Keep dependencies flowing toward the normalized event/state contracts rather than letting transport, device policy, or raw SDK callbacks grow into one central file.

## Module boundaries

- `app_main.c` is the composition root. It initializes NVS and starts the event bus, transport, Zigbee gateway, and console. It contains no protocol logic.
- `gateway_console.c/.h` owns the USB console command parser and delegates Zigbee actions through the public gateway API.
- `gateway_inputs.c/.h` defines the protocol-neutral input identity, measurement kinds/units, and capability bits. Zigbee endpoints and local buses must normalize into this contract before transport.
- `gateway_events.c/.h` defines the normalized internal event envelope, static event queue, drop accounting, timestamps, and common warning/event construction helpers. Zigbee lifecycle metadata may remain Zigbee-specific, while measurement events carry a protocol-neutral `gateway_input_id_t`.
- `gateway_transport.c/.h` consumes normalized events and renders the current serial/log transport. It must not own Zigbee state or interpretation policy.
- `local_i2c_bus.c/.h` owns the reusable local I2C master bus on SCL GPIO0 / SDA GPIO1. Sensor adapters request devices from this bus instead of configuring I2C independently.
- `local_inputs.c/.h` is the composition point for board-local input adapters. Local-bus absence is reported but does not disable the Zigbee gateway.
- `scd4x_input.c/.h` adapts an SCD4x-family sensor into the protocol-neutral input contract. It probes before driver initialization, throttles absence/read warnings, and owns SCD4x polling/recovery policy, not transport or Zigbee behavior.
- `gateway_zcl_value.c/.h` is a pure, host-testable ZCL attribute normalization layer. Unsupported or scaling-dependent data stays raw instead of being guessed.
- `gateway_reporting_policy.c/.h` is the pure, host-testable table for standard binding/reporting masks and Configure Reporting parameters.
- `gateway_device_state.c/.h` owns the bounded IEEE-first device/endpoint registry, generation-safe references, short-address replacement, and reclaim rules. It has no ESP Zigbee or FreeRTOS dependency.
- `zigbee_gateway.c/.h` is the SDK integration/orchestration boundary: stack lifecycle, app/ZCL signal dispatch, discovery jobs, bounded async callback contexts, binding/reporting submission, and permit-join control.

## Invariants

IEEE identity is authoritative; 16-bit Zigbee short addresses are mutable routes. Async work must use generation-safe device references so a recycled slot or reused short address cannot be mistaken for the old device.

Zigbee SDK callbacks must remain bounded and non-blocking. They may copy/normalize payloads, update bounded per-device request state when a response determines that state, publish normalized events, and enqueue follow-up discovery/retry work. They must not wait indefinitely for locks, run blocking discovery loops, or perform transport I/O.

The event bus is the transport boundary. Input adapters normalize measurements before publishing them. `gateway_transport` must consume `gateway_input_id_t` plus normalized measurements without branching on Zigbee cluster IDs or local sensor register formats. A later UART/SPI link to another MCU should serialize this normalized input contract; the ESP32-S3 can then own the current input list/state used by LiteGraph.

Stable input identity belongs to the adapter boundary. Zigbee uses IEEE identity plus endpoint as the logical channel; short addresses are only a provisional fallback when IEEE recovery has not completed. The SCD4x adapter uses the sensor 48-bit serial number and channel 0, with `scd4x:0x62` only as a fallback when the serial cannot be read.

The ESP32-C6 is an input gateway. Zigbee and local I2C are peer input adapters. The future UART/SPI transport to the ESP32-S3 must serialize input identity, availability/capabilities, and normalized measurements; the ESP32-S3 owns the application-facing current input registry used by LiteGraph. A later link resynchronization message may send a snapshot, but transport must not reinterpret source protocols.

All fixed-capacity structures must fail visibly rather than allocate without bounds or silently overwrite live state. Startup/task creation failures must return an error to the composition root or publish a warning before a task terminates.

## Verification

`gateway_inputs`, `gateway_zcl_value`, `gateway_reporting_policy`, and `gateway_device_state` have strict C11 host tests compiled with `-Wall -Wextra -Werror -pedantic`. GitHub CI also builds the complete firmware with the pinned ESP-IDF/ESP Zigbee versions. Hardware/RF behavior remains a separate validation gate after source/CI validation.

## Growth rule

Prefer extending the pure policy/state modules when adding supported clusters or device lifecycle behavior. Keep SDK-specific request/callback lifetime management in the gateway integration layer unless a future extraction creates a smaller coherent discovery API rather than merely moving functions between files.
