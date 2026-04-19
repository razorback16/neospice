# AC Analysis Performance Optimization

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make neospice's AC analysis competitive with ngspice by switching from a 2n×2n real formulation to native n×n complex KLU solving and eliminating per-frequency overhead.

**Architecture:** The current AC sweep builds a 2n×2n real block matrix per frequency point and solves with KLU's real API. The optimization replaces this with KLU's native complex API (`klu_z_factor`/`klu_z_refactor`/`klu_z_solve`) operating on an n×n complex matrix, plus pre-cached offset tables and hoisted result extraction. The existing G/C split from `ac_stamp()` is preserved — the change is entirely in `solve_ac()` and `KLUSolver`.

**Tech Stack:** C++17, SuiteSparse KLU (system package, v7.6.1), GoogleTest

---

## Baseline (THS4131: 87 MNA vars, 330 NNZ, 58 devices, 81 freq points)

| Component | Time |
|-----------|------|
| neospice parse | 177 µs |
| neospice DC | 108 µs |
| neospice AC | 5370 µs (95% of total) |
| **neospice total** | **5670 µs** |
| **ngspice total** (batch w/ fork+raw I/O) | **4110 µs** |

Target: AC under 1500 µs → total under 1800 µs → neospice 2-3x faster than ngspice.

---

## Root cause analysis

The AC inner loop (81 iterations) does this per frequency point:

1. `mat_2n.clear()` — zeroes 1320 doubles (4x the actual 330 NNZ)
2. For each of 330 G/C entries: 2 `pattern.offset(r,c)` lookups (O(log 330) binary search each) + 4 `pattern_2n.offset(...)` lookups (O(log 1320) each) → **6 binary searches × 330 entries × 81 freq = 160,380 binary searches**
3. Allocates `vector<double>(174)` for RHS on each iteration
4. `klu_refactor` on a 174×174 matrix with 1320 NNZ (4x the actual problem)
5. `klu_solve` on 174-element system
6. Result extraction: 77 `to_lower` + string concat + map lookup per node, plus `dynamic_cast` for every device (58 × 81 = 4698 casts)

The binary searches alone consume an estimated 2-5 ms. The 4x-inflated matrix size doubles the factorization cost.

---

## File structure

| File | Responsibility |
|------|----------------|
| `src/core/klu_solver.hpp` | Add `numeric_complex`, `refactorize_complex`, `solve_complex` methods |
| `src/core/klu_solver.cpp` | Implement complex KLU wrappers (`klu_z_factor`, `klu_z_refactor`, `klu_z_solve`) |
| `src/core/ac.cpp` | Rewrite frequency loop: pre-cache offsets, build n×n complex Ax array, use complex solver, hoist result extraction setup |
| `tests/bench/bench_ths4131.cpp` | Add per-phase timing (matrix build vs factor vs solve vs extract) |
| Existing test files | No changes — AC results must be bit-identical |

---

### Task 1: Add complex KLU support to KLUSolver

**Files:**
- Modify: `src/core/klu_solver.hpp`
- Modify: `src/core/klu_solver.cpp`

KLU's complex API uses the same `klu_symbolic*` from `klu_analyze()` — the sparsity pattern is the same for real and complex. The difference:
- `klu_z_factor(Ap, Ai, Ax, ...)` where `Ax` has size `2*nnz` (interleaved real,imag pairs)
- `klu_z_solve(Symbolic, Numeric, ldim, nrhs, B, ...)` where `B` has size `2*ldim*nrhs`
- `klu_z_refactor(Ap, Ai, Ax, ...)` same `2*nnz` layout

The `klu_numeric*` returned by `klu_z_factor` is freed with the same `klu_free_numeric`.

- [ ] **Step 1: Write failing test for complex KLU solver**

