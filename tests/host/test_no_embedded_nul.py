#!/usr/bin/env python3
import subprocess
from pathlib import Path

TEXT_SUFFIXES = {".c", ".h", ".md", ".py", ".yml", ".yaml", ".json", ".txt"}
TEXT_NAMES = {"CMakeLists.txt", "Kconfig", "sdkconfig.defaults"}

files = subprocess.check_output(["git", "ls-files"], text=True).splitlines()
bad = []
for name in files:
    path = Path(name)
    if path.suffix not in TEXT_SUFFIXES and path.name not in TEXT_NAMES:
        continue
    if 0 in path.read_bytes():
        bad.append(name)
if bad:
    raise SystemExit("embedded NUL bytes: " + ", ".join(bad))
print("text source NUL scan passed")
