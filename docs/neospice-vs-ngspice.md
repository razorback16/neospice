# neospice and ngspice: implementation differences

The sole reference is the [checksum-pinned ngspice47 release](ngspice47-reference.md).
Architecture differences do not establish numerical novelty,
accuracy superiority or speedup. Current failures remain in [JOSS progress](joss-progress.md).

---

## 1. Global node-voltage LTE (opt-in)

**ngspice** calls device truncation functions through `CKTtrunc`. Charge-history
error estimates are part of timestep control; source breakpoints and
transmission-line delay/history constraints also affect the accepted grid.

**neospice** uses device truncation by default and
offers an *optional* second check — global node-voltage LTE using second
finite differences of the solution vector — gated behind `.option newtrunc`.
`.option interp` changes output sampling only; it does not enable this check.
The controller's local estimate is:

```
delta2[i] = sol[i] - 2*sol_prev[i] + sol_prev2[i]
lte[i] = |delta2[i]| * lte_coeff        (1/12 for Trap, 2/9 for Gear-2)
tol[i] = reltol * |sol[i]| + vntol
accept if max(lte[i]/tol[i]) <= trtol   (default trtol = 7.0)
```

When enabled, the check is **proposal-only**: it never rejects a step while
the aggregate device-step proposal is finite. Otherwise the global check may
reject a step. The implementation uses a circuit-level proposal test, not an
independent per-node classification of which devices provide LTE. Startup and
post-breakpoint guards exclude insufficient or discontinuous history.

**Why:** The optional check adds a solution-history-based step proposal. It can
also restrict sampling of purely resistive/source circuits, but that is not an
estimate of accumulated charge-integration error where no such integration
occurs. Its benefit and interaction with device truncation require validation.

**Impact:** The optional global check is off by default. This does not establish
identical timestep grids or outputs: retained transient comparisons still fail.

**Source:** [transient driver](../src/core/transient.cpp),
[timestep controller](../src/core/timestep.cpp).

---

## 2. AC analysis: G/C matrix caching

**ngspice** rebuilds the complex admittance matrix Y = G + jwC at every
frequency point by calling device AC load functions that stamp directly into
a complex matrix.

**neospice** pre-builds separate real G (conductance) and C (capacitance)
matrices once, caches their nonzero values, then at each frequency point
assembles the complex matrix from the cached arrays:

```cpp
for (int k = 0; k < nnz; ++k) {
    ax[2*k]     = g_vals[k];           // Re(Y) = G
    ax[2*k + 1] = omega * c_vals[k];   // Im(Y) = wC
}
```

**Why:** Device AC stamp functions are deterministic at a given DC operating
point (no frequency dependence for most devices). Calling them N times for
N frequency points is redundant. The G/C split calls devices once and reuses
the result.

**Impact:** This avoids repeated frequency-independent device stamping.
Per-frequency matrix assembly, frequency-dependent stamps, factorization and
solves remain. The performance benefit needs paired, correctness-validated
measurements; it does not follow quantitatively from the architecture alone.

**NQS support:** Devices with frequency-dependent AC behavior (e.g.,
BSIM4v7 acnqsMod) override `ac_stamp_freq(omega, ax, nnz, ac_rhs)`.
The base G+jwC is assembled from cached arrays as above, then the hook
adds per-frequency delta corrections directly into the complex `ax`
array. Such hooks still execute at each frequency. LTRA also uses them for
frequency-dependent propagation. Support is bounded by the device/analysis
checks in [capabilities](capabilities.md).

**Source:** `src/core/ac.cpp:128` (G/C value cache), `src/core/ac.cpp:195-196`
(per-frequency assembly), `src/devices/device.hpp` (ac_stamp_freq)

---

## 3. Noise analysis: pre-built adjoint pattern

**ngspice** reuses the existing factors for its adjoint solve. `NInzIter` calls
`SMPcaSolve`; the Sparse path calls `spSolveTransposed`. It does not require a
second transposed-matrix factorization. The transpose is not a Hermitian
conjugate transpose.

