#!/usr/bin/env bash
set -euo pipefail

REPO_DIR="/Users/michal/agent-workspace/repos/esp32-c6-zigbee"
BRANCH="integration/c6-s3-i2c-20260903"
EXPECTED_BASE="5801964144ccc8f825c4d4548daca4a1e526937c"
cd "$REPO_DIR"

git fetch origin "$BRANCH"
git checkout -B "$BRANCH" "origin/$BRANCH"
if [[ "$(git rev-parse HEAD)" != "$EXPECTED_BASE" ]]; then
  echo "unexpected base HEAD: $(git rev-parse HEAD) expected $EXPECTED_BASE" >&2
  exit 2
fi
# Remove only generated local debris from previous builds; never erase hardware/NVS.
git clean -fdx -e .agent/ >/dev/null 2>&1 || true

PROMPT=$(cat <<'EOF'
You are preparing the repository MichalMatu/esp32_c6_zigbee to be absorbed later as an ESP32-C6 Zigbee extension/module inside a larger LiteGraph ESP32-S3 monorepo. Work ONLY in this repository. Do not inspect, clone, search, or modify any LiteGraph/S3 repository. This is a pre-migration cleanup and packaging task, not the migration itself.

Hard technical invariants:
- ESP32-C6 native ESP-IDF, current verified environment ESP-IDF v5.5.4 and espressif/esp-zigbee-lib v2.0.4.
- Preserve GatewayLink v2 wire contract. v1 is historical only; do not add a v1 shim.
- Preserve IEEE-first Zigbee identity and mutable short-address routing.
- Preserve bounded/nonblocking callback/event architecture.
- Preserve UART backend as the verified fallback.
- Preserve C6 I2C-master backend: I2C0, SDA GPIO1, SCL GPIO0, planned S3 slave 0x42, SCD4x shared bus at 0x62, bounded missing-peer behavior.
- Do not change or erase Zigbee storage expectations.
- Do not introduce Arduino/Matter/Thread/Wi-Fi/BLE/external RCP responsibilities.
- Avoid gratuitous source moves/renames. The goal is import-readiness with low diff risk.

The repository was already structurally refactored and hardware-verified. Your job is to make the handoff into a future monorepo smooth and obvious.

Required work:
1. Audit the tracked tree for generated files, obsolete scratch/config artifacts, stale references, duplicated instructions, repository-specific assumptions, and migration hazards. Clean only evidence-backed clutter. Update .gitignore so normal ESP-IDF generated debris such as sdkconfig.old/build artifacts does not dirty the tree.
2. Keep the firmware independently buildable from its own module directory after it is nested under a larger repository. Audit CMake/scripts/tests for brittle assumptions about absolute paths, repository name, or being at filesystem root. Make small path-robustness fixes where justified, without changing runtime behavior.
3. Create a concise machine-readable module manifest at `module.json` containing at least: module id/name, target esp32c6, framework/verified ESP-IDF version, Zigbee library version, app entrypoint/project build root, GatewayLink protocol version, supported physical backends, verified/default backend, I2C wiring/address facts, canonical host test command, canonical UART/I2C build commands, emulator location/build command, current verified hardware identities, and migration destination suggestion such as `firmware/extensions/zigbee-c6/`. Do not invent external dependency versions.
4. Create `docs/LITEGRAPH_MIGRATION.md` as the single canonical absorption guide. It must clearly state:
   - what to copy into the monorepo and what NOT to copy (.git, .agent, old control-task evidence, standalone CI as-is, generated build/sdkconfig.old);
   - recommended destination layout;
   - which files/contracts should become shared later versus stay C6-private;
   - exact GatewayLink v2/I2C boundary that the future S3 side must implement;
   - migration sequence with no behavior changes first, then test/build, then preserved-NVS hardware smoke, then only afterwards S3 mailbox implementation;
   - rollback/recovery SHAs/tags;
   - a post-import verification checklist;
   - warning that actual C6↔S3 peer communication is still unverified, while C6 I2C with S3 absent/shared SCD41 bus is verified.
5. Create or update `docs/README.md` as a documentation index separating active contracts, verified evidence, migration docs, and historical GatewayLink v1.
6. Update README.md so the project is described as the ESP32-C6 Zigbee extension firmware intended for integration into the LiteGraph controller, while remaining independently buildable/testable. Make quick-start build/test commands current and reference the migration guide.
7. Update `docs/CONTINUATION.md` to make the active goal explicitly "freeze/export this verified C6 module into the LiteGraph monorepo" and remove wording that suggests more speculative C6 work should happen before migration. Preserve all useful hardware evidence/history.
8. Update AGENTS.md only if needed to distinguish stable C6 architectural invariants (which should travel with the module) from Local-Agent/repository-control instructions (which are standalone-repo operational metadata and should not be copied verbatim into the future monorepo).
9. Add a lightweight, path-robust `scripts/verify_migration_ready.sh` that can be run from anywhere, resolves repository/module root from its own location, rejects tracked/generated migration debris, validates required migration docs/manifest fields, runs existing canonical host tests, and performs non-destructive static/source checks. Do not make it flash hardware.
10. If a very small cohesive source/API cleanup materially improves module boundary clarity, you may do it, but prefer documentation/build/test hygiene over runtime churn. Do not redesign the already verified Zigbee gateway.
11. Do not add source copies of GatewayLink for S3. Document future sharing; one canonical wire contract is enough.
12. Run formatting/diff checks and ensure no generated build artifacts are staged.

Acceptance criteria before you finish:
- `git diff --check` passes.
- `./scripts/run_host_tests.sh` passes.
- `./scripts/verify_migration_ready.sh` passes.
- Clean default UART ESP-IDF build passes.
- Clean I2C-backend ESP-IDF build passes.
- Zigbee emulator build passes.
- repository has no unexpected tracked generated artifacts and final git status contains only intentional source/docs changes before commit.
- No hardware flash in this task.

Make the edits directly. Do not commit; the wrapper will validate and commit after your work.
EOF
)

codex exec --ignore-user-config -m gpt-5.6-sol -c model_reasoning_effort=high --approve-for-me "$PROMPT"

echo "=== DIFF CHECK ==="
git diff --check

echo "=== HOST TESTS ==="
./scripts/run_host_tests.sh

echo "=== MIGRATION READINESS CHECK ==="
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
rm -rf /tmp/c6-emu-build-v49 /tmp/c6-emu-sdkconfig-v49 /tmp/c6-emu-sdkconfig-v49.old
idf.py -C tools/zigbee_device_emulator -B /tmp/c6-emu-build-v49 -D SDKCONFIG=/tmp/c6-emu-sdkconfig-v49 set-target esp32c6 >/dev/null
idf.py -C tools/zigbee_device_emulator -B /tmp/c6-emu-build-v49 -D SDKCONFIG=/tmp/c6-emu-sdkconfig-v49 build

rm -rf build sdkconfig sdkconfig.old
./scripts/verify_migration_ready.sh
git diff --check

# Ensure generated debris is not part of the commit.
git status --short
if git status --short | grep -E '(^|/)(build/|sdkconfig\.old$|sdkconfig$)' >/dev/null; then
  echo "generated build/config debris remains" >&2
  exit 3
fi

git add -A
git diff --cached --check
if git diff --cached --quiet; then
  echo "no migration-readiness changes produced" >&2
  exit 4
fi

git commit -m "Prepare C6 module for LiteGraph migration"
git push origin HEAD:"$BRANCH"
FINAL_HEAD=$(git rev-parse HEAD)
echo "MIGRATION_READY_SOFTWARE_HEAD=$FINAL_HEAD"
git status --short
