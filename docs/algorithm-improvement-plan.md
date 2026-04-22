# Algorithm Improvement Plan

Based on industry comparison research (see `docs/industry-comparison.md`), this
plan identifies concrete improvements to neospice's core algorithms, ordered by
impact and feasibility.

---

## Priority 1: True Source Stepping

**Gap**: neospice's `source_stepping()` is a gmin-stepping proxy with a different
schedule. It does not actually scale source values. Every other surveyed simulator
(ngspice, Xyce, HSPICE, PSpice) implements true source stepping.

**Impact**: High. Circuits with high-gain feedback loops or strong bias-dependent
nonlinearities may fail to converge where true source stepping would succeed.

**Feasibility**: Straightforward. `VSource::set_dc_value()` and
`ISource::set_dc_value()` already exist and are used by DC sweep and sensitivity.

**Implementation**:

1. Collect all independent sources (VSource, ISource) in the circuit
2. Save their original DC values
3. Scale all source values to `fraction × original_value` for fraction in
   {0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0}
4. At each step, solve the DC operating point using the previous step's solution
   as the initial guess
5. If any step fails, halve the fraction increment and retry
6. Restore original values after completion (even on failure)

**Files to modify**:
- `src/core/convergence.cpp` — rewrite `source_stepping()` function
- `src/core/convergence.hpp` — no signature change needed

**Test**:
- Existing tests must still pass (regression)
- Add a high-gain feedback circuit that fails with gmin-only but converges with
  true source stepping (e.g., Schmitt trigger or latch at a difficult bias point)

**Estimated effort**: Small (half day). The infrastructure is already in place.

---

## Priority 2: Configurable LTE Reference Mode

**Inspiration**: Xyce's `NEWLTE` option offers 4 reference modes:
- Mode 0: Normalize against current node value
- Mode 1: Normalize against max of all signals (default)
- Mode 2: Normalize against max over all time
- Mode 3: Normalize against max per signal over all time

**Current state**: neospice uses a fixed per-node tolerance:
`tol = reltol * |sol[i]| + vntol` (equivalent to Xyce mode 0).

**Impact**: Medium. Mode 1 or 3 prevents large signals from masking errors in
small signals. Useful for mixed-signal circuits where a 1.8V supply rail coexists
with a 1mV analog signal.

**Implementation**:

1. Add `lte_ref_mode` option to `SimOptions` (default 0 for backward compatibility)
2. In `TimeStepController::evaluate_step()`:
   - Mode 0 (current): `tol = reltol * |sol[i]| + vntol` (existing)
   - Mode 1 (max all): `tol = reltol * max(|sol|) + vntol`
   - Mode 3 (max per signal): track `max_seen[i]` across time, use
     `tol = reltol * max_seen[i] + vntol`
3. Expose via `.options lte_ref_mode=N` in the netlist parser

**Files to modify**:
- `src/core/timestep.hpp` — add `max_seen_` vector, `lte_ref_mode` parameter
- `src/core/timestep.cpp` — modify `evaluate_step()` with mode dispatch
- `src/core/options.hpp` — add `lte_ref_mode` field to `SimOptions`
- `src/parser/parser.cpp` — parse `.options lte_ref_mode`

**Test**:
- Circuit with a small analog signal on a node adjacent to a large digital swing.
  Verify that mode 1/3 takes smaller steps near the small signal than mode 0.

**Estimated effort**: Small (half day).

---

## Priority 3: Breakpoint Step Recovery

**Inspiration**: Xyce's `NEWBPSTEPPING` with `RESTARTSTEPSCALE=0.005`.

**Current state**: neospice reduces dt to `0.1 × min(saved_delta, bp_gap)` at
breakpoints. This is a fixed 10× reduction regardless of the breakpoint's nature.

**Improvement**: Adaptive post-breakpoint recovery:

1. **Classify breakpoints**: Distinguish between hard edges (PULSE rise/fall) and
   soft transitions (SIN zero-crossing, PWL interpolation). Hard edges need more
   aggressive dt reduction; soft transitions can use a milder reduction.
2. **Graduated recovery**: After a hard breakpoint, grow dt back toward the
   pre-breakpoint value over 3-5 accepted steps using a 2× growth cap (current
   behavior). After a soft breakpoint, allow faster recovery (4× growth).
3. **Configurable restart scale**: Add `restart_step_scale` option (default 0.1,
   matching current behavior). Xyce uses 0.005 as default, which is more
   aggressive. Allow users to tune this.

**Files to modify**:
- `src/core/timestep.hpp` — add breakpoint classification, `restart_step_scale`
- `src/core/timestep.cpp` — modify `clamp_to_breakpoint()` and add recovery logic
- `src/core/transient.cpp` — pass breakpoint type to controller
- `src/devices/vsource.hpp` — expose `SourceFunction` for breakpoint classification
- `src/core/options.hpp` — add `restart_step_scale` to `SimOptions`

**Test**:
- SIN source circuit: verify that dt recovers faster after zero-crossing
  breakpoints than after PULSE edges.
- Compare step count with ngspice on a mixed PULSE+SIN testbench.

**Estimated effort**: Medium (1-2 days). Requires changes across multiple files.

---

## Priority 4: Current Variable LTE

**Inspiration**: Xyce's `MASKIVARS=0` (default) includes current variables in
global LTE, not just voltages.

**Status**: **Investigated and rejected.**

**Investigation findings**: Branch current variables in the MNA solution vector
fall into two categories:

