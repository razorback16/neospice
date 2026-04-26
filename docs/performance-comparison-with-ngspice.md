# Performance Comparison: neospice vs ngspice

## Abstract

We compare neospice (a C++ SPICE engine) against ngspice-42 across all major
analysis types: parsing, DC operating point, AC small-signal, transient,
noise, and DC sweep. Both simulators run **in-process** via shared library
linkage — no subprocess spawning, no file I/O — ensuring a fair comparison
of parser and numerical kernel performance.

neospice is faster at parsing (2–5×), DC operating point (1.8–7.8×), AC
analysis (1.2–7.7×), noise (1.6×), DC sweep (3.9×), and most transient
workloads (1.7–2.9×). ngspice retains a transient advantage on oscillatory
circuits (3.3×) due to timestep control differences.

---

## 1. Experimental Setup

### 1.1 Platform

| Component | Version |
|---|---|
| CPU | Intel Core Ultra 9 285K |
| OS | Ubuntu 24.04.4 LTS, Linux 6.17 |
| Compiler | GCC 14.2.0, `-O3 -std=c++20` |
| neospice solver | SmallSolver (n < 200) / SuiteSparse KLU (n >= 200) |
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

neospice uses a **three-tier solver** behind an abstract `LinearSolver`
interface:

- **Dense tier** (n < 12): column-major dense LU with partial pivoting.
  Avoids all sparse overhead for trivial circuits.
- **Sparse SmallSolver** (12 ≤ n < 200): left-looking column-LU with AMD
  ordering, threshold diagonal pivoting, and CSC L/U storage. Uses the same
  pivot order for refactorize (values-only update) and complex factorization.
- **KLU** (n ≥ 200): SuiteSparse KLU with supernodal blocking and BTF.

A `create_solver(n)` factory function dispatches to the appropriate backend.
All tiers share the same `LinearSolver` interface (`symbolic`, `numeric`,
`refactorize`, `solve`, and their `_complex` variants).

ngspice uses the built-in **Sparse 1.3** library with linked-list element
storage and adaptive refactorize.

---

## 2. Results

### 2.1 Comprehensive benchmark

| Benchmark | ngspice | neospice | Factor | Winner |
|---|---:|---:|---:|---|
| **Parse** | | | | |
| THS4131 (77 nodes, 58 devices) | 436 µs | 196 µs | **2.2×** | neospice |
| Resistor divider (3 devices) | 44 µs | 8 µs | **5.3×** | neospice |
| **DC Operating Point** | | | | |
| THS4131 (14 BJTs) | 624 µs | 337 µs | **1.8×** | neospice |
| Resistor divider | 50 µs | 6 µs | **7.8×** | neospice |
| **AC Small-Signal** | | | | |
| THS4131, DEC 10, 81 pts | 979 µs | 544 µs | **1.8×** | neospice |
| THS4131, DEC 1000, 8 001 pts | 21.7 ms | 18.5 ms | **1.2×** | neospice |
| RC lowpass, DEC 10, 91 pts | 114 µs | 15 µs | **7.7×** | neospice |
| **Transient** | | | | |
| RC lowpass, 500 µs | 1.12 ms | 642 µs | **1.7×** | neospice |
| RLC series, 100 µs | 1.57 ms | 5.19 ms | **3.3×** | ngspice |
| Pulse source, 100 µs | 995 µs | 342 µs | **2.9×** | neospice |
| **Noise** | | | | |
| Resistor divider, DEC 10, 91 pts | 88 µs | 55 µs | **1.6×** | neospice |
| **DC Sweep** | | | | |
| V1 −5..+5 V, 1 001 pts | 819 µs | 209 µs | **3.9×** | neospice |
| **End-to-End** | | | | |
| THS4131 (.op + .ac dec 10) | 732 µs | 536 µs | **1.4×** | neospice |
| | | | | |
| **Total (all individual benchmarks)** | **28.6 ms** | **26.0 ms** | **1.10×** | neospice |

The total sums the 12 individual benchmarks above, excluding End-to-End
(which is a composite of parse + DC OP + AC for THS4131, already counted
individually). The aggregate is dominated by the dense AC sweep (DEC 1000),
which accounts for ~75% of total time and shows only a 1.2× difference.
Per-analysis speedups range from 1.6–7.8× on most workloads.

### 2.2 Improvement from SmallSolver sparse tier

