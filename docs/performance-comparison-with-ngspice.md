# Performance Comparison: neospice vs ngspice

## Abstract

We compare neospice (a C++ SPICE engine) against ngspice-42 across all major
analysis types: parsing, DC operating point, AC small-signal, transient,
noise, and DC sweep. Both simulators run **in-process** via shared library
linkage — no subprocess spawning, no file I/O — ensuring a fair comparison
of parser and numerical kernel performance.

neospice is faster at parsing (2–5×), DC operating point (1.9–6.5×), noise
(1.5×), DC sweep (1.8×), and low-density AC (1.6–7.2×). ngspice is faster
at high-density AC sweeps (1.4×) and certain transient workloads (up to 4.8×),
where its Sparse 1.3 solver outperforms KLU on small matrices and its
transient timestep control produces fewer total steps.

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

### 1.3 Solver difference

A key architectural difference: neospice uses a **two-tier solver** behind an
abstract `LinearSolver` interface, while the system ngspice uses the built-in
**Sparse 1.3** library.

- **SmallSolver** (n < 12): dense column-major LU with partial pivoting.
  Avoids KLU setup overhead for trivial circuits.
- **KLU** (n >= 12): SuiteSparse KLU with supernodal blocking and BTF.

A `create_solver(n)` factory function dispatches to the appropriate backend.
Both tiers share the same `LinearSolver` interface (`symbolic`, `numeric`,
`refactorize`, `solve`, and their `_complex` variants).

Sparse 1.3 uses a simpler linked-list element structure with less overhead
per factor/solve cycle, which gives it an advantage on small matrices.

Profiling confirms that **94% of neospice's per-AC-point time** is spent in
the linear solver (`refactor` 66%, `solve` 28%), with matrix assembly,
RHS copy, and result extraction accounting for the remaining 6%.

---

## 2. Results

### 2.1 Comprehensive benchmark

| Benchmark | neospice | ngspice | Factor | Winner |
|---|---:|---:|---:|---|
| **Parse** | | | | |
| THS4131 (77 nodes, 58 devices) | 195 µs | 435 µs | **2.2×** | neospice |
| Resistor divider (3 devices) | 9 µs | 43 µs | **5.0×** | neospice |
| **DC Operating Point** | | | | |
| THS4131 (14 BJTs) | 328 µs | 623 µs | **1.9×** | neospice |
| Resistor divider | 7 µs | 49 µs | **6.5×** | neospice |
| **AC Small-Signal** | | | | |
| THS4131, DEC 10, 81 pts | 628 µs | 990 µs | **1.6×** | neospice |
| THS4131, DEC 1000, 8 001 pts | 29.0 ms | 20.6 ms | **1.4×** | ngspice |
| RC lowpass, DEC 10, 91 pts | 16 µs | 114 µs | **7.2×** | neospice |
| **Transient** | | | | |
| RC lowpass, 500 µs | 1.06 ms | 1.12 ms | ~1.0× | tied |
| RLC series, 100 µs | 7.45 ms | 1.57 ms | **4.8×** | ngspice |
| Pulse source, 100 µs | 607 µs | 1.00 ms | **1.7×** | neospice |
| **Noise** | | | | |
| Resistor divider, DEC 10, 91 pts | 58 µs | 90 µs | **1.5×** | neospice |
| **DC Sweep** | | | | |
| V1 −5..+5 V, 1 001 pts | 457 µs | 828 µs | **1.8×** | neospice |
| **End-to-End** | | | | |
| THS4131 (.op + .ac dec 10) | 624 µs | 725 µs | **1.2×** | neospice |

### 2.2 AC scaling with sweep density

The AC crossover point — where ngspice's per-point solver advantage
overtakes neospice's parsing/setup advantage — occurs around 800 frequency
points for the THS4131 circuit (n=87, nnz=330).

| Sweep (THS4131) | neospice | ngspice | Factor | Winner |
|---|---:|---:|---:|---|
| DEC 10 (81 pts) | 0.62 ms | 0.96 ms | **1.5×** | neospice |
| DEC 100 (801 pts) | 3.19 ms | 2.69 ms | **1.2×** | ngspice |
| DEC 1000 (8 001 pts) | 28.96 ms | 21.25 ms | **1.4×** | ngspice |