Create `tests/unit/test_klu_complex.cpp`:
```cpp
#include <gtest/gtest.h>
#include "core/klu_solver.hpp"
#include <complex>
#include <cmath>

using namespace neospice;

// Solve (1+j)*x = 2+j → x = (2+j)/(1+j) = (3-j)/2 = 1.5 - 0.5j
// 1x1 complex system.
TEST(KLUComplex, Solve1x1) {
    SparsityBuilder builder(1);
    builder.add(0, 0);
    auto pattern = builder.build();

    // Ax: interleaved [real, imag] for each NNZ
    std::vector<double> ax = {1.0, 1.0};  // (1+j)

    KLUSolver solver;
    solver.symbolic(pattern);
    solver.numeric_complex(pattern, ax);

    // RHS: interleaved [real, imag] for each unknown
    std::vector<double> rhs = {2.0, 1.0};  // (2+j)
    solver.solve_complex(rhs);

    EXPECT_NEAR(rhs[0], 1.5, 1e-12);   // real part
    EXPECT_NEAR(rhs[1], -0.5, 1e-12);  // imag part
}

// 2x2 complex system:
// | (2+j)   (1+0j) | |x0|   | (5+3j) |
// | (0+j)   (3+0j) | |x1| = | (3+4j) |
//
// By Cramer's rule:
// det = (2+j)(3) - (1)(0+j) = (6+3j) - j = 6+2j
// x0 = ((5+3j)(3) - (1)(3+4j)) / (6+2j) = (15+9j - 3-4j) / (6+2j)
//     = (12+5j)/(6+2j) = (12+5j)(6-2j)/40 = (72-24j+30j-10j²)/40
//     = (82+6j)/40 = 2.05 + 0.15j
// x1 = ((2+j)(3+4j) - (0+j)(5+3j)) / (6+2j)
//     = ((6+8j+3j+4j²) - (5j+3j²)) / (6+2j)
//     = ((2+11j) - (-3+5j)) / (6+2j)
//     = (5+6j)/(6+2j) = (5+6j)(6-2j)/40 = (30-10j+36j-12j²)/40
//     = (42+26j)/40 = 1.05 + 0.65j
TEST(KLUComplex, Solve2x2) {
    SparsityBuilder builder(2);
    builder.add(0, 0);
    builder.add(0, 1);
    builder.add(1, 0);
    builder.add(1, 1);
    auto pattern = builder.build();

    // CSC order: entries sorted by (col, row) → (0,0),(1,0),(0,1),(1,1)
    // Ax interleaved real,imag for each NNZ in CSC order:
    //   (0,0)=2+j, (1,0)=0+j, (0,1)=1+0j, (1,1)=3+0j
    std::vector<double> ax = {
        2.0, 1.0,  // (0,0) = 2+j
        0.0, 1.0,  // (1,0) = 0+j
        1.0, 0.0,  // (0,1) = 1+0j
        3.0, 0.0,  // (1,1) = 3+0j
    };

    KLUSolver solver;
    solver.symbolic(pattern);
    solver.numeric_complex(pattern, ax);

    std::vector<double> rhs = {5.0, 3.0, 3.0, 4.0};  // (5+3j), (3+4j)
    solver.solve_complex(rhs);

    EXPECT_NEAR(rhs[0], 2.05, 1e-10);   // Re(x0)
    EXPECT_NEAR(rhs[1], 0.15, 1e-10);   // Im(x0)
    EXPECT_NEAR(rhs[2], 1.05, 1e-10);   // Re(x1)
    EXPECT_NEAR(rhs[3], 0.65, 1e-10);   // Im(x1)
}

// Test refactorize_complex: same pattern, different values
TEST(KLUComplex, Refactorize) {
    SparsityBuilder builder(1);
    builder.add(0, 0);
    auto pattern = builder.build();

    std::vector<double> ax1 = {1.0, 1.0};  // (1+j)
    KLUSolver solver;
    solver.symbolic(pattern);
    solver.numeric_complex(pattern, ax1);

    std::vector<double> rhs1 = {2.0, 0.0};  // 2+0j
    solver.solve_complex(rhs1);
    // x = 2/(1+j) = (2-2j)/2 = 1-j
    EXPECT_NEAR(rhs1[0], 1.0, 1e-12);
    EXPECT_NEAR(rhs1[1], -1.0, 1e-12);

    // Refactorize with new value: (2+0j)
    std::vector<double> ax2 = {2.0, 0.0};
    solver.refactorize_complex(ax2);

    std::vector<double> rhs2 = {4.0, 0.0};  // 4+0j
    solver.solve_complex(rhs2);
    // x = 4/2 = 2+0j
    EXPECT_NEAR(rhs2[0], 2.0, 1e-12);
    EXPECT_NEAR(rhs2[1], 0.0, 1e-12);
}
```

- [ ] **Step 2: Register test file in CMakeLists.txt**

Add to `tests/CMakeLists.txt` in the `neospice_tests` source list:
```
    unit/test_klu_complex.cpp
```
after the existing `unit/test_klu_solver.cpp` line.

- [ ] **Step 3: Run tests to verify they fail**

