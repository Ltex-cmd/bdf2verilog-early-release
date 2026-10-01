# bdf2verilog

> # WARNING: PURE VIBECODING. EXPECT AI SLOP.
> This is an experimental spare-time hobby project. I barely know C++. Do not expect quality, reliability or a guarantee that the generated HDL is correct. Review the output and test your design.

I wanted to convert BDF circuits to Verilog quickly, but kept getting stuck dealing with Quartus and its delays under Wine. So I took the problem into my own hands. Or, more accurately, into the hands of my Codex and Claude Code subscriptions.

This project is entirely vibecoded. I leaned on gpt-6.1-sol, gpt-6-astra and opus-5.5 to get this built. I am not presenting this as carefully hand-written C++ by someone who knows what they are doing.

Convert the supported subset of Quartus circuit Block Diagram Files into Verilog. The converter reads `.bdf` files, resolves referenced BDF components and emits a module hierarchy. It is a C++17 program with no Quartus runtime dependency.

Version `0.1.0-early-release.1` is an experimental early release. Compatibility is limited to the constructs described below. Review the generated HDL before using it in a hardware design.

## Build and run

The tested build uses GCC or Clang with a C++17 standard library and Make. Python 3 is needed for the source-manifest check and optional benchmark scripts. The converter itself needs no Python or Quartus runtime. Build from the repository root:

```sh
make all
make test
./build/bdf-tool inspect tests/minimal.bdf
./build/bdf-tool verilog tests/minimal.bdf minimal > build/minimal.v
```

`inspect` writes JSON describing the schematic. `verilog` writes Verilog to standard output. Diagnostics go to standard error. Invocations with fewer than three arguments, including the executable name, return exit code 2. Invalid options, a trailing `--library` without a directory, and conversion errors return exit code 1. Conversion completes before the program writes HDL. Shell redirection can still create or truncate the destination when conversion fails.

The module name defaults to the input filename without its extension. Pass an explicit name when the filename is not a supported Verilog identifier:

```sh
./build/bdf-tool verilog design.bdf top --library components --library shared > top.v
```

Each `--library` adds a directory for referenced BDF components. Keep ROM initialization files with the source that references them. This command requires your own `design.bdf` and component directories.

The synthetic example contains two BUF blocks connected between input `A` and output `Y`. Its output has a `minimal` module and connects `Y` to `A`. No device project or synthesis step is required.

There is no installer. To put the executable in a directory you control, copy `build/bdf-tool` there. The production source and default synthetic test suite passed on macOS arm64 with Apple Clang 21 and Linux x86-64 with GCC 14.2. GCC reported warnings documented in the benchmark report. Windows and MSVC have not been validated for this candidate. C++17 filesystem support and standard-library behavior still depend on your compiler.

Without Make, GCC and Clang can build the standalone converter directly:

```sh
c++ -std=c++17 -O2 -DBDF_CONTEXT_TAPE -Iinclude src/parser.cpp src/schematic.cpp src/signal_name.cpp src/lpm.cpp src/verilog.cpp src/project.cpp src/bdf_tape.cpp src/bdf_span.cpp src/project_context.cpp src/main.cpp -o bdf-tool
```

The shell commands shown here use a Unix-like shell. The source uses standard C++17; this is not a promise that every compiler or operating system works.

## Supported input

The converter accepts graphic BDF versions 1.3 and 1.4. It supports scalar wires, labeled buses, ordered bit ranges, named bit aliases and BDF component hierarchies. Junction records are accepted, but a standalone junction marker does not add a connection point. Terminals and connector endpoints determine geometric contacts; a marker alone does not connect crossing segment interiors. Connectivity keeps scalar and bus signals separate. Output traversal is deterministic.

Built-in blocks include VCC, GND, BUF, NOT, XOR, XNOR, BNOR4, DFF and TFF. AND, OR, NAND and NOR support 2, 3, 4, 6, 8 and 12 inputs. Flip-flop ports must match the supported clock, data or toggle, clear, preset and output interface.

LPM_COUNTER and LPM_ROM have restricted parameter and port support. They emit module bindings. Compatible HDL implementations or simulation models must be supplied separately under their own licenses. ROM file references in emitted HDL can be absolute paths. Check and update them before moving or sharing the HDL. This repository does not include vendor models.

## Limits

This is a format converter. It does not perform synthesis, place-and-route, timing analysis or general Boolean optimization. It does not promise complete Quartus compatibility or four-state equivalence for every vendor block.

Unsupported symbols, component interfaces, identifiers that need escaping, grouped top-level pins and bidirectional pins fail with an explicit diagnostic. Width mismatches and multiple drivers also fail. Undriven-input checks apply to built-in primitives, except omitted optional controls. Feedback checks cover the primitive combinational dependency graph within a module. Component and LPM connections skip those input/dependency checks, so this is not exhaustive validation across module boundaries. Conduits and arbitrary mapper-table connections have not been established as supported; do not infer support from geometric proximity. Check the exact input rather than assuming every BDF version or symbol library works.

## Benchmarks

Use the included driver on the synthetic example:

```sh
./build/benchmark-core tests/minimal.bdf > build/minimal-benchmark.json
```

Each timed conversion reads the file, parses it, resolves components, validates connectivity and emits an HDL string. The driver performs five warmups, chooses a batch size and records seven samples. The operating system file cache may be warm. Parsed project state is not retained between conversions. Process startup and HDL file writing are excluded. Passing a second argument adds output-file writing to the timed work:

```sh
./build/benchmark-core tests/minimal.bdf build/benchmark-output.v
```

These measurements describe this converter. They do not establish a speedup over Quartus, and they do not measure synthesis. A full CLI launch includes additional startup and stdout costs.

The [synthetic benchmark package](benchmarks/early-release/BENCHMARKS.md) generates 12 original circuits across scalar chains, repeated component hierarchies, 32-bit bus chains and dense scalar bus contacts. Its [raw Linux results](benchmarks/early-release/evidence/linux-5e3cc48/results.json) include the compiler, source and fixture hashes, warmups, samples and timed work.

Selected results on Linux x86-64, GCC 14.2, AMD EPYC 9V74 are below. Fresh conversion includes file reads, parsing, resolution, validation, emission and destruction. CLI samples include process launch and Python subprocess overhead. Neither mode writes an HDL file during timing.

| Synthetic case | Fresh median | Fresh range | CLI median |
| --- | ---: | ---: | ---: |
| 2,048 scalar segments | 0.934 ms | 0.911 to 1.064 ms | 2.959 ms |
| 256 repeated component instances | 1.695 ms | 1.649 to 1.785 ms | 4.442 ms |
| 1,024 bus segments | 0.538 ms | 0.494 to 0.579 ms | 2.511 ms |
| 1,024 dense scalar bus contacts | 5.892 ms | 5.725 to 6.719 ms | 8.763 ms |

There are seven samples per case and mode. Fresh samples are calibrated batch averages with two warmups per launch; the OS file cache is warm. All 12 outputs were byte-identical to the preceding source revision with the same fixtures. The Linux run passed the synthetic test suite and 17 CLI/identity checks. It did not simulate HDL, prove general four-state equivalence or compare performance with another revision or Quartus.

To check a source archive, run `python3 tools/verify_baseline.py`. Reproduction commands and optional independent identity simulations are in BENCHMARKS.md.

## License

This source package contains the converter and synthetic tests. It ships no Quartus/Altera source files, vendor binaries, models, decompiler output or vendor-DLL test host.

[MIT](LICENSE) applies to project-owned material. It grants no rights to vendor software or input circuits. The release remains experimental and comes without a quality or correctness guarantee.