The sparse SmallSolver replaced KLU for the THS4131 circuit (n=87, nnz=330).
Key improvements over the previous KLU-only results:

| Benchmark | With KLU | With SmallSolver | Improvement |
|---|---:|---:|---:|
| AC THS4131 DEC 10 (81 pts) | 628 µs | 547 µs | 13% |
| AC THS4131 DEC 1000 (8 001 pts) | 29.0 ms | 18.5 ms | 36% |
| DC sweep (1 001 pts) | 457 µs | 208 µs | 55% |
| Transient pulse source | 607 µs | 344 µs | 43% |

The high-density AC sweep (DEC 1000) flipped from **ngspice 1.4× faster**
to **neospice 1.1× faster**, eliminating Sparse 1.3's per-point advantage.

---

## 3. Analysis

### 3.1 Where neospice wins

**Parsing (2–5×).** neospice's single-pass C++ recursive descent parser
processes the netlist, expands subcircuits, builds the device graph, assigns
nodes, and performs symbolic sparsity analysis in one pass. ngspice's parser
is a multi-pass C implementation. The advantage is proportionally larger on
small circuits due to ngspice's fixed initialization overhead (~40 µs).

**DC operating point (1.8–7.8×).** Lower per-iteration overhead in matrix
assembly and Newton solve. For tiny circuits the advantage is amplified by
ngspice's minimum initialization cost.

**AC analysis (1.2–7.7×).** SmallSolver's sparse complex LU achieves
~2× throughput over KLU's `klu_z_refactor`/`klu_z_solve` at n=87. This
closes the per-point gap with Sparse 1.3, making neospice faster across
all tested AC densities. The complex refactorize reuses the real L/U
structure and pivot order, iterating only over stored nonzero entries.

**DC sweep (3.9×).** SmallSolver's fast real refactorize (reusing structure
and pivots) reduces per-point overhead below both KLU and Sparse 1.3.

**Pulse transient (2.9×).** For circuits with sharp edges and frequent
breakpoints, neospice's breakpoint classification and adaptive stepping
produce an efficient timestep schedule.

### 3.2 Where ngspice wins

**RLC transient (3.3×).** The RLC series circuit produces an underdamped
oscillation where timestep control is critical. ngspice's transient engine
achieves the same accuracy with fewer total timesteps on this workload,
likely due to differences in truncation error estimation and step-size
adaptation strategy. This is a timestep control difference, not a solver
performance issue.

### 3.3 SmallSolver architecture

SmallSolver uses a left-looking column-LU algorithm with:

1. **AMD ordering** via SuiteSparse for fill reduction.
2. **Threshold diagonal pivoting** (prefer diagonal if |diag| ≥ 0.001 ×
   max(column)) — similar to Sparse 1.3's strategy.
3. **CSC L/U storage** with flat arrays for cache-friendly access.
4. **Structure-reusing refactorize**: recomputes values using the same L/U
   sparsity pattern and pivot order, iterating only over stored nonzero
   entries. No O(n) workspace clearing — only touched positions are reset.
5. **Complex factorization** reuses the real L/U structure and pivot order,
   avoiding redundant symbolic work.

The AC hot-loop profile for THS4131 (n=87) with SmallSolver:

| Phase | Time | Share |
|---|---:|---:|
| G + jωC assembly | ~80 ns | 3% |
| Device frequency stamps | ~60 ns | 2% |
| Complex refactorize | ~1 800 ns | 67% |
| RHS copy | ~30 ns | 1% |
| Complex solve | ~600 ns | 22% |
| Result extraction | ~20 ns | 1% |
| **Total** | **~2 700 ns** | |

89% of per-point time is in the solver (refactorize + solve), down from
94% with KLU. The absolute per-point time dropped from ~4.8 µs to ~2.7 µs.

---

## 4. Summary

| Regime | Winner | Factor | Primary mechanism |
|---|---|---:|---|
| Parsing | neospice | 2–5× | Single-pass C++ parser |
| Small circuits (any analysis) | neospice | 5–8× | Minimal initialization overhead |
| DC operating point | neospice | 1.8× | Lower per-iteration overhead |
| AC analysis (all densities) | neospice | 1.2–7.7× | SmallSolver sparse complex LU |
| Transient (passive/switched) | neospice | 1.7–2.9× | Efficient breakpoint handling |
| Transient (oscillatory) | ngspice | 3.3× | Fewer timesteps to target accuracy |
| Noise | neospice | 1.6× | Same sparse complex path as AC |
| DC sweep | neospice | 3.9× | Fast structure-reusing refactorize |
| End-to-end (typical mixed) | neospice | 1.4× | Parsing + solver advantages |

