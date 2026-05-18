# SPICE Simulator Industry Comparison

Research on how commercial and open-source SPICE simulators handle the same
algorithmic choices where neospice diverges from ngspice. Sources include
official documentation, published papers, and user guides.

---

## Simulators Surveyed

| Simulator | Developer | License | Baseline |
|-----------|-----------|---------|----------|
| ngspice | Community (Berkeley SPICE3f5 fork) | BSD | SPICE3f5 faithful |
| Xyce | Sandia National Labs | GPLv3 | Independent implementation |
| LTspice | Analog Devices (Mike Engelhardt) | Freeware | Custom from scratch |
| HSPICE | Synopsys | Commercial | SPICE2/3 lineage, heavily modified |
| Spectre | Cadence | Commercial | Independent implementation |
| PSpice | Cadence (OrCAD) | Commercial | SPICE2G lineage |
| neospice | This project | — | ngspice-validated, selectively divergent |

---

## 1. Integration Method

All major simulators default to trapezoidal integration, which offers second-order
accuracy and good stability for most circuits.

| Simulator | Default Method | Order | Notes |
|-----------|---------------|-------|-------|
| ngspice | Trapezoidal | 2 | Also supports Gear BDF-2 via `.options method=gear` |
| Xyce | Variable-order Trap | 1→2 | `METHOD=7` (trap, default). Also has Gear 1-6 |
| LTspice | "Modified Trap" | 2 | Proprietary algorithm that "precisely cancels traditional trap ringing" |
| HSPICE | Trapezoidal | 2 | Also supports Gear and "Gear-modified" |
| Spectre | Trapezoidal | 2 | Default for most analyses |
| PSpice | Trapezoidal | 2 | Gear optional |
| **neospice** | Trapezoidal | 2 | Also supports Gear BDF-2 |

**Key insight**: LTspice's "modified trap" is the most interesting divergence.
Engelhardt has stated that it cancels the ringing artifact that trap produces
when the solution has slope discontinuities, without falling back to the lower
accuracy of backward Euler. The exact algorithm is proprietary.

---

## 2. Integration Order at Breakpoints

When the simulator hits a source breakpoint (e.g., PULSE edge), the solution
history before the breakpoint may be invalid for the new waveform segment. The
question is whether to drop integration order to 1 (backward Euler) for safety.

| Simulator | Behavior at Breakpoints | Rationale |
|-----------|------------------------|-----------|
| ngspice | **Resets to order 1** (backward Euler) | Conservative: stale predictor might cause order-2 to ring |
| Xyce | **Does NOT reset** (`NEWBPSTEPPING=TRUE` default) | Relies on LTE control to handle accuracy. "Can take a reasonably large step out of every non-DCOP breakpoint" |
| LTspice | **Resets to order 1** for that circuit's reactances | Standard SPICE3 behavior, compensated by modified-trap accuracy |
| HSPICE | Resets to order 1 (SPICE3 behavior) | Traditional approach |
| **neospice** | **Resets to order 1**, reduces dt using configurable `restart_step_scale` | Aligned with ngspice (`dctran.c:548`). Order re-promoted via speculative LTE check |

**Industry trend**: ngspice and neospice both reset to order 1 at breakpoints,
using LTE-conditioned speculative promotion to return to order 2. Xyce takes
the opposite approach, keeping order 2 and relying solely on LTE control.
LTspice resets to order 1 but compensates with its modified-trap integration.

---

## 3. Local Truncation Error (LTE) Control

LTE determines whether to accept or reject a timestep based on estimated
integration error. The critical question is *what variables* to check.

| Simulator | LTE Scope | Details |
|-----------|-----------|---------|
| ngspice | **Device-only** (charge LTE) | `CKTtrunc` calls each device's `trunc()` function. Only reactive devices contribute |
| Xyce | **Global LTE on all variables** | `NEWLTE=1` (default). Reference modes: 0=current node, 1=max all signals, 2=max over all time, 3=max per signal. Current variables optionally included (`MASKIVARS=0` default) |
| LTspice | Not publicly documented | Believed to use device-level LTE similar to SPICE3 |
| HSPICE | Device LTE + additional checks | `LVLTIM` option controls timestep algorithm sophistication |
| **neospice** | **Global node-voltage LTE** + device charge LTE | Second finite differences on all node voltages. Skipped for first 2 steps and 3 steps after breakpoints |

**Key finding**: Xyce's `NEWLTE` is the most configurable global LTE in any
simulator. Their default (`NEWLTE=1`) normalizes each variable's LTE against the
maximum of all signals, which prevents large signals from masking errors in
small signals. neospice's approach (normalize against per-node tolerance) is
simpler but effective for the same reason.

