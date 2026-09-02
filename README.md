# ESP32-C6 Zigbee coordinator / gateway MVP

Native ESP-IDF firmware for an ESP32-C6 coordinator. It is pinned to **ESP-IDF v5.5.4** and the exact managed component **`espressif/esp-zigbee-lib` v2.0.4**. The generated [dependencies.lock](dependencies.lock) is committed to retain that resolution.

This is SDK 2.x firmware: it uses the `esp_zigbee_*` and `ezb_*` APIs and one `ezb_af_create_gateway_endpoint()` gateway endpoint. It does not add a pretend client-cluster data model, nor does it use a ZBOSS/v1 compatibility API, Wi-Fi, BLE, Matter, Thread, MQTT, or an external RCP. An SCD4x-family sensor is supported as an independent local I2C input adapter.

## Verified stable baseline

The hardware-verified SONOFF SNZB-02D baseline is frozen at tag `c6-sonoff-stable-2026-09-02`, pointing to firmware commit `0d64fb03164d3bcb9f5cddd639977b4027bc581f`. The controlled pairing test completed with successful authorization, no `LEAVE_RESET` and no APS security failures; a following 20-minute read-only soak received repeated temperature/humidity reports with zero rejoin, leave, queue-drop, panic or watchdog events. See [docs/VERIFIED_BASELINE.md](docs/VERIFIED_BASELINE.md) for the exact root cause, fix and evidence.

## Architecture

The firmware keeps ESP Zigbee SDK integration separate from a protocol-neutral input contract, normalized events, transport, value decoding, reporting policy, device state, and console handling. Zigbee is one input adapter; local I2C sensors can use the same `gateway_input_id_t` + normalized measurement boundary. The pure input/value/policy/state modules have strict host tests in addition to the full ESP-IDF firmware build. See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for module responsibilities and invariants.

## Local SCD4x input

The board-local SCD4x adapter uses the shared I2C bus with **SCL GPIO0**, **SDA GPIO1**, and the sensor's fixed address `0x62`. The managed dependency is pinned to `jef-sure/scd4x` v0.0.3.

On successful detection the adapter publishes one protocol-neutral input identity based on the sensor's 48-bit serial number, advertises temperature / humidity / CO2 capabilities, starts periodic measurement, and publishes normalized `temperature` (C), `humidity` (%), and `co2` (ppm) events. Sensor absence or read failures are reported without stopping Zigbee operation.

The local sensor does not enter `zigbee_gateway.c`: Zigbee reports and SCD4x readings converge only at `gateway_input_id_t` + `gateway_measurement_t`. This is the same boundary intended for the later C6 -> UART/SPI -> ESP32-S3 link and LiteGraph input registry.

## GatewayLink to ESP32-S3

The protocol-neutral C6-to-S3 contract is specified in [docs/GATEWAY_LINK_V1.md](docs/GATEWAY_LINK_V1.md). GatewayLink v1 uses bounded binary COBS frames with CRC32 and carries stable input identity, descriptors, normalized measurements and versioned controls. UART1 runs at 460800 baud on TX GPIO18 / RX GPIO19. TX is bounded/non-blocking to event handling; RX resynchronizes at COBS delimiters and currently handles HELLO/ACK, PING/PONG and `PERMIT_JOIN`. Descriptor snapshot/resync is implemented with a bounded transport cache and TX-task replay; source-neutral measurement-policy application remains disabled until its real backing policy layer is implemented.

## Build and flash

Install the official ESP-IDF v5.5.4 toolchain, then source its environment:

```sh
git clone --branch v5.5.4 --depth 1 --recursive https://github.com/espressif/esp-idf.git ~/esp/esp-idf-v5.5.4
~/esp/esp-idf-v5.5.4/install.sh esp32c6
. ~/esp/esp-idf-v5.5.4/export.sh

idf.py set-target esp32c6
idf.py build
idf.py -p /dev/cu.usbmodemXXXX flash monitor
```

The build must report `espressif/esp-zigbee-lib (2.0.4)` and `idf (5.5.4)`. `idf.py build` was validated against this exact pair.

## First boot, pairing, and reboot

The custom partition table has three relevant partitions:

- `nvs` for normal ESP-IDF storage;
- dedicated `zb_storage` NVS, initialized with `nvs_flash_init_partition("zb_storage")`;
- `zb_fct`, the SDK factory partition.

Before `esp_zigbee_init`, firmware explicitly sets `platform_config.storage_partition_name = "zb_storage"`. It therefore does not depend on the SDK 2.0.4 default storage name.

On a factory-new C6 the coordinator forms a network, persists it, and automatically opens joining for 180 seconds. Reset the joining device while that window is open. A normal C6 reboot restores the persisted coordinator network and leaves joining closed; it does not form a second network. The Trust Center explicitly allows well-known-key **rejoins** so an already-associated sleepy device can reconnect without opening joining to new devices. The SDK 2.0.4 baseline includes the coordinator-reboot PanCoordinator-bit fix relevant to that workflow.

