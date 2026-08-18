# ESP32-C6 Zigbee coordinator / gateway MVP

Native ESP-IDF firmware for an ESP32-C6 coordinator. It is pinned to **ESP-IDF v5.5.4** and the exact managed component **`espressif/esp-zigbee-lib` v2.0.4**. The generated [dependencies.lock](dependencies.lock) is committed to retain that resolution.

This is SDK 2.x firmware: it uses the `esp_zigbee_*` and `ezb_*` APIs and one `ezb_af_create_gateway_endpoint()` gateway endpoint. It does not add a pretend client-cluster data model, nor does it use a ZBOSS/v1 compatibility API, Wi-Fi, BLE, Matter, Thread, MQTT, an external RCP, or an SCD41.

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

On a factory-new C6 the coordinator forms a network, persists it, and automatically opens joining for 180 seconds. Reset the joining device while that window is open. A normal C6 reboot restores the persisted coordinator network and leaves joining closed; it does not form a second network. The SDK 2.0.4 baseline includes the coordinator-reboot PanCoordinator-bit fix relevant to that workflow.

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

Recognized report attributes are normalized only when their incoming cluster, attribute, and ZCL type agree: temperature, humidity, illuminance, occupancy, CO2, power configuration, electrical measurement, metering, and On/Off. Electrical and metering values are emitted in their reported units; device-specific multiplier/divisor handling is deliberately future work.

When discovery finds the matching standard server clusters, the gateway sends one Configure Reporting request per cluster. Temperature (`0x0402/0x0000`) reports after at least 60 seconds on a 0.10 °C change and at least once every 300 seconds. Relative humidity (`0x0405/0x0000`) uses the same 60/300-second intervals with a 1.00 %RH change. Battery percentage (`0x0001/0x0021`) reports after at least one hour on a 1 % change and at least once every six hours. This is deliberately limited to standard attributes; manufacturer-specific clusters are neither configured nor interpreted. The serial transport logs each Configure Reporting response.

For a sleepy end device, discovery creates standard ZDO bindings for its temperature, humidity, battery, and Poll Control clusters before configuring reports. The gateway also handles the standard Poll Control Check-In command. It requests a temporary fast-poll window, queues fresh endpoint discovery, and submits any reporting configuration not confirmed in the current boot. Thus a short normal wake-up/button press is sufficient after a coordinator reboot; removing the battery or re-pairing is not required.

Other attributes delivered through the normal ZCL report callback are preserved as raw attributes. A raw event has cluster ID, attribute ID, ZCL type, original byte length, copied byte length, up to 96 bytes, and a `truncated` flag. Completely unsupported/unregistered frames are not fabricated: no APS interception is registered because the MVP has no confirmed additive 2.0.4 indication path that is needed here.

`ZIGBEE_DEVICE_LEAVE`, `ZIGBEE_DEVICE_UPDATE`, and `ZIGBEE_DEVICE_UNAVAILABLE` are distinct events. In particular, `DEVICE_UNAVAILABLE` is intentionally not converted to an authoritative generic offline state.

Callbacks only validate/copy/enqueue. A static 16-event queue feeds the serial transport task; queue overflows are counted and reported as aggregated warnings. This narrow transport boundary is the intended replacement point for a later C6 normalized-events → UART/SPI → ESP32-S3 pipeline.

## MVP limitation and hardware test checklist

The gateway configures standard reports only after the target endpoint advertises the relevant server cluster. Battery-powered sleepy devices must be awake to receive this request; for the SNZB-02D, press its rear button after flashing or after a failed reporting request. Other devices may still reject reporting or emit no useful measurements, in which case the raw fallback and response logs preserve the evidence without fabricating support.

Hardware-test the following on the target C6:

1. factory-new formation and its 180-second joining window;
2. coordinator reboot recovery, including that joining remains closed;
3. `permit 180` then `permit 0`;
4. join and rejoin announcements, endpoint/simple-descriptor output, and Basic manufacturer/model reads;
5. a known passive report, unknown-attribute raw fallback and a payload over 96 bytes;
6. leave, device-update, and `ZIGBEE_DEVICE_UNAVAILABLE` events.

For the SDK 2.x gateway data-model behavior, see Espressif’s [migration guide](https://docs.espressif.com/projects/esp-zigbee-sdk/en/latest/esp32c3/migration-guide/v2.x/zigbee-cluster.html) and [APS/AF reference](https://docs.espressif.com/projects/esp-zigbee-sdk/en/latest/esp32h2/api-reference/esp_zigbee_core/aps_af.html).
