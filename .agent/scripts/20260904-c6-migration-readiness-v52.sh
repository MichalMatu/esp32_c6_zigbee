#!/usr/bin/env bash
set -euo pipefail

BRANCH="integration/c6-s3-i2c-20260903"
EXPECTED_BASE="5801964144ccc8f825c4d4548daca4a1e526937c"
ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

git fetch origin "$BRANCH"
git checkout -B "$BRANCH" "origin/$BRANCH"
git reset --hard "origin/$BRANCH"
if [[ "$(git rev-parse HEAD)" != "$EXPECTED_BASE" ]]; then
  echo "unexpected base HEAD: $(git rev-parse HEAD) expected $EXPECTED_BASE" >&2
  exit 2
fi
if [[ -n "$(git status --short)" ]]; then
  echo "workspace not clean before migration prep" >&2
  git status --short
  exit 3
fi
rm -rf build sdkconfig sdkconfig.old

python3 - <<'PY'
from pathlib import Path
import json

root = Path.cwd()

# .gitignore: keep a nested ESP-IDF module clean after migration.
p = root / '.gitignore'
lines = [x for x in p.read_text().splitlines() if x.strip()]
for entry in ['/build/', '/managed_components/', '/sdkconfig', '/sdkconfig.old', '.DS_Store']:
    if entry not in lines:
        lines.append(entry)
p.write_text('\n'.join(lines) + '\n')

module = {
    "schema_version": 1,
    "module_id": "zigbee-c6",
    "name": "ESP32-C6 Zigbee extension firmware",
    "role": "LiteGraph controller Zigbee and deterministic low-level I/O extension",
    "target": "esp32c6",
    "framework": {"name": "ESP-IDF", "verified_version": "5.5.4"},
    "dependencies": {
        "espressif/esp-zigbee-lib": "2.0.4",
        "jef-sure/scd4x": "0.0.3"
    },
    "project": {"build_root": ".", "entrypoint": "main/app_main.c"},
    "gatewaylink": {
        "protocol_version": 2,
        "contract": "docs/GATEWAY_LINK_V2.md",
        "supported_backends": ["uart1", "i2c0-mailbox"],
        "default_backend": "uart1",
        "verified_fallback_backend": "uart1",
        "i2c": {
            "controller": "I2C0",
            "role": "master",
            "sda_gpio": 1,
            "scl_gpio": 0,
            "frequency_hz": 400000,
            "planned_s3_slave_address": "0x42",
            "shared_scd4x_address": "0x62",
            "transaction_timeout_ms": 20,
            "absent_peer_backoff_ms": 1000
        }
    },
    "verification": {
        "canonical_host_tests": "./scripts/run_host_tests.sh",
        "migration_readiness": "./scripts/verify_migration_ready.sh",
        "uart_build": ". $IDF_PATH/export.sh && idf.py set-target esp32c6 && idf.py build",
        "i2c_build": "select CONFIG_GATEWAY_LINK_BACKEND_I2C=y then idf.py build",
        "emulator_path": "tools/zigbee_device_emulator",
        "emulator_build": "idf.py -C tools/zigbee_device_emulator set-target esp32c6 && idf.py -C tools/zigbee_device_emulator build",
        "hardware_tested_source": "f13b293be2de6b1601d179568424e0046d6219a7",
        "hardware_identities": {
            "gateway_c6_usb_serial": "40:4C:CA:5D:0A:00",
            "emulator_c6_usb_serial": "40:4C:CA:5D:01:D8"
        }
    },
    "migration": {
        "suggested_destination": "firmware/extensions/zigbee-c6/",
        "guide": "docs/LITEGRAPH_MIGRATION.md"
    }
}
(root / 'module.json').write_text(json.dumps(module, indent=2) + '\n')

