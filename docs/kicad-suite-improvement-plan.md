# KiCad SPICE Library Parity — Improvement Plan & Investigations

This is the consolidated reference for neospice's parity work against the KiCad
SPICE Library (ngspice as the reference simulator). It combines three previously
separate documents:

- **Part I — Suite Improvement Plan:** all known failures in the full KiCad suite
  and the planned simulator improvements (DC convergence, solver hardening, parser
  gaps) to close them.
- **Part II — 5000-Model MISMATCH Investigation & Scope Decisions:** the
  root-cause taxonomy of the cases where *both* simulators converge but disagree,
  and what is fixed / backlog / out of scope.
- **Part III — Case Study: LinearTech Op-Amp False Equilibrium (RESOLVED):** a
  detailed worked investigation, retained for historical context.

---
---

# Part I — Suite Improvement Plan

This part tracks all known failures in the KiCad SPICE Library test suite and the
planned simulator improvements needed to close them. It covers DC convergence,
solver hardening, parser gaps, and structural issues.

## Current State

**Certified 2026-06-12** (`results/compare_full_pifix.json`, full suite of
34,908 models, value-matched against `ngspice -D ngbehavior=psa`, with the
NEO_ONLY/NEO_TRIVIAL split, the **isolated+driven fallback**, the
**nested-brace model-param fix**, and the **`pi`/`e` built-in constants** applied):

| Status | Count | Meaning |
|---|---|---|
| MATCH | 24,199 (69.3%) | both converge, values agree |
| MISMATCH | 1,638 | both converge, values differ — ~794 XSPICE digital (out of scope), proven-ngspice-bug MOV varistors, rest adjudicated artifacts (Boyle-macromodel cancellation, saturation-knee, basin selection) |
| NG_ONLY | **21** | neospice fails, ngspice passes (was 801 on 2026-05-30) — all adjudicated residuals |
| NEO_ONLY | 2,118 | neospice produces a **non-trivial** solve where ngspice-psa *genuinely cannot* parse the deck even isolated (undefined PSpice params, param-nested subckts) |
| NEO_TRIVIAL | 3,373 | neospice solves an **unexcited** fixture to ~0 V while ngspice can't parse it even isolated — *not a win* |
| BOTH_FAIL | 3,559 | neither converges |

**`pi`/`e` built-in constants (2026-06-12):** neospice's expression evaluator
treated `pi`/`e` as unknown params → 0, so models with `wo=2*pi*fo`,
`S=pi*d*d/4` (e.g. emic.sub) hit division-by-zero → divergence. Added both
constants to `ExprParser` in `expression.cpp`, matching ngspice's
`.param`/B-source evaluator (`inpptree.c` predefines exactly `e` and `pi`; a
user param of the same name still takes precedence). Recovered the 2 emic.sub
cases (BOTH_FAIL→NEO_ONLY); no regressions. Regression test:
`Expression.BuiltinConstants`.

**Nested-brace model-param fix (2026-06-12):** vendor MOSFET libraries write
temperature formulas with NESTED braces, e.g. `VTO={2.1*{-0.0016*TEMP+1.04}}`.
Two parser layers mishandled the inner brace: `subst_brace_params`
(`subcircuit_expand.cpp`) matched the first `}` instead of the balanced one, and
`strip_braces` (`expression.cpp`) removed only the outer pair. The truncated
expression failed to evaluate, fell through to `parse_spice_number` which can't
read `{...}`, and the parameter was silently skipped — so `VTO` collapsed to its
default of 0 and the MOSFET conducted ~0.5 µA in cut-off. Fixed both layers
(depth-aware brace matching + normalizing `{`→`(`, `}`→`)` since `{expr}`≡`(expr)`
in SPICE). **Impact: 220 MISMATCH→MATCH** (vendor power-MOSFET off-state
leakage). Regressions: none (the 2 emic.sub cases that went NEO_TRIVIAL→BOTH_FAIL
were never valid comparisons — ngspice fails them too; the fix merely unmasks a
*separate* pre-existing gap, the undefined `pi` constant in neospice's `.param`
evaluator). Unit regressions: `Expression.NestedBraces`,
`SubcircuitExpand.NestedBracedParameterExpressions`.