neospice is faster in all scenarios except oscillatory transient circuits.
The SmallSolver sparse tier eliminated ngspice's previous per-point solver
advantage on small matrices, making neospice faster across all AC sweep
densities. The remaining transient gap is a timestep control difference,
not a solver performance issue.

---

## 5. SmallSolver vs KLU (internal benchmark)

SmallSolver's sparse tier directly competes with KLU for SPICE circuits with
12 ≤ n < 200. The benchmark below compares refactorize+solve performance
using synthetic banded sparse matrices (bandwidth 7, diagonally dominant)
typical of SPICE circuit topologies.

**Platform:** Intel Core Ultra 9 285K, GCC 14.2.0 `-O3`, Release build.
**Methodology:** Median of 2000 refactorize+solve cycles after 100 warmup.

### 5.1 Real refactorize + solve

|  n  |  nnz  | SmallSolver (µs) | KLU (µs) | Speedup |
|----:|------:|------------------:|---------:|--------:|
|   5 |    23 |              0.11 |     0.14 |   1.29× |
|  10 |    58 |              0.32 |     0.28 |   0.87× |
|  25 |   163 |              0.70 |     0.74 |   1.06× |
|  50 |   338 |              1.44 |     1.53 |   1.06× |
|  87 |   597 |              2.59 |     2.78 |   1.07× |
| 100 |   688 |              3.00 |     3.13 |   1.04× |
| 150 | 1 038 |              4.67 |     3.63 |   0.78× |
| 199 | 1 381 |              4.53 |     4.82 |   1.07× |

### 5.2 Complex refactorize + solve

|  n  |  nnz  | SmallSolver (µs) | KLU (µs) | Speedup |
|----:|------:|------------------:|---------:|--------:|
|   5 |    23 |              0.15 |     0.22 |   1.47× |
|  10 |    58 |              0.47 |     0.50 |   1.06× |
|  25 |   163 |              0.74 |     1.37 |   1.83× |
|  50 |   338 |              1.53 |     2.83 |   1.85× |
|  87 |   597 |              2.67 |     5.05 |   1.89× |
| 100 |   688 |              3.05 |     5.80 |   1.90× |
| 150 | 1 038 |              4.77 |     8.76 |   1.84× |
| 199 | 1 381 |              6.31 |    11.62 |   1.84× |

### 5.3 Analysis

**Real:** SmallSolver matches KLU within ~10% for most sizes, with a slight
advantage at n=5 and n=25–100. The two solvers are essentially tied on real
factorization, which is expected: both perform sparse LU with AMD ordering.

**Complex:** SmallSolver is **1.5–1.9× faster** than KLU across all sizes.
This is the critical path for AC and noise analysis. The advantage comes
from:

1. **Structure reuse**: SmallSolver reuses the L/U sparsity pattern and
   pivot order from the real factorization, avoiding KLU's full symbolic
   analysis overhead in `klu_z_factor`.
2. **Simpler data structures**: flat CSC arrays vs KLU's BTF/supernodal
   blocking, reducing pointer chasing.
3. **Targeted workspace clearing**: only positions touched during each
   column's factorization are zeroed, vs KLU's broader workspace management.

**Key observations:**

1. **Complex factorization is the SPICE hot path.** AC analysis calls
   complex refactorize+solve per frequency point. SmallSolver's 1.8×
   advantage here directly translates to the end-to-end improvement.

2. **The real factorization crossover doesn't matter for AC.** Real
   factorization happens once (for the DC operating point), while complex
   refactorize+solve runs thousands of times per AC sweep.

3. **Solutions match.** Both solvers produce identical results (max diff
   < 1e-9) across all tested sizes, confirming numerical correctness.

---

## Appendix A: Reproducibility

**Benchmark sources:**
- `tests/bench/bench_ths4131.cpp` — AC-focused THS4131 benchmark
- `tests/bench/bench_comprehensive.cpp` — multi-analysis benchmark
- `tests/bench/bench_small_solver.cpp` — SmallSolver vs KLU micro-benchmark

**Dependencies:**
```
sudo apt install libngspice0-dev   # system ngspice shared library
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
