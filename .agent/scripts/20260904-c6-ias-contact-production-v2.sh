#!/usr/bin/env bash
set -euo pipefail

git fetch origin agent-control
git show origin/agent-control:.agent/scripts/20260904-c6-ias-contact-production-v1.sh > /tmp/c6-ias-contact-production-v2-inner.sh
python3 - <<'PY'
from pathlib import Path
p=Path('/tmp/c6-ias-contact-production-v2-inner.sh')
s=p.read_text()
old="""python3 - <<'PY'\nfrom pathlib import Path\nbad=[]\nfor root in ['main','tests','docs']:\n    for p in Path(root).rglob('*'):\n        if p.is_file() and b'\\x00' in p.read_bytes():\n            bad.append(str(p))\nif bad:\n    raise SystemExit('embedded NUL: ' + ', '.join(bad))\nprint('source NUL scan passed')\nPY\n"""
new="""python3 tests/host/test_no_embedded_nul.py\n"""
if old not in s:
    raise SystemExit('v1 NUL gate marker missing')
p.write_text(s.replace(old,new,1))
PY
bash /tmp/c6-ias-contact-production-v2-inner.sh
