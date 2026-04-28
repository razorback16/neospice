# OPA1632 Performance Regression: Root Cause Analysis and Research

## Problem Statement

The OPA1632 fully-differential amplifier circuit (357 elements, 48 VCVSs, n=257 MNA variables) runs **29.6x slower** than ngspice:
- neospice: 188ms E2E (DC OP ~112ms)
- ngspice:  6.4ms E2E

All other circuits (THS4131, resistor dividers, RC/RLC, etc.) are 1.1x-7.9x **faster** than ngspice. The problem is isolated to this one complex opamp model.

## Current State (843/843 tests passing)

### What We Built
- Custom NeoSolver with AMD ordering + maximum transversal matching + Gilbert-Peierls reach
- Selective gmin injection (organic diagonal tracking) — fixed 3 ASRC test regressions
- Left-looking column-LU with threshold pivoting
- Refactorize path (reuses L/U structure, recomputes values)

### What We Tried and Ruled Out

| Approach | Result |
|----------|--------|
| **Perturbation in refactorize** (±1e-12 for near-zero pivots) | Newton diverged — corrupted LU factors propagate through n=257 factorization |
| **Maximum transversal matching** (BFS augmenting paths) | No perf improvement. Fill-in nearly identical. Matching places structural nonzeros on diagonal but doesn't prevent near-zero *reduced* pivots |
| **BTF decomposition** (Tarjan SCC) | OPA1632 barely decomposes: one 239-var block out of 257 total. No benefit |
| **Gilbert-Peierls reach** (DFS-based sparse left-looking) | Correct, all tests pass, but no measurable speedup — the bottleneck is NOT in sparse_factor |
| **Nonzero tracking** (in_nz flag array for pivot/L-column scans) | Eliminated O(n) scans within sparse_factor, but inner loop was never the bottleneck |

## Root Cause: Convergence Cascade, Not Solver Speed

### Profiling Results

Instrumentation of the Newton solver revealed:
- **0 refactorize fallbacks** — refactorize succeeds every time (selective gmin fixed this)
- Each refactorize takes **~3µs**, each numeric takes **~48µs**
- **145 newton_solve calls** with **5,685 total solver invocations** for a single DC OP
- Average **~39 Newton iterations** per newton_solve call

### Convergence Flow Trace

```
[initial] newton_solve → FAILED (100 iters, never converges)
[gmin]    gmin=0.01    → FAILED after 100 iters
[src]     frac=0.05    → converged in 58 iters
[src]     frac=0.15    → converged in 37 iters
[src]     frac=0.25    → converged in 38 iters
[src]     frac=0.35    → converged in 100 iters  (barely)
[src]     frac=0.45    → FAILED
[src]     frac=0.40    → converged in 32 iters
[src]     frac=0.50    → FAILED
[src]     frac=0.45    → FAILED
[src]     frac=0.425   → FAILED
...step halving continues...
[src]     frac=0.400195 → FAILED (step < min_step, source stepping FAILS)
[pseudo_transient] → eventually converges (this is the 4th fallback)
```

**The circuit hits a bifurcation point at ~40% source amplitude.** Source stepping gets stuck and wastes enormous effort halving steps. Then pseudo-transient continuation rescues it, but the total work is massive.

### Why ngspice Is Fast

ngspice uses a completely different convergence strategy:

1. **Dynamic gmin stepping (Gillespie algorithm)** — `cktop.c:dynamic_gmin()`:
   - Adaptive step sizing based on iteration count (fewer iters → bigger steps, more iters → smaller steps)
   - Saves/restores solution state on failure (can backtrack)
   - Uses `factor = sqrt(factor)` for refinement instead of fixed 10x reduction

2. **Gillespie source stepping** — `cktop.c:gillespie_src()`:
   - Starts with `raise = 0.001` (0.1%), much finer initial step than our 10%
   - Adaptive: `raise *= 1.5` when fast convergence, `raise *= 0.5` when slow
   - On failure: `raise /= 10` (aggressive reduction) and restores old solution + state
   - Minimum step: `1e-8` fraction (vs our `1e-4`)
   - **Saves/restores both node voltages AND device state** (`OldCKTstate0`)

