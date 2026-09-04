#!/usr/bin/env bash
set -euo pipefail

BRANCH='main'
BASE='067fa6f744f5e28233eb5b267edd7d6daa262c2c'
ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

git fetch origin "$BRANCH"
git checkout -B "$BRANCH" "origin/$BRANCH"
test "$(git rev-parse HEAD)" = "$BASE"
test -z "$(git status --short)"

python3 - <<'PY'
from pathlib import Path

# README: make the first screen explain purpose, status, ownership and navigation.
p = Path('README.md')
s = p.read_text()
marker = '## Verified stable baseline\n'
if marker not in s:
    raise SystemExit('README verified-baseline marker missing')
_, rest = s.split(marker, 1)
intro = '''# ESP32-C6 Zigbee and low-level I/O extension

Native ESP-IDF firmware for the ESP32-C6 hardware extension used by the LiteGraph controller architecture.

## Purpose

This firmware exists to move deterministic, hardware-facing work away from the ESP32-S3 application processor. The C6 owns Zigbee coordination and local low-level I/O, converts device-specific data into a normalized source-neutral model, and exposes that model to the S3 through GatewayLink.

The separation keeps Zigbee timing, device discovery, reporting, local sensor drivers, and bounded hardware queues out of the application layer. The ESP32-S3 can therefore focus on Wi-Fi, BLE, web/UI, configuration, and LiteGraph automation without needing to understand Zigbee clusters or local C6 driver details.

## Project status

| Item | Value |
| --- | --- |
| Target | ESP32-C6 |
| Framework | ESP-IDF v5.5.4 |
| Zigbee stack | `espressif/esp-zigbee-lib` v2.0.4 |
| Active MCU protocol | GatewayLink v2 |
| Default GatewayLink backend | UART1 |
| Alternate backend | C6-master I2C0 mailbox |
| Migration state | Ready for import into the LiteGraph monorepo |
| Migration-ready tag | `c6-litegraph-migration-ready-2026-09-04` |
| Hardware-proven runtime source | `5ce963d6ee3b03b9b788f9d02bd9acb4910acead` |

The repository remains independently buildable and testable. The C6 is still a separate firmware image after monorepo import; migration combines source management and contracts, not the C6 and S3 binaries.

## Responsibility split

**ESP32-C6 owns:**

- Zigbee coordinator lifecycle, persisted Zigbee state, discovery, binding, reporting, IAS handling, and normalized commands;
- stable IEEE-first Zigbee identities and route-safe short-address handling;
- board-local deterministic I/O such as the SCD4x sensor;
- normalized input capabilities, availability, measurements, and low-level command translation;
- GatewayLink runtime and its UART/I2C physical backends.

**ESP32-S3 owns:**

- Wi-Fi and BLE;
- web/UI and user configuration;
- LiteGraph automation/application logic;
- the application-facing input registry and the complementary GatewayLink peer.

The C6 intentionally does not add Arduino, Matter, Thread, Wi-Fi, BLE, MQTT, or an external RCP.

## Documentation

Start with these documents:

1. [Architecture](docs/ARCHITECTURE.md) — ownership, module boundaries, and invariants.
2. [GatewayLink v2](docs/GATEWAY_LINK_V2.md) — active C6↔S3 protocol contract.
3. [LiteGraph migration guide](docs/LITEGRAPH_MIGRATION.md) — exact import sequence and post-import gates.
4. [Verified baseline](docs/VERIFIED_BASELINE.md) — physical hardware evidence and recovery points.
5. [Continuation handoff](docs/CONTINUATION.md) — concise current state and next action.
6. [Documentation index](docs/README.md) — complete active/historical documentation map.

## Technical baseline

The firmware is pinned to **ESP-IDF v5.5.4** and **`espressif/esp-zigbee-lib` v2.0.4**. The generated [dependencies.lock](dependencies.lock) is committed to retain that exact dependency resolution.

This is SDK 2.x firmware using the `esp_zigbee_*` and `ezb_*` APIs with one `ezb_af_create_gateway_endpoint()` gateway endpoint. Zigbee is one input adapter; an SCD4x-family sensor is supported as an independent local I2C input adapter.

'''
s = intro + marker + rest
s = s.replace('## Build and flash\n', '## Quick start: build and flash\n', 1)
# Improve scanability of the longest prose blocks without changing semantics.
s = s.replace(
    'The current protocol-neutral C6-to-S3 development contract is specified in [docs/GATEWAY_LINK_V2.md](docs/GATEWAY_LINK_V2.md). GatewayLink v2 uses bounded binary COBS frames with CRC32 and carries stable input identity, normalized capability access profiles, descriptors, normalized measurements and versioned controls. The frozen v1 contract remains documented for the `c6-gatewaylink-stable-2026-09-03` recovery point; the active branch has no v1 compatibility shim because there is no deployed S3 peer to migrate. UART1 runs at 460800 baud on TX GPIO18 / RX GPIO19. TX is bounded/non-blocking to event handling; RX resynchronizes at COBS delimiters and currently handles HELLO/ACK, PING/PONG and `PERMIT_JOIN`. Descriptor snapshot/resync is implemented with a bounded transport cache and TX-task replay; source-neutral measurement-policy application remains disabled until its real backing policy layer is implemented.',
    'The current protocol-neutral C6-to-S3 development contract is specified in [docs/GATEWAY_LINK_V2.md](docs/GATEWAY_LINK_V2.md). GatewayLink v2 uses bounded binary COBS frames with CRC32 and carries stable input identity, normalized capability access profiles, descriptors, normalized measurements, and versioned controls. The frozen v1 contract remains documented for the `c6-gatewaylink-stable-2026-09-03` recovery point; the active firmware has no v1 compatibility shim because there is no deployed S3 peer to migrate.\n\nUART1 runs at 460800 baud on TX GPIO18 / RX GPIO19. TX is bounded/non-blocking to event handling; RX resynchronizes at COBS delimiters and currently handles HELLO/ACK, PING/PONG, and `PERMIT_JOIN`. Descriptor snapshot/resync is implemented with a bounded transport cache and TX-task replay; source-neutral measurement-policy application remains disabled until its real backing policy layer is implemented.'
)
s = s.replace(
    'After a successful Simple Descriptor, an endpoint with at least one actually normalized server cluster also publishes a protocol-neutral `INPUT_AVAILABLE` descriptor. Its v2 capability profile separates readable normalized values from reportable/configurable values and commandable state, so the future S3 does not need raw Zigbee cluster knowledge. The descriptor also carries bounded Basic manufacturer/model metadata when known and is emitted only after an authoritative IEEE identity is known; provisional short-address identities are never exposed on GatewayLink. A known RESET leave and a known REJOIN leave publish `INPUT_UNAVAILABLE` for previously announced endpoints; the generic descriptor is re-announced after rediscovery. `DEVICE_UNAVAILABLE` and unknown leave/update signals remain non-authoritative and do not fabricate generic offline state.',
    'After a successful Simple Descriptor, an endpoint with at least one actually normalized server cluster also publishes a protocol-neutral `INPUT_AVAILABLE` descriptor. Its v2 capability profile separates readable normalized values from reportable/configurable values and commandable state, so the future S3 does not need raw Zigbee cluster knowledge. The descriptor also carries bounded Basic manufacturer/model metadata when known and is emitted only after an authoritative IEEE identity is known; provisional short-address identities are never exposed on GatewayLink.\n\nA known RESET leave and a known REJOIN leave publish `INPUT_UNAVAILABLE` for previously announced endpoints; the generic descriptor is re-announced after rediscovery. `DEVICE_UNAVAILABLE` and unknown leave/update signals remain non-authoritative and do not fabricate generic offline state.'
)
s = s.replace(
    'For a sleepy end device, discovery creates standard ZDO bindings for its temperature, humidity, battery, and Poll Control clusters before configuring reports. The gateway also handles the standard Poll Control Check-In command. Its reply explicitly requests a **five-second** fast-poll window (`20` quarter-seconds; the similarly named SDK predefined macro is documented in milliseconds and is intentionally not used). It queues fresh endpoint discovery and retries each failed/lock-delayed operation up to three times outside Zigbee callbacks. Binding/reporting state is tracked per IEEE identity, endpoint, and cluster; it is marked configured only after a successful response. An unconfirmed request becomes retryable after ten seconds, preventing immediate duplicate commands during a burst of rejoin/check-in events. Thus a normal wake-up/button press is sufficient after a coordinator reboot; removing the battery or re-pairing is not required.',
    'For a sleepy end device, discovery creates standard ZDO bindings for its temperature, humidity, battery, and Poll Control clusters before configuring reports. The gateway also handles the standard Poll Control Check-In command. Its reply explicitly requests a **five-second** fast-poll window (`20` quarter-seconds; the similarly named SDK predefined macro is documented in milliseconds and is intentionally not used).\n\nThe gateway queues fresh endpoint discovery and retries each failed/lock-delayed operation up to three times outside Zigbee callbacks. Binding/reporting state is tracked per IEEE identity, endpoint, and cluster; it is marked configured only after a successful response. An unconfirmed request becomes retryable after ten seconds, preventing immediate duplicate commands during a burst of rejoin/check-in events. Thus a normal wake-up/button press is sufficient after a coordinator reboot; removing the battery or re-pairing is not required.'
)
p.write_text(s.rstrip() + '\n')

