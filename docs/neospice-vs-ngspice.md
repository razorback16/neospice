# neospice vs ngspice: Algorithmic Differences

Catalog of deliberate algorithmic divergences from ngspice.
Each entry documents what neospice does differently, why, and the measured impact.

---

## 1. ~~Integration order at source breakpoints~~ (RESOLVED)

**Status:** Aligned with ngspice as of commit `53244e7`.

neospice now resets integration order to 1 at breakpoint crossings, matching
ngspice `dctran.c:548`. Order is re-promoted via speculative LTE check (see
item 4).

**Source:** `src/core/transient.cpp:937`

---

## 2. Global node-voltage LTE

**ngspice** controls timestep using only device-level charge truncation error
(`CKTtrunc` calls each device's `trunc` function, which computes LTE from
charge state history).

**neospice** adds a second, independent check: global node-voltage LTE using
second finite differences of the solution vector:

```
delta2[i] = sol[i] - 2*sol_prev[i] + sol_prev2[i]
lte[i] = |delta2[i]| * lte_coeff        (1/12 for Trap, 2/9 for Gear-2)
tol[i] = reltol * |sol[i]| + vntol
accept if max(lte[i]/tol[i]) <= trtol   (default trtol = 7.0)
```

This check is skipped for the first 2 steps (need 3 history points) and for
3 steps after a source breakpoint (history contaminated by pre-edge values).

**Why:** Device-level LTE only monitors charge-storing devices. Nodes driven
purely by resistive networks or voltage sources have no charge LTE. The
global check catches accumulated integration error on all nodes.

**Impact:** May reject additional steps during fast transitions that ngspice
would accept, contributing to the timestep sequence divergence described in
item 1.

**Source:** `src/core/transient.cpp:470-483`, `src/core/timestep.cpp:36-78`

---

## 3. ~~Output interpolation~~ (RESOLVED)