To deliberately start over during development, erase the C6 flash and flash again; this removes the persisted Zigbee network and makes the next boot factory-new:

```sh
idf.py -p /dev/cu.usbmodemXXXX erase-flash flash monitor
```

Serial commands over the ESP32-C6 USB Serial/JTAG console:

```text
help
permit 180
permit 0
```

`permit` calls acquire the Zigbee lock with a finite 100 ms timeout. Callbacks never wait for that lock.

Expected startup output includes lines similar to:

```text
gateway_transport: zigbee stack ready; endpoint=1, storage=zb_storage
gateway_transport: network formed; joining opens for 180 seconds
gateway_transport: ZIGBEE_DEVICE_ANNOUNCE 00124b.../0x1234
gateway_transport: descriptor ... ep=1 profile=0x0104 device=0x.... in=...[0000,0402] out=...
gateway_transport: basic ... manufacturer=...
gateway_transport: measurement ... temperature=21.500 C cluster=0x0402 attr=0x0000 type=0x29
```

## Gateway events

The gateway endpoint registers SDK 2.x app-signal and ZCL core-action handlers. Arrival/rejoin queues asynchronous Active Endpoint discovery followed by Simple Descriptor requests. Descriptor events retain profile, device type, endpoint, and server/input plus client/output cluster IDs. If an endpoint exposes the Basic server cluster, the gateway issues one Basic read for manufacturer and model.

Recognized report attributes are normalized only when their incoming cluster, attribute, and ZCL type agree and the value is not a Zigbee invalid/sentinel value: temperature, humidity, illuminance, occupancy, CO2, battery voltage/percentage, and On/Off. Electrical Measurement and Metering reports are deliberately emitted through the raw fallback for now: their integer values are not presented as volts, amps, watts, or kWh until the required per-endpoint multiplier/divisor attributes are cached.

After a successful Simple Descriptor, an endpoint with at least one actually normalized server cluster also publishes a protocol-neutral `INPUT_AVAILABLE` descriptor. Capabilities are derived from the same ZCL normalization support table, and the descriptor is emitted only after an authoritative IEEE identity is known; provisional `zigbee-short:*` identities are never exposed on GatewayLink. A known RESET leave and a known REJOIN leave publish `INPUT_UNAVAILABLE` for previously announced endpoints; the generic descriptor is re-announced after rediscovery. `DEVICE_UNAVAILABLE` and unknown leave/update signals remain non-authoritative and do not fabricate generic offline state.

When discovery finds the matching standard server clusters, the gateway sends one Configure Reporting request per cluster. Temperature (`0x0402/0x0000`) reports after at least 60 seconds on a 0.10 °C change and at least once every 300 seconds. Relative humidity (`0x0405/0x0000`) uses the same 60/300-second intervals with a 1.00 %RH change. Battery percentage (`0x0001/0x0021`) reports after at least one hour on a 1 % change and at least once every six hours. This is deliberately limited to standard attributes; manufacturer-specific clusters are neither configured nor interpreted. The serial transport logs each Configure Reporting response.

For a sleepy end device, discovery creates standard ZDO bindings for its temperature, humidity, battery, and Poll Control clusters before configuring reports. The gateway also handles the standard Poll Control Check-In command. Its reply explicitly requests a **five-second** fast-poll window (`20` quarter-seconds; the similarly named SDK predefined macro is documented in milliseconds and is intentionally not used). It queues fresh endpoint discovery and retries each failed/lock-delayed operation up to three times outside Zigbee callbacks. Binding/reporting state is tracked per IEEE identity, endpoint, and cluster; it is marked configured only after a successful response. An unconfirmed request becomes retryable after ten seconds, preventing immediate duplicate commands during a burst of rejoin/check-in events. Thus a normal wake-up/button press is sufficient after a coordinator reboot; removing the battery or re-pairing is not required.

### SNZB-02D pairing and wake-up flow

1. Enter `permit 180`, then put the SNZB-02D into pairing mode. The announcement stores its IEEE address as the durable identity; its short address is only the current route.
2. The gateway discovers endpoint `1` and its Simple Descriptor. A normal SNZB-02D exposes Basic (`0x0000`), Power Configuration (`0x0001`), Poll Control (`0x0020`), Temperature (`0x0402`), and Relative Humidity (`0x0405`) server clusters.
3. It reads Basic manufacturer/model once, binds the Temperature, Humidity, Battery Percentage, and Poll Control clusters to gateway endpoint `1`, then sends the three standard Configure Reporting requests.
4. While awake, the device returns Configure Reporting responses and normal temperature/humidity (and, if the device supports and reports it, battery-percentage) reports. A response with a non-success status remains eligible for a later retry; one unsupported cluster does not stop the rest of discovery.
5. After a coordinator reboot, the persisted network is restored with joining closed. The sensor does not need a factory reset or a new pairing: **single-press** its button for wake-up/reconnection. Do not hold it for five seconds: SONOFF documents that action as entering pairing mode. The gateway requests five seconds of fast polling when it receives Poll Control Check-In and repeats bounded discovery/configuration work that was not confirmed in this boot.