(root / 'docs' / 'LITEGRAPH_MIGRATION.md').write_text(r'''# LiteGraph absorption guide — ESP32-C6 Zigbee extension

## Purpose

This repository is prepared to move as one independently buildable ESP32-C6 firmware module into the LiteGraph controller monorepo. The migration itself must be behavior-preserving: first move the verified C6 module unchanged, prove it still builds/tests/runs from the nested location, and only then implement the S3 side of GatewayLink I2C.

Recommended destination:

```text
firmware/extensions/zigbee-c6/
```

The C6 remains a separate ESP-IDF firmware image. Monorepo integration means shared context, contracts, CI and release orchestration; it does not mean merging the C6 and S3 binaries.

## Copy into the monorepo

Copy the tracked firmware/module content needed for an independent C6 build:

- root `CMakeLists.txt`, `dependencies.lock`, `main/`, `partitions.csv` and project configuration sources;
- `tests/`, `scripts/`, and `tools/zigbee_device_emulator/`;
- `module.json`;
- active documentation: `docs/ARCHITECTURE.md`, `docs/GATEWAY_LINK_V2.md`, `docs/VERIFIED_BASELINE.md`, this guide and the documentation index;
- README content useful for the C6 extension module.

Keep `docs/GATEWAY_LINK_V1.md` only as historical/recovery documentation if retaining repository history is useful.

## Do not copy as module content

Do not import standalone-repository operational debris:

- `.git/`;
- `.agent/` Local Agent task/run/control evidence;
- generated `build/`, `managed_components/`, `sdkconfig`, `sdkconfig.old`;
- host-specific temporary files;
- the standalone `.github/workflows/quality.yml` verbatim. Recreate equivalent monorepo CI at the monorepo level and call the module's canonical scripts instead.

Standalone `AGENTS.md` contains both architectural rules and repository-control/Local-Agent rules. Carry forward the architectural invariants, but rewrite operational agent instructions for the monorepo rather than copying the current binding/control-branch rules verbatim.

## What stays C6-private

Keep these responsibilities inside the C6 module:

- ESP Zigbee coordinator lifecycle, commissioning and storage handling;
- IEEE-first device identity and mutable short-address routing;
- discovery/work queues and bounded Zigbee SDK callbacks;
- ZCL/IAS handling, binding/reporting and outbound Zigbee commands;
- local wired input adapters such as SCD4x;
- GatewayLink runtime/backends on the C6 side;
- C6 console, emulator and C6-specific host tests.

Do not move Wi-Fi, BLE, web/UI, LiteGraph/application registry, Matter, Thread or external-RCP responsibilities onto C6 during migration.

## Contract that may become shared later

GatewayLink v2 is the only active wire contract. `docs/GATEWAY_LINK_V2.md` and the v2 message/framing definitions are the canonical boundary. During the initial import, keep the existing C6 implementation exactly where it is. After the monorepo import is proven, shared pure protocol definitions/tests may be extracted to a neutral monorepo location only if both C6 and S3 can consume one canonical implementation without target-specific coupling.

Do not create a second hand-maintained S3 copy of the protocol and do not add a v1 compatibility shim.

## Exact future C6 ↔ S3 I2C boundary

C6 side already implemented and verified with S3 absent:

- C6 is I2C master on I2C0;
- SDA GPIO1, SCL GPIO0;
- 400 kHz;
- SCD4x shares the bus at `0x62`;
- planned S3 slave address `0x42`;
- C6 transaction timeout 20 ms;
- absent-peer backoff about 1 s;
- mailbox operations carry one complete encoded GatewayLink v2 frame:
  - `0x01 WRITE_FRAME`: opcode + LE16 length + encoded frame;
  - `0x02 PENDING_LENGTH`: obtain LE16 pending S3→C6 length;
  - `0x03 READ_FRAME`: read exactly that encoded frame.

The future S3 implementation must implement the complementary slave/mailbox behavior and the same GatewayLink v2 semantics. Physical C6↔S3 peer traffic has **not** yet been verified. What is verified is the C6 I2C backend with the S3 intentionally absent while Zigbee and the shared SCD41 continue operating.

## Migration sequence

1. Freeze the source/export checkpoint from this repository and record its SHA.
2. Copy this module into `firmware/extensions/zigbee-c6/` without runtime source redesign.
3. Make only path/build-system changes required by nesting.
4. From the nested module run `./scripts/run_host_tests.sh` and `./scripts/verify_migration_ready.sh`.
5. Build the C6 UART-default firmware with ESP-IDF v5.5.4.
6. Build the C6 I2C-backend configuration.
7. Build `tools/zigbee_device_emulator`.
8. Flash both C6 boards without erasing persisted Zigbee storage and run the preserved-storage UART smoke/regression.
9. Confirm the imported firmware still matches the verified behavior before any S3-side protocol work.
10. Only then implement the S3 I2C slave/mailbox in the monorepo.
11. Perform true C6↔S3 GatewayLink I2C validation: HELLO/ACK and `peer=1`, C6→S3 measurements, S3→C6 control, SCD4x shared-bus stability, S3 disconnect/backoff and reconnect recovery without C6 NVS erase.
12. Keep UART as fallback until the real S3 I2C gate and a bounded soak pass.

## Recovery points

Important immutable or hardware-proven checkpoints:

- `c6-sonoff-stable-2026-09-02` → `0d64fb03164d3bcb9f5cddd639977b4027bc581f`;
- `c6-gatewaylink-stable-2026-09-03` → `a4b1f629c1286d631ac208515b71aeeaa7c44b23`;
- dual-C6 IAS/rejoin source checkpoint `109a01f32d3bbc5c2ce2799ccbc8946a717b0e7a`;
- post-refactor firmware physically tested on both C6 boards: `f13b293be2de6b1601d179568424e0046d6219a7`;
- documentation closure before migration preparation: `5801964144ccc8f825c4d4548daca4a1e526937c`.

Do not move the historical stable tags.

## Post-import verification checklist

- [ ] module builds from its nested directory, not only repository root;
- [ ] `module.json` remains valid and paths are adjusted if the monorepo location changes;
- [ ] canonical host tests pass;
- [ ] migration-readiness script passes;
- [ ] UART-default ESP-IDF build passes;
- [ ] I2C-backend ESP-IDF build passes;
- [ ] emulator build passes;
- [ ] no generated `build/`, `sdkconfig` or `sdkconfig.old` is committed;
- [ ] GatewayLink protocol version remains 2;
- [ ] UART remains available as fallback;
- [ ] preserved Zigbee storage is not erased during migration validation;
- [ ] gateway C6 identity resolves to `40:4C:CA:5D:0A:00` before hardware access;
- [ ] emulator C6 identity resolves to `40:4C:CA:5D:01:D8` before hardware access;
- [ ] preserved-rejoin IAS contact false/true smoke passes after import;
- [ ] no claim is made that C6↔S3 I2C is verified until a real S3 slave passes the end-to-end gate.
''')

