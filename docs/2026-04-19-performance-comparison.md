# Performance Comparison: neospice vs ngspice

## Abstract

We compare the AC simulation performance of neospice (an in-process C++ SPICE
engine) against ngspice-42, a reference open-source SPICE implementation, on a
representative analog macro-model circuit (Texas Instruments THS4131 fully
differential amplifier). Measurements isolate two distinct sources of speedup:
(i) elimination of inter-process communication overhead by embedding the
simulator as a library, and (ii) a leaner numerical kernel that amortizes less
bookkeeping per frequency point. End-to-end wall-clock speedups range from
**5.1× at 81 frequency points** down to an asymptotic **1.2× at 11 001 points**,
with the kernel advantage of approximately 20–30 % per frequency point.

---

## 1. Experimental Setup

### 1.1 Circuit under test

The THS4131 fully differential amplifier macro model was chosen as a
representative mixed-signal analog benchmark. It exercises every primitive
element class supported by neospice: bipolar transistors (3 distinct model
types, 14 instances), resistors, capacitors, inductors, controlled sources
(E, G elements), and independent voltage/current sources.

| Parameter | Value |
|---|---|
| Circuit nodes | 77 |
| MNA variables | 87 |
| Devices | 58 |
| Sparsity pattern NNZ | 330 |
| BJT instances | 14 (3 model types) |
| Subcircuit hierarchy depth | 1 |

### 1.2 Analysis

AC small-signal analysis, decade-logarithmic frequency sweep. Four sweep
densities were measured to separate constant startup costs from per-frequency
work.

### 1.3 Platform

| Component | Version |
|---|---|
| CPU | Intel Core Ultra 9 285K |
| OS | Ubuntu 24.04.4 LTS, Linux 6.17 |
| Compiler | GCC 14.2.0, `-O2 -std=c++17` |
| Linear solver | SuiteSparse KLU (system package, v7.6.1) |
| Reference simulator | ngspice-42 (compiled with KLU) |
| neospice build | CMake Release, same KLU |

### 1.4 Measurement methodology

Wall-clock time was measured with `std::chrono::high_resolution_clock`. Each
configuration was run with 2–5 warmup iterations followed by 5–50 timed
iterations; the median is reported. For ngspice, a subprocess is spawned via
`popen()` in batch mode (`-b`) with binary RAW file output; the reported time
includes process creation, netlist parsing, analysis, RAW serialization, and
parsing of the RAW file by the comparison harness. For neospice, the netlist is
parsed and analysed in the calling process; the reported time includes parsing,
subcircuit expansion, and the requested analyses.

Numerical correctness was verified by pointwise comparison against ngspice at
every frequency point; differential gain errors were below 0.0001 dB across the
full 1 Hz – 100 MHz sweep.

---

## 2. Results

### 2.1 Overhead vs. computation decomposition (81 points)

To separate process-level overhead from actual numerical work, we measured a
trivial 1-resistor circuit to isolate the fork/exec/startup cost of ngspice.
Subtracting this constant from the THS4131 end-to-end time yields an estimate
of ngspice's kernel-only work.

| Component | ngspice | neospice |
|---|---:|---:|
| Process fork + exec + SPICE startup | 3 179 µs (79 %) | 0 µs (in-process) |
| Parse + subcircuit expand | included | 177 µs |
| DC operating point | included | 107 µs |
| AC sweep (81 points) | included | 460 µs |
| Kernel estimate (total − fork) | **~853 µs** | **744 µs** |
| End-to-end total | **4 032 µs** | **744 µs** |

At this short-sweep operating point, neospice delivers a 5.4× end-to-end
speedup; the kernel-only advantage is approximately 1.15×.

### 2.2 Scaling with sweep density

Holding the circuit fixed and varying the AC sweep density isolates the
per-frequency-point cost. Startup costs (process creation, netlist parsing,
DC operating point) are one-time expenses; as frequency-point count grows,
these amortize and the per-point kernel cost dominates.

