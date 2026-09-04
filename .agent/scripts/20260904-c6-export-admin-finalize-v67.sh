#!/usr/bin/env bash
set -euo pipefail

EXPECTED='6803d32543b7821f16e9f26c4559964a0f0de51d'
BRANCH='main'
TAG='c6-litegraph-export-ready-2026-09-04'
REPO='MichalMatu/esp32_c6_zigbee'
ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

git fetch origin "$BRANCH" --tags
git checkout -B "$BRANCH" "origin/$BRANCH"
git reset --hard "origin/$BRANCH"
test "$(git rev-parse HEAD)" = "$EXPECTED"
test -z "$(git status --short)"

python3 - <<'PY'
from pathlib import Path

p = Path('README.md')
s = p.read_text()
row = '| Export-ready tag | `c6-litegraph-export-ready-2026-09-04` |\n'
anchor = '| Migration-ready tag | `c6-litegraph-migration-ready-2026-09-04` |\n'
if row not in s:
    if anchor not in s:
        raise SystemExit('README project-status insertion point not found')
    s = s.replace(anchor, anchor + row, 1)

if '## Repository verification' not in s:
    marker = '## First boot, pairing, and reboot\n'
    section = '''## Repository verification\n\nRun the fast repository-quality gate and the canonical migration/host-test gate before publishing changes:\n\n```sh\n./scripts/check_repository_quality.sh\n./scripts/verify_migration_ready.sh\n```\n\nWith the ESP-IDF v5.5.4 environment active, the same build entry points used by CI are:\n\n```sh\n./scripts/ci_build_variant.sh uart\n./scripts/ci_build_variant.sh i2c\n./scripts/ci_build_variant.sh emulator\n```\n\nGitHub Actions runs these as independent gates so a failure identifies whether repository hygiene, migration/host behavior, UART firmware, I2C firmware, or the second-C6 emulator regressed.\n\n'''
    if marker not in s:
        raise SystemExit('README verification insertion point not found')
    s = s.replace(marker, section + marker, 1)
p.write_text(s)

p = Path('AGENTS.md')
s = p.read_text()
if '## Canonical repository quality gates' not in s:
    marker = '## Build and hardware rules\n'
    section = '''## Canonical repository quality gates\n\n- `scripts/check_repository_quality.sh` is the fast repository-maintenance gate for English-only maintained text, Markdown UX, generated-artifact hygiene, shell/Python syntax, and `git diff --check`.\n- `scripts/verify_migration_ready.sh` is the canonical migration-readiness gate and includes the strict host-test suite.\n- `scripts/ci_build_variant.sh` is the canonical ESP-IDF entry point for `uart`, `i2c`, and `emulator` builds; GitHub CI uses the same commands.\n\n'''
    if marker not in s:
        raise SystemExit('AGENTS quality-gates insertion point not found')
    s = s.replace(marker, section + marker, 1)
p.write_text(s)
PY

./scripts/check_repository_quality.sh
./scripts/verify_migration_ready.sh

if [[ -f "$HOME/esp/esp-idf-v5.5.4/export.sh" ]]; then
  . "$HOME/esp/esp-idf-v5.5.4/export.sh" >/dev/null
elif [[ -f "$HOME/esp/esp-idf/export.sh" ]]; then
  . "$HOME/esp/esp-idf/export.sh" >/dev/null
else
  echo 'ESP-IDF export.sh not found' >&2
  exit 10
fi

./scripts/ci_build_variant.sh uart >/dev/null
echo 'UART_BUILD=PASS'
./scripts/ci_build_variant.sh i2c >/dev/null
echo 'I2C_BUILD=PASS'
./scripts/ci_build_variant.sh emulator >/dev/null
echo 'EMULATOR_BUILD=PASS'

rm -rf build sdkconfig sdkconfig.old \
       tests/zigbee_device_emulator/build \
       tests/zigbee_device_emulator/sdkconfig \
       tests/zigbee_device_emulator/sdkconfig.old
