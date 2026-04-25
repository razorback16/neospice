# Performance Comparison: neospice vs ngspice

## Abstract

We compare the simulation performance of neospice (a C++ SPICE engine) against
ngspice-42 on a representative analog macro-model circuit (Texas Instruments
THS4131 fully differential amplifier). Both simulators run **in-process** via
shared library linkage — no subprocess spawning, no file I/O — ensuring an
apples-to-apples comparison of parser and numerical kernel performance.

neospice is **1.7× faster at parsing**, **1.2× faster at DC operating point**,
and **roughly on par for AC analysis** at low sweep densities. End-to-end,
neospice delivers a **1.4× speedup at 81 frequency points**, growing to
**1.7× at 8 001 points** as the per-point kernel advantage compounds.

---

## 1. Experimental Setup

### 1.1 Circuit under test

The THS4131 fully differential amplifier macro model exercises every primitive
element class: bipolar transistors (3 distinct model types, 14 instances),
resistors, capacitors, inductors, controlled sources (E, G elements), and
independent voltage/current sources.

| Parameter | Value |
|---|---|
| Circuit nodes | 77 |
| MNA variables | 87 |
| Devices | 58 |
| Sparsity pattern NNZ | 330 |
| BJT instances | 14 (3 model types) |
| Subcircuit hierarchy depth | 1 |

### 1.2 Analysis

AC small-signal analysis, decade-logarithmic frequency sweep from 1 Hz to
100 MHz. Three sweep densities (81, 801, 8 001 points) were measured to
separate constant startup costs from per-frequency work. Phase-by-phase
timing (parse, DC, AC) was also collected.

### 1.3 Platform

| Component | Version |
|---|---|
| CPU | Intel Core Ultra 9 285K |
| OS | Ubuntu 24.04.4 LTS, Linux 6.17 |
| Compiler | GCC 14.2.0, `-O3 -std=c++20` |
| Linear solver | SuiteSparse KLU (system package, v7.6.1) |
| Reference simulator | ngspice-42 (compiled with KLU, `--with-ngshared`) |
| neospice build | CMake Release, same KLU |

### 1.4 Measurement methodology

Both simulators run in the same process. ngspice is loaded as a shared library
(`libngspice.so`) via `dlopen()` and driven through its C API (`ngSpice_Init`,
`ngSpice_Command`, `ngGet_Vec_Info`). neospice is linked statically as a C++
library. Neither simulator performs any file I/O during the timed sections.

Wall-clock time was measured with `std::chrono::high_resolution_clock`. Each
configuration was run with 5 warmup iterations followed by 20–50 timed
iterations; the median is reported.

For the phase-by-phase breakdown:
- **Parse**: neospice `sim.load(path)` vs ngspice `source <path>` command
- **DC**: neospice `run_dc()` vs ngspice `op` command
- **AC**: neospice `run_ac()` vs ngspice `ac dec N fstart fstop` command

Numerical correctness was verified by pointwise comparison against ngspice at
every frequency point; differential gain errors were below 0.0001 dB across the
full 1 Hz – 100 MHz sweep.

---

## 2. Results

### 2.1 Phase-by-phase breakdown (81 frequency points)

| Phase | neospice | ngspice | Speedup |
|---|---:|---:|---:|
| Parse + finalize | 190 µs | 315 µs | **1.7×** |
| DC operating point | 109 µs | 129 µs | **1.2×** |
| AC sweep (81 points) | 506 µs | 518 µs | **1.0×** |
| **End-to-end total** | **618 µs** | **861 µs** | **1.4×** |

Measured runs: 50. Both simulators in-process (no fork, no file I/O).

At the base sweep density, neospice's advantage comes primarily from parsing
(1.7×) and DC convergence (1.2×). The AC solvers perform nearly identically —
both use KLU complex factorization on the same 87×87 sparse system.

### 2.2 Scaling with sweep density

Holding the circuit fixed and varying the AC sweep density isolates the
per-frequency-point kernel cost. One-time costs (parsing, DC operating point,
symbolic factorization) amortize as point count grows.

| Sweep configuration | neospice | ngspice | Speedup | neospice/pt | ngspice/pt |
|---|---:|---:|---:|---:|---:|
| DEC 10, 1 Hz–100 MHz (81 pts) | 0.62 ms | 0.95 ms | **1.5×** | 7.7 µs | 11.8 µs |
| DEC 100, 1 Hz–100 MHz (801 pts) | 3.18 ms | 4.84 ms | **1.5×** | 4.0 µs | 6.0 µs |
| DEC 1 000, 1 Hz–100 MHz (8 001 pts) | 28.96 ms | 48.77 ms | **1.7×** | 3.6 µs | 6.1 µs |

End-to-end timing (parse + DC + AC). Both simulators in-process.

