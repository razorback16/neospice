# DC Convergence Improvement Plan

## Current State

**Pass rate:** 98.4% (34,345/34,911) on KiCad SPICE Library test suite.

**Remaining failures:** 566 total, of which 419 are DC convergence (`SIM_ERROR`).

**Current convergence flow** (matches ngspice `CKTop` in `cktop.c`):

1. Direct Newton-Raphson (MODEINITJCT → MODEINITFIX → MODEINITFLOAT)
2. Dynamic gmin stepping (Gillespie algorithm with adaptive factor)
3. Source stepping (Gillespie variant with adaptive ramp)
4. Pseudo-transient continuation (fictitious capacitor decay)

**Failure breakdown** (419 SIM_ERROR):

| Category | Count | Description |
|----------|-------|-------------|
| Convergence | 319 | Newton fails across all 4 methods |
| Residual-zero | 77 | Solver reports residual=0 but not converged |
| Channel length | 22 | MOSFET L=1u below model minimum |
| Activation energy | 1 | Model parameter edge case |

---

## Gap Analysis: neospice vs ngspice vs Commercial Tools

### What ngspice has that we match

| Feature | ngspice | neospice | Status |
|---------|---------|----------|--------|
| Direct Newton (NIiter) | ✓ | ✓ | Matched |
| MODEINITJCT/FIX/FLOAT cascade | ✓ | ✓ | Matched |
| Dynamic gmin stepping (Gillespie) | ✓ | ✓ | Matched |
| Adaptive gmin factor (√factor) | ✓ | ✓ | Matched |
| Source stepping (Gillespie) | ✓ | ✓ | Matched |
| Gmin-at-zero-source fallback | ✓ | ✓ | Matched |
| Adaptive step size in source ramp | ✓ | ✓ | Matched |
| Per-device voltage limiting (pnjlim/fetlim) | ✓ | ✓ | Matched |
| `.nodeset` / `.ic` support | ✓ | ✓ | Matched |
| Final solve at true gmin after stepping | ✓ | ✓ | Matched |

### What ngspice has that we're missing

| Feature | ngspice Location | Impact | Difficulty |
|---------|-----------------|--------|------------|
| **Node damping** | `niiter.c:296-323` | High | Low |
| **True gmin stepping** (`new_gmin`) | `cktop.c:349-466` | Medium | Low |
| **SPICE3 gmin stepping** | `cktop.c:289-346` | Low | Low |
| **OPtran** (full transient startup) | `optran.c` | High | High |
| **CKTsrcFact** (source scaling in device evaluate) | Throughout devices | Medium | Medium |
| **NISHOULDREORDER** on singular pivot | `niiter.c:113,220` | Medium | Low |
| **ipass logic** for nodeset release | `niiter.c:326-329` | Low | Low |

### What commercial tools have beyond ngspice

| Feature | Used by | Impact | Difficulty |
|---------|---------|--------|------------|
| **Damped Newton / Line search** | Spectre, HSPICE | High | Medium |
| **Automatic node classification** | Spectre, HSPICE | Medium | Medium |
| **Linearized startup mode** | Spectre | Medium | Medium |
| **Variable-gain homotopy** | Academic (UCI) | High | High |
| **Model-aware homotopy** (ATANSH) | Academic (Roychowdhury) | High | High |
| **Topology checker** | PSpice, SIMetrix, Multisim | Medium | Low |
| **Matrix scaling/equilibration** | HSPICE, PrimeSim | Medium | Medium |
| **Trust region safeguards** | JiffyTune/LANCELOT | Medium | High |
| **Pseudo-arclength continuation** | Academic | Low | Very High |

---

## Recommended Improvements (Priority Order)

### Priority 1: Low-Hanging Fruit (High Impact, Low Effort)

#### 1A. Node Damping

**What:** When Newton updates produce large voltage swings (>10V), damp the update by scaling it so the maximum change is bounded.

**ngspice reference:** `niiter.c:296-323`

```
if (maxdiff > 10) {
    damp_factor = 10 / maxdiff;
    if (damp_factor < 0.1) damp_factor = 0.1;
    for each node:
        solution[i] = old[i] + damp_factor * (solution[i] - old[i]);
    for each state:
        state0[i] = old_state[i] + damp_factor * (state0[i] - old_state[i]);
}
```

