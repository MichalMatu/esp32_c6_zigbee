# GatewayLink v2

> **Status: Active.** GatewayLink v2 is the only protocol contract for new C6↔S3 integration.

GatewayLink v2 is the active protocol contract between the ESP32-C6 gateway and the future application host. It carries normalized source-neutral identity, capability access, availability and measurements. Standard Zigbee cluster IDs remain C6 implementation detail and are not required by the application host.

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

Standard Zigbee temperature, humidity and battery-percentage reporting policy populate `reportable` and `configurable`. Standard On/Off server endpoints populate `commandable` and accept the normalized `SET_ON_OFF` command. IAS Zone endpoints do not gain a generic capability from cluster `0x0500` alone: C6 first reads `ZoneType`, and only `ContactSwitch` (`0x0015`) exposes readable `CONTACT_OPEN`. `ZoneStatus.Alarm1` maps to boolean `CONTACT_OPEN=true`; IAS contact is not advertised as configurable/reportable through Configure Reporting. Local SCD4x exposes readable temperature/humidity/CO2 but no configurable measurement policy or command path yet.

## INPUT_DESCRIPTOR payload

After the stable input reference:

`available:u8, readable:u32, reportable:u32, configurable:u32, commandable:u32, manufacturer_length:u8, manufacturer:N, model_length:u8, model:N`.

Manufacturer and model are bounded to 23 data bytes each plus local NUL termination after decode. Another descriptor for the same stable input replaces its current normalized capability profile and availability in the host registry.

## Other messages

Message numbers are: HELLO/ACK `0x01/0x02`, PING/PONG `0x03/0x04`, snapshot `0x05..0x07`, INPUT_DESCRIPTOR `0x10`, MEASUREMENT `0x11`, SET_MEASUREMENT_POLICY `0x20`, CONFIG_RESULT `0x21`, PERMIT_JOIN `0x22`, COMMAND_REQUEST `0x30`, COMMAND_RESULT `0x31`.

HELLO advertises `snapshot`, `measurement-policy`, `permit-join`, `capability-profile` and `commands`. For supported Zigbee inputs, `SET_MEASUREMENT_POLICY` is translated into a standard Configure Reporting request. The C6 returns `CONFIG_RESULT` only after the device response, or an explicit normalized error/unsupported result if the request cannot be applied. Interval quantization or reportable-change quantization is returned as `CLAMPED` after a successful device response.

`COMMAND_REQUEST` payload is `request_id:u32`, stable input reference, `kind:u8`, `value:f64`, `transition_ms:u32`. Normalized command kinds are `SET_ON_OFF` and `SET_LEVEL`. `SET_ON_OFF` accepts value `0` or `1` and requires zero transition time. `SET_LEVEL` accepts `0..100` percent and uses transition time in exact 100 ms increments; C6 translates it to standard ZCL `MoveToLevel` without the implicit On/Off variant. `COMMAND_RESULT` is `request_id:u32,status:u8` with `TRANSMITTED`, `UNSUPPORTED`, `INVALID` and `ERROR`.

`TRANSMITTED` means the Zigbee AF transmission confirmation completed successfully. It does **not** assert that the actuator applied the requested state. A subsequent normalized On/Off `MEASUREMENT` report remains the authoritative state observation. This distinction keeps transport acknowledgement separate from device state.

Measurement wire kind `15` is `CONTACT_OPEN`. IAS contact state can arrive through the standard IAS `ZoneStatusChangeNotification` callback or a `ZoneStatus` attribute report; both paths are type-gated by the cached `ZoneType` before normalization. MEASUREMENT, SET_MEASUREMENT_POLICY, CONFIG_RESULT and PERMIT_JOIN payload encodings remain otherwise unchanged from v1. Supported measurement-policy targets are currently temperature, relative humidity and battery percentage, matching the standard reporting policy table. Requests are routed by authoritative Zigbee IEEE input identity plus endpoint; the GatewayLink RX task never uses a mutable short address as application identity. A peer must negotiate protocol version 2; the active branch intentionally does not decode v1 frames.
