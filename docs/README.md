# Documentation

This directory contains the active architecture/protocol contracts, verification evidence, migration instructions, and historical recovery material for the ESP32-C6 extension firmware.

## Start here

1. [Architecture](ARCHITECTURE.md) — C6 ownership, module boundaries, and architectural invariants.
2. [GatewayLink v2](GATEWAY_LINK_V2.md) — active C6↔S3 wire contract for all new integration.
3. [LiteGraph migration guide](LITEGRAPH_MIGRATION.md) — canonical monorepo import procedure and post-import gates.
4. [Continuation handoff](CONTINUATION.md) — concise current state, verified boundary, and next action.

## Verification and recovery

- [Verified baseline](VERIFIED_BASELINE.md) — physical hardware evidence and immutable recovery checkpoints.
- [Final critical re-audit](FINAL_REAUDIT_2026-09-04.md) — final pre-LiteGraph software/hardware audit and residual-risk statement.
- [Pre-S3 repository audit](AUDIT_2026-09-04.md) — structural-refactor audit and post-refactor hardware closure.

## Historical

- [GatewayLink v1](GATEWAY_LINK_V1.md) — historical recovery contract only. New integration uses v2 and must not add a v1 shim without an explicit migration requirement.