find . -type d -name __pycache__ -prune -exec rm -rf {} +
git checkout -- dependencies.lock tests/zigbee_device_emulator/dependencies.lock 2>/dev/null || true

./scripts/check_repository_quality.sh
./scripts/verify_migration_ready.sh
git diff --check

changed="$(git diff --name-only)"
printf '%s\n' "$changed"
if printf '%s\n' "$changed" | grep -Ev '^(README\.md|AGENTS\.md)$' | grep -q .; then
  echo 'unexpected path changed during final export documentation pass' >&2
  exit 11
fi

if ! git diff --quiet "$EXPECTED" -- main CMakeLists.txt partitions.csv sdkconfig.defaults dependencies.lock tests/host tests/zigbee_device_emulator/main tests/zigbee_device_emulator/CMakeLists.txt tests/zigbee_device_emulator/partitions.csv tests/zigbee_device_emulator/sdkconfig.defaults; then
  echo 'firmware/runtime/test input changed unexpectedly' >&2
  exit 12
fi

git add README.md AGENTS.md
git commit -m 'Document repository verification gates'
git push origin HEAD:"$BRANCH"
FINAL_HEAD="$(git rev-parse HEAD)"

if git ls-remote --exit-code --tags origin "refs/tags/$TAG" >/dev/null 2>&1; then
  remote_tag="$(git ls-remote --tags origin "refs/tags/$TAG^{}" | awk '{print $1}')"
  [[ -n "$remote_tag" ]] || remote_tag="$(git ls-remote --tags origin "refs/tags/$TAG" | awk '{print $1}')"
  test "$remote_tag" = "$FINAL_HEAD"
else
  git tag -a "$TAG" -m 'C6 LiteGraph export-ready repository freeze' "$FINAL_HEAD"
  git push origin "$TAG"
fi

echo "EXPORT_READY_HEAD=$FINAL_HEAD"
echo "EXPORT_READY_TAG=$TAG"
echo 'FIRMWARE_RUNTIME_UNCHANGED=TRUE'

if ! command -v gh >/dev/null 2>&1; then
  echo 'GH_ADMIN=UNAVAILABLE: gh command not installed'
  exit 20
fi
if ! gh auth status -h github.com >/dev/null 2>&1; then
  echo 'GH_ADMIN=UNAVAILABLE: gh is not authenticated'
  exit 21
fi

gh api --method PATCH "repos/$REPO" \
  -f description='ESP32-C6 Zigbee and deterministic low-level I/O extension for a LiteGraph-based ESP32-S3 controller.' \
  -F has_wiki=false \
  -F delete_branch_on_merge=true \
  -F allow_merge_commit=false \
  -F allow_squash_merge=true \
  -F allow_rebase_merge=true \
  -F allow_update_branch=true >/dev/null

gh api --method PUT -H 'Accept: application/vnd.github+json' "repos/$REPO/topics" --input - >/dev/null <<'JSON'
{
  "names": [
    "esp32-c6",
    "esp-idf",
    "zigbee",
    "iot",
    "embedded",
    "gateway",
    "litegraph",
    "i2c"
  ]
}
JSON

gh api --method PUT -H 'Accept: application/vnd.github+json' "repos/$REPO/branches/main/protection" --input - >/dev/null <<'JSON'
{
  "required_status_checks": {
    "strict": true,
    "contexts": [
      "repository-quality",
      "migration-readiness",
      "firmware-uart",
      "firmware-i2c",
      "emulator"
    ]
  },
  "enforce_admins": false,
  "required_pull_request_reviews": null,
  "restrictions": null,
  "required_linear_history": true,
  "allow_force_pushes": false,
  "allow_deletions": false,
  "block_creations": false,
  "required_conversation_resolution": true,
  "lock_branch": false,
  "allow_fork_syncing": false
}
JSON

echo 'GITHUB_METADATA=PASS'
echo 'GITHUB_TOPICS=PASS'
echo 'MAIN_PROTECTION=PASS'
echo 'REPOSITORY_EXPORT_FINALIZE=PASS'