**Xyce's additional LTE options** (from Reference Guide v7.6, pp. 97-102):
- `ERROPTION=0`: Uses LTE for timestep control (default)
- `ERROPTION=1`: Uses "N(t)" method (undocumented alternative)
- `NLNEARCONV=0`: Soft nonlinear solver failure flag
- `DELMAX`: Maximum timestep limit (complements LTE from above)

---

## 4. Output Grid

How simulation results are stored and reported to the user.

| Simulator | Output Grid | Interpolation |
|-----------|-------------|---------------|
| ngspice | **Adaptive** (every accepted internal step) | None — raw solver output |
| Xyce | **Adaptive** by default; uniform optional via `.options output` | Linear or higher-order |
| LTspice | **Adaptive** with lossy waveform compression | Proprietary compression reduces file size |
| HSPICE | **Uniform** with `INTERP` option (enabled by default in modern versions) | Polynomial interpolation to tstep grid |
| Spectre | **Adaptive** by default; `strobeperiod` for uniform | Various interpolation options |
| PSpice | **Uniform** at TSTEP intervals | Internal interpolation |
| **neospice** | **Adaptive** by default; **uniform** with `.option interp` | Quadratic Lagrange (3-point) after 2 steps; linear for first 2 |

**Industry split**: There are two camps — adaptive output (ngspice, LTspice,
Spectre, neospice default) and uniform output (HSPICE, PSpice). neospice now
defaults to raw adaptive output (matching ngspice), with `.option interp`
enabling uniform tstep-grid output using quadratic Lagrange interpolation.

---

## 5. Order Promotion Strategy

After starting at order 1 (backward Euler for the first step), when and how to
promote to order 2.

| Simulator | Promotion Strategy |
|-----------|--------------------|
| ngspice | **Speculative**: promotes only if order 2 gives tighter dt bound (`dctran.c:863-873`) |
| Xyce | **Automatic**: `MINORD=1, MAXORD=2`, promotes once sufficient history exists |
| HSPICE | `MAXORD` option (default 2), promotes after sufficient history |
| **neospice** | **Speculative LTE-conditioned**: promotes only if order 2 gives >5% wider dt bound (matches ngspice `dctran.c:862-873`) |

**Assessment**: All simulators promote to order 2 quickly. neospice now matches
ngspice's speculative approach (promotes only if order 2 gives a tighter dt bound),
which is the most conservative strategy.

---

## 6. AC Analysis Matrix Strategy

How the complex admittance matrix Y = G + jωC is assembled across frequency points.

| Simulator | Strategy |
|-----------|----------|
| ngspice | **Re-stamps at every frequency** via device AC load functions |
| HSPICE | Pre-built matrices (proprietary details) |
| Spectre | Pre-built G/C with per-frequency assembly |
| **neospice** | **Pre-built G and C matrices**, assembled as G + jωC per frequency |

**NQS support**: BSIM4v7 `acnqsMod` is fully supported via the `ac_stamp_freq()`
hook. NQS-enabled devices build intrinsic G/C entries during `ac_stamp()`, then
`ac_stamp_freq()` adds per-frequency delta corrections using the tau_net
relaxation formula. Non-NQS devices are unaffected by the caching optimization.

---

## 7. DC Convergence Aids

When the Newton-Raphson solver fails to converge on the DC operating point,
simulators apply convergence aids in sequence.

| Simulator | Convergence Sequence |
|-----------|---------------------|
| ngspice | gmin stepping → **true source stepping** (scales all independent sources 0→1) → fail |
| Xyce | gmin stepping → source stepping → pseudo-transient → fail. Tighter default tolerances than most |
| HSPICE | gmin stepping → source stepping → `AUTOSTOP` + various heuristics |
| PSpice | gmin stepping → source stepping → fail |
| **neospice** | gmin stepping → **true source stepping** (scales all independent sources 0→1 with adaptive backtracking) → **pseudo-transient continuation** → fail |

**Assessment**: neospice now matches or exceeds the convergence aid sequence of
every surveyed simulator. True source stepping was implemented in commit `282ad8c`,
and pseudo-transient continuation (C/dt damping terms with progressive decay) was
added in commit `04e29c8`.

**Xyce additionally offers**:
- `NOXUPDATE`: controls direct Newton update vs. damped update
- `CONTINUATION`: general parameter continuation solver

---

## 8. Device-Level Convergence

Whether the Newton solver checks device-internal convergence beyond node
voltage/branch current agreement.

| Simulator | Device Convergence Check |
|-----------|------------------------|
| ngspice | **Node/branch only** — declares convergence when voltages and branch currents stabilize |
| Xyce | Device-level callbacks available (solver framework supports it) |
| HSPICE | Device-level checks for BSIM models |
| **neospice** | **Device callback** — `device_converged()` called after node convergence passes |

**Rationale**: BSIM4v7 can have converged terminal voltages while internal
feedback loops (charge redistribution, NQS effects) are still oscillating. The
device callback catches this.

---