(root / 'docs' / 'README.md').write_text(r'''# Documentation index

## Active contracts

- [`ARCHITECTURE.md`](ARCHITECTURE.md) — C6 module ownership, runtime boundaries and architectural invariants.
- [`GATEWAY_LINK_V2.md`](GATEWAY_LINK_V2.md) — active C6↔S3 wire contract. New integrations use v2 only.
- [`LITEGRAPH_MIGRATION.md`](LITEGRAPH_MIGRATION.md) — canonical guide for absorbing this firmware into the LiteGraph monorepo.

## Verification and continuation

- [`VERIFIED_BASELINE.md`](VERIFIED_BASELINE.md) — frozen physical hardware evidence and recovery checkpoints.
- [`AUDIT_2026-09-04.md`](AUDIT_2026-09-04.md) — pre-S3 structural audit and post-refactor hardware closure.
- [`CONTINUATION.md`](CONTINUATION.md) — mutable current-work handoff; use repository evidence before historical chat context.

## Historical contract

- [`GATEWAY_LINK_V1.md`](GATEWAY_LINK_V1.md) — historical recovery documentation only. It is not a compatibility target for LiteGraph/S3 and no v1 shim should be added.
''')

(root / 'scripts' / 'verify_migration_ready.sh').write_text(r'''#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

echo "[migration] root=$ROOT"
python3 - <<'PY'
from pathlib import Path
import json
root = Path.cwd()
required = [
    'module.json', 'docs/LITEGRAPH_MIGRATION.md', 'docs/README.md',
    'docs/GATEWAY_LINK_V2.md', 'docs/ARCHITECTURE.md',
    'docs/VERIFIED_BASELINE.md', 'scripts/run_host_tests.sh'
]
missing = [p for p in required if not (root / p).exists()]
if missing:
    raise SystemExit('missing migration files: ' + ', '.join(missing))
m = json.loads((root / 'module.json').read_text())
assert m['module_id'] == 'zigbee-c6'
assert m['target'] == 'esp32c6'
assert m['framework']['verified_version'] == '5.5.4'
assert m['dependencies']['espressif/esp-zigbee-lib'] == '2.0.4'
assert m['gatewaylink']['protocol_version'] == 2
assert m['gatewaylink']['default_backend'] == 'uart1'
assert 'i2c0-mailbox' in m['gatewaylink']['supported_backends']
assert m['gatewaylink']['i2c']['sda_gpio'] == 1
assert m['gatewaylink']['i2c']['scl_gpio'] == 0
assert m['gatewaylink']['i2c']['planned_s3_slave_address'] == '0x42'
assert m['gatewaylink']['i2c']['shared_scd4x_address'] == '0x62'
assert m['migration']['suggested_destination'] == 'firmware/extensions/zigbee-c6/'
print('[migration] manifest PASS')
PY

tracked_debris="$(git ls-files | grep -E '(^|/)(build/|sdkconfig$|sdkconfig\.old$|managed_components/)' || true)"
if [[ -n "$tracked_debris" ]]; then
  echo "tracked generated debris:" >&2
  echo "$tracked_debris" >&2
  exit 2
fi

git diff --check
./scripts/run_host_tests.sh
python3 tests/host/test_no_embedded_nul.py

echo "[migration] readiness PASS"
''')
(root / 'scripts' / 'verify_migration_ready.sh').chmod(0o755)

