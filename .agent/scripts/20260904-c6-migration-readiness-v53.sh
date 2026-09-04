#!/usr/bin/env bash
set -euo pipefail

BRANCH="integration/c6-s3-i2c-20260903"
EXPECTED_BASE="5801964144ccc8f825c4d4548daca4a1e526937c"
ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

git fetch origin "$BRANCH"
git checkout -B "$BRANCH" "origin/$BRANCH"
git reset --hard "origin/$BRANCH"
[[ "$(git rev-parse HEAD)" == "$EXPECTED_BASE" ]] || { echo "unexpected base" >&2; exit 2; }
[[ -z "$(git status --short)" ]] || { git status --short; exit 3; }
rm -rf build sdkconfig sdkconfig.old managed_components

for entry in '/build/' '/managed_components/' '/sdkconfig' '/sdkconfig.old' '.DS_Store'; do
  grep -Fxq "$entry" .gitignore || printf '%s\n' "$entry" >> .gitignore
done

cat > module.json <<'EOF'
{
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
    "uart_build": "idf.py set-target esp32c6 && idf.py build",
    "i2c_build": "CONFIG_GATEWAY_LINK_BACKEND_I2C=y then idf.py build",
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
EOF

cat > docs/README.md <<'EOF'
# Documentation index

## Active contracts
- `ARCHITECTURE.md` — C6 ownership and architectural invariants.
- `GATEWAY_LINK_V2.md` — active C6↔S3 wire contract; new integration uses v2 only.
- `LITEGRAPH_MIGRATION.md` — canonical absorption procedure for the LiteGraph monorepo.

## Verification and handoff
- `VERIFIED_BASELINE.md` — physical hardware evidence and recovery checkpoints.
- `AUDIT_2026-09-04.md` — structural audit and post-refactor hardware closure.
- `CONTINUATION.md` — mutable current-work handoff.

## Historical
- `GATEWAY_LINK_V1.md` — recovery/history only; not a compatibility target and no v1 shim should be added.
EOF

cat > docs/LITEGRAPH_MIGRATION.md <<'EOF'
# LiteGraph absorption guide — ESP32-C6 Zigbee extension

## Goal
Move this repository into the LiteGraph controller as one independently buildable ESP32-C6 firmware module, without changing runtime behavior during the move. Recommended destination: `firmware/extensions/zigbee-c6/`.

The C6 remains a separate firmware image. Monorepo integration provides one source tree, shared contracts, common CI/release orchestration and one development context; it does not merge C6 and S3 binaries.

## Copy
Copy the tracked C6 build inputs and module content: root ESP-IDF project files, `main/`, `tests/`, `scripts/`, `tools/zigbee_device_emulator/`, `module.json`, active docs, and any partition/configuration source required by the standalone build.

## Do not copy as module content
Do not import `.git/`, `.agent/`, Local Agent task/run evidence, generated `build/`, `managed_components/`, `sdkconfig`, `sdkconfig.old`, host temporary files, or the standalone GitHub Actions workflow verbatim. Recreate monorepo CI and call the canonical module scripts from there.

`AGENTS.md` mixes stable architecture with standalone-repository Local Agent rules. Carry forward the architectural invariants, but rewrite repository/binding/control-branch instructions for the future monorepo instead of copying them verbatim.

## C6-private responsibilities
Keep Zigbee coordinator lifecycle/storage, IEEE-first identity, short-address routing, bounded discovery/work scheduling, ZCL/IAS handling, reporting/binding, Zigbee commands, local wired inputs such as SCD4x, C6 GatewayLink runtime/backends, console, emulator and C6-specific tests inside the C6 module.

Do not move Wi-Fi, BLE, web/UI, LiteGraph application logic, Matter, Thread or external-RCP responsibilities onto C6 during migration.

## Shared contract later
GatewayLink v2 is the only active contract. During the initial import, keep its existing C6 implementation unchanged. After the nested module passes all gates, pure protocol definitions/tests may be extracted to a neutral shared monorepo location only if both processors consume one canonical implementation. Do not create two hand-maintained protocol copies and do not add a v1 compatibility shim.

## Future I2C peer boundary
The C6 side is already implemented:
- C6 master, I2C0;
- SDA GPIO1, SCL GPIO0, 400 kHz;
- SCD4x shares the bus at `0x62`;
- planned S3 slave address `0x42`;
- 20 ms transaction timeout and about 1 s absent-peer backoff;
- mailbox carries complete encoded GatewayLink v2 frames;
- opcodes: `0x01 WRITE_FRAME`, `0x02 PENDING_LENGTH`, `0x03 READ_FRAME`.

The future S3 side must implement the complementary slave/mailbox plus GatewayLink v2 semantics. True physical C6↔S3 I2C communication is still unverified. Verified today: C6 I2C backend with S3 absent while Zigbee and the shared SCD41 remain healthy.

## Migration sequence
1. Record the export checkpoint SHA from this repository.
2. Copy the module to `firmware/extensions/zigbee-c6/` with no runtime redesign.
3. Make only nesting/path/build-system adjustments.
4. Run `./scripts/run_host_tests.sh` and `./scripts/verify_migration_ready.sh` from the nested module.
5. Build UART-default C6 firmware with ESP-IDF v5.5.4.
6. Build the I2C-backend configuration.
7. Build `tools/zigbee_device_emulator`.
8. Flash both C6 boards without erasing persisted Zigbee storage and rerun the preserved-storage UART regression.
9. Only after the imported C6 behavior is proven, implement the S3 I2C slave/mailbox.
10. Validate real C6↔S3 traffic: HELLO/ACK + `peer=1`, C6→S3 measurements, S3→C6 control, shared-bus SCD4x stability, disconnect/backoff and reconnect recovery without C6 NVS erase.
11. Keep UART as fallback until real I2C E2E plus bounded soak pass.

## Recovery checkpoints
- `c6-sonoff-stable-2026-09-02` → `0d64fb03164d3bcb9f5cddd639977b4027bc581f`
- `c6-gatewaylink-stable-2026-09-03` → `a4b1f629c1286d631ac208515b71aeeaa7c44b23`
- dual-C6 IAS/rejoin source → `109a01f32d3bbc5c2ce2799ccbc8946a717b0e7a`
- post-refactor physically tested firmware → `f13b293be2de6b1601d179568424e0046d6219a7`
- pre-migration docs closure → `5801964144ccc8f825c4d4548daca4a1e526937c`

Do not move historical stable tags.

## Post-import checklist
- [ ] nested module host tests pass
- [ ] `verify_migration_ready.sh` passes from outside the module directory
- [ ] UART build passes
- [ ] I2C build passes
- [ ] emulator build passes
- [ ] generated build/sdkconfig files are not committed
- [ ] GatewayLink remains v2
- [ ] UART fallback remains available
- [ ] no Zigbee NVS erase during migration validation
- [ ] gateway identity resolves as `40:4C:CA:5D:0A:00`
- [ ] emulator identity resolves as `40:4C:CA:5D:01:D8`
- [ ] preserved-rejoin IAS false/true regression passes after import
- [ ] no claim of C6↔S3 I2C verification until a real S3 slave passes E2E
EOF

cat > scripts/verify_migration_ready.sh <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
echo "[migration] root=$ROOT"
python3 - <<'PY'
from pathlib import Path
import json
r=Path.cwd()
required=['module.json','docs/LITEGRAPH_MIGRATION.md','docs/README.md','docs/GATEWAY_LINK_V2.md','docs/ARCHITECTURE.md','docs/VERIFIED_BASELINE.md','scripts/run_host_tests.sh']
missing=[p for p in required if not (r/p).exists()]
if missing: raise SystemExit('missing: '+', '.join(missing))
m=json.loads((r/'module.json').read_text())
assert m['module_id']=='zigbee-c6'
assert m['target']=='esp32c6'
assert m['framework']['verified_version']=='5.5.4'
assert m['dependencies']['espressif/esp-zigbee-lib']=='2.0.4'
assert m['gatewaylink']['protocol_version']==2
assert m['gatewaylink']['default_backend']=='uart1'
assert 'i2c0-mailbox' in m['gatewaylink']['supported_backends']
assert m['gatewaylink']['i2c']['sda_gpio']==1 and m['gatewaylink']['i2c']['scl_gpio']==0
assert m['gatewaylink']['i2c']['planned_s3_slave_address']=='0x42'
assert m['gatewaylink']['i2c']['shared_scd4x_address']=='0x62'
assert m['migration']['suggested_destination']=='firmware/extensions/zigbee-c6/'
print('[migration] manifest PASS')
PY
tracked="$(git ls-files | grep -E '(^|/)(build/|managed_components/|sdkconfig$|sdkconfig\.old$)' || true)"
[[ -z "$tracked" ]] || { echo "$tracked" >&2; exit 2; }
git diff --check
./scripts/run_host_tests.sh
echo "[migration] readiness PASS"
EOF
chmod +x scripts/verify_migration_ready.sh

python3 - <<'PY'
from pathlib import Path
p=Path('README.md'); s=p.read_text()
if '## Migration-ready module' not in s:
    note=("\n## Migration-ready module\n\nThis firmware is now packaged as the ESP32-C6 Zigbee/low-level-I/O extension intended for later absorption into the LiteGraph controller monorepo, while remaining independently buildable and testable. See `module.json`, `docs/README.md`, and `docs/LITEGRAPH_MIGRATION.md`. The migration must preserve GatewayLink v2 and UART fallback; true C6↔S3 I2C traffic remains a future gate.\n")
    anchor='## Verified stable baseline\n'
    s=s.replace(anchor,note+'\n'+anchor,1)
p.write_text(s)

p=Path('docs/CONTINUATION.md'); s=p.read_text()
if '## Migration/export status — 2026-09-04' not in s:
    anchor='## 2026-09-04 pre-S3 structural audit\n'
    note=("## Migration/export status — 2026-09-04\n\nThe active C6 goal is now to freeze/export this verified firmware as a nested LiteGraph monorepo module. Do not invent additional speculative C6 feature work before migration. Preserve the physically tested firmware behavior, GatewayLink v2, UART fallback, Zigbee storage, and the C6 I2C missing-S3/shared-SCD41 gate. The canonical absorption procedure is `docs/LITEGRAPH_MIGRATION.md`; actual edits to the LiteGraph/S3 repository require that repository's own bound context.\n\n")
    s=s.replace(anchor,note+anchor,1)
p.write_text(s)

p=Path('AGENTS.md'); s=p.read_text()
if '## Monorepo migration note' not in s:
    s += ("\n\n## Monorepo migration note\n\nStable C6 architectural invariants in this file should travel with the module. Standalone Local Agent repository binding, control-branch, queue and wake mechanics are operational metadata for `MichalMatu/esp32_c6_zigbee` and must be rewritten rather than copied verbatim when the module is absorbed into a different monorepo.\n")
p.write_text(s)
PY

./scripts/verify_migration_ready.sh
source /Users/michal/esp/esp-idf/export.sh >/dev/null

rm -rf build sdkconfig sdkconfig.old
idf.py set-target esp32c6 >/dev/null
idf.py build >/tmp/c6_v53_uart_build.log

echo "[build] UART PASS"
python3 - <<'PY'
from pathlib import Path
p=Path('sdkconfig'); s=p.read_text()
s=s.replace('CONFIG_GATEWAY_LINK_BACKEND_UART=y','# CONFIG_GATEWAY_LINK_BACKEND_UART is not set')
if 'CONFIG_GATEWAY_LINK_BACKEND_I2C=y' not in s: s += '\nCONFIG_GATEWAY_LINK_BACKEND_I2C=y\n'
p.write_text(s)
PY
idf.py build >/tmp/c6_v53_i2c_build.log
echo "[build] I2C PASS"

rm -rf /tmp/c6-emu-v53 /tmp/c6-emu-v53-sdkconfig /tmp/c6-emu-v53-sdkconfig.old
idf.py -C tools/zigbee_device_emulator -B /tmp/c6-emu-v53 -D SDKCONFIG=/tmp/c6-emu-v53-sdkconfig set-target esp32c6 >/dev/null
idf.py -C tools/zigbee_device_emulator -B /tmp/c6-emu-v53 -D SDKCONFIG=/tmp/c6-emu-v53-sdkconfig build >/tmp/c6_v53_emu_build.log
echo "[build] EMULATOR PASS"

rm -rf build sdkconfig sdkconfig.old managed_components
./scripts/verify_migration_ready.sh
git diff --check
if git status --short | grep -E '(^|/)(build/|managed_components/|sdkconfig$|sdkconfig\.old$)' >/dev/null; then
  echo "generated debris remains" >&2; git status --short; exit 4
fi

git add -A
git diff --cached --check
[[ -n "$(git diff --cached --name-only)" ]] || { echo "no changes" >&2; exit 5; }
git commit -m "Prepare C6 module for LiteGraph migration"
git push origin HEAD:"$BRANCH"
echo "MIGRATION_READY_SOFTWARE_HEAD=$(git rev-parse HEAD)"
git status --short