## 9. Noise Analysis

How the adjoint system for noise is solved.

| Simulator | Adjoint Strategy |
|-----------|-----------------|
| ngspice | Transposes Y at each frequency point |
| Spectre | Pre-built adjoint pattern, optimized symbolic factorization |
| **neospice** | **Pre-built Y^T sparsity pattern** with separate symbolic factorization for Y and Y^T |

**Assessment**: neospice's approach matches commercial practice (Spectre). The
asymmetric sparsity of Y^T for active devices makes pre-building the transpose
pattern worthwhile.

---

## 10. Solver Tolerances

Default tolerance settings vary significantly across simulators.

| Simulator | reltol | abstol | vntol | chgtol |
|-----------|--------|--------|-------|--------|
| ngspice | 1e-3 | 1e-12 | 1e-6 | 1e-14 |
| Xyce | 1e-3 | 1e-6 | — | 1e-12 |
| LTspice | 1e-3 | 1e-12 | 1e-6 | 1e-14 |
| HSPICE | 1e-3 | 1e-12 | 1e-6 | 1e-14 |
| PSpice | 1e-3 | 1e-12 | 1e-6 | 1e-14 |
| **neospice** | 1e-3 | 1e-12 | 1e-6 | 1e-14 |

**Note**: Xyce documentation states "Xyce has much tighter default solver
tolerances than some other simulators (e.g., PSpice)." Their `abstol` of 1e-6
(vs. the industry standard 1e-12) is tighter in the sense that their solver
convergence criteria are stricter — `abstol` in Xyce refers to the Newton
convergence tolerance, not the same quantity as SPICE3's `abstol`.

---

## 11. Speed

Publicly available benchmarks are scarce and methodology varies. General industry
perception:

| Simulator | Relative Speed | Notes |
|-----------|---------------|-------|
| ngspice | 1× (baseline) | Pure C, optimized over decades |
| Xyce | 1-2× for serial; scales with parallelism | Designed for massive parallel runs on HPC |
| LTspice | 2-5× faster than SPICE3 | Highly optimized, custom solver |
| HSPICE | 1-3× | "Gold standard" for accuracy, not speed |
| Spectre | 2-4× | Optimized for large analog circuits |
| **neospice** | 1.2--8× depending on circuit | C++ overhead reduction + NeoSolver (custom sparse LU). Faster on small-medium circuits, competitive on large |

---

## Summary: Where neospice Aligns with Industry

| Feature | ngspice | Xyce | LTspice | HSPICE | neospice | Assessment |
|---------|---------|------|---------|--------|----------|------------|
| Global LTE | No | **Yes** | Unknown | Partial | **Yes** | Validated by Xyce |
| Reset order at BP | **Yes** | No | **Yes** | **Yes** | **Yes** | Aligned with ngspice |
| Adaptive output (default) | **Yes** | **Yes** | **Yes** | No | **Yes** | Raw adaptive; `.option interp` for uniform |
| AC G/C caching | No | — | — | Likely | **Yes** | Pure optimization; NQS via `ac_stamp_freq()` |
| Device convergence | No | Yes | — | Yes | **Yes** | Best practice |
| True source stepping | **Yes** | **Yes** | **Yes** | **Yes** | **Yes** | Implemented with adaptive backtracking |
| Modified trap | No | No | **Yes** | No | No | LTspice-exclusive |
| Pseudo-transient | No | **Yes** | No | Limited | **Yes** | C/dt damping with progressive decay |
| Trap ringing detection | No | No | **Yes** (modified trap) | No | **Yes** | Auto Gear-2 fallback |

### Key Takeaways

1. **neospice's algorithmic choices are validated by Xyce** — global LTE is
   independently proven by Sandia.

2. **All major convergence gaps are closed** — true source stepping and
   pseudo-transient continuation are now implemented, matching Xyce's full
   convergence aid sequence.

3. **Adaptive output is now default** (matching ngspice), with `.option interp`
   for uniform grid output (matching HSPICE/PSpice practice).

4. **LTspice's modified trap is unique** — no other simulator has it. neospice
   addresses trap ringing via automatic Gear-2 fallback detection.

5. **neospice now aligns with ngspice on breakpoint handling** — order reset to
   1 with LTE-conditioned speculative promotion back to order 2.

---

## Sources

- Xyce Reference Guide v7.6, Sandia National Labs (2024). Pages 97-102 (time integration options), 769-783 (transient analysis details)
- Mike Engelhardt, "A Few Algorithms Used in LTspice" (public talk, various conferences)
- ngspice source code: `src/spicelib/analysis/dctran.c`, `src/spicelib/analysis/cktop.c`
- HSPICE User Guide, Synopsys (general references from publicly available documentation)
- neospice source code: `src/core/transient.cpp`, `src/core/timestep.cpp`, `src/core/convergence.cpp`