Device records are IEEE-first. If a rejoin changes a short address, the record is updated rather than duplicated. A `LEAVE_INDICATION` with SDK leave type `RESET` (`0`, without rejoin) retires the record once its static queued jobs and ZDO callback contexts have drained. Type `REJOIN` (`1`, with rejoin) instead moves it to a non-routable rejoin-pending state: its IEEE identity and endpoint/binding/reporting state are retained until the same IEEE returns with a new short address. `DEVICE_UPDATE/DEVICE_LEFT` does not carry `leave_type`; it is logged as `ZIGBEE_DEVICE_LEAVE_UNKNOWN` and is conservatively retained rather than guessed to be permanent. If a short address is reused by another IEEE, the displaced record becomes non-routable and is reclaimed only after all static queued jobs and ZDO callback contexts referencing its generation have drained.

Other attributes delivered through the normal ZCL report callback are preserved as raw attributes. A raw event has cluster ID, attribute ID, ZCL type, original byte length, copied byte length, up to 96 bytes, and a `truncated` flag. Completely unsupported/unregistered frames are not fabricated: no APS interception is registered because the MVP has no confirmed additive 2.0.4 indication path that is needed here.

`ZIGBEE_DEVICE_LEAVE_RESET`, `ZIGBEE_DEVICE_LEAVE_REJOIN`, `ZIGBEE_DEVICE_LEAVE_UNKNOWN`, `ZIGBEE_DEVICE_UPDATE`, and `ZIGBEE_DEVICE_UNAVAILABLE` are distinct events. In particular, `DEVICE_UNAVAILABLE` is intentionally not converted to an authoritative generic offline state.

Zigbee SDK callbacks stay bounded and non-blocking: they copy/normalize payloads, update bounded request state when a response determines that state, publish normalized events, and enqueue longer discovery/retry work. They never wait indefinitely for the Zigbee lock or run blocking discovery loops in callback context. A static 16-event queue feeds the serial transport task; queue overflows are counted and reported as aggregated warnings. This narrow transport boundary is the intended replacement point for a later C6 normalized-events → UART/SPI → ESP32-S3 pipeline.

## MVP limitation and hardware test checklist

The gateway configures standard reports only after the target endpoint advertises the relevant server cluster. Battery-powered sleepy devices must be awake to receive this request. For the SNZB-02D, after flashing or coordinator reboot, **single-press** the rear button for its documented wake-up/reconnection action; do not hold it for five seconds because that enters pairing mode. The C6 then receives the device's next traffic and, on Poll Control Check-In, returns the five-second fast-poll window to complete queued work. Do **not** remove the battery or re-pair merely to collect a new temperature/humidity report. Other devices may still reject reporting or emit no useful measurements until a later Configure Reporting extension supports their cluster/attribute, in which case the raw fallback and response logs preserve the evidence without fabricating support.

Hardware-test the following on the target C6:

1. factory-new formation and its 180-second joining window;
2. coordinator reboot recovery, including that joining remains closed;
3. `permit 180` then `permit 0`;
4. join and rejoin announcements, endpoint/simple-descriptor output, and Basic manufacturer/model reads;
5. a known passive report, unknown-attribute raw fallback and a payload over 96 bytes;
6. leave, device-update, and `ZIGBEE_DEVICE_UNAVAILABLE` events.

### SNZB-02D leave/rejoin test

1. Factory-pair the SNZB-02D and confirm temperature/humidity reports.
2. Run `permit 0`; this only closes joining and never removes a record, binding, or network state.
3. If a leave indication arrives, note its exact `leave_type` in the monitor.
4. For `ZIGBEE_DEVICE_LEAVE_REJOIN`, keep joining closed, single-press the SNZB-02D to wake it, and verify the same IEEE reports `ZIGBEE_DEVICE_REJOIN`; `old_short` and `new_short` may differ and no second device record is created.
5. For `ZIGBEE_DEVICE_LEAVE_RESET`, verify `retained=false`; ordinary reporting must not resume until the device is paired again.

Do not hold the SNZB-02D button for five seconds during the rejoin test: that enters pairing mode.

For the SDK 2.x gateway data-model behavior, see Espressif’s [migration guide](https://docs.espressif.com/projects/esp-zigbee-sdk/en/latest/esp32c3/migration-guide/v2.x/zigbee-cluster.html) and [APS/AF reference](https://docs.espressif.com/projects/esp-zigbee-sdk/en/latest/esp32h2/api-reference/esp_zigbee_core/aps_af.html).