# Documentation index: clickable, ordered, and explicit about status.
Path('docs/README.md').write_text('''# Documentation

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
''')

# Continuation handoff: replace stale chronological notes with a current, compact handoff.
Path('docs/CONTINUATION.md').write_text('''# C6 Zigbee continuation handoff

> **Status:** C6-side work is frozen and ready for LiteGraph monorepo import. Do not start speculative C6 refactors before migration.

This file is the concise current-state handoff. Historical implementation details and physical evidence live in [Verified baseline](VERIFIED_BASELINE.md), [Pre-S3 repository audit](AUDIT_2026-09-04.md), and [Final critical re-audit](FINAL_REAUDIT_2026-09-04.md).

## Start here

Read these documents in order when resuming work:

1. [Architecture](ARCHITECTURE.md)
2. [GatewayLink v2](GATEWAY_LINK_V2.md)
3. [LiteGraph migration guide](LITEGRAPH_MIGRATION.md)
4. [Verified baseline](VERIFIED_BASELINE.md)
5. [Final critical re-audit](FINAL_REAUDIT_2026-09-04.md)

## Current repository state

- repository: `MichalMatu/esp32_c6_zigbee`
- source branch: `main`
- Local Agent control branch: `agent-control`
- migration-ready tag: `c6-litegraph-migration-ready-2026-09-04`
- migration-ready freeze commit: `067fa6f744f5e28233eb5b267edd7d6daa262c2c`
- exact hardware-proven runtime source: `5ce963d6ee3b03b9b788f9d02bd9acb4910acead`
- active protocol: GatewayLink v2 only
- default physical backend: UART1
- selectable C6-side alternate backend: I2C0 mailbox

`main` may contain documentation-only cleanup commits after the migration-ready tag. The runtime/build/test inputs must remain identical to the hardware-proven runtime source until the module is imported and revalidated in the LiteGraph monorepo.

## What this firmware is

The ESP32-C6 is the deterministic hardware and field-I/O extension for the LiteGraph controller architecture. It owns Zigbee and local low-level I/O, normalizes source-specific data into a common input/capability/measurement model, and exposes that model to the ESP32-S3 through GatewayLink.

The ESP32-S3 remains the application processor. It owns Wi-Fi, BLE, web/UI, configuration, LiteGraph automation, and the application-facing input registry.

## C6 responsibilities

- Zigbee coordinator lifecycle and persisted Zigbee/NVS state;
- IEEE-first device identity and route-safe short-address handling;
- bounded discovery, ZDO work, binding, reporting, Poll Control, and IAS handling;
- normalized measurement and command translation;
- local I2C input adapters such as SCD4x;
- GatewayLink v2 runtime, snapshot/resync, and physical backends;
- C6-specific console, host tests, and two-C6 emulator tests.

Do not move Wi-Fi, BLE, web/UI, LiteGraph application logic, Matter, Thread, MQTT, or external-RCP responsibilities onto the C6 during migration.

## Verified software state

The final software re-audit passed:

- repository/source hygiene;
- manifest/document/source-contract consistency;
- active GatewayLink v2-only checks;
- shell syntax and `shellcheck`;
- C portability/warning scan with `cppcheck`;
- canonical strict host tests;
- migration-readiness verification;
- UART-default ESP-IDF build;
- I2C-backend ESP-IDF build;
- Zigbee device-emulator ESP-IDF build.

See [Final critical re-audit](FINAL_REAUDIT_2026-09-04.md) for the exact evidence.

## Verified hardware state

The exact runtime source `5ce963d6ee3b03b9b788f9d02bd9acb4910acead` passed the final two-C6 hardware gate without erasing persisted Zigbee storage.

Verified behaviors include:

- preserved-storage UART rejoin and device announce;
- IAS CIE write, enrollment, and contact false/true reporting;
- no gateway or emulator panic/watchdog failure;
- no gateway-event queue drop;
- C6 I2C0 backend selected with the S3 intentionally absent;
- expected missing-peer state (`peer=0`) with bounded retry behavior;
- SCD4x CO2/temperature/humidity remaining healthy on the shared I2C bus;
- Zigbee contact traffic remaining healthy while the I2C backend is selected;
- no GatewayLink event/link queue drops;
- successful final restoration to the UART1 fallback.

Hardware identities:

- gateway/coordinator C6: `40:4C:CA:5D:0A:00`
- emulator C6: `40:4C:CA:5D:01:D8`

Always rediscover the live serial ports from these identities before hardware access.

## Deliberately unverified

Do **not** claim any of the following until a real S3 peer has been implemented and physically tested:

- physical C6↔S3 I2C traffic;
- S3 mailbox/slave implementation at address `0x42`;
- GatewayLink v2 HELLO/ACK with `peer=1` over physical I2C;
- C6→S3 measurement delivery over the physical I2C link;
- S3→C6 normalized command/control over the physical I2C link;
- disconnect/reconnect recovery with a real S3 peer;
- post-integration two-MCU soak stability.

## Next action

The next implementation step belongs in the LiteGraph/S3 repository context, not in this standalone C6 repository:

1. Import this C6 module unchanged into `firmware/extensions/zigbee-c6/`.
2. Re-run the nested host/readiness/UART/I2C/emulator gates.
3. Re-run the preserved-NVS two-C6 smoke after import.
4. Implement the S3 I2C slave/mailbox at `0x42`.
5. Run the first true C6↔S3 GatewayLink v2 hardware E2E gate.
6. Keep UART as fallback until the physical I2C E2E and bounded soak pass.

Follow [LiteGraph migration guide](LITEGRAPH_MIGRATION.md) for the canonical sequence.

## Local Agent boundary

For this standalone repository only:

- Local Agent repository id: `esp32-c6-zigbee`
- agent binding: `64877d7d-af3f-4312-a511-699c44aa42dd`
- control branch: `agent-control`
- source branch: `main`
- hardware resource: `board:zigbee-c6`

Before queueing a task, require fresh `agent-control:.agent/status/daemon.json` evidence for this exact repository/binding and `state=idle`. Every executable Local Agent task must contain exactly the bound `agent_binding`. Never route work through another repository's control branch.

These Local Agent details are operational metadata for the standalone repository. Rewrite them when the module is absorbed into a different monorepo; do not copy them verbatim as architecture.

## Recovery points

- `c6-litegraph-migration-ready-2026-09-04` — final migration-ready repository freeze
- `c6-gatewaylink-stable-2026-09-03` — pre-v2-integration GatewayLink recovery point
- `c6-sonoff-stable-2026-09-02` — verified SONOFF Zigbee baseline

Do not move historical tags. Create a new tag after any future independently verified runtime change.

## Maintenance rule

Keep this handoff short and current. Do not append chronological task logs here. Put durable hardware evidence in `VERIFIED_BASELINE.md`, architecture rules in `ARCHITECTURE.md`, migration instructions in `LITEGRAPH_MIGRATION.md`, and dated audit reports in their dedicated files.
''')