# Refresh README introduction without disturbing the detailed operational reference below.
p = root / 'README.md'
s = p.read_text()
marker = '## Verified stable baseline\n'
idx = s.index(marker)
intro = '''# ESP32-C6 Zigbee extension firmware\n\nNative ESP-IDF firmware for the ESP32-C6 Zigbee/low-level-I/O extension of the LiteGraph controller. It remains independently buildable, flashable and host-testable, but is now packaged for later absorption into the LiteGraph monorepo under the suggested path `firmware/extensions/zigbee-c6/`.\n\nThe verified toolchain is **ESP-IDF v5.5.4** with **`espressif/esp-zigbee-lib` v2.0.4**. The active C6↔S3 contract is **GatewayLink v2**; UART remains the verified fallback and the C6 I2C-master mailbox backend is ready for a future S3 slave. Physical C6↔S3 I2C communication is not yet claimed as verified.\n\n## Migration-ready module\n\nMachine-readable integration facts live in [`module.json`](module.json). The canonical absorption procedure is [`docs/LITEGRAPH_MIGRATION.md`](docs/LITEGRAPH_MIGRATION.md), and the documentation index is [`docs/README.md`](docs/README.md).\n\nCanonical non-hardware verification:\n\n```sh\n./scripts/run_host_tests.sh\n./scripts/verify_migration_ready.sh\n```\n\nThe migration is intentionally staged: import this verified C6 firmware without behavior changes, rerun tests/builds and a preserved-NVS hardware smoke, then implement the S3 mailbox side.\n\n'''
p.write_text(intro + s[idx:])

