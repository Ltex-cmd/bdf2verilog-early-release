# Distribution changes after measurement

The raw results retain their original source and harness hashes, revision and command strings. They were measured before this directory was renamed from benchmarks/alpha to benchmarks/early-release. Historical command strings are not current reproduction instructions; use BENCHMARKS.md.

The distributed emitter removes only line comments naming old research artifacts or circuits. Its non-comment lines are identical to the measured source. The original measured hash and the current distribution hash are recorded separately in distribution-inputs.json. Production behavior was not rewritten and no new performance result is claimed.

The harness now uses converter-measure, benchmark-results and identity_tb names. These are naming changes made after measurement. Generated BDF contents are unchanged. Identity testbench names and generator metadata changed; the recorded BDF fixture hashes remain the original measured values.
