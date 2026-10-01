# Reproducible synthetic converter measurements

These scripts generate original circuit BDF inputs. They contain no user circuits,
borrowed circuit source, Quartus models, or vendor programs. They exercise four
shapes at three scales:

- Scalar connector chain: 128, 512, 2048 segments
- Hierarchy: 16, 64, 256 serial instances of one scalar identity leaf
- 32-bit bus chain: 64, 256, 1024 segments with endpoint contacts
- Dense scalar taps on labeled bus rows: 64, 256, 1024 connectors and the same
  number of output terminals. The first connector covers every terminal and has
  a single-bit label, so each scalar terminal takes the early-hit contact path.
  Vector terminals would bypass that classification and are intentionally not
  used here. This is not a full worst-case complexity bound.

All generated circuits make each output equal A. Generation is deterministic. The emitted
manifest records source hashes and workload sizes. The hierarchy case has two
source files regardless of instance count; it measures repeated use of one
component, not discovery of hundreds of distinct component types.

## Run

Requires Python 3, a C++17 compiler, and the project's normal build dependencies.
Run serially from a verified source archive. The runner records the immutable source reference supplied by BASELINE.json and the actual code hashes. No Git installation is required for an archive.

```sh
python3 tools/verify_baseline.py
make -j1 all
make -j1 test
python3 benchmarks/early-release/run.py --source-root . --commit "$(python3 -c 'import json; print(json.load(open("BASELINE.json"))["commit"])')" --output benchmark-results
python3 benchmarks/early-release/check_usage.py --source-root . --output benchmark-results
```

In a Git checkout, use `--commit "$(git rev-parse HEAD)"` instead and keep tracked files clean. The runner requires the supplied revision to match HEAD when `.git` is present.

The benchmark compiles its own driver with the same normal optimized C++17 flags
listed in results.json. Build the converter with the normal Makefile defaults;
if you override its compiler/flags, record those overrides alongside the results.
Use --cxx to choose the benchmark driver's compiler. Each conversion subprocess
has a 120-second timeout. The case count is fixed, and calibration caps each batch
at 1000 repetitions. No CPU-heavy jobs should run alongside it.

## What is timed

Fresh API conversion includes creation of a new project context, reading source
files, parsing, resolving hierarchy, connectivity validation, Verilog rendering,
and destruction. It excludes process startup and output file writes. No converter
context survives between conversions. The operating-system file cache is warm.

There are two warmup conversions in every driver launch. One preliminary
measurement selects a batch near 40 ms, subject to the repetition cap. Seven
batches provide raw per-conversion samples, minimum, maximum, and median. These
are batch averages, not individual-conversion percentiles.

CLI results separately include a fresh process, full conversion, and captured
stdout, including Python subprocess overhead. They exclude writing HDL to disk.
CLI runs have two warmups and seven single-conversion samples. The expected
output bytes are checked in every timed CLI trial. No Quartus process, synthesis,
place-and-route, cold-cache measurement, or retained-context cache benchmark runs.

This runner measures one revision only. Do not turn its absolute numbers into a
claim of superiority over Quartus or another revision. For a revision comparison,
use identical generated fixtures/compiler/flags and alternate launch order
A/B then B/A across rounds; retain all samples and establish output equivalence
separately before reporting a speedup.

## Correctness is separate

The runner checks successful conversion, the requested top module, and repeated
byte-identical CLI output. These are smoke checks, not proof of HDL equivalence.
The separate check_usage.py script exercises inspect, explicit module names,
argument errors, both existing benchmark-core modes, and literal identity
assignments in the generated non-hierarchical output. It does not simulate HDL.
Run the project's test suite separately. Each generated case also has check.v,
an independently authored identity oracle testing zero, one, alternating bits,
X, and Z using case-inequality. With Icarus Verilog installed, for example:

```sh
iverilog -g2012 -s identity_tb -o benchmark-results/check \
  benchmark-results/dense_early_hit_64.v \
  benchmark-results/fixtures/dense_early_hit_64/check.v
vvp benchmark-results/check
```

Repeat for each case and record simulator version and exit status. Simulation is
never included in converter timings. The generated oracle covers these identity
circuits only; it does not establish general primitive/LPM/sequential correctness.

## Portability and interpretation

The scripts use Python standard-library functions and portable C++17 facilities.
Linux results apply only to the recorded Linux CPU/compiler/runtime. They do not
validate a macOS or Windows build. On non-Linux systems CPU metadata can be less
specific; fill it in before publishing results. Virtual/cloud CPU scheduling,
frequency scaling, filesystem cache state, and background load can change timings.
Use the distributions, not only the median. No universal complexity, memory-use,
or speed claim follows from these four circuit families.