# Explicit English + Markdown-maintenance rules for future edits.
p = Path('AGENTS.md')
s = p.read_text()
s = s.replace(
    '- Keep execution content, code, comments, logs, task metadata, and commit messages in English.\n',
    '- Keep all repository-maintained text in English, including README/docs, code comments, logs, console/user-facing strings, task metadata, and commit messages.\n- Keep Markdown skimmable: one H1 per file, sentence-case headings, relative links for repository documents, language-tagged code fences, and no stale branch/current-state claims in active documentation.\n'
)
s = s.replace('\n\n\n## Monorepo migration note\n', '\n\n## Monorepo migration note\n')
p.write_text(s.rstrip() + '\n')

# Architecture wording: remove duplication and describe the transport that now exists.
p = Path('docs/ARCHITECTURE.md')
s = p.read_text()
s = s.replace(
    'The event bus is the internal transport boundary. Input adapters normalize measurements before publishing them. GatewayLink is the external MCU boundary and serializes only the normalized contract. Input adapters normalize measurements before publishing them. `gateway_transport` must consume `gateway_input_id_t` plus normalized measurements without branching on Zigbee cluster IDs or local sensor register formats. A later UART/SPI link to another MCU should serialize this normalized input contract; the ESP32-S3 can then own the current input list/state used by LiteGraph.',
    'The event bus is the internal transport boundary. Input adapters normalize measurements before publishing them. GatewayLink is the external MCU boundary and serializes only the normalized contract. `gateway_transport` must consume `gateway_input_id_t` plus normalized measurements without branching on Zigbee cluster IDs or local sensor register formats. GatewayLink carries this normalized input contract to the ESP32-S3, which owns the current application-facing input list/state used by LiteGraph.'
)
s = s.replace(
    'The future UART/SPI transport to the ESP32-S3 must serialize input identity, availability/capabilities, and normalized measurements;',
    'GatewayLink transport to the ESP32-S3 must serialize input identity, availability/capabilities, and normalized measurements;'
)
s = s.replace('\n\n\n## Normalized capability access profile\n', '\n\n## Normalized capability access profile\n')
s = s.replace('\n\n\nLevel Control follows', '\n\nLevel Control follows')
p.write_text(s.rstrip() + '\n')

