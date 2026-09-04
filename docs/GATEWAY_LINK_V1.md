# GatewayLink v1

> **Status: Historical.** GatewayLink v1 is frozen for recovery only. New C6↔S3 integration uses [GatewayLink v2](GATEWAY_LINK_V2.md). Do not add a v1 compatibility shim unless an explicit migration requirement appears.

GatewayLink is the protocol-neutral MCU-to-MCU link between the ESP32-C6 input gateway and the ESP32-S3 application host. It intentionally transports normalized input identity, capabilities, availability and measurements rather than Zigbee clusters, I2C registers or driver-specific structures.

## Physical link reserved for the next stage

The default C6 application-link backend uses UART1 at 460800 baud, 8-N-1, no flow control, with C6 TX on GPIO18 and C6 RX on GPIO19. GPIO0/GPIO1 remain the local I2C SCL/SDA pair used by SCD4x. The RX pin has an internal pull-up so an unconnected S3 does not create a floating UART input. The transport-independent GatewayLink runtime owns the bounded TX queue and incremental delimiter-resynchronizing decoder; the UART backend owns only physical driver I/O. The C6 currently implements HELLO/HELLO_ACK, PING/PONG and PERMIT_JOIN control in addition to descriptor/measurement TX. Snapshot is implemented as bounded descriptor replay; measurement-policy application is not advertised until its backing policy layer exists.

## I2C mailbox backend

UART remains the default backend. The alternative I2C backend keeps the C6 as bus master on GPIO0/GPIO1 and uses S3 slave address `0x42` by default. C6-to-S3 writes use opcode `0x01` followed by a little-endian 16-bit frame length and one complete existing GatewayLink encoded frame. C6 polls S3 with opcode `0x02` for a two-byte pending-frame length and fetches one complete pending frame with opcode `0x03`. A zero pending length means no frame is waiting. Lengths above `GATEWAY_LINK_MAX_FRAME_BYTES` are rejected.

The I2C backend deliberately does not probe the S3 during startup. Missing or unpowered S3 peers produce bounded 20 ms transaction failures and a one-second retry backoff; GatewayLink short-write logging is throttled while counters retain every failure. This lets the shared SCD4x bus keep operating when no S3 is connected.

## Framing

Each on-wire frame is a COBS-encoded binary packet followed by one `0x00` delimiter. `0x00` cannot appear inside the COBS body, so a receiver can recover framing after lost or corrupt bytes by scanning for the next delimiter.

The decoded packet is little-endian:

| Field | Bytes | Notes |
| --- | ---: | --- |
| magic | 2 | ASCII `GL` |
| protocol_version | 1 | `1` |
| message_type | 1 | stable v1 wire value |
| flags | 1 | reserved for message semantics |
| reserved | 1 | must be zero in v1 |
| sequence | 4 | sender-local monotonically increasing sequence, wrap allowed |
| payload_length | 2 | 0..220 |
| payload | N | message-specific |
| CRC32 | 4 | IEEE CRC32 over header + payload |

Maximum encoded frame size is 256 bytes. The codec uses fixed-size buffers and performs no dynamic allocation.

## Stable input reference

Every input-bearing payload begins with:

| Field | Bytes | Notes |
| --- | ---: | --- |
| source | 1 | `1=Zigbee`, `2=local I2C` |
| channel | 1 | logical endpoint/channel |
| id_length | 1 | 1..39 |
| id | N | stable UTF-8/ASCII identifier, not NUL terminated on wire |

For example the validated local sensor is `source=2`, `channel=0`, `id=scd4x:a12bef073b43`. A Zigbee adapter should expose the authoritative IEEE-based identity before publishing it over GatewayLink; mutable short addresses are not application identities.

The C6 Zigbee adapter enforces this rule: supported endpoints are announced only after IEEE recovery succeeds, and known reset/rejoin leaves emit protocol-neutral unavailability for those stable identities.

## Message types

