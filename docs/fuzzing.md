# Fuzzing

Two [libFuzzer](https://llvm.org/docs/LibFuzzer.html) targets exercise the
netlist front end with AddressSanitizer and UndefinedBehaviorSanitizer:

| Target | Input path |
|---|---|
| `fuzz_parse` | `NetlistParser::parse()` on arbitrary netlist text |
| `fuzz_op` | `Simulator::parse()` then a DC operating point (`no_throw`) |

A `std::exception` from parsing or simulation is an expected rejection.
Sanitizer reports, crashes, other exceptions, timeouts and out-of-memory
conditions are findings. Inputs mentioning `/dev/` or `/proc/` are skipped so
`.include` cannot read device or process files.

## Build

Fuzzing requires Clang with compiler-rt. Instrument the library and enable the
targets:

```sh
CC=clang CXX=clang++ cmake -S . -B build-fuzz \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DNEOSPICE_BUILD_TESTS=OFF -DNEOSPICE_BUILD_CLI=OFF \
  -DNEOSPICE_BUILD_FUZZERS=ON -DCMAKE_DISABLE_FIND_PACKAGE_OpenMP=TRUE \
  -DCMAKE_CXX_FLAGS="-fsanitize=fuzzer-no-link,address,undefined -fno-sanitize-recover=undefined" \
  -DCMAKE_C_FLAGS="-fsanitize=fuzzer-no-link,address,undefined"
cmake --build build-fuzz --target fuzz_parse fuzz_op
```

## Run

Seed with netlists that have their includes inlined, for example the test
circuits and a sample of the [KiCad corpus](kicad-experiment.md) fixtures.
Use the SPICE dictionary in `tests/fuzz/spice.dict`:

```sh
mkdir -p corpus artifacts
ASAN_OPTIONS=detect_leaks=0:allocator_may_return_null=1 \
build-fuzz/fuzz_parse -fork=12 -ignore_crashes=1 -ignore_timeouts=1 \
  -ignore_ooms=1 -max_total_time=3600 -max_len=65536 -timeout=10 \
  -rss_limit_mb=4096 -close_fd_mask=3 -dict=tests/fuzz/spice.dict \
  -artifact_prefix=artifacts/ corpus seeds
```

Fork mode keeps fuzzing after a finding and saves every crash, timeout and
out-of-memory input to `artifacts/`. Leak detection is disabled, so
the campaign targets memory-safety and undefined-behavior defects.
`-close_fd_mask=3` silences model-version notices printed by some devices.
Reproduce a finding with `build-fuzz/fuzz_parse artifacts/crash-<hash>`.

## Campaign results

Campaign of October 14, 2026: Clang 18, ASan and UBSan, 12 fork workers per
target, on a 24-core x86-64 Linux host. 2,500 seed netlists, with includes
inlined, came from the test circuits and the KiCad corpus.

| Round | Code | Duration | `fuzz_parse` runs | `fuzz_op` runs | Crashes | OOM | Timeouts |
|---|---|---:|---:|---:|---:|---:|---:|
| 1 | `906930c` | 8.5 min | 720,452 | 119,520 | 33 | 1 | 87 |
| 2 | first fixes | 60 min | 1,022,126 | 527,972 | 2 | 0 | 516 |
| 3 | `f33a3e5` | 30 min | 205,109 | 68,311 | 0 | 0 | 98 |

The 35 crash inputs and the OOM input reduce to eight defects. Commit `f33a3e5`
fixes all of them. Unit or ngspice-comparison tests cover every fix except
the sparse refactorization guard, which only its replayed fuzz input covers.
All 36 inputs pass on the fixed build. The same commit fixes one more defect, found while writing the
filter-design example: `.control` … `.endc` blocks were not skipped, so script
lines such as `let out 0 1` became devices.

| Defect | Effect before the fix |
|---|---|
| HiSIM-HV LTE read NQS-only states | Heap overflow. Every HiSIM-HV transient failed at t=0 |
| Missing pivot in sparse refactorization | Null dereference after a singular reordering |
| Incomplete second `.dc` group | Read past the token list |
| Recursive or exponential `.FUNC` | Expansion never terminated; one input exhausted 4 GB |
| Huge number exponent | Signed integer overflow |
| Out-of-range `LEVEL` | Undefined double-to-int conversion (MOSFET, BJT, JFET, HFET) |
| Out-of-range integer model parameter | Undefined double-to-int conversion (BSIM4, HiSIM2) |
| Out-of-range `.ac`/`.noise`/`.meas` counts | Undefined double-to-int conversion |

HiSIM-HV and HiSIM2 truncation now check exactly the charges ngspice 47 checks.
The HiSIM-HV fix restores transient simulation for that model. A test
transient is now bit-identical to ngspice 47. On the frozen corpus of 67,359
operating-point fixtures, the commit changes only these cases: four former
segfaults now exit with an error, and two fixtures with `.control` blocks now
parse. Transient fixtures are byte-identical.

Open findings are timeouts, not memory-safety defects:

- **Nested subcircuit definitions.** An unterminated `.subckt` nests every
  later definition inside it. Expanding the resulting hierarchy is
  superlinear: one 8.6 KB input expands to 38,771 devices in 12.8 s, about
  330 µs per device instead of about 6 µs. ngspice rejects unbalanced
  `.subckt`/`.ends`.
- **Unbounded DC convergence cascade.** In a sample of `fuzz_op` timeouts
  replayed on a release build, parsing took negligible time. The operating-point
  convergence aids ran past 10 s in about half of the inputs and past 60 s in a
  quarter. A wall-clock limit for DC analysis is not implemented.
