# C6 Zigbee continuation handoff

> **Status:** standalone development closed after absorption into the LiteGraph monorepo.

The canonical continuation point is now:

`MichalMatu/esp32s3_LiteGraph/firmware/extensions/zigbee-c6/`

Read the canonical LiteGraph integration document before resuming work:

`docs/main_docs/integrations/ZIGBEE_C6_INTEGRATION.md`

## Historical checkpoints retained here

- export-ready source: `b33fa5b117f9b2658c906b549083988269c8986f`;
- hardware-proven runtime source: `5ce963d6ee3b03b9b788f9d02bd9acb4910acead`;
- `c6-litegraph-export-ready-2026-09-04`;
- `c6-litegraph-migration-ready-2026-09-04`;
- `c6-gatewaylink-stable-2026-09-03`;
- `c6-sonoff-stable-2026-09-02`.

Historical hardware evidence remains in `VERIFIED_BASELINE.md`, `AUDIT_2026-09-04.md`, and `FINAL_REAUDIT_2026-09-04.md`.

## Development rule

Do not resume implementation, documentation planning, or Local Agent tasks in this standalone repository. The former standalone Local Agent binding/control state is historical only. All post-absorption work belongs to the LiteGraph repository so there is one canonical C6 source tree.
