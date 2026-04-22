# neospice vs ngspice: Algorithmic Differences

Catalog of deliberate algorithmic divergences from ngspice.
Each entry documents what neospice does differently, why, and the measured impact.

---

## 1. Integration order at source breakpoints

**ngspice** resets integration order to 1 (backward Euler) at every breakpoint
crossing (`dctran.c:548`). This is the conservative choice: history before the
breakpoint is stale, and order-2 methods can ring when fed a stale predictor.

**neospice** keeps order 2 through breakpoints (`transient.cpp:510-512`).
The dt is still reduced to 0.1x (matching ngspice), but the integration
coefficients stay at Trap or Gear-2.

**Why:** Dropping to order 1 triples the voltage error in BSIM4v7 charge
integration near switching edges. The 0.1x dt reduction is sufficient to
control accuracy without falling back to first-order.

**Impact:** On a CMOS inverter (42ps rise, 46ps fall), this produces a 1-3%
difference in 10%-90% rise/fall time vs ngspice at default tolerances. Both
converge to the same answer at tight tolerances (0.03% agreement at
reltol=1e-5). Neither normal-tolerance result is more accurate than the
other: ngspice is 2.2%/3.3% off on fall/rise, neospice is 3.1%/2.3% off.
The errors are complementary, not systematic.

**Source:** `src/core/transient.cpp:508-518`

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

**Source:** `src/core/transient.cpp:462-472`, `src/core/timestep.cpp:29-66`

---

## 3. Output interpolation

**ngspice** dumps the raw solution at every accepted internal timestep. The
output grid is the adaptive timestep grid itself (irregular spacing, denser
during fast transitions). For a 20ns simulation with 10ps tstep, ngspice
typically produces ~2000 points with non-uniform spacing.

**neospice** outputs at uniform tstep intervals. When an internal step
overshoots an output point, the solution is interpolated:
- First 2 steps: linear interpolation
- After 2 steps: quadratic Lagrange interpolation using 3 history points

**Why:** Uniform output grid is expected by downstream tools and makes
waveform comparison straightforward. Quadratic interpolation matches the
order of the integration method (order 2), so the interpolation error is
bounded by the integration error.

**Impact:** Output quality depends on the ratio of tstep to transition time.
When the internal adaptive step is much finer than tstep (common during fast
edges), the quadratic interpolation accurately reconstructs the waveform.
When comparing against ngspice, the different output grids are a major source
of apparent error in point-wise comparison.

**Source:** `src/core/transient.cpp:547-579`

---

## 4. Order promotion strategy

**ngspice** promotes from order 1 to order 2 speculatively: after a step
passes at order 1, it re-evaluates `CKTtrunc` at order 2 and keeps the
higher order only if order 2 produces a tighter (smaller) dt bound
(`dctran.c:863-873`). This means ngspice may stay at order 1 for several
steps if order 2 doesn't help.

**neospice** promotes unconditionally to order 2 after 2 accepted steps
(`transient.cpp:521-523`).

**Why:** Simplicity. The speculative approach adds complexity and in practice
the unconditional promotion works because the 0.1x dt reduction at
breakpoints already ensures the first few post-breakpoint steps are small
enough for order 2 to be stable.

**Source:** `src/core/transient.cpp:520-523`, ngspice `dctran.c:863-873`

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

**Source:** `src/core/ac.cpp:112-161`, `src/devices/device.hpp` (ac_stamp_freq)

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

**Source:** `src/core/noise.cpp:180-203`

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

**Source:** `src/core/newton.cpp:146-159`

---

## 8. Source stepping

**ngspice** implements true source stepping: scales all independent source
values from 0 to 1 in stages, solving the DC operating point at each stage.

**neospice** also implements true source stepping with the same approach:
collects all independent sources, saves their original DC values, scales
from 0 to 1 through {0.1, 0.2, ..., 1.0}, and uses each converged
solution as the initial guess for the next step. If any step fails, the
fraction increment is halved and retried.

**Impact:** Matches ngspice's convergence behavior on high-gain feedback
circuits. No longer a divergence.

**Source:** `src/core/convergence.cpp:37-80`

---

## 9. Speed comparison

On a CMOS inverter transient (20ns, BSIM4v7 NMOS + PMOS, 10fF load):
- neospice: 23ms
- ngspice: 27ms (subprocess call, includes process startup)
- neospice is ~1.2x faster for the simulation itself

The speed advantage comes primarily from C++ vs C overhead reduction and
the KLU sparse solver integration.

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