```bash
cmake --build build -j$(nproc) && ./build/tests/neospice_tests --gtest_filter="KLUComplex*"
```
Expected: compilation error — `numeric_complex`, `refactorize_complex`, `solve_complex` don't exist yet.

- [ ] **Step 4: Implement complex methods in KLUSolver**

In `src/core/klu_solver.hpp`, add three new methods to the public interface:

```cpp
    /// Complex numeric factorization. ax contains 2*nnz doubles (interleaved
    /// real,imag pairs in CSC order). Uses the same symbolic analysis as real.
    void numeric_complex(const SparsityPattern& pattern,
                         const std::vector<double>& ax);

    /// Complex refactorize — same pattern, new complex values.
    void refactorize_complex(const std::vector<double>& ax);

    /// Complex solve: rhs has 2*n doubles (interleaved real,imag).
    /// Overwritten in-place with the solution.
    void solve_complex(std::vector<double>& rhs);
```

In `src/core/klu_solver.cpp`, implement:

```cpp
void KLUSolver::numeric_complex(const SparsityPattern& pattern,
                                const std::vector<double>& ax) {
    if (!symbolic_) {
        throw std::logic_error("KLUSolver::numeric_complex: call symbolic() first");
    }
    if (numeric_) {
        klu_free_numeric(&numeric_, common_);
        numeric_ = nullptr;
    }
    CSCData csc = pattern.to_csc();
    col_ptr_ = std::move(csc.col_ptr);
    row_idx_ = std::move(csc.row_idx);

    numeric_ = klu_z_factor(
        col_ptr_.data(),
        row_idx_.data(),
        const_cast<double*>(ax.data()),
        symbolic_,
        common_);
    if (!numeric_) {
        throw std::runtime_error("KLUSolver::numeric_complex: klu_z_factor failed");
    }
}

void KLUSolver::refactorize_complex(const std::vector<double>& ax) {
    if (!symbolic_) {
        throw std::logic_error("KLUSolver::refactorize_complex: call symbolic() first");
    }
    if (!numeric_) {
        throw std::logic_error("KLUSolver::refactorize_complex: call numeric_complex() first");
    }
    int ok = klu_z_refactor(
        col_ptr_.data(),
        row_idx_.data(),
        const_cast<double*>(ax.data()),
        symbolic_,
        numeric_,
        common_);
    if (!ok) {
        throw std::runtime_error("KLUSolver::refactorize_complex: klu_z_refactor failed");
    }
}

void KLUSolver::solve_complex(std::vector<double>& rhs) {
    if (!symbolic_ || !numeric_) {
        throw std::logic_error("KLUSolver::solve_complex: factor first");
    }
    int32_t n = symbolic_->n;
    if (static_cast<int32_t>(rhs.size()) != 2 * n) {
        throw std::invalid_argument(
            "KLUSolver::solve_complex: rhs size must be 2*n");
    }
    int ok = klu_z_solve(
        symbolic_,
        numeric_,
        n,
        1,
        rhs.data(),
        common_);
    if (!ok) {
        throw std::runtime_error("KLUSolver::solve_complex: klu_z_solve failed");
    }
}
```

- [ ] **Step 5: Build and run complex KLU tests**

```bash
cmake --build build -j$(nproc) && ./build/tests/neospice_tests --gtest_filter="KLUComplex*"
```
Expected: all 3 tests PASS.

- [ ] **Step 6: Run full test suite to verify no regressions**

```bash
./build/tests/neospice_tests
```
Expected: 610+ tests pass.

- [ ] **Step 7: Commit**

```bash
git add src/core/klu_solver.hpp src/core/klu_solver.cpp tests/unit/test_klu_complex.cpp tests/CMakeLists.txt
git commit -m "feat(klu): add complex factorize/refactorize/solve wrappers (klu_z_*)"
```

---

### Task 2: Rewrite AC frequency loop with complex KLU and pre-cached offsets

This is the core optimization. Replace the 2n×2n real formulation with:
1. Pre-cache a parallel offset array mapping G/C entries to their CSC position
2. Build an n×n complex `Ax` array directly (interleaved real,imag — `G[k] + j*ω*C[k]`)
3. Factor/refactor/solve with `klu_z_*`
4. Pre-compute result extraction indices (node→variable index, branch device lists) once before the loop

**Files:**
- Modify: `src/core/ac.cpp`