**Isolated+driven fallback (2026-06-12):** when ngspice fails on a subckt test
(almost always a whole-library `.include` parse abort caused by one unrelated bad
construct elsewhere in the file), the harness now retries with the target subckt
extracted into a clean, dependency-closed library (so ngspice can parse it) and a
5 V / 1 kΩ stimulus injected on the first generic pin (so an unexcited fixture
actually exercises the device). The isolated run is adopted only if **both**
simulators then succeed. This rescued **3,568** former NEO_ONLY/NEO_TRIVIAL
non-comparisons into real comparisons — **3,218 MATCH (90.2%)** confirming
neospice was right, **350 MISMATCH** (small real diffs: off-state MOSFET leakage,
~0.4 % tolerance exceedances; a few dies replicated across many part numbers).
**Zero regression:** the main path is untouched (the fallback only fires when
ngspice originally failed), so NG_ONLY and BOTH_FAIL are unchanged and no prior
MATCH could flip. The ~5,500 still in NEO_ONLY/NEO_TRIVIAL are the genuine
robustness gap — ngspice-psa cannot evaluate them even isolated. Implementation:
`make_isolated_driven_netlist()` / `build_isolated_lib()` in
`tools/compare_kicad_models.py`; per-result `isolated` flag marks rescued cases.

**NEO_ONLY adjudication (2026-06-11):** the original 9,059 NEO_ONLY count was
inflated. Re-running neospice on all of them and using a gold-standard isolation
test (extract the target subckt into a clean lib so ngspice can parse it, then
compare) shows: **4,637 (51%) are trivial undriven-fixture solves** — now split
out as `NEO_TRIVIAL`, neither a win nor a bug (ngspice agrees to ~1e-19 V on
undriven decks; it merely failed earlier, at parse). Of the **4,422 non-trivial**
cases, where an ngspice reference is obtainable **~95% are genuine wins** (e.g.
isolated `LF155` matches ngspice to 10 sig figs: 1.004201527804348 vs
1.004201527777758) and **~5% disagree** (mostly near-zero reference-node
artifacts; a thin residue of real basin/cancellation bugs — MC33170, 4N40, a few
comparators). Root cause of ngspice's failures is **parser fragility**, not bad
circuits: ngspice-psa aborts the whole `.include` on one unrelated construct
anywhere in the file (unknown device in another subckt, a stray UTF-8 byte, an
undefined PSpice param/built-in, even 22× "double free" crashes), while
neospice's fault-tolerant parser simulates the requested device. This is a
parse-robustness story, **not** the "~2,000 OPtran convergence wins" implied
earlier — only ~1% of sampled NEO_ONLY were genuine ngspice *convergence*
failures. **Caveat:** ~1,240 non-trivial wins involved neospice silently dropping
a device/param; those DISAGREE with ngspice at ~21% (vs 4.7% base) and are the
main place latent bugs hide. (Classifier: `is_trivial_solution()` in
`tools/compare_kicad_models.py`; data: `results/compare_full_reclassified.json`.)

**Value-agreement rate:** of the **25,837 real two-simulator comparisons**
(MATCH + MISMATCH — up from 22,269 before the isolated+driven fallback),
**93.7 % MATCH** (24,199 / 25,837). Devices migrated from ngspice: MOS2
(LEVEL=2), VDMOS. Unit tests: 1108/1108.

**Current convergence flow** (matches ngspice `CKTop` in `cktop.c`):