- `0x01 HELLO`, `0x02 HELLO_ACK`: negotiate compatible protocol range, maximum frame size and features.
- `0x03 PING`, `0x04 PONG`: four-byte opaque token.
- `0x05 SNAPSHOT_REQUEST`, `0x06 SNAPSHOT_BEGIN`, `0x07 SNAPSHOT_END`: four-byte request/snapshot token. Input descriptors are sent between begin/end.
- `0x10 INPUT_DESCRIPTOR`: current availability, capability mask and model for one stable input.
- `0x11 MEASUREMENT`: normalized measurement for one stable input.
- `0x20 SET_MEASUREMENT_POLICY`: source-neutral reporting/publishing policy request.
- `0x21 CONFIG_RESULT`: request result (`APPLIED`, `CLAMPED`, `UNSUPPORTED`, `ERROR`).
- `0x22 PERMIT_JOIN`: Zigbee commissioning command intentionally kept at the gateway-control boundary rather than exposed as a measurement policy.

## HELLO payload

`role:u8, min_version:u8, max_version:u8, max_frame_bytes:u16, features:u32`.

Roles are `1=C6 gateway`, `2=S3 host`. The C6 currently advertises `snapshot` and `permit-join`. Measurement-policy remains reserved by v1 but is not advertised until its runtime implementation exists.

## INPUT_DESCRIPTOR payload

After the stable input reference:

`available:u8, capabilities:u32, model_length:u8, model:N`.

The S3 owns the application-facing input registry. Receiving another descriptor for an existing stable input updates that registry entry rather than creating a protocol-specific object.

## MEASUREMENT payload

After the stable input reference:

`uptime_ms:u32, kind:u8, unit:u8, quality:u8, value:f64`.

`value` is an IEEE-754 64-bit value serialized little-endian; it is not a raw C struct. Measurement and unit wire numbers are explicitly mapped by the codec and do not depend on compiler enum layout.

Quality values are `0=VALID`, `1=STALE`, `2=ESTIMATED`, `3=INVALID`.

## SET_MEASUREMENT_POLICY payload

`request_id:u32`, followed by the stable input reference, then:

`kind:u8, min_interval_ms:u32, max_interval_ms:u32, reportable_change:f64`.

This request is deliberately source-neutral. A local SCD4x adapter may implement it as publication filtering while a Zigbee adapter may translate supported fields into Configure Reporting. Unsupported policy must return `CONFIG_RESULT=UNSUPPORTED`; the transport must not guess protocol-specific behavior.

## CONFIG_RESULT payload

`request_id:u32, status:u8` where status is `0=APPLIED`, `1=CLAMPED`, `2=UNSUPPORTED`, `3=ERROR`.

## PERMIT_JOIN payload

`request_id:u32, duration_seconds:u8`. Duration zero closes joining; nonzero durations follow the coordinator policy limits.

## Ownership and recovery

The C6 owns physical input adapters, discovery and normalization. The S3 owns the current application input registry, freshness policy and LiteGraph-facing nodes. Neither side sends raw in-memory C structures.

After either MCU reconnects, the S3 requests a snapshot. The C6 sends descriptors for all currently known stable inputs between `SNAPSHOT_BEGIN` and `SNAPSHOT_END`, then continues with incremental descriptors and measurements. A lost frame is detected by sequence gaps and/or CRC; COBS provides delimiter-level resynchronization.

The C6 snapshot cache has a fixed capacity of 64 descriptors. Snapshot execution runs inside the UART TX task rather than enqueueing every descriptor into the normal 16-item TX queue, so a legitimate snapshot cannot self-overflow that queue. Cache exhaustion is counted as a visible link drop; no unbounded allocation is used.

## Host end-to-end verification

The C6 repository includes a virtual-S3 host test that exercises the real v1 codec, incremental stream decoder, control parser/response builders and descriptor snapshot cache together. It covers fragmented and concatenated frames, CRC corruption recovery, oversized-frame resynchronization, sequence wrap/gap preservation, HELLO compatibility, PING/PONG, snapshot framing/replay, explicit unsupported measurement-policy responses and unknown-message ignore behavior. This test does not emulate the ESP-IDF UART driver or Zigbee stack; those remain firmware/hardware gates.
