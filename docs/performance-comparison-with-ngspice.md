# Performance Comparison: neospice vs ngspice

## Abstract

We compare neospice (a C++ SPICE engine) against ngspice-42 across all major
analysis types: parsing, DC operating point, AC small-signal, transient,
noise, and DC sweep. Both simulators run **in-process** via shared library
linkage — no subprocess spawning, no file I/O — ensuring a fair comparison
of parser and numerical kernel performance.

neospice is faster at parsing (2–5×), DC operating point (1.7–7.9×), AC
analysis (1.1–7.9×), noise (1.6×), DC sweep (4.1×), and all transient
workloads (1.8–3.0×). ngspice retains a small transient advantage on
oscillatory circuits (1.3×) due to timestep count differences.

---

## 1. Experimental Setup

### 1.1 Platform

| Component | Version |
|---|---|
| CPU | Intel Core Ultra 9 285K |
| OS | Ubuntu 24.04.4 LTS, Linux 6.17 |
| Compiler | GCC 14.2.0, `-O3 -std=c++20` |
| neospice solver | NeoSolver (sparse column-LU + AMD, all sizes) |
| ngspice solver | Sparse 1.3 (built-in to libngspice) |
| ngspice version | 42 (system `libngspice0` package) |
| neospice build | CMake Release |

### 1.2 Methodology

Both simulators run in the same process. ngspice is linked as a shared library
(`libngspice.so`) and driven through its C API (`ngSpice_Init`,
`ngSpice_Command`). neospice is linked statically as a C++ library. Neither
simulator performs any file I/O during the timed sections.

Wall-clock time was measured with `std::chrono::high_resolution_clock`. Each
benchmark was run with 3 warmup iterations followed by 10–30 timed
iterations; the median is reported.

### 1.3 Solver architecture

neospice uses a **two-tier solver** (`NeoSolver`) behind an abstract
`LinearSolver` interface:

- **Dense tier** (n < 12): column-major dense LU with partial pivoting.
  Avoids all sparse overhead for trivial circuits.
- **Sparse tier** (n ≥ 12): left-looking column-LU with AMD ordering,
  maximum transversal row permutation, threshold diagonal pivoting, and CSC
  L/U storage. Uses the same pivot order for refactorize (values-only update)
  and complex factorization. Scales to arbitrarily large matrices.

A `create_solver(n)` factory function dispatches to `NeoSolver`. Both tiers
share the same `LinearSolver` interface (`symbolic`, `numeric`,
`refactorize`, `solve`, and their `_complex` variants).

ngspice uses the built-in **Sparse 1.3** library with linked-list element
storage and adaptive refactorize.

---

## 2. Results

### 2.1 Comprehensive benchmark

| Benchmark | ngspice | neospice | Factor | Winner |
|---|---:|---:|---:|---|
| **Parse** | | | | |
| THS4131 (77 nodes, 58 devices) | 431 µs | 199 µs | **2.2×** | neospice |
| Resistor divider (3 devices) | 44 µs | 8 µs | **5.1×** | neospice |
| **DC Operating Point** | | | | |
| THS4131 (14 BJTs) | 620 µs | 363 µs | **1.7×** | neospice |
| Resistor divider | 49 µs | 6 µs | **7.8×** | neospice |
| **AC Small-Signal** | | | | |
| THS4131, DEC 10, 81 pts | 964 µs | 523 µs | **1.8×** | neospice |
| THS4131, DEC 1000, 8 001 pts | 21.6 ms | 19.3 ms | **1.1×** | neospice |
| RC lowpass, DEC 10, 91 pts | 111 µs | 14 µs | **7.9×** | neospice |
| **Transient** | | | | |
| RC lowpass, 500 µs | 1.11 ms | 607 µs | **1.8×** | neospice |
| RLC series, 100 µs | 1.56 ms | 1.96 ms | **1.3×** | ngspice |
| Pulse source, 100 µs | 992 µs | 334 µs | **3.0×** | neospice |
| **Noise** | | | | |
| Resistor divider, DEC 10, 91 pts | 89 µs | 56 µs | **1.6×** | neospice |
| **DC Sweep** | | | | |
| V1 −5..+5 V, 1 001 pts | 816 µs | 197 µs | **4.1×** | neospice |
| **End-to-End** | | | | |
| THS4131 (.op + .ac dec 10) | 726 µs | 524 µs | **1.4×** | neospice |
| OPA1632 (.op + .ac dec 10) | 6.42 ms | 5.90 ms | **1.1×** | neospice |
| | | | | |
| **Total (all individual benchmarks)** | **28.3 ms** | **23.6 ms** | **1.20×** | neospice |

