#!/usr/bin/env python3
"""Check the manifest against its immutable Git revision and working files."""
import hashlib
import json
import subprocess
from pathlib import Path

root = Path(__file__).resolve().parents[1]
manifest = json.loads((root / "BASELINE.json").read_text())
revision = manifest["commit"]
def git(*args):
    return subprocess.check_output(["git", "-C", str(root), *args])
has_git = (root / ".git").exists()
paths = set(git("ls-tree", "-r", "--name-only", revision).decode().splitlines()) - {"BASELINE.json"} if has_git else set()
expected = manifest["files_sha256"]
if has_git and paths != set(expected):
    raise SystemExit("Manifest file set differs from its immutable revision")
for name, digest in expected.items():
    data = git("show", f"{revision}:{name}") if has_git else (root / name).read_bytes()
    if hashlib.sha256(data).hexdigest() != digest:
        raise SystemExit(f"Revision hash mismatch: {name}")
    if hashlib.sha256((root / name).read_bytes()).hexdigest() != digest:
        raise SystemExit(f"Working file differs from baseline: {name}")
print(f"Verified {len(expected)} files for {revision}; BASELINE.json excluded from self-hashing; Git revision checked={has_git}")
