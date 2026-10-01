#!/usr/bin/env python3
"""Package only the installed converter and public metadata, never build logs."""
import hashlib
import platform
import zipfile
from pathlib import Path

version = Path("VERSION").read_text().strip()
os_name = platform.system().lower()
arch = platform.machine().lower()
arch = {"amd64": "x86_64", "aarch64": "arm64"}.get(arch, arch)
name = f"bdf2verilog-{version}-{os_name}-{arch}"
files = [Path("stage/LICENSE"), Path("stage/README.md"), Path("stage/VERSION")]
files += list(Path("stage/bin").glob("bdf-tool*"))
if len(files) != 4 or not all(p.is_file() for p in files):
    raise SystemExit("Expected exactly one installed converter and three metadata files")
Path("dist").mkdir(exist_ok=True)
out = Path("dist") / (name + ".zip")
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    for p in files:
        z.write(p, name + "/" + str(p.relative_to("stage")).replace("\\", "/"))
digest = hashlib.sha256(out.read_bytes()).hexdigest()
(Path("dist") / f"SHA256SUMS-{os_name}-{arch}.txt").write_text(f"{digest}  {out.name}\n")
print(out.name)