# Make protocol status unmistakable without changing historical wire semantics.
p = Path('docs/GATEWAY_LINK_V1.md')
s = p.read_text()
notice = '> **Status: Historical.** GatewayLink v1 is frozen for recovery only. New C6↔S3 integration uses [GatewayLink v2](GATEWAY_LINK_V2.md). Do not add a v1 compatibility shim unless an explicit migration requirement appears.\n\n'
if notice not in s:
    s = s.replace('# GatewayLink v1\n\n', '# GatewayLink v1\n\n' + notice, 1)
s = s.replace('\n The C6 Zigbee adapter enforces this rule:', '\n\nThe C6 Zigbee adapter enforces this rule:')
p.write_text(s.rstrip() + '\n')

p = Path('docs/GATEWAY_LINK_V2.md')
s = p.read_text()
notice = '> **Status: Active.** GatewayLink v2 is the only protocol contract for new C6↔S3 integration.\n\n'
if notice not in s:
    s = s.replace('# GatewayLink v2\n\n', '# GatewayLink v2\n\n' + notice, 1)
s = s.replace('GatewayLink v2 is the current development contract', 'GatewayLink v2 is the active protocol contract', 1)
p.write_text(s.rstrip() + '\n')

# Historical baseline must not look like a live branch instruction after branch cleanup.
p = Path('docs/VERIFIED_BASELINE.md')
s = p.read_text()
old = '''### Next integration branch

Further C6-side work for physical S3 integration starts from:

`integration/c6-s3-i2c-20260903`

That branch is created after this verification note is committed, while the immutable firmware recovery tag remains on `a4b1f629c1286d631ac208515b71aeeaa7c44b23`.

The next bench milestone is physical C6↔S3 I²C validation, including coexistence with the existing local SCD4x on the shared C6 I²C bus and recovery when the S3 peer is absent or unpowered. S3-side implementation belongs in its own correctly bound repository context.
'''
new = '''### Historical integration branch

Physical-S3 preparation originally continued on `integration/c6-s3-i2c-20260903` after this baseline was recorded. That branch was later fast-forwarded into `main` and deleted during the final repository cleanup. Its history remains reachable from `main` and the immutable recovery tags.

The remaining physical milestone is C6↔S3 I²C validation with a real S3 slave/mailbox peer. That work belongs after the C6 module is imported into the LiteGraph monorepo; UART remains the verified fallback until the cross-MCU gate passes.
'''
if old not in s:
    raise SystemExit('VERIFIED_BASELINE historical branch block not found')
