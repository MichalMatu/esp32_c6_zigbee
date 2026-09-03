# GatewayLink v2

GatewayLink v2 is the current development contract between the ESP32-C6 gateway and the future application host. It carries normalized source-neutral identity, capability access, availability and measurements. Standard Zigbee cluster IDs remain C6 implementation detail and are not required by the application host.

GatewayLink v1 remains documented in `docs/GATEWAY_LINK_V1.md` and frozen by `c6-gatewaylink-stable-2026-09-03`. The active branch does not implement a v1 compatibility shim because there is no deployed S3 peer or persisted application flow requiring migration.

## Framing and physical backends

Framing is unchanged from v1: a COBS-encoded binary packet terminated by `0x00`, little-endian fields, IEEE CRC32, maximum encoded frame size 256 bytes and maximum payload 220 bytes. The decoded header is `GL`, protocol version `2`, message type, flags, reserved zero byte, sender-local sequence, payload length, payload and CRC32.

UART remains the known-working default physical backend. The selectable C6 I2C mailbox backend transports the same complete encoded GatewayLink frame; changing UART to I2C does not change the normalized data model.

## Stable input reference

Every input-bearing payload starts with `source:u8, channel:u8, id_length:u8, id:N`. Zigbee input IDs use authoritative IEEE identity plus endpoint. Zigbee short addresses are mutable routes and are never normalized application identities.

## Capability profile

`INPUT_DESCRIPTOR` exposes four independent source-neutral capability masks:

- `readable`: normalized values/state that C6 understands for this input;
- `reportable`: values for which C6 has a defined reporting path;
- `configurable`: values for which C6 can translate a reporting/configuration policy;
- `commandable`: values/state for which C6 has a defined write/command path.

A bit is advertised only when C6 has an explicit standard implementation. Manufacturer-specific or Tuya-style behavior must not silently set generic bits.

At this phase, standard Zigbee temperature, humidity and battery-percentage reporting policy populate `reportable` and `configurable`. Standard On/Off server endpoints populate `commandable`; the actual command request/result path is added in the following implementation phase. Local SCD4x exposes readable temperature/humidity/CO2 but no configurable measurement policy yet.

## INPUT_DESCRIPTOR payload

After the stable input reference:

`available:u8, readable:u32, reportable:u32, configurable:u32, commandable:u32, manufacturer_length:u8, manufacturer:N, model_length:u8, model:N`.

Manufacturer and model are bounded to 23 data bytes each plus local NUL termination after decode. Another descriptor for the same stable input replaces its current normalized capability profile and availability in the host registry.

## Other messages

Message numbers remain: HELLO/ACK `0x01/0x02`, PING/PONG `0x03/0x04`, snapshot `0x05..0x07`, INPUT_DESCRIPTOR `0x10`, MEASUREMENT `0x11`, SET_MEASUREMENT_POLICY `0x20`, CONFIG_RESULT `0x21`, PERMIT_JOIN `0x22`.

HELLO advertises `snapshot`, `measurement-policy`, `permit-join` and `capability-profile`. For supported Zigbee inputs, `SET_MEASUREMENT_POLICY` is translated into a standard Configure Reporting request. The C6 returns `CONFIG_RESULT` only after the device response, or an explicit normalized error/unsupported result if the request cannot be applied. Interval quantization or reportable-change quantization is returned as `CLAMPED` after a successful device response.

MEASUREMENT, SET_MEASUREMENT_POLICY, CONFIG_RESULT and PERMIT_JOIN payload encodings remain otherwise unchanged from v1. Supported measurement-policy targets are currently temperature, relative humidity and battery percentage, matching the standard reporting policy table. Requests are routed by authoritative Zigbee IEEE input identity plus endpoint; the GatewayLink RX task never uses a mutable short address as application identity. A peer must negotiate protocol version 2; the active branch intentionally does not decode v1 frames.