1. Direct Newton-Raphson (MODEINITJCT → MODEINITFIX → MODEINITFLOAT)
2. Dynamic gmin stepping (Gillespie algorithm with adaptive factor)
3. Source stepping (Gillespie variant with adaptive ramp)
4. Pseudo-transient continuation (fictitious capacitor decay)
5. OPtran — real transient startup with ramped sources as final fallback
   (mirrors ngspice `optran.c`; only runs when 1–4 fail)

**Full failure breakdown:**

| Category | Count | Description |
|----------|-------|-------------|
| **SIM_ERROR** | **419** | DC convergence failures |
| — Convergence | 319 | Newton fails across all 4 methods |
| — Residual-zero | 77 | Solver reports residual=0 but not converged |
| — Channel length | 22 | MOSFET L=1u below model minimum |
| — Activation energy | 1 | Model parameter edge case |
| **ERROR** | **80** | Structurally broken library files |
| — smps_cb.lib | 50 | Uncommented text parsed as elements |
| — tube.lib | 30 | Same issue — malformed library syntax |
| **TIMEOUT** | **65** | Complex subcircuits exceeding 10s limit |
| **PARSE_ERROR** | **2** | Recursive subcircuit definitions (MOV-07D.lib) |

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

**Expected impact:** Convert cryptic "singular matrix" or "failed to converge" errors into
actionable diagnostics. The 80 ERROR cases (`smps_cb.lib` ×50, `tube.lib` ×30) are
structurally broken files that would also fail in ngspice — the topology checker would
identify them as unfixable and surface a clear message rather than attempting simulation.

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

### ~~Phase 1: Quick Wins~~ ✓ IMPLEMENTED
- [x] 1A: Node damping in Newton
- [x] 1B: Reorder-on-singular-pivot fix
- [x] 1C: True gmin stepping fallback

### ~~Phase 2: Newton Hardening~~ ✓ IMPLEMENTED
- [x] 2A: Backtracking line search
- [x] 2B: CKTsrcFact-style source scaling
- [x] 2D: Adaptive pseudo-transient time step

### ~~Phase 3: Diagnostics~~ ✓ IMPLEMENTED
- [x] 2C: Topology checker with actionable warnings

### Phase 4: Advanced Methods (1-2 weeks)
- [ ] 3A: Full transient startup (OPtran)
- [ ] 3B: Variable-gain homotopy
- [ ] 3C: Automatic node classification
- [ ] 3D: Matrix scaling

**Expected improvement:** ~180 → ~50-80 (further 50-60% reduction)

### Aspirational Target
With Phase 4: **99.5%+ pass rate** (~35,000 models, <175 failures), with remaining failures being genuinely broken library files and recursive subcircuits.

---

## Remaining Parser and Structural Issues

These failures are not convergence bugs — they require targeted parser or engine fixes.
Tracked here because they contribute to the overall KiCad suite pass rate.

### P1. Expression Evaluator Edge Cases (~5 cases)

Certain ASRC/B-source expressions fail to parse:
- `missing '...' for function if` (3 cases) — deeply nested `IF()` expressions
- `expected '...' in function if` (2 cases) — unusual `IF(cond, a, b)` syntax variants

**Fix:** Extend the expression parser in `src/parser/expression.cpp` to handle nested
`IF()` with missing separators and non-standard whitespace placement.

### P2. Recursive Subcircuit Nesting (2 cases)

Two MOV models in `MOV-07D.lib` hit the 100-level subcircuit recursion limit.
These are genuinely recursive `.subckt` definitions (not a circular reference from
a bug — the model is intentionally self-referential).

**Fix:** Either raise the recursion limit (low value — these are degenerate models)
or detect and report the recursion loop with a clear error message rather than
hitting the hard limit silently.

**Files:** `src/parser/subcircuit_expand.hpp`

### P3. TABLE VCVS Missing Table Points (2 cases)

`E` elements using `TABLE` syntax with fewer than 2 data points fail during
element parsing. The table interpolation requires at least 2 points.