3. **Reordering on mode transitions** — `niiter.c` line 249:
   ```c
   } else if(ckt->CKTmode & MODEINITJCT) {
       ckt->CKTmode = (ckt->CKTmode&(~INITF))|MODEINITFIX;
       ckt->CKTniState |= NISHOULDREORDER;  // <-- FORCES REORDER
   }
   ```
   ngspice forces a full Markowitz reorder when transitioning from INITJCT to INITFIX. This re-pivots with current values, getting better numerical conditioning.

4. **Node damping** — `niiter.c` lines 204-229:
   ```c
   if (ckt->CKTnodeDamping != 0 && ckt->CKTnoncon != 0 && ...) {
       if (maxdiff > 10) {
           damp_factor = 10/maxdiff;
           if (damp_factor < 0.1) damp_factor = 0.1;
           // damp BOTH node voltages AND device states
       }
   }
   ```
   When Newton takes large steps (max voltage change > 10V), ngspice damps both the solution AND the device states. We don't damp device states at all.

5. **Sparse 1.3 Markowitz solver** — `spfactor.c`:
   - Value-aware dynamic pivoting interleaved with elimination (NOT a fixed AMD ordering)
   - `spOrderAndFactor()` tries fixed-order refactorize first, falls back to partial reorder at the first column where the pivot fails threshold test
   - `spFactor()` (the "refactorize") uses the SAME linked-list structure for fill-in — no separate L/U CSC arrays
   - Sparse linked-list structure means no O(n) scans at all — follow NextInRow/NextInCol pointers

### Key Differences Summary

| Aspect | ngspice | neospice |
|--------|---------|----------|
| **Pivot strategy** | Markowitz (value-aware, dynamic per-factorization) | AMD (structural-only, fixed at symbolic time) |
| **Reorder on mode change** | Yes (INITJCT→INITFIX forces reorder) | No (same AMD order throughout) |
| **gmin stepping** | Dynamic (Gillespie: adaptive factor, backtrack with state restore) | Fixed 10x reduction, no backtrack |
| **Source stepping** | Gillespie: 0.1% initial step, adaptive ×1.5/÷10, saves device state, min step 1e-8 | Fixed 10% initial step, ×2/÷2, no device state save, min step 1e-4 |
| **Node damping** | Damps both voltages AND device states when step > 10V | `limit_voltages()` on devices only, no global damping |
| **Factorization fallback** | Partial reorder from failed column onward | Full numeric() from scratch |
| **Matrix storage** | Linked-list (direct fill-in, O(1) traversal) | CSC + dense accumulator (O(nnz) per column) |

## Reference Code Locations

### ngspice (~/Codes/ngspice)

| File | What It Does |
|------|-------------|
| `src/maths/ni/niiter.c` — `NIiter()` | Newton iteration loop. Mode transitions, reorder flags, node damping, convergence test. 279 lines. |
| `src/spicelib/analysis/cktop.c` — `CKTop()` | DC convergence cascade: direct solve → dynamic_gmin → gillespie_src. Dispatcher for all strategies. |
| `src/spicelib/analysis/cktop.c` — `dynamic_gmin()` | Gillespie's adaptive gmin stepping. Saves/restores solution+state, adaptive factor via sqrt(). Lines 127-258. |
| `src/spicelib/analysis/cktop.c` — `gillespie_src()` | Gillespie's adaptive source stepping. Initial raise=0.001, adaptive ×1.5/÷10, saves state. Lines 354-546. |
| `src/maths/sparse/spfactor.c` — `spOrderAndFactor()` | Markowitz ordering + factorization in one pass. Tries fixed-order first, partial reorder on pivot failure. Lines 191-284. |
| `src/maths/sparse/spfactor.c` — `spFactor()` | Refactorize using existing pivot order. Left-looking with direct/indirect scatter-gather. Lines 322-414. |
| `src/maths/sparse/spfactor.c` — `RealRowColElimination()` | Gaussian elimination for one pivot. Outer product update with linked-list fill-in. Lines 2554-2598. |

### SuiteSparse KLU (~/Codes/SuiteSparse)

