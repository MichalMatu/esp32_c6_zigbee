#!/usr/bin/env bash
set -euo pipefail

EXPECTED='5b45f31412c03c11aff603c7762fd6c3d7f68194'
BRANCH='main'
TAG='c6-litegraph-export-ready-2026-09-04'
ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

git fetch origin "$BRANCH" --tags
git checkout -B "$BRANCH" "origin/$BRANCH"
git reset --hard "origin/$BRANCH"
test "$(git rev-parse HEAD)" = "$EXPECTED"
test -z "$(git status --short)"

cat > .editorconfig <<'EOF'
root = true

[*]
charset = utf-8
end_of_line = lf
insert_final_newline = true
trim_trailing_whitespace = true

[*.{c,h}]
indent_style = space
indent_size = 4

[*.{json,yml,yaml}]
indent_style = space
indent_size = 2

[*.{md,sh,py}]
indent_style = space
indent_size = 2
EOF

cat > scripts/check_repository_quality.sh <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

echo '[quality] repository hygiene'
tracked_debris="$(git ls-files | grep -E '(^|/)(build/|managed_components/|sdkconfig$|sdkconfig\.old$|\.DS_Store$)' || true)"
if [[ -n "$tracked_debris" ]]; then
  echo "$tracked_debris" >&2
  exit 2
fi
git diff --check

echo '[quality] English-only maintained text + Markdown UX'
python3 - <<'PY'
from pathlib import Path
import re
import subprocess

files = subprocess.check_output(['git', 'ls-files'], text=True).splitlines()
text_ext = {'.md', '.txt', '.json', '.yml', '.yaml', '.c', '.h', '.sh', '.py'}

# Keep the detector itself English-only by storing Polish tokens as hex.
polish_word_hex = [
    '7a6f7374616c', '7a6f7374616c61', '7a6f7374616c6f', '6e616c657a79',
    '706f77696e69656e', '706f77696e6e61', '6d6f7a6e61', '6a657a656c69',
    '7370726177647a', '75727563686f6d', '7265706f7a79746f7269756d',
    '646f6b756d656e7461636a61', '757a796a', '757a797761', '77796d616761',
    '706f6e696577617a'
]
polish_words = [bytes.fromhex(x).decode('ascii') for x in polish_word_hex]
polish_word_re = re.compile(r'\b(?:' + '|'.join(map(re.escape, polish_words)) + r')\b', re.I)
polish_char_re = re.compile('[\u0105\u0107\u0119\u0142\u0144\u00f3\u015b\u017a\u017c\u0104\u0106\u0118\u0141\u0143\u00d3\u015a\u0179\u017b]')

language_issues = []
for name in files:
    p = Path(name)
    if p.suffix.lower() not in text_ext and p.name not in {'CMakeLists.txt', 'Kconfig.projbuild'}:
        continue
    try:
        text = p.read_text()
    except UnicodeDecodeError:
        continue
    for line_no, line in enumerate(text.splitlines(), 1):
        if polish_char_re.search(line) or polish_word_re.search(line):
            language_issues.append((name, line_no, line[:180]))

if language_issues:
    for issue in language_issues:
        print('LANGUAGE', *issue, sep=' | ')
    raise SystemExit('non-English repository-maintained text detected')

markdown_issues = []
md_files = [Path(x) for x in files if x.lower().endswith('.md')]
for p in md_files:
    lines = p.read_text().splitlines()
    h1 = 0
    previous_level = 0
    in_fence = False
    fence_start = 0
    paragraph = []
    paragraph_start = 1

    def flush_paragraph():
        nonlocal paragraph, paragraph_start
        if paragraph and len(' '.join(paragraph)) > 900:
            markdown_issues.append((str(p), paragraph_start, 'long-prose-paragraph>900chars'))
        paragraph = []

    for line_no, line in enumerate(lines, 1):
        if line.rstrip() != line:
            markdown_issues.append((str(p), line_no, 'trailing-whitespace'))
        if '\t' in line:
            markdown_issues.append((str(p), line_no, 'tab'))

        if line.startswith('```'):
            flush_paragraph()
            if not in_fence:
                in_fence = True
                fence_start = line_no
                if line.strip() == '```':
                    markdown_issues.append((str(p), line_no, 'code-fence-missing-language'))
            else:
                in_fence = False
            continue

        if in_fence:
            continue

        heading = re.match(r'^(#{1,6})\s+(.+)$', line)
        if heading:
            flush_paragraph()
            level = len(heading.group(1))
            h1 += level == 1
            if previous_level and level > previous_level + 1:
                markdown_issues.append((str(p), line_no, f'heading-jump-{previous_level}-to-{level}'))
            previous_level = level
            continue

        structural = (
            not line
            or line.startswith(('- ', '* ', '> ', '|'))
            or re.match(r'^\d+\.\s+', line) is not None
        )
        if structural:
            flush_paragraph()
        else:
            if not paragraph:
                paragraph_start = line_no
            paragraph.append(line)

        for target in re.findall(r'\[[^]]+\]\(([^)]+)\)', line):
            target_path = target.split('#', 1)[0]
            if not target_path or '://' in target_path or target_path.startswith('mailto:'):
                continue
            if not (p.parent / target_path).resolve().exists():
                markdown_issues.append((str(p), line_no, 'broken-relative-link:' + target))

    flush_paragraph()
    if in_fence:
        markdown_issues.append((str(p), fence_start, 'unclosed-code-fence'))
    if h1 != 1:
        markdown_issues.append((str(p), 1, f'h1-count-{h1}'))

