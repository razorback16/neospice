# Performance Comparison: neospice vs ngspice

## Abstract

This benchmark compares neospice against ngspice-42 across parsing, DC
operating point, AC small-signal, transient, noise, DC sweep, and end-to-end
workloads. Both simulators run in-process through library calls, so the timed
sections avoid subprocess startup and raw-file I/O.

The reference ngspice run uses its default direct solver path: Sparse 1.3.
`.options klu` is not used. neospice uses its in-tree Sparse 1.3-compatible
NeoSolver with no KLU dependency.

Current release results show neospice faster on every benchmark except the
long THS4131 8 001-point AC sweep, where ngspice's complex Sparse path remains
1.0x faster. The OPA1632 end-to-end benchmark now runs 1.9x faster in neospice
while converging to the ngspice operating point.

---

## 1. Experimental Setup

| Component | Version |
|---|---|
| CPU | Intel Core Ultra 9 285K |
| OS | Ubuntu 24.04.4 LTS, Linux 6.17 |
| Compiler | GCC 14.2.0, `-O3 -std=c++20` |
| neospice solver | NeoSolver, self-contained Sparse 1.3-compatible LU |
| ngspice solver | Sparse 1.3 default path; `.options klu` not used |
| ngspice version | 42, system `libngspice0` package |
| neospice build | CMake Release |

Both simulators run in the same process. ngspice is linked through
`libngspice.so` and driven with the C API (`ngSpice_Init`, `ngSpice_Command`).
neospice is linked as a C++ library. The benchmark uses 3 warmup iterations
and 30 timed iterations; the median is reported.

---

## 2. Solver Architecture

NeoSolver is a standalone Sparse 1.3-compatible direct solver. It does not
link SuiteSparse KLU, AMD, COLAMD, or BTF.

Important parity details:

1. Matrix structure is taken from device stamping only. neospice does not add
   blanket structural diagonal placeholders because ngspice Sparse 1.3 does
   not add those to the original matrix pattern either.
2. MNA preordering runs inside NeoSolver before factorization, matching the
   Sparse 1.3 path used by ngspice.
3. Diagonal gmin is applied to the solver's internal diagonal after MNA
   preordering, matching ngspice `LoadGmin(Matrix, CKTdiagGmin)` behavior.
4. Real DC/transient and complex AC/noise analyses use the same in-tree solver
   family; there is no external KLU tier.

For the OPA1632 benchmark circuit, this makes the original sparse pattern
match ngspice at 983 nonzeros.

---

## 3. Comprehensive Benchmark

| Benchmark | neospice | ngspice | Factor | Winner |
|---|---:|---:|---:|---|
| **Parse** | | | | |
| THS4131 (77 nodes, 58 devices) | 312 us | 435 us | **1.4x** | neospice |
| Resistor divider (3 devices) | 11 us | 45 us | **4.1x** | neospice |
| **DC Operating Point** | | | | |
| THS4131 (14 BJTs) | 483 us | 623 us | **1.3x** | neospice |
| Resistor divider | 10 us | 54 us | **5.4x** | neospice |
| **AC Small-Signal** | | | | |
| THS4131, DEC 10, 81 pts | 706 us | 1.04 ms | **1.5x** | neospice |
| THS4131, DEC 1000, 8 001 pts | 23.33 ms | 22.35 ms | **1.0x** | ngspice |
| RC lowpass, DEC 10, 91 pts | 18 us | 120 us | **6.7x** | neospice |
| **Transient** | | | | |
| RC lowpass, 500 us | 284 us | 1.17 ms | **4.1x** | neospice |
| RLC series, 100 us | 397 us | 1.66 ms | **4.2x** | neospice |
| Pulse source, 100 us | 118 us | 1.02 ms | **8.6x** | neospice |
| **Noise** | | | | |
| Resistor divider, DEC 10, 91 pts | 54 us | 91 us | **1.7x** | neospice |
| **DC Sweep** | | | | |
| V1 -5..+5 V, 1 001 pts | 207 us | 847 us | **4.1x** | neospice |
| **End-to-End** | | | | |
| THS4131 (.op + .ac dec 10) | 633 us | 746 us | **1.2x** | neospice |
| OPA1632 (.op + .ac dec 10) | 3.59 ms | 6.72 ms | **1.9x** | neospice |

The sum of the individual non-composite benchmarks is 25.93 ms for neospice
and 29.46 ms for ngspice, a 1.14x aggregate advantage for neospice. The total
is dominated by the dense 8 001-point AC sweep.

---

## 4. Analysis

neospice's main wins come from lower parser overhead, lower per-iteration DC
solve overhead, AC conductance/capacitance matrix caching, and efficient
structure-reusing refactorization. Small circuits benefit most because
ngspice has a larger fixed setup cost.

The previous OPA1632 performance gap was caused by solver-structure
mismatches, not by missing KLU. neospice had been adding generic diagonal
placeholders that changed the Sparse 1.3 Markowitz counts and pivot path. It
also applied continuation gmin through the external MNA matrix rather than the
Sparse-internal diagonal used after MNA preordering. Fixing those two details
brought the OPA1632 matrix path into parity with ngspice Sparse 1.3 and made
neospice faster on the benchmark.

The remaining ngspice win is the long THS4131 AC sweep. That path is dominated
by repeated complex sparse refactorization; improving it should focus on the
complex Sparse-compatible factor/refactor path rather than adding KLU.

---

## 5. NeoSolver Scaling Benchmark

NeoSolver's sparse tier handles all matrix sizes with no external solver
dependencies. The synthetic scaling benchmark uses banded sparse matrices with
bandwidth 7 and diagonal dominance, representative of local SPICE matrix
connectivity.

| n | nnz | Real refactorize + solve | Complex refactorize + solve |
|---:|---:|---:|---:|
| 5 | 23 | 0.10 us | 0.16 us |
| 10 | 58 | 0.32 us | 0.47 us |
| 25 | 163 | 0.70 us | 0.76 us |
| 50 | 338 | 1.40 us | 1.56 us |
| 100 | 688 | 2.90 us | 3.15 us |
| 300 | 2 088 | 6.89 us | 9.62 us |
| 1 000 | 6 988 | 22.10 us | 32.85 us |
| 10 000 | 69 988 | 224.63 us | 334.13 us |

The synthetic benchmark scales approximately linearly with nonzeros for this
matrix family. Complex factorization is slower because each stored element
carries complex arithmetic, but it follows the same in-tree Sparse-compatible
solver architecture.
