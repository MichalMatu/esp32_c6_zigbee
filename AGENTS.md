# AGENTS.md

These instructions apply to the archived standalone repository `MichalMatu/esp32_c6_zigbee`.

## Repository state

This repository is no longer the canonical development location. Its export-ready source was absorbed into:

`MichalMatu/esp32s3_LiteGraph/firmware/extensions/zigbee-c6/`

Use the LiteGraph repository for all new C6 firmware, GatewayLink, Zigbee integration, tests, documentation, and Local Agent work.

## Fail-closed development rule

Do not queue new implementation tasks through this repository's former Local Agent binding, control branch, or workspace. The old standalone binding is historical operational metadata only.

For active work use the LiteGraph control plane documented by the canonical module `AGENTS.md`:

- repository: `MichalMatu/esp32s3_LiteGraph`;
- repository id: `litegraph`;
- control branch: `agent-control`;
- active agent binding is defined by the LiteGraph repository's current control state.

Do not copy changes from LiteGraph back into this repository and do not create a second canonical C6 source tree.

A Chat Bridge conversation bound to this archival repository must pause for an explicit operator **Rebind** before following work in LiteGraph. Never substitute the LiteGraph binding from model context. The archival catalog entry and local registry remain disabled for execution.

## Historical use

This repository remains useful for read-only provenance and recovery:

- export-ready source: `b33fa5b117f9b2658c906b549083988269c8986f`;
- hardware-proven runtime source: `5ce963d6ee3b03b9b788f9d02bd9acb4910acead`;
- migration, hardware-audit, and verified-baseline history;
- immutable recovery tags.

Do not move or rewrite historical tags. If a historical investigation requires an archival correction, keep it narrowly scoped and do not resume product development here.