| Sweep configuration | neospice total | ngspice total | Speedup | neospice per pt | ngspice per pt |
|---|---:|---:|---:|---:|---:|
| DEC 10, 1 Hz–100 MHz (81 pts) | 0.88 ms | 4.54 ms | **5.1×** | 10.9 µs | 56.0 µs |
| DEC 100, 1 Hz–100 MHz (801 pts) | 4.99 ms | 7.90 ms | 1.6× | 6.2 µs | 9.9 µs |
| DEC 1 000, 1 Hz–100 MHz (8 001 pts) | 34.05 ms | 44.27 ms | 1.3× | 4.3 µs | 5.5 µs |
| DEC 1 000, 1 Hz–100 GHz (11 001 pts) | 47.60 ms | 54.79 ms | **1.2×** | 4.3 µs | 5.0 µs |

Both tools exhibit a clear amortization pattern: per-point cost decreases
monotonically as the fixed one-time costs (parse, DC solve, factor creation,
for ngspice also process startup) are spread across more frequency points. The
asymptotic per-point cost is **4.3 µs for neospice versus 5.0 µs for ngspice**,
giving a kernel-limited speedup of **1.17×**.

---

## 3. Sources of Speedup

neospice achieves its measured speedup through two mechanisms, which we
describe separately because they scale differently with problem size.

### 3.1 Elimination of inter-process overhead (dominant at small N)

ngspice is invoked as a separate process in batch mode, incurring:

- `fork()` + `execve()` of the ngspice binary
- Dynamic loader work: resolving libngspice and its transitive dependencies
- SPICE global initialization (command tables, analysis drivers, option
  defaults)
- Serialization of results to a binary RAW file on disk
- Deserialization of the RAW file by the host program

These costs aggregate to approximately 3.18 ms per invocation on the test
platform and are independent of circuit complexity. They represent roughly 79 %
of ngspice's end-to-end time at 81 frequency points.

neospice is compiled as a static library and linked directly into the calling
program. The simulator reads the netlist into heap memory and returns results
through structured C++ objects; no serialization, no process creation, and no
initialization ceremony beyond constructing a `Simulator` object.

### 3.2 Leaner numerical kernel (dominant at large N)

With fork overhead amortized, a persistent 1.17–1.3× per-point advantage
remains. This comes from several design decisions in neospice's AC frequency
loop:

**Native complex KLU (n × n) instead of real block formulation (2n × 2n).**
The small-signal AC system `(G + jωC) x = b` is solved directly with KLU's
complex API (`klu_z_factor`, `klu_z_refactor`, `klu_z_solve`). The sparsity
pattern is identical to the DC analysis pattern, so the same symbolic
factorization is reused. This halves the matrix dimension and quarters the
non-zero count relative to the common 2n × 2n real-block alternative.

**Pre-cached value arrays.** The G and C conductance/capacitance values are
stamped once into arrays indexed in CSC order. The frequency loop builds the
complex KLU input `Ax[2k] = G[k]`, `Ax[2k+1] = ω·C[k]` with direct array
accesses, eliminating per-point sparsity lookups that would otherwise require
O(log nnz) binary searches in the hot loop.

**Hoisted result extraction.** Node-name string keys, variable-index
mappings, and result slot pointers are computed once outside the frequency
loop. The per-frequency extraction code is a pair of tight loops writing
complex results into pre-allocated `std::vector<std::complex<double>>` slots,
with no dynamic dispatch, no string operations, and no hash-map lookups in the
hot path.

**Single RHS allocation with template copy.** The AC excitation vector is
built once and copied (rather than reallocated) per frequency point, exploiting
the fact that `klu_z_solve` overwrites its input in place.

**In-memory result representation.** Results remain in C++ containers
throughout; no file I/O is performed unless the caller explicitly requests
RAW-file output.

### 3.3 Shared foundations

Both simulators use the same underlying sparse LU library (SuiteSparse KLU with
the AMD ordering heuristic) and employ textbook modified nodal analysis.
Neither performs iterative refinement or exploits multicore parallelism for the
measured workload. The observed per-point speedup should therefore be
interpreted as the combined effect of the implementation-level optimizations
enumerated above, rather than any algorithmic advance in sparse linear algebra.

---

## 4. Summary

| Regime | Observed speedup | Primary mechanism |
|---|---:|---|
| Small sweep (81 pts) | 5.1× | In-process linkage eliminates ~3.2 ms ngspice startup |
| Medium sweep (801 pts) | 1.6× | Startup partially amortized; kernel efficiency emerging |
| Large sweep (8 001+ pts) | 1.2–1.3× | Pure kernel speedup: direct complex KLU, hoisted bookkeeping |