if markdown_issues:
    for issue in markdown_issues:
        print('MARKDOWN', *issue, sep=' | ')
    raise SystemExit('Markdown UX audit failed')

readme = Path('README.md').read_text()
for heading in ('## Purpose', '## Project status', '## Documentation', '## Quick start: build and flash'):
    if heading not in readme:
        raise SystemExit('README missing required UX section: ' + heading)

print(f'[quality] language PASS; markdown_files={len(md_files)}')
PY

echo '[quality] shell/python syntax'
while IFS= read -r file; do bash -n "$file"; done < <(git ls-files '*.sh')
while IFS= read -r file; do python3 -m py_compile "$file"; done < <(git ls-files '*.py')
find . -type d -name __pycache__ -prune -exec rm -rf {} +

if command -v shellcheck >/dev/null 2>&1; then
  shellcheck scripts/*.sh
fi

echo '[quality] PASS'
EOF
chmod +x scripts/check_repository_quality.sh

cat > scripts/ci_build_variant.sh <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
variant="${1:-}"

case "$variant" in
  uart)
    rm -rf build sdkconfig sdkconfig.old
    idf.py set-target esp32c6
    idf.py build
    ;;
  i2c)
    rm -rf build sdkconfig sdkconfig.old
    idf.py set-target esp32c6
    python3 - <<'PY'
from pathlib import Path
p = Path('sdkconfig')
s = p.read_text()
old = 'CONFIG_GATEWAY_LINK_BACKEND_UART=y'
new = '# CONFIG_GATEWAY_LINK_BACKEND_UART is not set\nCONFIG_GATEWAY_LINK_BACKEND_I2C=y'
if old not in s:
    raise SystemExit('default UART GatewayLink backend not found in sdkconfig')
p.write_text(s.replace(old, new, 1))
PY
    idf.py reconfigure
    idf.py build
    ;;
  emulator)
    rm -rf tests/zigbee_device_emulator/build \
           tests/zigbee_device_emulator/sdkconfig \
           tests/zigbee_device_emulator/sdkconfig.old
    idf.py -C tests/zigbee_device_emulator set-target esp32c6
    idf.py -C tests/zigbee_device_emulator build
    ;;
  *)
    echo "usage: $0 {uart|i2c|emulator}" >&2
    exit 2
    ;;
esac
EOF
chmod +x scripts/ci_build_variant.sh

cat > .github/workflows/quality.yml <<'EOF'
name: Quality

on:
  push:
    branches: [main]
  pull_request:

permissions:
  contents: read

concurrency:
  group: quality-${{ github.workflow }}-${{ github.ref }}
  cancel-in-progress: true

jobs:
  repository-quality:
    runs-on: ubuntu-latest
    timeout-minutes: 10
    steps:
      - uses: actions/checkout@v4
      - name: Check repository quality
        run: ./scripts/check_repository_quality.sh

  migration-readiness:
    runs-on: ubuntu-latest
    timeout-minutes: 10
    steps:
      - uses: actions/checkout@v4
      - name: Verify migration readiness and host tests
        run: ./scripts/verify_migration_ready.sh

  firmware-uart:
    runs-on: ubuntu-latest
    timeout-minutes: 30
    steps:
      - uses: actions/checkout@v4
      - uses: espressif/esp-idf-ci-action@v1
        with:
          esp_idf_version: v5.5.4
          target: esp32c6
          command: ./scripts/ci_build_variant.sh uart

  firmware-i2c:
    runs-on: ubuntu-latest
    timeout-minutes: 30
    steps:
      - uses: actions/checkout@v4
      - uses: espressif/esp-idf-ci-action@v1
        with:
          esp_idf_version: v5.5.4
          target: esp32c6
          command: ./scripts/ci_build_variant.sh i2c

  emulator:
    runs-on: ubuntu-latest
    timeout-minutes: 30
    steps:
      - uses: actions/checkout@v4
      - uses: espressif/esp-idf-ci-action@v1
        with:
          esp_idf_version: v5.5.4
          target: esp32c6
          command: ./scripts/ci_build_variant.sh emulator
EOF

python3 - <<'PY'
from pathlib import Path

p = Path('README.md')
s = p.read_text()
if '## Repository verification' not in s:
    marker = '## First boot, pairing, and reboot\n'
    section = '''## Repository verification\n\nRun the fast repository-quality gate and the canonical migration/host-test gate before publishing changes:\n\n```sh\n./scripts/check_repository_quality.sh\n./scripts/verify_migration_ready.sh\n```\n\nWith the ESP-IDF v5.5.4 environment active, the same build entry points used by CI are:\n\n```sh\n./scripts/ci_build_variant.sh uart\n./scripts/ci_build_variant.sh i2c\n./scripts/ci_build_variant.sh emulator\n```\n\nGitHub Actions runs these as independent gates so a failure identifies whether repository hygiene, migration/host behavior, UART firmware, I2C firmware, or the second-C6 emulator regressed.\n\n'''
    if marker not in s:
        raise SystemExit('README insertion point not found')
    s = s.replace(marker, section + marker, 1)
p.write_text(s)

p = Path('AGENTS.md')
s = p.read_text()
needle = '## Build and hardware rules\n'
addition = '''## Canonical repository quality gates\n\n- `scripts/check_repository_quality.sh` is the fast repository-maintenance gate for English-only maintained text, Markdown UX, generated-artifact hygiene, shell/Python syntax, and `git diff --check`.\n- `scripts/verify_migration_ready.sh` is the canonical migration-readiness gate and includes the strict host-test suite.\n- `scripts/ci_build_variant.sh` is the canonical ESP-IDF entry point for `uart`, `i2c`, and `emulator` builds; GitHub CI uses the same commands.\n\n'''
if '## Canonical repository quality gates' not in s:
    if needle not in s:
        raise SystemExit('AGENTS insertion point not found')
    s = s.replace(needle, addition + needle, 1)
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
echo "$changed"
if echo "$changed" | grep -Ev '^(\.editorconfig|\.github/workflows/quality\.yml|AGENTS\.md|README\.md|scripts/(check_repository_quality|ci_build_variant)\.sh)$' | grep -q .; then
  echo 'unexpected path changed during repository hardening' >&2
  exit 11
fi

# Runtime/build inputs that existed before this task must remain unchanged.
if ! git diff --quiet "$EXPECTED" -- main CMakeLists.txt partitions.csv sdkconfig.defaults dependencies.lock tests/host tests/zigbee_device_emulator/main tests/zigbee_device_emulator/CMakeLists.txt tests/zigbee_device_emulator/partitions.csv tests/zigbee_device_emulator/sdkconfig.defaults; then
  echo 'firmware/runtime/test input changed unexpectedly' >&2
  git diff --stat "$EXPECTED" -- main CMakeLists.txt partitions.csv sdkconfig.defaults dependencies.lock tests/host tests/zigbee_device_emulator/main tests/zigbee_device_emulator/CMakeLists.txt tests/zigbee_device_emulator/partitions.csv tests/zigbee_device_emulator/sdkconfig.defaults >&2
  exit 12
fi

git add .editorconfig .github/workflows/quality.yml AGENTS.md README.md scripts/check_repository_quality.sh scripts/ci_build_variant.sh
git commit -m 'Harden repository quality gates'
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

test -z "$(git status --short)"
echo 'REPOSITORY_HARDENING=PASS'
echo "EXPORT_READY_HEAD=$FINAL_HEAD"
echo "EXPORT_READY_TAG=$TAG"
echo 'FIRMWARE_RUNTIME_UNCHANGED=TRUE'
