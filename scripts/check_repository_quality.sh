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


def flush_paragraph(path, paragraph, start_line):
    if paragraph and len(' '.join(paragraph)) > 900:
        markdown_issues.append((str(path), start_line, 'long-prose-paragraph>900chars'))
    paragraph.clear()


for p in md_files:
    lines = p.read_text().splitlines()
    h1 = 0
    previous_level = 0
    in_fence = False
    fence_start = 0
    paragraph = []
    paragraph_start = 1

    for line_no, line in enumerate(lines, 1):
        if line.rstrip() != line:
            markdown_issues.append((str(p), line_no, 'trailing-whitespace'))
        if '\t' in line:
            markdown_issues.append((str(p), line_no, 'tab'))

        if line.startswith('```'):
            flush_paragraph(p, paragraph, paragraph_start)
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
            flush_paragraph(p, paragraph, paragraph_start)
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
            flush_paragraph(p, paragraph, paragraph_start)
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

    flush_paragraph(p, paragraph, paragraph_start)
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