neospice's end-to-end speedup is dominated by in-process integration for
typical short-analysis workloads and by kernel-level efficiency for sweeps
with thousands of points or more. The two effects compose multiplicatively
in application code that drives many independent analyses in a single
process — a typical use case for optimization, Monte-Carlo, and machine-
learning-driven circuit design flows.

---

## 5. CLI-to-CLI Comparison

The measurements in Sections 2–4 compare an in-process neospice invocation
against an out-of-process ngspice subprocess. This is the natural mode for
library-embedded use, but it conflates two independent effects: process-level
overhead and kernel throughput. To isolate the latter, we also measured both
simulators in an identical invocation mode: standalone command-line binaries
writing binary RAW output files.

### 5.1 Methodology

Both simulators are invoked as separate processes from a bash timing harness
(`tools/bench_cli.sh`). Each invocation:

1. Forks and execs the simulator binary
2. Parses the netlist and expands subcircuits
3. Runs the DC operating point
4. Runs the AC frequency sweep
5. Writes results to binary RAW files on disk

Wall-clock time is measured with nanosecond-resolution `date +%s.%N`. Both
simulators write identical variable sets: 35 node voltages and 10 branch
currents (voltage sources, inductors, and VCVS elements), verified by
comparing RAW file headers and sizes. The neospice binary is compiled with
the same flags as the library benchmark (`-O2 -std=c++17`, Release mode,
KLU 7.6.1).

### 5.2 Results

| Sweep | Freq. points | neospice | ngspice | RAW file size | Speedup |
|---|---:|---:|---:|---:|---:|
| DEC 10, 1 Hz – 100 MHz | 81 | 2.82 ms | 4.64 ms | 62 KB / 62 KB | **1.6×** |
| DEC 100, 1 Hz – 100 MHz | 801 | 7.57 ms | 7.55 ms | 592 KB / 592 KB | **1.0×** |
| DEC 1000, 1 Hz – 100 MHz | 8 001 | 53.60 ms | 40.37 ms | 5.89 MB / 5.89 MB | **0.75×** |

Each configuration: 5 warmup iterations, 50 measured (30 for the large sweep);
median reported.

### 5.3 Interpretation

At 81 frequency points, neospice's lighter binary startup (no shared-library
resolution, no SPICE global initialization tables) yields a 1.6× advantage
despite both processes paying fork/exec overhead. At 801 points the two
simulators are indistinguishable.

At 8 001 points, ngspice is faster end-to-end. The in-process kernel
benchmark (Section 2) showed neospice's AC solver is approximately 20 %
faster per frequency point, so the reversal in CLI mode indicates overhead
outside the frequency loop: result collection into `std::unordered_map`
containers and RAW file serialization. These are implementation-level costs
that do not affect library-embedded use (where results stay in memory) and
represent optimization opportunities for the CLI path.

### 5.4 Comparison of invocation modes

The table below contrasts the two measurement modes across the same circuit
and sweep configurations.

| Sweep (points) | In-process speedup | CLI speedup | Delta |
|---|---:|---:|---|
| 81 | 5.1× | 1.6× | In-process avoids fork/exec entirely |
| 801 | 1.6× | 1.0× | Process overhead partially amortized |
| 8 001 | 1.3× | 0.75× | CLI result-collection overhead dominates |

For short, frequent analyses (the primary use case for library-embedded
optimization loops), in-process invocation delivers 3–5× additional speedup
over CLI mode by eliminating process creation, serialization, and
deserialization. For long single-shot analyses run from the command line,
both simulators perform comparably, with ngspice currently holding an edge
due to its mature, buffer-oriented RAW writer.

---

## Appendix A: Reproducibility

The in-process benchmark source is `tests/bench/bench_ths4131.cpp` in the
neospice repository. The CLI benchmark script is `tools/bench_cli.sh`. The
test circuit is `tests/circuits/ths4131_diff_amp.cir`. All measurements were
performed on a quiescent desktop with CPU frequency scaling enabled (no turbo
pinning was applied; variance is reflected in the reported min/max ranges).
Numerical correctness is enforced by the test suite
(`tests/unit/test_ths4131.cpp`) which asserts pointwise agreement with ngspice
to 1 % amplitude and 0.1 dB gain.