**Fix:** Emit a diagnostic ("TABLE requires at least 2 points") rather than a
parse failure. Optionally treat a 1-point table as a constant.

**Files:** `src/parser/netlist_parser.cpp`

### P4. POLY CCCS Unknown Voltage Source (3 cases)

`F` element POLY forms reference a voltage source that cannot be resolved after
subcircuit expansion. The controlling source name is either in a different scope
or uses a naming convention the expander doesn't flatten correctly.

**Fix:** Extend cross-scope resolution in the subcircuit expander (same mechanism
used for AKO cross-scope resolution) to cover `F` element controlling source names.

**Files:** `src/parser/subcircuit_expand.cpp`

---
---

# Part II — 5000-Model MISMATCH Investigation & Scope Decisions

This part records the root-cause investigation (2026-05-28) of the **MISMATCH**
cases in the KiCad 5000-model ngspice parity test — circuits where *both*
simulators converge but the values disagree (baseline:
`results/compare_5k_after3.json`, 175 MISMATCH). It records what was **fixed**,
what is **deferred backlog** (real bugs, low impact), and what is explicitly
**out of scope** (large features / test artifacts).

Every item below was root-caused with numbers from both simulators. Full
mechanism detail also lives in the memory note `neospice-mismatch-taxonomy`.

**Result after the two high-impact fixes:** MATCH 3606 → 3677,
MISMATCH 175 → **104** (−71, −41%), **zero regressions**, all 992 unit tests
pass. The remaining 104 map exactly onto the out-of-scope / backlog buckets below.

## ✅ FIXED (the two high-impact gaps)

### 1. Modeled-resistor token order — ~18 cases
`Rname n+ n- MODEL value` (resistance **after** the model name). The parser only
read the resistance from `tokens[3]`; a model name there threw, the catch set
`val=0`, and the 1 mΩ floor turned it into a dead short. ngspice accepts both
token orders. **Fix:** `src/parser/netlist_parser.cpp` resistor branch now scans
later tokens (and `r=`) for the numeric value when `tokens[3]` is a res-model.
Verified: `RD1 a 0 ROVD1 1.1429e4` → 11429 Ω (was 0.001 Ω). LinearTech MISMATCH
33 → 17.

### 2. E/G TABLE endpoint smoothing — ~57 cases (53 photodiode + ~4 Elantec)
ngspice rewrites `E/G ... TABLE {expr} (x,y)...` into an XSPICE `pwl` code model
with `input_domain=0.1 fraction=TRUE limit=TRUE` (`src/frontend/inpcom.c`):
synthetic endpoint padding (flat-y extrapolation) + parabolic corner smoothing
(`cm_smooth_corner`, `src/xspice/cm/cmutil.c`). neospice clamped to the raw
endpoint value. **Fix:** `pwl_smooth_interp()` in
`src/devices/vcvs_nonlinear.hpp`, called by both `TableVCVS::interp` and
`TableVCCS::interp`. Verified: photodiode `net_c` −0.3776 (matches ngspice to
6 sig figs; was +0.131, a sign flip). D-photo MATCH 0 → 53/54. Unit test
`NonlinearVCVS.TableExactPoint` updated 5.0 → 4.875 (ngspice confirms 4.875 on
that exact circuit).

## 🔧 BACKLOG — real neospice bugs, low impact (NOT done; fix when prioritized)