Asymptotic per-point cost: neospice **3.6 µs/pt**, ngspice **2.7 µs/pt**
(1.3× advantage to ngspice's Sparse 1.3 solver on this matrix size).

---

## 3. Analysis

### 3.1 Where neospice wins

**Parsing (2–5×).** neospice's single-pass C++ recursive descent parser
processes the netlist, expands subcircuits, builds the device graph, assigns
nodes, and performs symbolic sparsity analysis in one pass. ngspice's parser
is a multi-pass C implementation. The advantage is proportionally larger on
small circuits due to ngspice's fixed initialization overhead (~40 µs).

**DC operating point (1.9–6.5×).** Lower per-iteration overhead in matrix
assembly and Newton solve. For tiny circuits the advantage is amplified by
ngspice's minimum initialization cost.

**Low-density AC / Noise / DC sweep (1.5–7.2×).** When one-time setup costs
(parse, DC, symbolic factorization) dominate over per-point solve cost,
neospice's leaner startup wins. The noise solver uses the same KLU complex
factor/solve path as AC but with additional adjoint RHS solves, so the
relative advantage carries over.

**Pulse transient (1.7×).** For circuits with sharp edges and frequent
breakpoints, neospice's breakpoint classification and adaptive stepping
produce an efficient timestep schedule.

### 3.2 Where ngspice wins

**High-density AC (1.4× at 8 001 pts).** Sparse 1.3's `spFactor`/`spSolve`
outperforms KLU's `klu_z_refactor`/`klu_z_solve` on this 87×87 matrix.
Sparse 1.3 stamps values directly into the factorization data structure
(linked-list elements), avoiding KLU's CSC array indirection. KLU's
supernodal blocking would pay off on larger matrices (n > 500) but adds
overhead here.

**RLC transient (4.8×).** The RLC series circuit produces an underdamped
oscillation where timestep control is critical. ngspice's transient engine
achieves the same accuracy with fewer total timesteps on this workload,
likely due to differences in truncation error estimation and step-size
adaptation strategy.

### 3.3 Solver tradeoff: KLU vs Sparse 1.3

The AC hot-loop profile for THS4131 (n=87):

| Phase | Time | Share |
|---|---:|---:|
| G + jωC assembly | 78 ns | 1.6% |
| Device frequency stamps | 60 ns | 1.2% |
| `klu_z_refactor` | 3 184 ns | 65.7% |
| RHS copy | 34 ns | 0.7% |
| `klu_z_solve` | 1 378 ns | 28.4% |
| Result extraction | 21 ns | 0.4% |
| **Total** | **4 846 ns** | |

94% of per-point time is in KLU. The matrix assembly, RHS management, and
result extraction are negligible. This means the only way to close the gap
on high-density sweeps is to either:
1. Replace KLU with a solver better suited to small matrices, or
2. Move to larger circuits where KLU's supernodal approach pays off.

---

## 4. Summary

| Regime | Winner | Factor | Primary mechanism |
|---|---|---:|---|
| Parsing | neospice | 2–5× | Single-pass C++ parser |
| Small circuits (any analysis) | neospice | 5–7× | Minimal initialization overhead |
| DC operating point | neospice | 1.9× | Lower per-iteration overhead |
| Low-density AC/noise (< 800 pts) | neospice | 1.5–7× | Setup cost dominates |
| High-density AC (> 800 pts) | ngspice | 1.2–1.4× | Sparse 1.3 faster per-point on small matrices |
| Transient (passive/switched) | neospice | 1.1–1.7× | Efficient breakpoint handling |
| Transient (oscillatory) | ngspice | up to 4.8× | Fewer timesteps to target accuracy |
| DC sweep | neospice | 1.8× | Lower per-point solve overhead |
| End-to-end (typical mixed) | neospice | 1.2× | Parsing advantage + mixed analysis |

neospice is faster in the majority of scenarios, particularly for
library-embedded use cases with many short simulations (optimization loops,
Monte Carlo, ML-driven design). ngspice has a per-point solver advantage
on small matrices (n < 200) that emerges on high-density frequency sweeps
and a transient timestep advantage on certain oscillatory circuits.

---

## 5. SmallSolver vs KLU (internal benchmark)

neospice's `SmallSolver` replaces KLU for circuits with n < 200 MNA variables.
It uses dense column-major LU factorization with partial pivoting. For
n >= 25, AMD ordering is applied to the sparsity pattern before scattering
into the dense matrix, which reduces fill and improves cache locality.

The benchmark below directly compares SmallSolver vs KLU at the solver level
(no simulation overhead), using synthetic banded sparse matrices (bandwidth 7,
diagonally dominant) typical of SPICE circuit topologies.

**Platform:** Intel Core Ultra 9 285K, GCC 14.2.0 `-O3`, Release build.
**Methodology:** Median of 2000 refactorize+solve cycles after 100 warmup.

### 5.1 Real refactorize + solve

|  n  |  nnz  | SmallSolver (us) | KLU (us) | Speedup |
|----:|------:|------------------:|---------:|--------:|
|   5 |    23 |              0.22 |     0.29 |   1.30x |
|  10 |    58 |              0.65 |     0.58 |   0.90x |
|  25 |   163 |              2.23 |     0.76 |   0.34x |
|  50 |   338 |             12.84 |     1.24 |   0.10x |
|  87 |   597 |             45.13 |     2.05 |   0.05x |
| 100 |   688 |             69.38 |     2.41 |   0.03x |
| 150 | 1 038 |            258.32 |     3.47 |   0.01x |
| 199 | 1 381 |            623.07 |     4.57 |   0.01x |

### 5.2 Complex refactorize + solve

|  n  |  nnz  | SmallSolver (us) | KLU (us) | Speedup |
|----:|------:|------------------:|---------:|--------:|
|   5 |    23 |              0.15 |     0.22 |   1.45x |
|  10 |    58 |              0.47 |     0.50 |   1.07x |
|  25 |   163 |              3.61 |     1.37 |   0.38x |
|  50 |   338 |             21.40 |     2.83 |   0.13x |
|  87 |   597 |            102.06 |     5.08 |   0.05x |
| 100 |   688 |            151.52 |     5.79 |   0.04x |
| 150 | 1 038 |            477.51 |     8.74 |   0.02x |
| 199 | 1 381 |          1 106.08 |    11.63 |   0.01x |

### 5.3 Analysis

SmallSolver wins only for very small matrices (n <= 5 real, n <= 10 complex).
Beyond that, KLU's sparse factorization exploits the matrix sparsity that
the dense O(n^3) factorization cannot.

The crossover occurs much earlier than anticipated. For the banded matrices
typical of SPICE circuits (nnz/n ratio ~7), even at n=10 the dense
approach is marginally slower than KLU's sparse factorization. The factory
threshold was tuned to n < 12 based on these measurements.

SmallSolver avoids KLU's setup overhead for trivial circuits and provides
a self-contained fallback. The abstract `LinearSolver` interface allows
adding a true sparse LU solver in the future to close the gap with
ngspice's Sparse 1.3 in the n=25-200 range.

**Key observations:**

1. **Dense LU is O(n^3), sparse LU is O(nnz * fill).** For banded matrices
   with bandwidth 7, the sparse factorization scales nearly linearly with n,
   while dense factorization scales cubically.

2. **AMD ordering helps but cannot overcome the dense-vs-sparse gap.** For
   n >= 25, AMD reordering reduces the effective fill in the dense matrix,
   but it still processes n^2 elements instead of nnz.

3. **Complex factorization shows the same pattern** with SmallSolver winning
   at slightly larger n (up to ~10) due to the higher per-element overhead in
   KLU's complex routines.

4. **Solutions match.** Both solvers produce identical results (max diff < 1e-8)
   across all tested sizes, confirming numerical correctness.

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
