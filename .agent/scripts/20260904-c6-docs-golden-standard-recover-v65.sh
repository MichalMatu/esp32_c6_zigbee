#!/usr/bin/env bash
set -euo pipefail

BRANCH='main'
BASE='067fa6f744f5e28233eb5b267edd7d6daa262c2c'
CHECKPOINT='/Users/michal/agent-workspace/repos/esp32-c6-zigbee/checkpoints/20260904-c6-docs-golden-standard-v63/1788555226972749000-task-exit'
ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

git fetch origin "$BRANCH"
git checkout -B "$BRANCH" "origin/$BRANCH"
test "$(git rev-parse HEAD)" = "$BASE"
git reset --hard "$BASE"
git clean -fd
git apply "$CHECKPOINT/tracked.patch"

BAD="$(git diff --name-only | grep -Ev '^(README\.md|AGENTS\.md|docs/.*\.md|tests/zigbee_device_emulator/README\.md)$' || true)"
if [[ -n "$BAD" ]]; then
  echo 'unexpected non-documentation changes:' >&2
  echo "$BAD" >&2
  exit 20
fi

git diff --check

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
ordered=re.compile(r'^\d+\.\s+')
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
        structural=(
            not line
            or line.startswith('#')
            or line.startswith('- ')
            or line.startswith('* ')
            or line.startswith('```')
            or line.startswith('|')
            or line.startswith('>')
            or bool(ordered.match(line))
        )
        if not structural:
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
cont=Path('docs/CONTINUATION.md').read_text()
for stale in ['active integration branch:', 'source work branch is `integration/c6-s3-i2c-20260903`']:
    if stale in cont: raise SystemExit('stale continuation state: '+stale)
print('LANGUAGE_AUDIT=PASS')
print('MARKDOWN_UX_AUDIT=PASS')
print('CONTINUATION_CURRENTNESS=PASS')
PY

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
echo FIRMWARE_RUNTIME_UNCHANGED=TRUE