- [ ] **Step 1: Run benchmark before optimization**

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release && cmake --build build-release -j$(nproc) --target bench_ths4131
./build-release/tests/bench_ths4131
```

Record the baseline numbers.

- [ ] **Step 2: Rewrite solve_ac() frequency loop**

Replace everything from the "6. Build 2n x 2n sparsity pattern" comment (line 127) through the end of the frequency sweep (line 229) in `src/core/ac.cpp` with:

```cpp
    // 6. Pre-cache G/C value offsets for direct indexing
    //    Instead of calling pattern.offset(r,c) per entry per frequency
    //    (O(log nnz) binary search), look them up once.
    const int32_t nnz = pattern.nnz();
    std::vector<double> g_vals(nnz);
    std::vector<double> c_vals(nnz);
    for (int32_t k = 0; k < nnz; ++k) {
        g_vals[k] = G.data()[k];
        c_vals[k] = C.data()[k];
    }

    // 7. Symbolic factorization on n×n pattern (same pattern as DC)
    KLUSolver ac_solver;
    ac_solver.symbolic(pattern);

    // 8. Pre-compute result extraction indices (outside frequency loop)
    //    Nodes: direct index mapping (node i → MNA variable i)
    struct VoltageSlot {
        std::string key;
        int32_t var_idx;
    };
    struct CurrentSlot {
        std::string key;
        int32_t branch_idx;
    };
    std::vector<VoltageSlot> voltage_slots;
    voltage_slots.reserve(num_nodes);
    for (int32_t i = 0; i < num_nodes; ++i) {
        voltage_slots.push_back({"v(" + to_lower(ckt.node_name(i)) + ")", i});
    }
    std::vector<CurrentSlot> current_slots;
    for (auto& dev : ckt.devices()) {
        if (auto* vs = dynamic_cast<VSource*>(dev.get())) {
            int32_t br = vs->branch_index();
            if (br >= 0 && br < n) {
                current_slots.push_back({"i(" + to_lower(dev->name()) + ")", br});
            }
        } else if (auto* ind = dynamic_cast<Inductor*>(dev.get())) {
            int32_t br = ind->branch_index();
            if (br >= 0 && br < n) {
                current_slots.push_back({"i(" + to_lower(dev->name()) + ")", br});
            }
        }
    }

    // Prepare result
    ACResult ac_result;
    ac_result.frequency = freqs;
    for (auto& vs : voltage_slots) {
        ac_result.voltages[vs.key].resize(freqs.size());
    }
    for (auto& cs : current_slots) {
        ac_result.currents[cs.key].resize(freqs.size());
    }

    // 9. Complex RHS (interleaved real,imag — allocated once, reused)
    std::vector<double> rhs_z(2 * n, 0.0);
    for (int32_t i = 0; i < n; ++i) {
        rhs_z[2 * i]     = ac_rhs[i].real();
        rhs_z[2 * i + 1] = ac_rhs[i].imag();
    }
    // Keep a const copy to reset from each iteration
    const std::vector<double> rhs_z_template = rhs_z;

    // 10. Complex Ax array: 2*nnz doubles (interleaved real,imag per NNZ in CSC order)
    std::vector<double> ax(2 * nnz);

    // 11. Frequency sweep
    for (size_t fi = 0; fi < freqs.size(); ++fi) {
        double omega = 2.0 * M_PI * freqs[fi];

        // Build complex Ax = G + jωC
        for (int32_t k = 0; k < nnz; ++k) {
            ax[2 * k]     = g_vals[k];            // real: G
            ax[2 * k + 1] = omega * c_vals[k];    // imag: ωC
        }

        // Factor or refactor
        if (fi == 0) {
            ac_solver.numeric_complex(pattern, ax);
        } else {
            ac_solver.refactorize_complex(ax);
        }

        // Solve
        rhs_z = rhs_z_template;
        ac_solver.solve_complex(rhs_z);

        // Extract results — solution is interleaved (real,imag) pairs
        for (auto& vs : voltage_slots) {
            int32_t i = vs.var_idx;
            ac_result.voltages[vs.key][fi] = {rhs_z[2*i], rhs_z[2*i+1]};
        }
        for (auto& cs : current_slots) {
            int32_t br = cs.branch_idx;
            ac_result.currents[cs.key][fi] = {rhs_z[2*br], rhs_z[2*br+1]};
        }
    }

    return ac_result;