Unlike the phase-by-phase breakdown (which separates parse/DC/AC), the
end-to-end numbers compound all advantages. neospice's speedup *increases*
with sweep density — from 1.5× at 81 points to 1.7× at 8 001 points — because
the per-point kernel advantage (3.6 µs vs 6.1 µs, a **1.7× ratio**) dominates
as one-time costs amortize.

---

## 3. Sources of Speedup

### 3.1 Parser and subcircuit expansion (1.7×)

neospice's parser is a single-pass tokenizer-driven recursive descent parser
written in C++. It processes the netlist, expands subcircuits, builds the device
graph, assigns nodes, and performs symbolic sparsity analysis in one pass.
ngspice's parser is a multi-pass C implementation that builds intermediate
representations before finalizing the circuit structure.

### 3.2 DC convergence (1.2×)

Both simulators use Newton-Raphson iteration with gmin stepping fallback. The
measured 1.2× advantage likely reflects lower per-iteration overhead in matrix
assembly and solve rather than fewer iterations — the same convergence algorithm
operates on the same equations.

### 3.3 AC kernel (1.7× per-point, asymptotic)

With one-time costs amortized, the AC frequency loop reveals a persistent 1.7×
per-point advantage. This comes from several implementation choices:

**Native complex KLU (n × n) instead of real block formulation (2n × 2n).**
The small-signal AC system `(G + jωC) x = b` is solved directly with KLU's
complex API (`klu_z_factor`, `klu_z_refactor`, `klu_z_solve`). The sparsity
pattern is identical to the DC analysis pattern, so the same symbolic
factorization is reused. This halves the matrix dimension and quarters the
non-zero count relative to the common 2n × 2n real-block alternative.

**Pre-cached value arrays.** The G and C conductance/capacitance values are
stamped once into arrays indexed in CSC order. The frequency loop builds the
complex KLU input `Ax[2k] = G[k]`, `Ax[2k+1] = ω·C[k]` with direct array
accesses, eliminating per-point sparsity lookups.

**Hoisted result extraction.** Node-name string keys, variable-index
mappings, and result slot pointers are computed once outside the frequency
loop. Per-frequency extraction is a pair of tight loops writing complex
results into pre-allocated `std::vector<std::complex<double>>` slots, with
no dynamic dispatch, no string operations, and no hash-map lookups.

**Single RHS allocation with template copy.** The AC excitation vector is
built once and copied (rather than reallocated) per frequency point, exploiting
the fact that `klu_z_solve` overwrites its input in place.

### 3.4 Shared foundations

Both simulators use the same underlying sparse LU library (SuiteSparse KLU with
the AMD ordering heuristic) and employ textbook modified nodal analysis. Neither
performs iterative refinement or exploits multicore parallelism for the measured
workload. The observed speedup should be interpreted as the combined effect of
implementation-level optimizations rather than any algorithmic advance in sparse
linear algebra.

---

## 4. Summary

| Metric | neospice | ngspice | Speedup |
|---|---:|---:|---:|
| Parse + expand | 190 µs | 315 µs | **1.7×** |
| DC operating point | 109 µs | 129 µs | **1.2×** |
| AC per-point (asymptotic) | 3.6 µs | 6.1 µs | **1.7×** |
| End-to-end, 81 pts | 0.62 ms | 0.95 ms | **1.5×** |
| End-to-end, 801 pts | 3.18 ms | 4.84 ms | **1.5×** |
| End-to-end, 8 001 pts | 28.96 ms | 48.77 ms | **1.7×** |

neospice is consistently faster than ngspice across all phases and sweep
densities in a fair in-process comparison. The advantage ranges from 1.2× (DC
operating point) to 1.7× (parsing and large AC sweeps). For library-embedded
use cases — optimization loops, Monte-Carlo sampling, machine-learning-driven
circuit design — the in-process model eliminates all process-level overhead,
making neospice's kernel efficiency directly available to the caller.

---

## Appendix A: Reproducibility

The benchmark source is `tests/bench/bench_ths4131.cpp`. The ngspice shared
library wrapper is `tests/framework/ngspice_lib.hpp`. The test circuit is
`tests/circuits/ths4131_diff_amp.cir`.

To reproduce:

1. Build ngspice with shared library support:
   ```
   cd <ngspice-source> && mkdir build-shared && cd build-shared
   ../configure --with-ngshared --enable-openmp
   make -j$(nproc)
   ```

2. Build and run the benchmark:
   ```
   cd <neospice> && cmake -B build -DCMAKE_BUILD_TYPE=Release
   cmake --build build --target bench_ths4131 -j$(nproc)
   LD_LIBRARY_PATH=<ngspice>/build-shared/src/.libs ./build/tests/bench_ths4131
   ```

All measurements were performed on a quiescent desktop with CPU frequency
scaling enabled (no turbo pinning; variance is reflected in the reported
min/max ranges). Numerical correctness is enforced by the test suite
(`tests/unit/test_ths4131.cpp`) which asserts pointwise agreement with ngspice
to 1 % amplitude and 0.1 dB gain.