| File | What It Does |
|------|-------------|
| `KLU/Source/klu_kernel.c` — `dfs()` | Gilbert-Peierls DFS with symmetric pruning (Eisenstat-Liu). The reference implementation for reach computation. Lines 24-119. |
| `KLU/Source/klu_kernel.c` — `lsolve_symbolic()` | Symbolic reach: calls dfs() from nonzero rows of A(:,k), produces topological order in Stack[top..n-1]. Lines 128-203. |
| `KLU/Source/klu_kernel.c` — `lsolve_numeric()` | Numeric left-looking solve using the topological order from lsolve_symbolic. Lines 311-351. |
| `KLU/Source/klu_kernel.c` — `lpivot()` | Partial pivoting: diagonal preference with tolerance, scales L column by pivot. Lines 360-513. |
| `KLU/Source/klu_factor.c` — `factor2()` | BTF block dispatch: singletons handled directly, larger blocks go to klu_kernel_factor. Lines 21-382. |

### neospice (src/core/)

| File | What It Does |
|------|-------------|
| `neo_solver.cpp` — `sparse_factor()` | Left-looking LU with Gilbert-Peierls reach + threshold pivoting. Lines 107-221. |
| `neo_solver.cpp` — `sparse_refactor()` | Recompute L/U values using existing structure/pivots. Lines 224-265. |
| `neo_solver.cpp` — `symbolic()` | AMD ordering + maximum transversal + permuted CSC build. Lines 13-48. |
| `newton.cpp` — `newton_solve()` | Newton iteration with init-phase mode cascade (mirrors niiter.c). Lines 10-219. |
| `convergence.cpp` — `gmin_stepping()` | Fixed 10x gmin reduction, no backtrack. Lines 11-57. |
| `convergence.cpp` — `source_stepping()` | Adaptive source stepping with ×2/÷2, min_step=1e-4, no state save. Lines 59-146. |
| `convergence.cpp` — `pseudo_transient()` | Pseudo-transient continuation (our 4th fallback). Lines 148-205. |
| `circuit.cpp` — `finalize()` | Sparsity pattern build, organic diagonal tracking. Lines 76-137. |

## Recommended Fix Strategy

The bottleneck is NOT the sparse solver — it's the convergence cascade. The solver is fast enough (3µs refactorize, 48µs numeric for n=257). The problem is:

1. **gmin stepping fails immediately** (first step at gmin=0.01 doesn't converge in 100 iters)
2. **Source stepping gets stuck at 40%** (bifurcation point, step halving wastes effort)
3. **Only pseudo-transient works**, but by then we've burned 100+ failed newton_solve calls

### Priority 1: Implement Gillespie-style dynamic gmin stepping
Port `dynamic_gmin()` from ngspice `cktop.c`:
- Adaptive factor (sqrt when slow, square when fast)
- Save/restore both solution AND device state on failure
- Start from zero solution (not current guess)

### Priority 2: Implement Gillespie-style source stepping
Port `gillespie_src()` from ngspice `cktop.c`:
- Start with 0.1% step instead of 10%
- Adaptive: ×1.5 when fast (iters < max/4), ×0.5 when slow (iters > 3*max/4)
- On failure: ÷10 the step (not ÷2), restore saved state
- Save/restore BOTH solution AND device state
- Min step 1e-8 (not 1e-4)

### Priority 3: Node damping in Newton
Port from `niiter.c` lines 204-229:
- When max voltage change > 10V and not converged, apply damping
- Damp factor = min(10/maxdiff, 1.0), clamped to >= 0.1
- Apply to BOTH node voltages AND device states

### Priority 4: Force reorder on INITJCT→INITFIX transition
In `newton.cpp`, when mode transitions from INITJCT to INITFIX, set a flag that forces the next factorization to use `numeric()` instead of `refactorize()`. This re-pivots with current values.

### Non-goals
- The sparse solver (NeoSolver) is fine. Gilbert-Peierls reach, AMD ordering, threshold pivoting all work correctly.
- BTF and matching are in place but don't help this particular circuit — that's expected.
- Dense/sparse tier selection is correct (DENSE_LIMIT=12).
