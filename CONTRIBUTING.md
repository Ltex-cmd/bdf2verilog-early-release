# Contributing

Small fixes, clearer docs and reproducible bug reports are welcome. This is an
experimental, vibecoded spare-time project. There is no support schedule or
guarantee that a change will be accepted. Read the [README](README.md) for the
supported BDF subset and limits, and follow the [code of conduct](CODE_OF_CONDUCT.md).

## Issues

Search existing issues first. For a bug, include the release or commit, OS and
architecture, compiler version if building from source, exact command, expected
result and actual diagnostic or output. A tiny synthetic BDF example is usually
more useful than a full circuit. Include referenced components only when needed.

Share only material you have permission to publish. Remove private paths,
usernames, secrets and proprietary circuit details from inputs, logs and emitted
HDL. No Quartus installation or vendor models are required for a useful report.
For suspected vulnerabilities, keep details private. Security reporting setup is
pending; do not post vulnerability details in public issues or pull requests.

For a substantial feature or compatibility change, open an issue first to discuss
the input construct, expected HDL and scope. Small fixes can go straight to a PR.

## Changes and tests

Fork the repository and use a focused branch. Keep C++17 compatibility, match the
surrounding style and avoid unrelated formatting or new dependencies. Explain the
change and add a small regression test under `tests/` for changed behavior.
Use original synthetic fixtures that can be distributed under the project license.

From the repository root, with CMake 3.16+, a C++17 toolchain and Python 3:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
python3 tools/verify_baseline.py
```

On Windows, run these in a shell with the compiler toolchain available. On Linux
or macOS, `make all` and `make test` are also supported. State which platform and
checks you actually ran; CI builds and tests Linux, macOS and Windows. Review
generated HDL and test the intended behavior. Passing converter tests is not a
general proof of HDL correctness.

## Source package metadata

`tools/package-files.json` lists the public source package. Add new distributable
files there. `BASELINE.json` hashes an immutable content commit and excludes
itself. A maintainer can help refresh it before merge: commit the content and
whitelist first, then update the manifest to that commit and its file hashes in a
separate commit. Preserve the historical measurement reference and validation
input hashes unless new evidence justifies changing them. Do not bypass the
manifest check to make CI pass.

## License

Contributions to project-owned material use the existing [MIT license](LICENSE).
Only contribute code, documentation and fixtures you have the right to share.
Do not include vendor source, binaries or models.
