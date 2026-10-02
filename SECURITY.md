# Security policy

## Scope and support

This is an experimental, vibecoded hobby project. It provides no production-safety guarantee or long-term security-support commitment. Report
problems against the latest early release or current `main`; older snapshots
do not have a separate maintenance branch. No response or fix deadline is promised.

## Report a vulnerability privately

Open the repository's [Security page](https://github.com/Ltex-cmd/bdf2verilog-early-release/security)
and choose **Report a vulnerability**. Use that private GitHub report for
vulnerability details. Do not disclose them in public issues, pull requests or
comments. If the reporting button is unavailable, keep the details private
rather than posting them publicly.

Include the affected release or commit, OS/architecture, compiler if relevant,
steps to reproduce and the potential security impact. Prefer a small original
synthetic BDF example. Remove secrets, personal paths and proprietary circuit
information; share only files you have permission to share.

Ordinary conversion mistakes and build problems without a security impact can
use the public bug-report template.

## Use with care

The converter reads BDF inputs and referenced component files. It is not a
sandbox for hostile inputs. Use an unprivileged account and an isolated working
directory for untrusted files; apply operating-system resource limits when
needed. Generated HDL can contain absolute ROM paths, so review it before
sharing. Review and test generated designs before using them in hardware.