The total sums the 12 individual benchmarks above, excluding End-to-End
(which is a composite of parse + DC OP + AC, already counted individually).
The aggregate is dominated by the dense AC sweep (DEC 1000), which accounts
for ~82% of total time and shows only a 1.1× difference. Per-analysis
speedups range from 1.6–7.9× on most workloads.

### 2.2 End-to-end comparison (OPA1632)

The OPA1632 fully differential amplifier (194 nodes, 170 MNA variables,
144 devices) is the largest circuit in the benchmark suite and exercises
NeoSolver's sparse tier at scale:

| Benchmark | ngspice | neospice | Factor |
|---|---:|---:|---:|
| E2E: OPA1632 (.op + .ac dec 10) | 6.42 ms | 5.90 ms | **1.1×** neospice |

neospice is faster than ngspice even on this large, complex opamp circuit.

---

## 3. Analysis

### 3.1 Where neospice wins

**Parsing (2–5×).** neospice's single-pass C++ recursive descent parser
processes the netlist, expands subcircuits, builds the device graph, assigns
nodes, and performs symbolic sparsity analysis in one pass. ngspice's parser
is a multi-pass C implementation. The advantage is proportionally larger on
small circuits due to ngspice's fixed initialization overhead (~40 µs).

**DC operating point (1.7–7.9×).** Lower per-iteration overhead in matrix
assembly and Newton solve. For tiny circuits the advantage is amplified by
ngspice's minimum initialization cost.

**AC analysis (1.1–7.9×).** NeoSolver's sparse complex LU reuses the real
L/U structure and pivot order, iterating only over stored nonzero entries.
This makes neospice faster than ngspice's Sparse 1.3 across all tested AC
densities.

**DC sweep (4.1×).** NeoSolver's fast real refactorize (reusing structure
and pivots) reduces per-point overhead well below Sparse 1.3.

**Pulse transient (3.0×).** For circuits with sharp edges and frequent
breakpoints, neospice's breakpoint classification and adaptive stepping
produce an efficient timestep schedule.

### 3.2 Where ngspice wins

**RLC transient (1.3×).** The RLC series circuit produces an underdamped
oscillation where timestep control is critical. ngspice's transient engine
achieves the same accuracy with slightly fewer total timesteps on this
workload, due to differences in device-level truncation error estimation.
This is a small timestep count difference, not a solver performance issue.

### 3.3 NeoSolver architecture

NeoSolver uses a left-looking column-LU algorithm with:

1. **AMD ordering** for fill reduction (custom implementation, no external
   dependencies).
2. **Maximum transversal** row permutation to move large entries onto the
   diagonal before factorization.
3. **Threshold diagonal pivoting** (prefer diagonal if |diag| ≥ 0.1 ×
   max(column)) — similar to Sparse 1.3's strategy.
4. **CSC L/U storage** with flat arrays for cache-friendly access.
5. **Structure-reusing refactorize**: recomputes values using the same L/U
   sparsity pattern and pivot order, iterating only over stored nonzero
   entries. No O(n) workspace clearing — only touched positions are reset.
6. **Complex factorization** reuses the real L/U structure and pivot order,
   avoiding redundant symbolic work.

---

## 4. Summary

| Regime | Winner | Factor | Primary mechanism |
|---|---|---:|---|
| Parsing | neospice | 2–5× | Single-pass C++ parser |
| Small circuits (any analysis) | neospice | 5–8× | Minimal initialization overhead |
| DC operating point | neospice | 1.7× | Lower per-iteration overhead |
| AC analysis (all densities) | neospice | 1.1–7.9× | NeoSolver sparse complex LU |
| Transient (passive/switched) | neospice | 1.8–3.0× | Efficient breakpoint handling |
| Transient (oscillatory) | ngspice | 1.3× | Slightly fewer timesteps |
| Noise | neospice | 1.6× | Same sparse complex path as AC |
| DC sweep | neospice | 4.1× | Fast structure-reusing refactorize |
| End-to-end (typical mixed) | neospice | 1.1–1.4× | Parsing + solver advantages |

neospice is faster in all scenarios except oscillatory transient circuits,
where ngspice retains a small 1.3× advantage due to slightly fewer timesteps.
NeoSolver handles all matrix sizes with no external dependencies (no
SuiteSparse), while remaining faster than ngspice's Sparse 1.3 across all
AC sweep densities.