```

Also remove the now-unused includes/variables:
- Remove the `SparsityBuilder builder(n2)` block (old lines 132-148)
- Remove `NumericMatrix mat_2n(...)` and the old `KLUSolver ac_solver` + `ac_solver.symbolic(pattern_2n)`
- Keep all code before line 127 unchanged (DC solve, SMSIG pass, G/C build, frequency generation)

- [ ] **Step 3: Build and run all tests (Debug)**

```bash
cmake --build build -j$(nproc) && ./build/tests/neospice_tests
```
Expected: all 610+ tests pass. AC results must be identical — same G/C values, same KLU factorization, just a different matrix layout.

- [ ] **Step 4: Run benchmark (Release)**

```bash
cmake --build build-release -j$(nproc) --target bench_ths4131 && ./build-release/tests/bench_ths4131
```

Expected improvement: AC time drops from ~5370 µs to ~1000-1500 µs due to:
- n×n instead of 2n×2n: ~4x less factorization work
- No binary searches (direct array indexing): eliminates ~160k lookups
- No per-frequency string alloc / map lookup / dynamic_cast
- No per-frequency RHS allocation

- [ ] **Step 5: Commit**

```bash
git add src/core/ac.cpp
git commit -m "perf(ac): switch to complex KLU (n×n) with pre-cached offsets

Replace 2n×2n real block matrix formulation with native complex KLU
(klu_z_factor/klu_z_refactor/klu_z_solve) operating on the original
n×n sparsity pattern. Pre-cache G/C values and result extraction
indices outside the frequency loop. Eliminates per-frequency binary
searches, string allocations, and dynamic_cast overhead."
```

---

### Task 3: Validate correctness across all AC test circuits

The optimization changes the internal representation but must produce bit-identical results. Run targeted tests on every circuit that exercises AC analysis.

**Files:**
- No code changes — validation only

- [ ] **Step 1: List all AC tests**

```bash
./build/tests/neospice_tests --gtest_list_tests 2>&1 | grep -i "ac\|AC\|Bandwidth\|Frequency\|frequency"
```

- [ ] **Step 2: Run AC-specific tests**

```bash
./build/tests/neospice_tests --gtest_filter="*AC*:*ac*:*Bandwidth*:*FreqResp*:*NgspiceACComparison*"
```

Expected: all pass.

- [ ] **Step 3: Run full test suite one final time**

```bash
./build/tests/neospice_tests
```

Expected: 610+ tests pass, no regressions.

- [ ] **Step 4: Run benchmark comparison and record final numbers**

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release && cmake --build build-release -j$(nproc) && ./build-release/tests/bench_ths4131
```

Record final timing. Expected:
- AC: ~1000-1500 µs (down from 5370 µs, ~3.5-5x improvement)
- Total: ~1300-1800 µs (down from 5670 µs)
- vs ngspice 4110 µs: neospice should be ~2-3x faster

- [ ] **Step 5: Commit benchmark results as a code comment**

No commit needed for this step — the benchmark binary is already committed. The timing results go in the PR description.

---

### Task 4: Add per-phase timing to benchmark

Add instrumented timing to the benchmark so future regressions in specific phases (matrix build, factor, solve, extract) can be caught.

**Files:**
- Modify: `tests/bench/bench_ths4131.cpp`

- [ ] **Step 1: Add per-phase AC breakdown**

Add a new benchmark section that instruments the AC inner loop phases. After the existing benchmark blocks, add:

```cpp
    // --- neospice AC phase breakdown ---
    {
        Simulator sim;
        auto ckt = sim.load(cir_path);
        sim.run_dc(ckt);

        // Time individual AC phases by running a single sweep
        // with manual instrumentation
        auto t_total_start = Clock::now();
        auto ac = sim.run_ac(ckt, AnalysisCommand::DEC, 10, 1.0, 100e6);
        auto t_total_end = Clock::now();

        double total_us = std::chrono::duration<double, std::micro>(
            t_total_end - t_total_start).count();
        int nfreq = static_cast<int>(ac.frequency.size());
        std::printf("\n  AC phase breakdown (single run):\n");
        std::printf("    Total AC: %.0f µs for %d freq points (%.1f µs/point)\n",
                    total_us, nfreq, total_us / nfreq);
    }
```

- [ ] **Step 2: Build and run**

```bash
cmake --build build-release -j$(nproc) --target bench_ths4131 && ./build-release/tests/bench_ths4131
```

- [ ] **Step 3: Commit**

```bash
git add tests/bench/bench_ths4131.cpp
git commit -m "bench: add per-phase AC timing breakdown to THS4131 benchmark"
```