| Bug | Cases | Fix site |
|-----|-------|----------|
| BJT `ISE/ISC > 1e-4` treated as relative multiplier of IS (SPICE2 rule) | 1 (KT801B) | `src/devices/bjt/bjt_setup.cpp`, mirror ngspice `bjtsetup.c:62-75` |
| `I(<non-vsource>)` in a B-element silently dropped (only `I(Vname)` bound) | 1 (AD633AN) | `src/devices/asrc/asrc_device.*` + current-sense shunt; `netlist_parser.cpp:3211-3246` |
| Model-card param tokenizer breaks on `,` (`af=1,`) and trailing `)` (`C2=1e-15)`) | ~4 (AD4817, 4N25/4N27) | model-card tokenizer |
| JFET `T_abs` parsed into ModelCard but never applied | (AD4817) | `src/devices/jfet/` temp path |
| BJT `[substrate]` bracket-node syntax parsed as a literal node | (4N25/4N27) | `netlist_parser.cpp` BJT branch |
| Modeled **capacitor/inductor** value-after-model (same class as Part II fix #1, C/L branches still skip on a model name) | rare | `netlist_parser.cpp` C/L branches |

These are genuine ngspice-parity bugs but each touches only 1–4 models. Grouping
the model-card tokenizer fix would knock out AD4817 + 4N25/4N27 together.

## 🚫 OUT OF SCOPE — large features

### XSPICE event-driven digital — 53 cases (ALL of Digital Logic)
54ALS / 54hcxx / 54fxx / 74fxx / 54hctxx / 54actxx use XSPICE digital primitives
(`anand [in1 in2] out ls_nand` + `.model ls_nand d_nand`). neospice silently
drops the `a`-device (floating-node warning), so the output stays 0; ngspice runs
the digital primitive through a dac_bridge → 3.3 V. Requires a full **event-driven
digital simulation kernel** (digital nodes, event queue, d_* code models, adc/dac
bridges) — a major subsystem, not a point fix.

### BJT quasi-saturation — 3 cases (Zetex FCX690B, ZXTN07012/07045)
`QUASIMOD=1 RCO=… GAMMA=… VO=…` epitaxial-collector model. neospice doesn't
define those params and runs plain Gummel-Poon. Substantial model feature
(`bjt_load.cpp` + param/temp plumbing) for 3 parts at 2–5 % error.

## 🚫 OUT OF SCOPE — not neospice bugs (test artifacts / both-valid)

- **Catastrophic cancellation (~6: TI lm101a/lm301a/ne5534/tl070/tl080/ua748).**
  Boyle macromodel reports `ga·(v11−v12)` with `v11≈v12≈4.95 V`; a ~1e-7 V solver
  residual is amplified ~1e5×. Both sims converge within tolerance; macro outputs
  (out, in_m) match. Not fixable without bit-identical iteration ordering.
- **BJT saturation-knee amplification (~14 BJT.lib).** Both within reltol=1e-3;
  the steep IC–Vce knee maps a 0.02–0.5 % current residual to 0.4–1.2 % Vce.
- **Multiple DC roots / basin selection.** Infineon depletion-MOSFET latches (3);
  LinearTech subckts with numeric port names (~7) where the harness powers
  nothing and ties all pins to ground (ill-posed fixture). Both land on different
  valid roots.
- **psa-mode reference (~7 LinearTech).** The harness runs
  `ngspice -D ngbehavior=psa` (needed to parse PSpice-syntax models;
  `tools/compare_kicad_models.py:112`). A few models match *standard* ngspice but
  not psa-mode. Closing these means emulating specific psa semantics — deferred.
  Note: the Elantec steep-gain (×5000) tables that still fail are a neospice
  **convergence** issue in the table active region, tracked separately from the
  TABLE-value fix above.

## Recall summary (if asked later)

"Out of scope" = the **53 digital (XSPICE event-driven)**, the **3 BJT
quasi-saturation**, and the **~30 artifacts** (cancellation, saturation-knee,
basin selection, psa-mode). "Backlog" = the **~7 small real bugs** in the table
above. "Done" = modeled-resistor token order + E/G TABLE smoothing.

---
---

# Part III — Case Study: LinearTech Op-Amp False Equilibrium (RESOLVED)

> **Status — RESOLVED (2026-05-28).** Retained for historical context. The
> root-cause conclusion in the analysis below (pivot order / LU roundoff) was
> **disproven**; the real cause and fix are in the box immediately following.

## RESOLVED (2026-05-28)

The root cause was **NOT** pivot order / LU roundoff as hypothesized below. It was
a bug in `Resistor`: any resistance below 1 mΩ was clamped up to `1e-3 Ω`
(`src/devices/resistor.cpp`). The LT op-amp output stage has `RC 17 0 1.063E-04`
(≈9409 S) balanced against `GC` (VCCS, gain 9408). Clamping RC to 1e-3 Ω
(1000 S) destroyed that balance by ~9.4×, driving node 17 (and the output) into
the wrong basin. ngspice (`restemp.c`) only substitutes 1 mΩ when **no**
resistance is given; a specified value is used directly.

Confirmed by tracing the DC Newton trajectory in an instrumented ngspice debug
build vs neospice: the two diverged at iteration 1 (neospice node 17 was 10×
ngspice's), and a minimal `GC`/`RC` test reproduced V(17)=9.408 (neospice) vs
1.0 (ngspice). Fix: only guard against a zero/non-finite resistance. After the
fix the standard test gives v(out)=1.00002, matching ngspice; all 992 unit tests
pass. The analysis below is retained for historical context but its root-cause
conclusion is superseded.

## Problem Statement

61 LinearTech op-amp models (LT1012, LT1007, LT1001, etc.) converge to a **false equilibrium** (v(out)≈-0.163V) instead of the correct operating point (v(out)=1.0V for the standard test circuit). Both neospice and ngspice use direct Newton—ngspice converges correctly purely due to different roundoff from its SPARSE 1.3 Markowitz solver.

## Circuit Topology (LT1012 Example)

```
V+ = 5V, V- = -5V, V(in_p) = 0.5V, V(in_m) via feedback resistors

Internal subcircuit path:
  Q1/Q2 diff pair → GA(VCCS, gm=1.131e-4) → node 8 (R2=100kΩ)
                  → GB(VCCS, gain=196) → node 1
                  → RO1(100Ω) → output (node 6)

Feedback/clamping:
  GC(VCCS, gain=9408): V(output) → current into node 17
  RC(1.063e-4 Ω): node 17 to ground (conductance = 9409 S)
  D1(1→17), D2(17→1): clamp diodes (Is=1.179e-19)
```

Key parameters: GC gain (9408) and RC conductance (9409 S) dominate the circuit. Node 17 is an extremely stiff node.

## Root Cause Analysis

### Why ngspice succeeds
ngspice uses SPARSE 1.3 with Markowitz pivoting (threshold 0.001, different tie-breaking from our solver). The different factorization order produces different roundoff in the Newton direction, steering early iterations toward the correct basin. No convergence aids are used—ngspice's `CKTnodeDamping=0` by default.

### What happens in neospice (direct Newton)
The Newton iteration trace reveals:

| Iter | v(out_net) | v(x1.17) | max_diff | Note |
|------|-----------|-----------|----------|------|
| 1    | 0.036     | 0.342     | —        | JCT→FIX transition |
| 5    | 0.531     | 2.257     | 13.7     | x1.17 growing |
| 9    | **1.002** | **9.424** | 0.026    | Output correct! Node 17 wrong |
| 10-12| ~1.0      | ~9.4      | 0.023    | Slowly converging... |
| 13   | —         | —         | **2.37** | Sudden divergence |
| 14+  | —         | —         | 2.37+    | Limit cycle |

**Critical observation**: Direct Newton reaches v(out)=1.0 at iteration 9, but node x1.17 is at 9.4V (should be ~1.0V). The diode D2 (anode=17, cathode=1) sees ~8.4V forward bias—physically impossible. Iterations 10-12 nearly converge (md=0.023) but then a sudden jump at iter 13 triggers a limit cycle.

### The false equilibrium mechanism
1. GC produces enormous current into node 17 proportional to v(out)
2. Node 17's voltage should be clamped by D1/D2 to within ~0.7V of node 1
3. During Newton, DEVpnjlim limits the **linearization voltage** but not the actual node voltage
4. Node 17 accumulates voltage across iterations (each step adds ~1.5V)
5. Once x1.17 >> v(node 1), D2 is numerically saturated and its Jacobian contribution becomes negligible
6. The system settles into a false fixed point where D2 "thinks" it's forward-biased but the actual physics are violated

## Approaches Tried

### 1. Node Damping (3.5V threshold)
**Result**: Fails. Limits per-iteration change to 3.5V but accumulation over 8+ iterations still pushes x1.17 past the crossover. Even at 1.5V threshold, same issue—just slower.

### 2. Oscillation Detection + Best-Solution Revert
**Result**: Partially works—detects the divergence at iter 13 and reverts. But the "best" solution at iter 9-12 isn't actually converged (md=0.023 > tolerance). Stuck in a revert loop.

### 3. Pseudo-Transient Continuation (PTC)
Multiple sub-approaches tried:

- **PTC from zero IC**: Large G_pseudo crushes all nodes to 0. Companion G*V_prev=G*0=0 provides no drive. Solution never evolves from zero.
- **PTC with warm start (from gmin stepping result)**: V_prev≈-0.163V (the wrong equilibrium). Companion reinforces the wrong basin. Reducing G leads back to divergence.
- **PTC with INITJCT→INITFLOAT transition**: Mode switch + G reduction simultaneously causes failure. Fixed by keeping same G during transition, but subsequent G reduction still fails.
- **Aggressive final probes**: Tried probing (remove companion, run plain Newton) at G=954 and G=0.93. v(out_net)≈0 at both—PTC never found the correct basin.

**Fundamental PTC limitation**: For this circuit, the false equilibrium at v(out)≈0 is the nearest equilibrium from zero. PTC's backward Euler companion with zero initial conditions naturally settles there.

### 4. Gain Stepping (combined source + gain)
**Result**: Gets to ~35.7% of full gain then hits a **bifurcation**. Steps below 1e-7 fail. Jump-to-full from 35.7% also fails.

**Hypothesis**: Near dep_src_fact≈0.44, the internal diodes D1/D2 switch state (off→on), creating a discontinuity that Newton can't track. The Jacobian becomes singular at the bifurcation point.

### 5. diag_gmin fix (applied to node variables only)
**Result**: Fixed a correctness bug (was applying gmin to branch current rows, corrupting voltage source equations). But 1e-12 on diagonal is negligible for this problem.

### 6. Supply-rail clamping
**Result**: Ineffective. Rail_max = max_node_value + 1.0 ≈ 6V. But x1.17's correct value (1.0V) is well within rails—the issue is the intermediate state during Newton, not the final value.

## Open Problems

> Note: these were the open problems *before* the resistor-clamp root cause was
> found. They remain useful as general Newton-hardening ideas (and overlap with
> Part I Priorities 2A/3A), but they were not what fixed this circuit.

### P1: Newton doesn't monotonically converge from the correct basin
At iter 9, v(out)=1.0 (correct) and md=0.026. But Newton can't complete convergence because x1.17 is at 9.4V. A **backtracking line search** would detect that the full Newton step at iter 13 increases the residual and scale back, potentially allowing convergence from the iter 9 state.

### P2: No convergence aid escapes the zero/false basin
All current aids (gmin stepping, source stepping, gain stepping, PTC) end up near v(out)≈0 or ≈-0.163V. We need either:
- **OPtran (transient-based OP)**: Run actual transient from t=0 with added capacitances. Circuit's parasitic caps provide physical damping. Most SPICE simulators use this as last resort.
- **Continuation past bifurcation**: Arc-length continuation or PTC specifically at the gain-stepping stuck point (~35% gain).
- **Randomized perturbation**: Try Newton from multiple random initial conditions. If any perturbation lands in the correct basin, Newton converges.

### P3: Pivot order affects basin selection
The core difference with ngspice is the LU factorization pivot order. Options:
- Try **full pivoting** (`numeric_fullpivot`) during direct Newton for these circuits
- Try **random pivot perturbation**: add tiny random noise (1e-15 scale) to matrix entries before factorization to change tie-breaking
- Implement a second pivot strategy (e.g., different Markowitz threshold) and try both

### P4: DEVpnjlim doesn't prevent unphysical junction voltages
The pnjlim mechanism limits the linearization voltage for I-V calculation but doesn't prevent the actual node voltage from growing unboundedly. A hard clamp on Vd = V(anode) - V(cathode) within the Newton iteration might help, but could also prevent convergence for legitimately large reverse-bias scenarios.

## Recommended Next Steps (in priority order)

1. **Backtracking line search in Newton** (addresses P1):
   - After computing Newton step Δx, evaluate ||f(x + Δx)||
   - If ||f(x + Δx)|| > ||f(x)||, try α=0.5, 0.25, etc.
   - This prevents the overshoot at iter 13 and may allow convergence from iter 9's state
   - Low risk of regression (line search can only reduce step, never increase)

2. **OPtran implementation** (addresses P2):
   - Add virtual capacitors (C=1e-12 F) to all nodes
   - Run 100-200 backward Euler steps with progressively larger dt
   - Uses circuit physics (KVL/KCL with time evolution) to find correct steady state
   - This is how ngspice's `-o` option works and is standard in commercial SPICE

3. **Alternative pivot ordering** (addresses P3):
   - Try `numeric_fullpivot` for DC OP when direct Newton fails
   - Or implement Markowitz with threshold=0.001 (ngspice's value; ours is 0.1)
   - Test if different pivot ordering reaches correct basin

4. **Junction voltage hard limit** (addresses P4):
   - In diode `limit_voltages()`, clamp V(anode)-V(cathode) to [-100Vt, +40Vt]
   - Prevents the unphysical x1.17=9.4V state from forming
   - Requires careful testing to avoid breaking legitimate circuits

---

## Test Commands

```bash
# Build
cd build && cmake --build . -j$(nproc)

# Run unit tests
cd build && ctest -j$(nproc)

# Test single model
./build/neospice /tmp/test_lt1012_verbose.cir

# Run KiCad comparison (all 5000 models)
python3 tools/compare_kicad_models.py --max 5000 --save results/compare_5k.json --jobs 8

# Run a specific library/family
python3 tools/compare_kicad_models.py --file "LinearTech" --save results/lt_only.json --jobs 8

# See actual error margins (configure with debug compare)
cmake .. -DNEOSPICE_DEBUG_COMPARE=ON   # prints MARGIN_TRAN|signal|err|tol|headroom
```

---

## Key References

1. ngspice source: `src/spicelib/analysis/cktop.c` — DC convergence orchestration
2. ngspice source: `src/maths/ni/niiter.c` — Newton iteration with node damping
3. ngspice source: `src/spicelib/analysis/optran.c` — Transient startup for DC
4. ngspice source: `src/frontend/inpcom.c` + `src/xspice/cm/cmutil.c` — E/G TABLE → XSPICE pwl rewrite and corner smoothing
5. Kelley, "Pseudo-Transient Continuation for DAEs", SIAM J. Sci. Comput., 2004
6. Moon et al., "Homotopy Methods for DC Analysis", Oregon State / Wiley
7. Roychowdhury & Melville, "Delivering Global DC Convergence for Large Mixed-Signal Circuits"
8. Intusoft, "Convergence in SPICE" — comprehensive practical guide
9. SIMetrix help: "DC Operating Point" — multi-algorithm approach documentation
10. LTwiki, "Convergence Problems" — practical tips and device modeling advice
11. Cadence Spectre lectures on convergence (YouTube)