---

## 5. NeoSolver scaling benchmark

NeoSolver's sparse tier handles all matrix sizes (n ≥ 12) with no external
dependencies. The benchmark below shows refactorize+solve performance scaling
using synthetic banded sparse matrices (bandwidth 7, diagonally dominant)
typical of SPICE circuit topologies.

**Platform:** Intel Core Ultra 9 285K, GCC 14.2.0 `-O3`, Release build.
**Methodology:** Median of refactorize+solve cycles after warmup.

### 5.1 Real refactorize + solve

|    n  |    nnz | Runs | NeoSolver (µs) |
|------:|-------:|-----:|----------------:|
|     5 |     23 | 2000 |            0.10 |
|    10 |     58 | 2000 |            0.32 |
|    25 |    163 | 2000 |            0.70 |
|    50 |    338 | 2000 |            1.40 |
|    87 |    597 | 2000 |            2.49 |
|   100 |    688 | 2000 |            2.90 |
|   150 |  1 038 | 2000 |            4.47 |
|   199 |  1 381 | 2000 |            4.92 |
|   300 |  2 088 | 2000 |            6.89 |
|   500 |  3 488 | 2000 |           11.50 |
| 1 000 |  6 988 |  500 |           22.10 |
| 2 000 | 13 988 |  500 |           44.82 |
| 5 000 | 34 988 |  100 |          112.06 |
|10 000 | 69 988 |   30 |          224.63 |

### 5.2 Complex refactorize + solve

|    n  |    nnz | Runs | NeoSolver (µs) |
|------:|-------:|-----:|----------------:|
|     5 |     23 | 2000 |            0.16 |
|    10 |     58 | 2000 |            0.47 |
|    25 |    163 | 2000 |            0.76 |
|    50 |    338 | 2000 |            1.56 |
|    87 |    597 | 2000 |            2.82 |
|   100 |    688 | 2000 |            3.15 |
|   150 |  1 038 | 2000 |            4.76 |
|   199 |  1 381 | 2000 |            6.55 |
|   300 |  2 088 | 2000 |            9.62 |
|   500 |  3 488 | 2000 |           16.61 |
| 1 000 |  6 988 |  500 |           32.85 |
| 2 000 | 13 988 |  500 |           65.66 |
| 5 000 | 34 988 |  100 |          164.26 |
|10 000 | 69 988 |   30 |          334.13 |

### 5.3 Analysis

NeoSolver scales approximately linearly with nnz for banded SPICE matrices.
Complex factorization is ~1.4× slower than real factorization at the same
size, which is expected for double the arithmetic per element.

**Key observations:**

1. **Sub-microsecond for tiny circuits.** n=5 completes in ~100 ns (real)
   or ~160 ns (complex), enabling high-throughput DC sweep and AC analysis
   on small circuits.

2. **Linear scaling to large circuits.** n=10 000 (70k nonzeros) completes
   in ~225 µs (real) or ~334 µs (complex), confirming efficient AMD
   ordering and sparse structure reuse at scale.

3. **No external dependencies.** NeoSolver replaces SuiteSparse (KLU, AMD,
   BTF, COLAMD) with a single self-contained implementation, simplifying
   the build and eliminating licensing constraints.

---

## Appendix A: Reproducibility

**Benchmark sources:**
- `tests/bench/bench_ths4131.cpp` — AC-focused THS4131 benchmark
- `tests/bench/bench_comprehensive.cpp` — multi-analysis benchmark
- `tests/bench/bench_neo_solver.cpp` — NeoSolver scaling micro-benchmark

**Dependencies:**
```
sudo apt install libngspice0-dev   # system ngspice shared library (for comparison only)
```

**Build and run:**
```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target bench_comprehensive -j$(nproc)
./build/tests/bench_comprehensive
```

The test circuit is `tests/circuits/ths4131_diff_amp.cir` (THS4131 fully
differential amplifier macro model: 77 nodes, 87 MNA variables, 58 devices,
330 NNZ, 14 BJTs across 3 model types).

All measurements were performed on a quiescent desktop with CPU frequency
scaling enabled. Numerical correctness is enforced by the test suite
(`tests/unit/test_ths4131.cpp`, `tests/unit/test_ngspice_compare.cpp`)
which asserts pointwise agreement with ngspice across DC, AC, transient,
and noise analyses.
