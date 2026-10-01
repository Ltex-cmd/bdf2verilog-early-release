#!/usr/bin/env python3
"""Export only the declared public source whitelist and its verified manifest."""
import hashlib
import json
import subprocess
import sys
import zipfile
from pathlib import Path

subprocess.run([sys.executable, "tools/verify_baseline.py"], check=True)
version = Path("VERSION").read_text().strip()
files = json.loads(Path("tools/package-files.json").read_text()) + ["BASELINE.json"]
root = f"bdf2verilog-{version}"
Path("dist").mkdir(exist_ok=True)
out = Path("dist") / (root + "-source.zip")
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    for name in files:
        if Path(name).is_absolute() or ".." in Path(name).parts or not Path(name).is_file():
            raise SystemExit("Invalid source whitelist entry")
        z.write(name, root + "/" + name)
digest = hashlib.sha256(out.read_bytes()).hexdigest()
Path("dist/SHA256SUMS-source.txt").write_text(f"{digest}  {out.name}\n")
print(out.name)