**ngspice conditions:** Only active when `CKTnodeDamping != 0` AND mode is DCOP or TRANOP AND `iterno > 1` AND Newton hasn't converged.

**Where:** `newton.cpp`, after voltage limiting (line 145), before convergence check.

**Expected impact:** Prevents oscillation in high-gain feedback circuits (comparators, regulators). This is the single most impactful missing feature — ngspice enables it by default for DC operating point.

#### 1B. Reorder-on-Singular-Pivot

**What:** When `refactorize()` encounters a near-zero pivot, force a full `numeric()` reorder on the *same* iteration rather than only on the next.

**ngspice reference:** `niiter.c:219-230` — when `SMPluFac` returns `E_SINGULAR`, ngspice sets `NISHOULDREORDER` and does `continue` (retries the iteration with full reorder).

**Current behavior:** We already fall back to `numeric()` when `refactorize()` throws, but we don't retry the iteration — we proceed with the approximate factorization's result. Additionally, we should handle the KLU-style in-place reorder (factor from scratch in the same iteration rather than waiting for next).

**Where:** `newton.cpp:121-133`

**Expected impact:** Prevents stale pivot ordering from accumulating errors across iterations, especially during gmin stepping where matrix structure changes significantly.

#### 1C. True Gmin Stepping (new_gmin)

**What:** Instead of adding diagonal `diag_gmin` to the matrix (which adds artificial shunt conductances), step the *actual* `gmin` parameter that lives inside every device model. This changes the junction shunt conductance within MOSFETs, diodes, and BJTs, which is more physically meaningful.

**ngspice reference:** `cktop.c:349-466` — `new_gmin()` function. Uses the same adaptive factor logic as `dynamic_gmin`, but modifies `ckt->CKTgmin` instead of `ckt->CKTdiagGmin`.

**Algorithm:** Same as dynamic gmin stepping, but the gmin parameter is passed through device evaluate calls rather than added to the matrix diagonal. This affects junction shunt conductances in all semiconductor devices.

**Sequencing in ngspice:** When `CKTnumGminSteps == 1` (default), ngspice tries `dynamic_gmin` first, then falls back to `new_gmin` if that fails. When `CKTnumGminSteps > 1`, it uses the simpler `spice3_gmin` instead.

**Where:** `convergence.cpp`, new function `true_gmin_stepping()`. Call it in `dc.cpp` between `gmin_stepping()` and `source_stepping()`.

**Expected impact:** Some circuits respond better to per-device gmin adjustment than to a global diagonal shunt. The two approaches are complementary.

### Priority 2: Medium Impact, Medium Effort

#### 2A. Backtracking Line Search in Newton

**What:** After computing the Newton step Δx, check if the residual actually decreases. If not, reduce the step length α until it does (Armijo backtracking).

**Algorithm:**
```
solve J(x_k) Δx = -F(x_k)
α = 1.0
for attempts = 0 to max_backtrack:
    x_trial = x_k + α * Δx
    apply device limiting to x_trial
    evaluate F(x_trial)
    if ||F(x_trial)|| <= (1 - c*α) * ||F(x_k)||:
        accept x_trial
        break
    α *= 0.5
if no decrease found:
    accept full step anyway (let outer convergence handle it)
```

**Key detail:** The line search evaluates F(x_trial) which requires a full device evaluation pass but NOT a matrix factorization — just load the matrix/RHS and compute the residual norm. Cost is ~1 extra device evaluation per backtrack step.

**Where:** `newton.cpp`, wrap the solve/update logic.

**Expected impact:** Prevents Newton from oscillating between two basins of attraction, which is a common failure mode for circuits with positive feedback. Commercial tools (Spectre, HSPICE) implement this implicitly.

**Caution:** Must interact correctly with the MODEINITJCT→FIX→FLOAT cascade. Line search should only be active in MODEINITFLOAT phase.

#### 2B. Source Scaling via CKTsrcFact

**What:** Instead of explicitly modifying each source's DC value during source stepping (our current approach), use a global scale factor that devices read during evaluation. This is how ngspice does it — `CKTsrcFact` is a field on the circuit that all source devices multiply their values by.

