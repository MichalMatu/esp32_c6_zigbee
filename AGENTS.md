# AGENTS.md

These instructions apply to the whole repository.

## Project role

This repository contains native ESP-IDF firmware for an ESP32-C6 Zigbee coordinator/gateway. Keep changes aligned with the current README contract unless the user explicitly asks to change that architecture.

For cross-chat continuation, read `docs/CONTINUATION.md` when it exists on the active branch. It is the living source for current work state, completed milestones, and exact next steps; do not reconstruct that state from remembered chat history.

Current baseline:

- target: ESP32-C6;
- framework: native ESP-IDF;
- ESP-IDF: v5.5.4;
- Zigbee component: `espressif/esp-zigbee-lib` v2.0.4;
- default source branch: `main`.

Do not silently replace the pinned SDK/component versions, introduce Arduino, Matter, Thread, Wi-Fi, BLE, an external RCP, or other architecture changes unless the user explicitly requests them.

## Local Agent workflow

This repository is registered in the shared `MichalMatu/local-agent` bounded-parallel multi-repository supervisor.

Repository identity:

- repository: `MichalMatu/esp32_c6_zigbee`;
- local-agent repository id: `esp32-c6-zigbee`;
- agent binding: `64877d7d-af3f-4312-a511-699c44aa42dd`;
- control branch: `agent-control`;
- default source branch: `main`.

Use this repository's own `agent-control` branch for Local Agent tasks and evidence. Never route ESP32-C6 Zigbee work through another repository's control branch.

If Chat Bridge is active, require every wake to identify exactly `LA_AGENT=64877d7d-af3f-4312-a511-699c44aa42dd`, `LA_REPO=esp32-c6-zigbee`, and `LA_REPOSITORY=MichalMatu/esp32_c6_zigbee`. Never infer or switch repository identity from conversation history. A different repository requires explicit Bridge **Rebind**.

Before queueing work:

1. Read this file, `docs/CONTINUATION.md` when present on the active branch, the current `README.md`, and `docs/ARCHITECTURE.md`.
2. Inspect the exact source branch/HEAD relevant to the request.
3. Inspect `.agent/binding.json` and `.agent/status/daemon.json` on `agent-control`. The binding file must match the repository identity above. Treat `daemon_version`, `self_revision`, `execution_model` / `execution_variant`, current task state, and `supervisor_pid` as repository-worker truth. Supervisor-wide fields such as `max_parallel_workers` are not guaranteed to be repeated in every repository-worker snapshot; read the shared supervisor status when that field matters.
4. Follow any active task instead of queueing a duplicate.

Task/evidence contract:

- queue immutable tasks under `.agent/tasks/<task-id>.json`;
- every executable task must contain exactly `"agent_binding": "64877d7d-af3f-4312-a511-699c44aa42dd"`;
- follow `.agent/runs/<task-id>.json`;
- read `.agent/results/<task-id>.json` before reporting completion;
- use a new unique task id for every intentional retry or changed payload;
- set `work_branch` explicitly when work must run anywhere other than `main`;
- a successful Local Agent result proves execution/verification, not remote source publication unless the task explicitly publishes source.

Hard binding is fail-closed. The executor requires local registry `agent_binding == .agent/binding.json agent_binding == task.agent_binding` before claim/execution. Missing repository binding reports `unbound`; a control mismatch reports `binding_error`; missing/wrong task binding is terminally rejected before any task command runs. Both production parallel execution and the serial fallback enforce this contract. Do not guess, rotate, or borrow another repository's binding to make a task run.

The canonical executor/planner contract lives in `MichalMatu/local-agent`, especially `docs/OPERATIONS.md`, `docs/AUTONOMOUS_CHAT_LOOP.md`, `docs/MULTI_REPOSITORY.md`, and `docs/GOLDEN_STANDARD.md`. Do not pin a Local Agent release number here; read live status and canonical `local-agent/main` instead.

## Resource classification