s = s.replace(old, new, 1)
p.write_text(s.rstrip() + '\n')

# Dated audit: formatting only.
p = Path('docs/AUDIT_2026-09-04.md')
s = p.read_text().replace('\n\n\n## Completion status\n', '\n\n## Completion status\n')
p.write_text(s.rstrip() + '\n')

# Emulator README: add an immediate build path.
p = Path('tests/zigbee_device_emulator/README.md')
s = p.read_text()
block = '''## Build

From the repository root:

```sh
. ~/esp/esp-idf-v5.5.4/export.sh
idf.py -C tests/zigbee_device_emulator set-target esp32c6
idf.py -C tests/zigbee_device_emulator build
```

Use `idf.py -C tests/zigbee_device_emulator menuconfig` to select a profile before building when the default mixed profile is not desired.

'''
if '## Build\n' not in s:
    s = s.replace('This directory is an independent ESP-IDF application for a distinct second ESP32-C6. It does not link or include production `main/` sources.\n\n', 'This directory is an independent ESP-IDF application for a distinct second ESP32-C6. It does not link or include production `main/` sources.\n\n' + block, 1)
p.write_text(s.rstrip() + '\n')
PY

# Only documentation/instructions may change.
BAD="$(git diff --name-only | grep -Ev '^(README\.md|AGENTS\.md|docs/.*\.md|tests/zigbee_device_emulator/README\.md)$' || true)"
if [[ -n "$BAD" ]]; then
  echo 'unexpected non-documentation changes:' >&2
  echo "$BAD" >&2
  exit 20