**ngspice reference:** `gillespie_src` sets `ckt->CKTsrcFact` and every VSource/ISource reads it in their `DEVload` function.

**Advantage over our approach:** Our current implementation saves/restores DC values and calls `set_dc_value()` in a loop. This works but (1) requires an RAII guard, (2) doesn't scale behavioral sources or dependent sources that reference parameters, and (3) is fragile if a source's DC value is computed from parameters that also get modified.

**Where:** Add `src_fact` field to `SimOptions` or `IntegratorCtx`. VSource/ISource read it in `evaluate()`. Behavioral sources should also respect it for their DC component.

**Expected impact:** Broader source stepping coverage, especially for circuits with behavioral sources.

#### 2C. Topology Checker

**What:** Before attempting DC, analyze the circuit graph for structural problems that guarantee convergence failure.

**Checks:**
1. **Floating nodes** — nodes with only one connection (no DC path)
2. **Voltage source loops** — loops of voltage sources with no resistance
3. **Current source cut-sets** — current sources with no parallel path
4. **Disconnected components** — nodes unreachable from ground

**Where:** New function in `circuit.cpp` called before `solve_dc()`.

**Expected impact:** Convert cryptic "singular matrix" or "failed to converge" errors into actionable diagnostics. Many of the 80 ERROR cases are structural.

#### 2D. Adaptive Pseudo-Transient Time Step

**What:** Currently, pseudo-transient doubles dt on success and halves on failure. Use residual-based adaptive stepping instead.

**Improved algorithm** (from Kelley, SIAM 2004):
```
dt_new = dt_old * (||F(x_old)|| / ||F(x_new)||)
```
When the residual drops fast, dt grows aggressively (approaching pure Newton). When it drops slowly, dt stays small (more damping). When it increases, dt shrinks.

**Safeguards:** Clamp dt growth to 4x per step. Floor at dt_min = 1e-15. Ceiling at dt_max = 1e6.

**Where:** `convergence.cpp:316-391`, replace the fixed 2x/0.5x logic.

**Expected impact:** Faster convergence for circuits that need pseudo-transient (fewer steps to reach steady state), and better handling of circuits where the current fixed 2x growth overshoots.

### Priority 3: High Impact, High Effort (Future Work)

#### 3A. Full Transient Startup (OPtran)

**What:** When all algebraic methods fail, run a true transient simulation with sources ramped from zero, using the transient engine's time-stepping, integration, and convergence logic. Extract the final state as the DC operating point.

**ngspice reference:** `optran.c` — a 500-line transient engine stripped of output handling. Uses backward Euler with adaptive time stepping, breakpoint management, and LTE-based step control. Default parameters: `opstepsize=1e-8`, `opfinaltime=1e-6`, `opramptime=0`.

**Why it's different from pseudo-transient:** Pseudo-transient adds a fictitious conductance G=C/dt to the diagonal and does Newton at each step. OPtran runs the *real* transient integrator with actual device capacitances, actual integration order control, and actual LTE checking. This means it captures real circuit dynamics — a voltage regulator's soft-start, a bandgap's startup sequence, an oscillator's self-excitation — that pseudo-transient with fictitious capacitors cannot.

**Where:** New file `src/core/optran.cpp` that reuses the transient engine.

**Expected impact:** This is the ultimate fallback. In ngspice, OPtran resolves most of the remaining DC failures after gmin/source stepping. However, it's expensive (full transient simulation) and requires a working transient engine.

#### 3B. Variable-Gain Homotopy

**What:** Instead of scaling sources, scale device nonlinearity. Start with MOSFETs having near-zero transconductance (almost resistive) and BJTs with β≈0 (almost open), then gradually ramp gains to true values.

**Academic reference:** Moon et al., "Homotopy methods for DC analysis of SPICE circuits" (Oregon State / Wiley). UCI group (ASP-DAC 2006).

**Algorithm:**
```
for λ = 0 to 1:
    for each MOSFET: effective_gm = λ * true_gm
    for each BJT: effective_beta = λ * true_beta
    solve DC at current λ using previous solution as initial guess
```

**Where:** Add a `device_gain_factor` to `SimOptions`. Devices scale their transconductance/gain during evaluate.