# Make the active continuation goal explicit without deleting historical evidence.
p = root / 'docs' / 'CONTINUATION.md'
s = p.read_text()
anchor = '## START HERE — new ChatGPT window handoff (2026-09-04)\n'
insert = '''## Current migration goal — freeze/export into LiteGraph monorepo\n\nThe active C6 goal is now to export this verified firmware as an independently buildable module for absorption into the LiteGraph controller monorepo. Do not invent additional C6 feature work before that import. The initial migration must preserve runtime behavior, GatewayLink v2, UART fallback, Zigbee storage and the hardware-proven baselines.\n\nUse `module.json` and `docs/LITEGRAPH_MIGRATION.md` as the canonical import package/sequence. The actual LiteGraph/S3 repository is outside this repository binding and must not be inspected or modified from this context. The post-refactor firmware source physically tested on both C6 boards remains `f13b293be2de6b1601d179568424e0046d6219a7`; `5801964144ccc8f825c4d4548daca4a1e526937c` recorded that evidence before migration packaging.\n\nAfter the module is imported and its preserved-NVS C6 smoke passes from the nested location, the next cross-device milestone is the real S3 I2C slave/mailbox and physical C6↔S3 GatewayLink v2 validation.\n\n'''
if insert not in s:
    s = s.replace(anchor, insert + anchor)
p.write_text(s)

# Clarify what part of AGENTS.md should travel to a monorepo.
p = root / 'AGENTS.md'
s = p.read_text()
note = '''\n## Migration / monorepo handoff boundary\n\nThis file mixes stable C6 engineering invariants with standalone-repository Local Agent operations. When the firmware is absorbed into the LiteGraph monorepo, carry forward the C6 architecture, safety, GatewayLink v2, Zigbee identity/storage and hardware-resource rules. Do **not** copy standalone repository binding, control-branch, queue/run/task mechanics verbatim; replace those with the destination monorepo's own agent instructions. See `docs/LITEGRAPH_MIGRATION.md`.\n'''
if '## Migration / monorepo handoff boundary' not in s:
    p.write_text(s.rstrip() + '\n' + note)
PY

chmod +x scripts/run_host_tests.sh scripts/verify_migration_ready.sh

echo "=== DIFF CHECK ==="
git diff --check

echo "=== HOST TESTS ==="
./scripts/run_host_tests.sh

echo "=== MIGRATION READINESS ==="
./scripts/verify_migration_ready.sh

source /Users/michal/esp/esp-idf/export.sh >/dev/null

echo "=== UART BUILD ==="
rm -rf build sdkconfig sdkconfig.old
idf.py set-target esp32c6 >/dev/null
idf.py build

echo "=== I2C BUILD ==="
python3 - <<'PY'
from pathlib import Path
p=Path('sdkconfig')
s=p.read_text()
s=s.replace('CONFIG_GATEWAY_LINK_BACKEND_UART=y', '# CONFIG_GATEWAY_LINK_BACKEND_UART is not set')
if 'CONFIG_GATEWAY_LINK_BACKEND_I2C=y' not in s:
    s += '\nCONFIG_GATEWAY_LINK_BACKEND_I2C=y\n'
p.write_text(s)
PY
idf.py build

echo "=== EMULATOR BUILD ==="
rm -rf /tmp/c6-emu-build-v52 /tmp/c6-emu-sdkconfig-v52 /tmp/c6-emu-sdkconfig-v52.old
idf.py -C tools/zigbee_device_emulator -B /tmp/c6-emu-build-v52 -D SDKCONFIG=/tmp/c6-emu-sdkconfig-v52 set-target esp32c6 >/dev/null
idf.py -C tools/zigbee_device_emulator -B /tmp/c6-emu-build-v52 -D SDKCONFIG=/tmp/c6-emu-sdkconfig-v52 build

rm -rf build sdkconfig sdkconfig.old
./scripts/verify_migration_ready.sh
git diff --check

if git status --short | grep -E '(^|/)(build/|sdkconfig\.old$|sdkconfig$|managed_components/)' >/dev/null; then
  echo "generated build/config debris remains" >&2
  git status --short
  exit 4
fi

git add -A
git diff --cached --check
if git diff --cached --quiet; then
  echo "no migration-readiness changes produced" >&2
  exit 5
fi

git commit -m "Prepare C6 module for LiteGraph migration"
git push origin HEAD:"$BRANCH"
FINAL_HEAD="$(git rev-parse HEAD)"
echo "MIGRATION_READY_SOFTWARE_HEAD=$FINAL_HEAD"
if [[ -n "$(git status --short)" ]]; then
  echo "workspace dirty after commit" >&2
  git status --short
  exit 6
fi