**neospice** represents the complex equations as doubled real systems. It
pre-builds separate patterns and solver instances for gain and adjoint problems,
then assembles and factors both systems at each frequency.

**Trade-off:** Separate patterns fit the current solver interface, but add a
second numerical factorization compared with reuse of an existing transpose
solve. This is not evidence of an advantage over ngspice's noise implementation.

**Source:** [noise implementation](../src/core/noise.cpp); ngspice
`src/spicelib/analysis/noisean.c`, `NInzIter` and
`src/maths/sparse/spsmp.c`.

---

## 4. Device-level convergence check

**ngspice** checks device nonconvergence as well as terminal-variable agreement.
Device loads such as `b4v7ld.c` set `CKTnoncon`; `NIiter` consults that flag and
calls `NIconvTest` when appropriate.

ngspice47 preserves the post-solve failure flag set by `DEVconvTest` as well
as load-time nonconvergence. Both checks matter for the retained
[RFF70N06 investigation](rff70n06-investigation.md).

**neospice** exposes a device-level convergence callback. After node/branch
convergence passes, each device's `device_converged()` method is called.
If any device reports non-convergence, Newton continues iterating.

**Devices using this:**
- BSIM4v7: internal current-based convergence (CKTnoncon from the load function)
- BJT/JFET/VBIC: junction current convergence
- Switches: state-change detection

**Why:** BSIM4v7 can have converged terminal voltages while internal
currents are still oscillating due to the model's internal feedback loops.
The device-level check prevents premature declaration of convergence.

This is an interface choice for established SPICE behavior. Agreement claims
must identify the reference version and distinguish load-time flags from
post-solve callbacks. **Source:** [Newton solver](../src/core/newton.cpp);
ngspice `src/maths/ni/{niiter.c,niconv.c}`, `src/spicelib/analysis/cktop.c`
and device load routines.

---

## 5. DC operating-point convergence fallback order

**ngspice** `CKTop` tries direct Newton first, then dynamic diagonal-gmin
stepping, then device-level `new_gmin`, then source stepping and transient-OP
fallback under the default options. Explicit options can change that sequence.

**neospice** now tries direct Newton, dynamic diagonal-gmin, true-gmin,
source stepping and OPtran in that order. If those fail, additional attempts
include gain stepping, pseudo-transient continuation, a continuation-seeded
OPtran retry, node-classification initialization, gain homotopy and equilibration.
The DC-sweep path has its own continuation implementation and must be checked
separately; this sequence describes `solve_dc`.

**Why:** The port now matches ngspice's `NIiter` result-vector convention:
when Newton converges, callers keep the previous iterate (`CKTrhsOld`) rather
than the just-solved proposal. That fixed a real continuation discrepancy.
Continuation order and state transfer can affect nonlinear operating-point
selection. Matching the reference sequence is therefore part of correctness
work, with the original model, tolerances and failing regressions preserved.

**Impact:** The retained RFF70N06 fixture fails in both neospice and ngspice47.
Extra continuation methods do not establish better convergence. Performance
claims require paired accuracy-qualified measurements.

**Source:** `src/core/dc.cpp`, `src/core/convergence.cpp`,
`src/core/newton.cpp`; ngspice `src/spicelib/analysis/cktop.c`
(`dynamic_gmin`, `new_gmin`) and `src/maths/ni/niiter.c`.

---

## 6. Solver policy and measurement

The current automatic real-solver policy selects `AmdLuSolver` only for linear
circuits with at least256 unknowns, with a first-factorization fallback to
`NeoSolver`. Complex operations delegate to `NeoSolver`. The in-tree ordering
explicitly forms fill cliques and scans for minimum degree; it is not SuiteSparse
AMD's quotient-graph implementation. Production solver paths do not call BTF.
A unique mathematical solution does not imply bit-identical floating-point
results under different orderings.

Use [paired benchmark methods](benchmark-methods.md) for measured comparisons.
Earlier timing and convergence narratives are preserved in archived evidence;
they do not establish current speedup or accuracy.