fi

git diff --check

# Language + Markdown UX audit.
python3 - <<'PY'
from pathlib import Path
import re, subprocess
files=subprocess.check_output(['git','ls-files'], text=True).splitlines()
md=[Path(x) for x in files if x.lower().endswith('.md')]
text_ext={'.md','.txt','.json','.yml','.yaml','.c','.h','.sh','.py'}
polish_chars=re.compile(r'[ąćęłńóśźżĄĆĘŁŃÓŚŹŻ]')
polish_words=re.compile(r'\b(zostal|zostala|zostalo|nalezy|powinien|powinna|mozna|jezeli|sprawdz|uruchom|repozytorium|dokumentacja|uzyj|uzywa|wymaga|poniewaz)\b', re.I)
lang=[]
for f in map(Path,files):
    if f.suffix.lower() not in text_ext and f.name not in {'CMakeLists.txt','Kconfig.projbuild'}: continue
    try:s=f.read_text()
    except UnicodeDecodeError:continue
    for n,line in enumerate(s.splitlines(),1):
        if polish_chars.search(line) or polish_words.search(line): lang.append((str(f),n,line))
if lang:
    for x in lang: print('LANG',x)
    raise SystemExit('non-English language suspect found')
issues=[]
for f in md:
    lines=f.read_text().splitlines(); fence=False; h1=0; prev=0
    for i,line in enumerate(lines,1):
        if line.rstrip()!=line: issues.append((str(f),i,'trailing-whitespace'))
        if '\t' in line: issues.append((str(f),i,'tab'))
        m=re.match(r'^(#{1,6})\s+(.+)$',line)
        if m and not fence:
            lev=len(m.group(1)); h1 += lev==1
            if prev and lev>prev+1: issues.append((str(f),i,'heading-jump'))
            prev=lev
        if line.startswith('```'):
            if not fence and line.strip()=='```': issues.append((str(f),i,'code-fence-no-language'))
            fence=not fence
    if fence: issues.append((str(f),1,'unclosed-code-fence'))
    if h1!=1: issues.append((str(f),1,f'h1-count-{h1}'))
    for i,line in enumerate(lines,1):
        for target in re.findall(r'\[[^]]+\]\(([^)]+)\)',line):
            t=target.split('#',1)[0]
            if not t or '://' in t or t.startswith('mailto:'): continue
            if not (f.parent/t).resolve().exists(): issues.append((str(f),i,'broken-relative-link:'+target))
    para=[]; start=0
    for i,line in enumerate(lines+[''],1):
        if line and not line.startswith('#') and not line.startswith('- ') and not line.startswith('```') and not line.startswith('|') and not line.startswith('>'):
            if not para:start=i
            para.append(line)
        else:
            if para and len(' '.join(para))>900: issues.append((str(f),start,'long-paragraph>900chars'))
            para=[]
if issues:
    for x in issues: print('MD',x)
    raise SystemExit('Markdown UX audit failed')
readme=Path('README.md').read_text().lower()
for required in ['## purpose','## project status','## responsibility split','## documentation','## quick start: build and flash']:
    if required not in readme: raise SystemExit('README missing '+required)
print('LANGUAGE_AUDIT=PASS')
print('MARKDOWN_UX_AUDIT=PASS')
PY

# Canonical repository readiness must still pass after documentation-only edits.
./scripts/verify_migration_ready.sh

git diff --check

git add README.md AGENTS.md docs/ARCHITECTURE.md docs/AUDIT_2026-09-04.md docs/CONTINUATION.md docs/GATEWAY_LINK_V1.md docs/GATEWAY_LINK_V2.md docs/README.md docs/VERIFIED_BASELINE.md tests/zigbee_device_emulator/README.md
git diff --cached --check
git commit -m 'Polish English documentation UX'
NEW_HEAD="$(git rev-parse HEAD)"
git push origin HEAD:main

test -z "$(git status --short)"
echo DOCS_GOLDEN_STANDARD=PASS
echo DOCS_HEAD="$NEW_HEAD"
echo RUNTIME_BASE_UNCHANGED="$BASE"