**Status:** Aligned with ngspice. neospice now outputs raw adaptive timesteps
by default, matching ngspice's default behavior. `.option interp` enables
uniform tstep grid output with quadratic Lagrange interpolation (matching
ngspice's `.option interp` with linear interpolation). Both Spectre
(`strobeperiod`) and Xyce offer similar interpolated output modes.

**Source:** `src/core/transient.cpp:514-553` (interpolation helper), `src/core/transient.cpp:965-973` (interp vs raw output branch)

---

## 4. ~~Order promotion strategy~~ (RESOLVED)

**Status:** Aligned with ngspice as of commit `53244e7`.

neospice now uses speculative LTE-conditioned promotion: after a step passes
at order 1, it re-evaluates device `compute_trunc` at order 2 and only
promotes if the order-2 dt bound exceeds the current dt by 5%, matching
ngspice `dctran.c:863-873`.

**Source:** `src/core/transient.cpp:947-959`

---

## 5. AC analysis: G/C matrix caching

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

**Impact:** AC sweep scales as O(N * factorize) instead of
O(N * (device_stamp + factorize)). For large circuits where device stamping
dominates, this can be significantly faster.

**NQS support:** Devices with frequency-dependent AC behavior (e.g.,
BSIM4v7 acnqsMod) override `ac_stamp_freq(omega, ax, nnz, ac_rhs)`.
The base G+jwC is assembled from cached arrays as above, then the hook
adds per-frequency delta corrections directly into the complex `ax`
array. This preserves the O(1) device-stamp cost while supporting
NQS scaling (tau_net relaxation: T0=wt, T2=1/(1+T0^2), T3=T0*T2).

**Source:** `src/core/ac.cpp:149-167`, `src/devices/device.hpp` (ac_stamp_freq)

---

## 6. Noise analysis: pre-built adjoint pattern

**ngspice** solves the adjoint system Y^T * adj = e_out by transposing the
admittance matrix at each frequency point (or by solving Y^H with a
Hermitian-aware solver).

**neospice** pre-builds a separate sparsity pattern for Y^T at setup time,
performs symbolic factorization once for each of Y and Y^T, then does only
numeric factorization per frequency.

**Why:** Y^T generally has a different sparsity structure than Y (the pattern
is asymmetric for active devices). Pre-building avoids runtime transpose and
enables separate symbolic factorization tuned to each pattern.

**Source:** `src/core/noise.cpp:209-219`

---

## 7. Device-level convergence check

**ngspice** declares Newton convergence based solely on node voltage and
branch current agreement between successive iterations.

**neospice** adds a device-level convergence callback. After node/branch
convergence passes, each device's `device_converged()` method is called.
If any device reports non-convergence, Newton continues iterating.

**Devices using this:**
- BSIM4v7: internal current-based convergence (CKTnoncon from the load function)
- BJT/JFET/VBIC: junction current convergence
- Switches: state-change detection

**Why:** BSIM4v7 can have converged terminal voltages while internal
currents are still oscillating due to the model's internal feedback loops.
The device-level check prevents premature declaration of convergence.

**Source:** `src/core/newton.cpp:193-206`

---

## 8. ~~Source stepping~~ (RESOLVED)

**Status:** Aligned with ngspice. True source stepping with adaptive
backtracking.

**Source:** `src/core/convergence.cpp:174-278`

---

## 9. DC operating-point convergence fallback order

**ngspice** `CKTop` tries direct Newton first, then dynamic diagonal-gmin
stepping, then device-level `new_gmin`, then source stepping.

**neospice** tries direct Newton first, then device-level true-gmin stepping,
then dynamic diagonal-gmin, then source/gain/pseudo-transient fallback.

**Why:** The port now matches ngspice's `NIiter` result-vector convention:
when Newton converges, callers keep the previous iterate (`CKTrhsOld`) rather
than the just-solved proposal. That fixed a real continuation discrepancy.
However, the translated diagonal-gmin path can still land on a Newton-stable
false branch in dependent-source macromodels such as OPA1632. ngspice's own
`new_gmin` path reaches the reference operating point without changing
reltol/abstol/vntol or accepting a looser result, so neospice uses that
official device-level continuation first.

**Impact:** OPA1632 `.op + .ac dec 10` now matches ngspice and runs within
1.3× of ngspice in-process. The dynamic diagonal-gmin implementation remains
available as a fallback, but it is not allowed to override the verified
ngspice operating point from true-gmin continuation.

**Source:** `src/core/dc.cpp`, `src/core/convergence.cpp`,
`src/core/newton.cpp`; ngspice `src/spicelib/analysis/cktop.c`
(`dynamic_gmin`, `new_gmin`) and `src/maths/ni/niiter.c`.

---

## 10. Speed comparison

On a CMOS inverter transient (20ns, BSIM4v7 NMOS + PMOS, 10fF load):
- neospice: 23ms
- ngspice: 27ms (subprocess call, includes process startup)
- neospice is ~1.2x faster for the simulation itself

The speed advantage comes primarily from C++ vs C overhead reduction and
the NeoSolver custom sparse LU implementation.

---

## Appendix: Convergence study

CMOS inverter v(out), first rising and falling edges. "Reference" is the
average of both simulators at 100x tighter tolerances (reltol=1e-5).

| Metric | Reference | ngspice (reltol=1e-3) | neospice (reltol=1e-3) |
|--------|-----------|----------------------|----------------------|
| Fall time | 44.75 ps | 45.73 ps (+2.2%) | 46.16 ps (+3.1%) |
| Rise time | 41.14 ps | 42.51 ps (+3.3%) | 42.08 ps (+2.3%) |
| Crossing (fall) | 85.57 ps | 85.57 ps (+0.001%) | 85.50 ps (-0.08%) |
| Crossing (rise) | 5179.0 ps | 5179.0 ps (~0%) | 5179.0 ps (~0%) |

At tight tolerances (reltol=1e-5), the two simulators agree to 0.03%.
At default tolerances, both are 2-3% off the converged answer in
complementary directions. Neither is systematically more accurate.
