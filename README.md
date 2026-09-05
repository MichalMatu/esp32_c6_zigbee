# ESP32-C6 Zigbee extension — archived source repository

> **Status:** absorbed into `MichalMatu/esp32s3_LiteGraph`. This standalone repository is retained for provenance, hardware evidence, tags, and recovery history. Do not use it as the source of new post-absorption development.

The canonical ESP32-C6 firmware now lives at:

`MichalMatu/esp32s3_LiteGraph/firmware/extensions/zigbee-c6/`

Canonical repository:

https://github.com/MichalMatu/esp32s3_LiteGraph

## Absorption checkpoint

- export-ready standalone source: `b33fa5b117f9b2658c906b549083988269c8986f`;
- hardware-proven runtime source: `5ce963d6ee3b03b9b788f9d02bd9acb4910acead`;
- LiteGraph import commit: `cec5da5a5648480e036677efb9331a52050a234b`;
- active cross-processor protocol: GatewayLink v2.

The C6 remains an independently compiled native ESP-IDF firmware image after absorption. It is not compiled together with the ESP32-S3 application firmware.

## What remains here

Historical tags and verification evidence remain intentionally available in this repository. Git history contains the complete pre-absorption README, architecture notes, migration preparation, hardware audits, and recovery points.

Do not move or rewrite historical tags. Do not continue feature development in this repository. Resume all implementation work from the canonical LiteGraph module and its current documentation.
