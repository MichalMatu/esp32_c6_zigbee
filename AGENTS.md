# AGENTS.md

These instructions apply to the whole repository.

## Project role

This repository contains native ESP-IDF firmware for an ESP32-C6 Zigbee coordinator/gateway. Keep changes aligned with the current README contract unless the user explicitly asks to change that architecture.

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
- control branch: `agent-control`;
- default source branch: `main`.

Use this repository's own `agent-control` branch for Local Agent tasks and evidence. Never route ESP32-C6 Zigbee work through another repository's control branch.

Before queueing work:

1. Read this file and the current `README.md`.
2. Inspect the exact source branch/HEAD relevant to the request.
3. Inspect `.agent/status/daemon.json` on `agent-control` and treat `daemon_version`, `self_revision`, `execution_model`, `max_parallel_workers`, and current task state as the running truth.
4. Follow any active task instead of queueing a duplicate.

Task/evidence contract:

- queue immutable tasks under `.agent/tasks/<task-id>.json`;
- follow `.agent/runs/<task-id>.json`;
- read `.agent/results/<task-id>.json` before reporting completion;
- use a new unique task id for every intentional retry or changed payload;
- set `work_branch` explicitly when work must run anywhere other than `main`;
- a successful Local Agent result proves execution/verification, not remote source publication unless the task explicitly publishes source.

The canonical executor/planner contract lives in `MichalMatu/local-agent`, especially `docs/OPERATIONS.md`, `docs/AUTONOMOUS_CHAT_LOOP.md`, `docs/MULTI_REPOSITORY.md`, and `docs/GOLDEN_STANDARD.md`. Do not pin a Local Agent release number here; read live status and canonical `local-agent/main` instead.

## Resource classification

Resource classification is conservative.

Omit `resources` for:

- `idf.py build` or other full ESP-IDF/xtensa/riscv toolchain work unless a narrower safe contract has been explicitly established;
- USB/serial access;
- flash, erase-flash, monitor, OpenOCD/JTAG, or hardware interaction;
- Zigbee RF/hardware tests;
- any uncertain machine-wide tooling.

Omitting `resources` means full `machine` exclusivity.

Use `resources: []` only for clearly software-only lightweight work that is safe to overlap, such as documentation checks or isolated static/source inspection, with an enabled `memory_limit_mb <= 1024` (normally 256-512 MiB for lightweight checks).

One task executes at a time for this repository, while unrelated registered repositories may overlap when Local Agent resource admission permits it. Production normally uses `max_workers=2`; read the live status rather than relying on this sentence as runtime truth.

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

- Keep execution content, code, comments, logs, task metadata, and commit messages in English.
- Keep changes scoped and avoid unrelated refactors.
- Preserve the IEEE-first device identity and callback-copy/enqueue architecture unless the requested change intentionally revisits it.
- Do not fabricate support for Zigbee clusters/attributes that the firmware does not actually interpret.
- Update README/docs when behavior, pairing/rejoin flow, supported reports, or hardware-test expectations materially change.
- Run the narrowest meaningful verification first, then broaden only when the changed integration boundary warrants it.

When the Local Agent task/control/resource/status/planner contract changes, audit this file together with the canonical `MichalMatu/local-agent` documentation so future chats do not depend on remembered conversation context.