Every task must declare `resources` explicitly. Missing, malformed, duplicated, oversized, or non-canonical declarations are terminal task-contract errors; there is no compatibility fallback to `machine`.

Use `resources: []` for repository-local software work that does not own an exclusive external device or host-global state. This includes host tests, static analysis, documentation checks, and normal ESP-IDF builds when they do not touch hardware. `memory_limit_mb` is an independent per-task RSS watchdog and does not change resource classification.

Use a narrow named resource for concrete shared hardware or external state. ESP32-C6 USB/serial, flash, monitor, OpenOCD/JTAG, and Zigbee RF/hardware tests should use a stable resource such as `board:zigbee-c6`. Tasks sharing that resource serialize; unrelated named resources may overlap.

Use `resources: ["machine"]` only for operations that genuinely require the whole host, such as global Local Agent maintenance or host-global toolchain mutation. A build or hardware task is not machine-exclusive merely because it is heavy.

Resource contention is a wait state: the immutable task remains pending and is retried. One task still executes at a time for this repository, while unrelated registered repositories may overlap when their declared external resources do not conflict. Production normally uses `max_workers=2`; read shared supervisor status when the exact live worker cap matters.

## Canonical repository quality gates

- `scripts/check_repository_quality.sh` is the fast repository-maintenance gate for English-only maintained text, Markdown UX, generated-artifact hygiene, shell/Python syntax, and `git diff --check`.
- `scripts/verify_migration_ready.sh` is the canonical migration-readiness gate and includes the strict host-test suite.
- `scripts/ci_build_variant.sh` is the canonical ESP-IDF entry point for `uart`, `i2c`, and `emulator` builds; GitHub CI uses the same commands.

## Build and hardware rules

Use the repository's pinned ESP-IDF v5.5.4 environment. The normal build flow is:

```sh
. ~/esp/esp-idf-v5.5.4/export.sh
idf.py set-target esp32c6
idf.py build
```

Before flash/monitor work, rediscover the actual serial device instead of assuming an old `/dev/cu.*` path. Hardware tests must use bounded monitor/capture windows and record the relevant Zigbee startup/join/rejoin evidence before claiming success.

Do not erase flash or deliberately destroy the persisted Zigbee network unless the requested test requires factory-new behavior or the user explicitly asks for it. Treat `erase-flash` as a destructive hardware action.

## Change discipline

- Keep all repository-maintained text in English, including README/docs, code comments, logs, console/user-facing strings, task metadata, and commit messages.
- Keep Markdown skimmable: one H1 per file, sentence-case headings, relative links for repository documents, language-tagged code fences, and no stale branch/current-state claims in active documentation.
- Keep changes scoped and avoid unrelated refactors.
- Preserve IEEE-first device identity and the bounded/non-blocking callback architecture unless the requested change intentionally revisits it. Callbacks may update bounded state and enqueue follow-up work, but must not wait indefinitely or perform long-running discovery/transport work.
- Respect the module ownership documented in `docs/ARCHITECTURE.md`; prefer extending a cohesive state/policy module over growing `zigbee_gateway.c` with unrelated responsibilities.
- Do not fabricate support for Zigbee clusters/attributes that the firmware does not actually interpret.
- Update README/docs when behavior, pairing/rejoin flow, supported reports, or hardware-test expectations materially change.
- Run the narrowest meaningful verification first, then broaden only when the changed integration boundary warrants it.
- Update `docs/CONTINUATION.md` after a major milestone or when the active branch, hardware state, current goal, or exact next step changes materially.

When the Local Agent task/control/resource/status/planner/binding contract changes, audit this file together with the canonical `MichalMatu/local-agent` documentation so future chats do not depend on remembered conversation context.

## Monorepo migration note

Stable C6 architectural invariants in this file should travel with the module. Standalone Local Agent repository binding, control-branch, queue and wake mechanics are operational metadata for `MichalMatu/esp32_c6_zigbee` and must be rewritten rather than copied verbatim when the module is absorbed into a different monorepo.