1. **Integration state variables** (inductor currents) — evolved via flux
   integration, their second differences are O(h²) and the LTE formula applies.
   However, `Inductor::compute_trunc()` already provides device-specific LTE on
   flux history with appropriate `chgtol`-based tolerance. Global LTE would be
   redundant.

2. **Algebraic variables** (voltage source, VCVS, CCVS currents) — determined
   by KCL, not by integration. Their second differences do NOT converge to zero
   with decreasing h, violating the LTE formula's O(h²) assumption. Including
   them causes irreversible timestep collapse in MOSFET circuits (observed:
   delta2 = 3.3e-3 at dt = 1e-17, ratio > 200× tolerance).

**Conclusion**: Global LTE should only check node voltages. Device-specific LTE
(`compute_trunc()`) is the correct mechanism for internal state variables —
devices have domain knowledge about their charge/flux offsets and appropriate
tolerances. This matches ngspice's architecture.

**Estimated effort**: Small (few hours).

---

## Priority 5: Pseudo-Transient Continuation

**Inspiration**: Xyce's LOCA-based pseudo-transient for extremely difficult DC
operating points.

**Concept**: When all other convergence aids fail, insert fictitious capacitors
on every node and "simulate" a transient from a known-good state (e.g., all
zeros) toward the steady-state operating point. The capacitor values are chosen
large enough that the transient is numerically stable, then gradually reduced.

**Impact**: Low frequency but high value. Only needed for circuits that defeat
both gmin and source stepping — but when it's needed, nothing else works.

**Implementation** (high-level):

1. Add fictitious capacitor stamps to every node: `C_pseudo / dt` added to the
   diagonal of the conductance matrix
2. Start with large `C_pseudo` (e.g., 1e-6) and `dt` = 1e-9
3. Run transient-like Newton iterations, advancing pseudo-time
4. Gradually reduce `C_pseudo` or increase `dt` until the system reaches steady
   state (pseudo-transient current → 0)
5. The final solution is the DC operating point

**Files to modify**:
- `src/core/convergence.cpp` — add `pseudo_transient()` function
- `src/core/convergence.hpp` — declare new function
- `src/core/dc.cpp` — add to convergence sequence after source stepping

**Test**:
- Known-difficult circuits: cross-coupled latch, Schmitt trigger, multi-stage
  feedback amplifier with pathological bias.

**Estimated effort**: Medium-large (2-3 days). Novel algorithm, needs careful
tuning of pseudo-capacitance schedule.

---

## Priority 6: Trap Ringing Detection

**Inspiration**: LTspice's "modified trap" and ngspice's BE fallback.

**Current state**: neospice keeps trapezoidal integration everywhere and relies on
global LTE to catch ringing. This works but may accept small oscillations that
don't exceed the LTE threshold.

**Approach** (not mimicking LTspice's proprietary algorithm):

1. **Detection**: After each accepted step, check for sign alternation in the
   second derivative of node voltages over the last 3 steps. If consecutive
   `delta2` values alternate in sign, the solution is oscillating.
2. **Response**: When ringing is detected, temporarily switch to Gear-2 (which
   is A-stable and does not exhibit trap ringing) for that step. Gear-2 is
   already implemented in neospice.
3. **Recovery**: After 2 stable steps without sign alternation, return to
   trapezoidal.

**Alternative**: Instead of switching method, reduce timestep by 4× when ringing
is detected. Simpler but slower.

**Files to modify**:
- `src/core/transient.cpp` — add ringing detection after step acceptance
- `src/core/timestep.hpp` — add oscillation tracking state

**Test**:
- Circuit known to exhibit trap ringing: LC oscillator with step input,
  switched-capacitor filter.

**Estimated effort**: Medium (1-2 days). Detection is straightforward; the
challenging part is avoiding false positives.

---

## Priority 7: Spectre-Style NQS AC Fallback

**Current state**: BSIM4v7 `acnqsMod` is unsupported because NQS conductances
depend on frequency, which breaks the G/C matrix caching optimization.

**Approach**: Detect NQS-enabled devices at circuit setup time. For those devices
only, evaluate the AC stamp per frequency and add the contribution to the cached
G + jωC matrix. Non-NQS devices still use the cache.

**Impact**: Low. NQS is rarely used in practice. But supporting it removes a
documented limitation.

**Estimated effort**: Medium (1-2 days).

---

## Implementation Status

| # | Improvement | Status | Commit |
|---|-------------|--------|--------|
| 1 | True source stepping | **Done** | `282ad8c` |
| 2 | Configurable LTE reference mode | **Done** | `9f5a39f` |
| 3 | Breakpoint step recovery | **Done** | `7fb4e64` |
| 4 | Current variable LTE | **Rejected** | Algebraic branch currents violate O(h²); device LTE covers inductors |
| 5 | Pseudo-transient continuation | **Done** | `04e29c8` |
| 6 | Trap ringing detection | **Done** | `362d744` |
| 7 | NQS AC fallback | Deferred | Requires BSIM4v7 NQS AC stamp |

All 820 tests pass after implementation.

---

## Metrics

For each improvement, measure:

1. **Convergence**: Number of circuits from the test suite that converge (should
   only increase)
2. **Accuracy**: Rise/fall time, crossing time agreement with ngspice and tight-
   tolerance reference (should not regress beyond existing tolerances)
3. **Performance**: Wall-clock time on the CMOS inverter benchmark (should not
   regress more than 5%)
4. **Step count**: Total accepted/rejected steps in transient (informational —
   some improvements may increase step count for better accuracy)