**Expected impact:** Attacks the root cause of many failures — steep nonlinearities. When gm is near zero, the circuit is almost linear and has a unique solution. As gm increases, the solution smoothly deforms toward the true operating point. This avoids the multiple-solution problem that plagues circuits with strong feedback at full gain.

**Risk:** The homotopy path can still have turning points if gain scaling interacts with protection diodes or clamp circuits. May need pseudo-arclength continuation for pathological cases.

#### 3C. Automatic Node Classification and Initial Guess

**What:** Before Newton, analyze the netlist to identify power rails, ground references, high-impedance nodes, and feedback networks. Set initial guesses based on expected bias conditions.

**Heuristics:**
- Nodes connected to VDD source → initialize to VDD
- Nodes connected to VSS source → initialize to VSS
- MOSFET gates in differential pairs → initialize to mid-supply
- Op-amp outputs with negative feedback → initialize near virtual ground of inputs
- Comparator outputs → initialize to one rail (break symmetry)

**Where:** New function `compute_initial_guess()` in `dc.cpp`, called before Newton.

**Expected impact:** Good initial guesses are the single most important factor for Newton convergence. A guess within the basin of attraction of the correct operating point will converge in a few iterations without needing any continuation methods.

#### 3D. Matrix Scaling and Equilibration

**What:** Before factorizing the Jacobian, apply row and column scaling to reduce the condition number. This improves the accuracy of the Newton step when the matrix has entries spanning many orders of magnitude.

**Algorithm:** Compute scaling factors D_r, D_c such that the scaled matrix D_r * J * D_c has rows and columns of comparable norms. Apply to RHS, solve scaled system, unscale solution.

**Where:** `neo_solver.cpp`, in the `numeric()` and `refactorize()` paths.

**Expected impact:** Improves accuracy of Newton steps for circuits with extreme impedance ratios (e.g., 1mΩ sense resistors next to 100MΩ feedback networks). May resolve some of the residual-zero failures where the solver thinks it converged but the scaled residual is still large.

---

## Implementation Roadmap

### Phase 1: Quick Wins (1-2 days)
- [ ] 1A: Node damping in Newton
- [ ] 1B: Reorder-on-singular-pivot fix
- [ ] 1C: True gmin stepping fallback

**Expected improvement:** 319 convergence failures → ~250-280 (10-20% reduction)

### Phase 2: Newton Hardening (2-3 days)
- [ ] 2A: Backtracking line search
- [ ] 2B: CKTsrcFact-style source scaling
- [ ] 2D: Adaptive pseudo-transient time step

**Expected improvement:** ~250 → ~180-200 (25-30% reduction)

### Phase 3: Diagnostics (1 day)
- [ ] 2C: Topology checker with actionable warnings

**Expected improvement:** Better error messages, may reclassify some SIM_ERROR as structural.

### Phase 4: Advanced Methods (1-2 weeks)
- [ ] 3A: Full transient startup (OPtran)
- [ ] 3B: Variable-gain homotopy
- [ ] 3C: Automatic node classification
- [ ] 3D: Matrix scaling

**Expected improvement:** ~180 → ~50-80 (further 50-60% reduction)

### Aspirational Target
With all improvements: **99.5%+ pass rate** (~35,000 models, <175 failures), with remaining failures being genuinely broken library files and recursive subcircuits.

---

## Key References

1. ngspice source: `src/spicelib/analysis/cktop.c` — DC convergence orchestration
2. ngspice source: `src/maths/ni/niiter.c` — Newton iteration with node damping
3. ngspice source: `src/spicelib/analysis/optran.c` — Transient startup for DC
4. Kelley, "Pseudo-Transient Continuation for DAEs", SIAM J. Sci. Comput., 2004
5. Moon et al., "Homotopy Methods for DC Analysis", Oregon State / Wiley
6. Roychowdhury & Melville, "Delivering Global DC Convergence for Large Mixed-Signal Circuits"
7. Intusoft, "Convergence in SPICE" — comprehensive practical guide
8. SIMetrix help: "DC Operating Point" — multi-algorithm approach documentation
9. LTwiki, "Convergence Problems" — practical tips and device modeling advice
10. Cadence Spectre lectures on convergence (YouTube)
