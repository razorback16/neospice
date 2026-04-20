# Device Accuracy Fixes: Matching ngspice Reference Implementation

**Date**: 2026-04-20
**Scope**: All 16 custom-implemented device models in neospice
**Reference**: ngspice source at `~/Codes/ngspice`

## Background

An exhaustive audit compared all custom neospice device models against their ngspice counterparts. The audit found ~40 discrepancies ranging from critical sign errors (coupled inductor) to missing features (temperature coefficients, multiplier parameter). Migrated devices (BSIM3, BSIM4v7, BJT, JFET, MOS1, DIO, VBIC, LTRA) are excluded — they use ngspice's original code via a shim layer.

## Approach

**Fix-by-severity with infrastructure grouping.** Critical formula bugs first, then accuracy-impacting missing features, then parameter completeness, then polish. Within each phase, fixes that share infrastructure are grouped together (e.g., LTE for capacitor + inductor + coupled inductor).

Each fix includes a dedicated ngspice comparison test using the existing `NgspiceRunner` + `Comparator` framework.

---

## Phase 1: Critical Formula/Sign Bugs

*These produce wrong results in any simulation that uses the affected device.*

### 1.1 Coupled Inductor RHS Sign Error + Double-Counting

**Files**: `src/devices/coupled_inductor.cpp`, `src/devices/coupled_inductor.hpp`

**Problem**: The trapezoidal companion model has two bugs:
- RHS stamps use `+=` (positive) but should use `-=` (negative) to match the inductor's convention at `inductor.cpp:55`
- `v_m12_prev_` / `v_m21_prev_` double-counts the mutual voltage already captured in the inductor's terminal voltage `v_prev_`

The Gear-2 path has the same sign error (positive instead of negative).

**Fix**:
- Trapezoidal: Change `add_rhs_if_valid(rhs, br1, v_eq_12)` to `add_rhs_if_valid(rhs, br1, -v_eq_12)` (and same for br2)
- Remove `v_m12_prev_` / `v_m21_prev_` from the trapezoidal path and recompute `v_eq` using only the current values: `v_eq_12 = r_eq_m * i2_prev_`
- Gear-2: Same sign flip: `add_rhs_if_valid(rhs, br1, -v_eq_12)`
- Remove `v_m12_prev_` / `v_m21_prev_` member variables if no longer needed by either integration method

**Validation**: Create `tests/circuits/coupled_inductor_transient.cir` — two coupled inductors with K=0.5, pulse source, compare transient waveforms against ngspice.

### 1.2 Current Source AC Excitation Missing

**Files**: `src/core/ac.cpp`

**Problem**: `solve_ac()` at lines 122-133 only populates `ac_rhs` for VSource devices. ISource with `ac_mag != 0` is silently ignored, producing zero output for any AC analysis driven by a current source.

**Fix**: After the VSource loop (line 133), add an ISource loop:
```cpp
for (auto* dev : ckt.devices()) {
    if (auto* is = dynamic_cast<ISource*>(dev); is && is->ac_mag() != 0.0) {
        auto exc = std::polar(is->ac_mag(), is->ac_phase_rad());
        // Match DC convention: rhs[np] -= I (current leaves np), rhs[nn] += I (current enters nn)
        if (is->pos_node() >= 0) ac_rhs[is->pos_node()] -= exc;
        if (is->neg_node() >= 0) ac_rhs[is->neg_node()] += exc;
    }
}
```
Sign verified against `isource.cpp:73-74` DC convention (`rhs[np] -= I`, `rhs[nn] += I`) and ngspice's `isrcacld.c` (which uses the same polarity after accounting for node-name inversion in struct fields).

**Validation**: `tests/circuits/isrc_ac.cir` — RC circuit driven by AC current source, compare magnitude/phase against ngspice.

### 1.3 Switch: Replace Smooth Step with Proper Hysteresis Model

**Files**: `src/devices/switch.cpp`, `src/devices/switch.hpp`

**Problem**: neospice uses a smooth cubic interpolation with no state memory. ngspice uses a 4-state hard-switching model with direction-dependent hysteresis thresholds. Additionally, Roff defaults to 1e6 instead of 1/gmin (~1e12).

**Fix**:
- Add state enum: `{REALLY_OFF, REALLY_ON, HYST_OFF, HYST_ON}`
- Store current state and previous state per instance
- Implement ngspice's switching logic from `swload.c`:
  - Positive Vh: on at Vt+Vh, off at Vt-Vh
  - Negative Vh: reversed thresholds with inversion logic
  - State persistence in hysteresis band
- Use hard conductance selection: `g = (state == ON) ? g_on : g_off`
- Force non-convergence on state change during INITFLOAT mode
- Default Roff: use circuit gmin (add gmin parameter or access from circuit)
- Parse ON/OFF initial state keywords from netlist

**Validation**: `tests/circuits/switch_hysteresis.cir` — switch with Vh > 0 driven by triangle wave, compare switching times against ngspice.

### 1.4 Transmission Line DC Short-Circuit and History Initialization

**Files**: `src/devices/tline.cpp`, `src/devices/tline.hpp`

**Problem**: At DC, neospice models each port as an independent Z0 shunt with no cross-coupling. ngspice correctly models the TL as a short circuit (V1=V2). Also, transient history is initialized to zero instead of from the DC operating point.

**Fix**:
- **DC mode**: Add cross-port coupling stamps. The simplest approach: in non-transient mode, stamp the TL as a wire (zero impedance between port 1 and port 2). Use ngspice's approach: add branch equations that enforce V1=V2 with small gmin resistance, or simply stamp a very large conductance (1/gmin) between the two ports.
- **History init**: After DC solve, in `init_dc_state()` or equivalent, seed the history buffer with `{t=0, v1=V1_dc, i1=I1_dc, v2=V2_dc, i2=I2_dc}` and pre-fill points at `t=-TD` and `t=-2*TD` with the same DC values (matching ngspice's 3-point initialization).

**Validation**: `tests/circuits/tline_dc.cir` — TL connecting two resistor dividers, verify DC operating point matches ngspice. `tests/circuits/tline_pulse.cir` — pulse propagation, verify first TD seconds of transient.

---

## Phase 2: Accuracy-Impacting Missing Features

*Simulations run but may produce inaccurate results under certain conditions.*

### 2.1 Capacitor & Inductor LTE (compute_trunc)

**Files**: `src/devices/capacitor.cpp/hpp`, `src/devices/inductor.cpp/hpp`, `src/devices/coupled_inductor.cpp/hpp`

**Problem**: None of the linear reactive devices implement `compute_trunc()`. In circuits dominated by L/C dynamics (RC filters, LC oscillators, switching converters), there is no device-level truncation error control.

**Fix**:
- **Capacitor**: Track charge history Q = C*V in a 3-point ring buffer. Override `compute_trunc()` to compute divided differences of charge and estimate LTE using the same formula as ngspice's `CKTterr`:
  - For trapezoidal: `LTE ≈ (1/12) * h³ * Q'''` → `dt_new = dt * (trtol * chgtol / |LTE|)^(1/3)`
  - For Gear-2: `LTE ≈ (2/9) * h³ * Q'''` → similar formula with Gear coefficient
- **Inductor**: Track flux history Phi = L*I in a 3-point ring buffer. Same `compute_trunc()` approach but using flux instead of charge.
- **Coupled Inductor**: Override `compute_trunc()` to check both inductors' flux LTE (since the inductor itself doesn't do it). Use total flux `Phi = L*I_self + M*I_partner`.

The `state_vars()` and `set_state_ptrs()` methods should NOT be used for these simple devices — they use the BSIM4-style circuit-wide state ring which is overkill. Instead, maintain private history buffers (the current `v_prev_`, `i_prev_` pattern extended with `q_prev_`, `q_prev2_`, `q_prev3_`).

**Validation**: RC integrator circuit with tight tolerances — verify neospice reduces timestep near sharp transitions. LC oscillator — verify period accuracy improves with LTE enabled.

### 2.2 Variable-Timestep Gear-2 Coefficients

**Files**: `src/devices/capacitor.cpp`, `src/devices/inductor.cpp`, `src/devices/coupled_inductor.cpp`

**Problem**: Gear-2 formulas hardcode equal-timestep coefficients (1.5, 4, -1). When the adaptive timestep controller changes dt, these are wrong.

**Fix**: Compute BDF-2 coefficients from the timestep ratio `r = dt_prev / dt`:
```
ag[0] = (1 + 2r) / ((1 + r) * dt)
ag[1] = -(1 + 2r) / (r * dt)  [for prev state]
ag[2] = (1 + 2r) / (r * (1 + r) * dt)  [for prev2 state]
```
This matches the general BDF-2 formula from ngspice's `NIcomCof`. Store `dt_prev_` alongside `dt_` so the ratio is available.

**Validation**: Transient circuit with varying timestep (e.g., pulse with sharp edge followed by flat region), compare capacitor voltage waveform against ngspice.

### 2.3 PULSE/SIN Default Parameter Values

**Files**: `src/devices/vsource.hpp`, `src/devices/isource.hpp`, `src/parser/netlist_parser.cpp`

**Problem**: PULSE defaults TR/TF/PW/PER to 0 (ngspice: tstep/tstep/tstop/tstop). SIN defaults freq to 0 (ngspice: 1/tstop).

**Fix**: The defaults depend on simulation parameters (tstep, tstop) which aren't known at parse time. Two options:
- **Option A**: Store sentinel values (e.g., -1) and resolve them when the transient analysis starts. This requires a `resolve_defaults(tstep, tstop)` call on each source before the transient loop.
- **Option B**: Pass tstep/tstop to the parser and set defaults immediately.

**Chosen**: Option A — cleaner separation, and sources might be reused across analyses.

Sentinel values:
- `PulseParams::tr = -1` → resolved to `tstep`
- `PulseParams::tf = -1` → resolved to `tstep`
- `PulseParams::pw = -1` → resolved to `tstop`
- `PulseParams::per = -1` → resolved to `tstop`
- `SinParams::freq = -1` → resolved to `1/tstop`

Add `resolve_source_defaults(tstep, tstop)` to transient.cpp preamble.

**Validation**: Sources with omitted parameters, compare against ngspice.

### 2.4 Transmission Line AC Model

**Files**: `src/devices/tline.cpp`, `src/devices/tline.hpp`

**Problem**: AC stamp is just G0 shunts with no frequency dependence. Should implement exact `exp(-jωTD)` cross-coupling.

**Fix**: Override `ac_stamp()` to stamp:
- Self-terms: G0 = 1/Z0 on each port diagonal (real, into G matrix)
- Cross-terms: `-G0 * exp(-jωTD)` between ports. Since the AC framework uses `G + jωC`, and the cross-coupling is frequency-dependent in a non-linear way (not proportional to ω), we need to stamp directly into the complex matrix per-frequency. This requires either:
  - (a) A new device callback `ac_stamp_complex(omega, complex_matrix)` called at each frequency point
  - (b) Encoding the delay as an approximate C-matrix contribution (only valid for small ωTD)

**Chosen**: Option (a) — add an optional `ac_stamp_freq(omega, complex_matrix, complex_rhs)` virtual to Device, with a default that falls back to `G + jωC`. The TL overrides this to stamp exact complex entries. This keeps the existing G/C framework for all other devices.

**Validation**: TL AC sweep — compare gain and phase against ngspice.

### 2.5 Transmission Line Breakpoints and Quadratic Interpolation

**Files**: `src/devices/tline.cpp`, `src/devices/tline.hpp`

**Problem**: Linear interpolation (1st-order) instead of quadratic (2nd-order). No breakpoint generation for delay events. No timestep truncation.

**Fix**:
- **Interpolation**: Replace linear interpolation with 3-point Lagrange (quadratic). Find the 3 history points bracketing `t - TD` and compute Lagrange basis polynomials.
- **Breakpoints**: In `accept_step()`, compute the second difference of the wave variables. If significant (relative to tolerance), schedule a breakpoint at `t_accepted + TD` by returning it from a new `suggest_breakpoints()` method (or store it for the transient solver to query).
- **Truncation**: Override `compute_trunc()` — use the same second-difference test to limit the timestep, preventing the solver from stepping past the arrival of a significant delayed event.

**Validation**: Sharp pulse through TL — verify timing accuracy of reflected pulse matches ngspice.

### 2.6 ASRC Convergence and Numerical Safeguards

**Files**: `src/devices/asrc/asrc_device.cpp`, `src/devices/asrc/expression_ast.cpp`

**Problem**: `device_converged()` always returns true. No division-by-zero protection, no domain error handling for sqrt/log/pow of negative.

**Fix**:
- **Convergence**: Store the previous expression value. In `device_converged()`, re-evaluate the expression and compare: `|new - old| < reltol * max(|new|, |old|) + tol` where tol = vntol (voltage mode) or abstol (current mode).
- **Division**: Add gmin-based fudge factor to divisor: `a / (b + copysign(gmin * 1e-20, b))`
- **sqrt(negative)**: Return `sqrt(abs(x))` with warning, matching ngspice's HUGE return
- **log(negative)**: Return `log(abs(x))` or a large negative value, matching ngspice
- **pow(negative, non-integer)**: Force base positive, matching ngspice

**Validation**: B-element with challenging expressions (division near zero, conditional switching), verify convergence against ngspice.

---

## Phase 3: Missing Parameter Support

*These affect netlists that use model cards or advanced instance parameters on simple devices.*

### 3.1 Temperature Coefficients for R/C/L

**Files**: `src/devices/resistor.cpp/hpp`, `src/devices/capacitor.cpp/hpp`, `src/devices/inductor.cpp/hpp`, `src/parser/netlist_parser.cpp`

**Problem**: No TC1, TC2, tnom, temp, dtemp, scale support.

**Fix**:
- Add a shared helper or inline pattern:
  ```cpp
  double temp_factor(double tc1, double tc2, double temp, double dtemp, double tnom) {
      double dt = (temp + dtemp) - tnom;
      return 1.0 + tc1 * dt + tc2 * dt * dt;
  }
  ```
- Each device stores: tc1_, tc2_, tnom_, temp_, dtemp_, scale_
- Effective value computed once at setup: `R_eff = R_nom * temp_factor(...) * scale`
- Temperature recomputation on `reset_temp()` call
- Parser: extend R/C/L parsing to handle inline `tc1=`, `tc2=`, `temp=`, `dtemp=`, `scale=`
- Parser: support `.model` card for resistor/capacitor (model type "r"/"c")

**Validation**: Resistor/capacitor at non-default temperature, compare against ngspice.

### 3.2 Multiplier (m) Parameter

**Files**: All simple device files, `src/parser/netlist_parser.cpp`

**Problem**: ngspice supports `m=N` on most devices to model N parallel instances. neospice ignores it everywhere.

**Fix**:
- Add `m_` field (default 1.0) to: Resistor, Capacitor, Inductor, VSource(?), ISource, VCCS, CCCS
- Apply in stamps:
  - Resistor: `g *= m`
  - Capacitor: `g_eq *= m`, `i_eq *= m`, AC `C *= m`
  - Inductor: `inductance_eff = L / m` (parallel inductors)
  - VCCS: `gm *= m`
  - CCCS: `gain *= m`
  - Noise: multiply noise spectral density by m
- Parser: parse `m=` from device instance line

**Validation**: Devices with m=2, compare against ngspice (equivalent to two parallel instances).

### 3.3 Geometry Models for R/C

**Files**: `src/devices/resistor.cpp/hpp`, `src/devices/capacitor.cpp/hpp`, `src/parser/netlist_parser.cpp`, `src/parser/model_cards.hpp`

**Problem**: ngspice computes R from sheet resistance and geometry, C from unit-area capacitance and geometry.

**Fix**:
- Resistor model: RSH (sheet resistance), NARROW, SHORT, DEFW, DEFL
  - `R = RSH * (L - SHORT) / (W - NARROW)` when no explicit R given
- Capacitor model: CJ (unit area), CJSW (sidewall), NARROW, SHORT, DEFW, DEFL
  - `C = CJ*(W-narrow)*(L-short) + CJSW*2*((L-short)+(W-narrow))`
- Add `ResistorModelCard` and `CapacitorModelCard` types
- Parser: support `.model rmod R (rsh=100 narrow=0.1u)` and instance `R1 n+ n- rmod W=1u L=10u`

**Validation**: Geometry-based R/C values, compare against ngspice.

### 3.4 Switch Timestep Control

**Files**: `src/devices/switch.cpp`, `src/devices/switch.hpp`

**Problem**: No `compute_trunc()` — can miss fast switching transitions.

**Fix**: Implement `compute_trunc()` that monitors the control voltage rate of change and limits timestep to prevent jumping over the switching threshold. Use the distance-to-threshold divided by the rate of change to estimate the safe timestep.

**Validation**: Fast-switching circuit, verify switching time accuracy.

### 3.5 ASRC Missing Functions and Ternary Fix

**Files**: `src/devices/asrc/expression_ast.cpp`

**Problem**: 16 missing math functions, wrong ternary semantics.

**Fix**: Add each function with proper AD derivative:
- `sgn(x)`: returns -1/0/1, derivative = 0
- `ustep(x)` / `u(x)`: returns 0 for x<0, 1 for x>=0, derivative = 0
- `uramp(x)`: returns max(0, x), derivative = (x > 0) ? 1 : 0
- `u2(x)`: smooth step (x²/(1+x²)), derivative via quotient rule
- `acosh/asinh/atanh`: standard inverse hyperbolic, derivatives from calculus
- `ceil/floor/nint`: round functions, derivative = 0
- `pwr(x, y)`: `sgn(x) * |x|^y`, derivative via chain rule
- `pwl(x, x1,y1, x2,y2, ...)`: piecewise linear lookup
- Fix ternary: change `cond > 0.0` to `cond != 0.0`
- Add `TEMPER` variable (reads circuit temperature) and `HERTZ` variable (reads current frequency in AC)

**Validation**: Expression evaluation unit tests for each new function. B-element circuits using these functions compared against ngspice.

---

## Phase 4: Polish and Edge Cases

### 4.1 Resistor Zero-Resistance Guard
Clamp `|R| < 1e-3` to `R = 1e-3` (1 milliohm), matching ngspice.

### 4.2 AC Resistance for Resistors
Add optional `acresist` parameter, use in `ac_stamp()` when given.

### 4.3 Initial Conditions (IC) for C/L
Parse `IC=` on capacitors (initial voltage) and inductors (initial current). Apply during transient init when UIC mode is active.

### 4.4 Transmission Line Initial Conditions
Support `IC=V1,I1,V2,I2` parameter on T-element.

### 4.5 Fix Header Comment Sign Errors
Correct inverted signs in VCCS and CCCS header file MNA stamp documentation.

---

## Testing Strategy

**Framework**: Existing `NgspiceRunner` + `Comparator` in `tests/framework/`

**Per-fix tests**:
- Each fix gets at least one dedicated `.cir` test circuit in `tests/circuits/`
- New gtest cases in `tests/unit/test_device_accuracy.cpp` (or per-device files)
- Compare against ngspice with appropriate tolerances

**Regression tests**:
- Run full test suite (`ctest`) after each phase
- Monitor existing tests for regressions (especially migrated device tests)

**Phase gates**:
- Phase N+1 begins only after Phase N passes all tests
- Each phase is a separate commit (or series of commits) on main

---

## Files Modified Summary

| Phase | Files Modified | New Files |
|-------|---------------|-----------|
| 1 | coupled_inductor.cpp/hpp, ac.cpp, switch.cpp/hpp, tline.cpp/hpp, netlist_parser.cpp | ~4 test circuits, 1 test source |
| 2 | capacitor.cpp/hpp, inductor.cpp/hpp, coupled_inductor.cpp/hpp, vsource.hpp, isource.hpp, tline.cpp/hpp, asrc_device.cpp, expression_ast.cpp, transient.cpp, device.hpp (optional ac_stamp_freq) | ~6 test circuits |
| 3 | resistor.cpp/hpp, capacitor.cpp/hpp, inductor.cpp/hpp, vccs.cpp/hpp, cccs.cpp/hpp, isource.cpp/hpp, switch.cpp/hpp, expression_ast.cpp, netlist_parser.cpp, model_cards.hpp | ~5 test circuits |
| 4 | resistor.cpp/hpp, capacitor.cpp/hpp, inductor.cpp/hpp, tline.cpp/hpp, vccs.hpp, cccs.hpp | ~3 test circuits |

## Risks

- **Switch model change**: Replacing smooth step with hard switching may affect convergence in circuits that currently rely on the smooth transition. Monitor test results.
- **LTE addition**: Adding device-level LTE will make transient simulations take smaller timesteps (more accurate but slower). This is correct behavior but may surprise users.
- **Parser complexity**: Extending the parser for model cards and inline parameters is the highest-effort change. Consider doing it incrementally (tc1/tc2 inline first, full model cards later).
- **TL AC rework**: Adding a frequency-dependent `ac_stamp_freq()` virtual changes the Device interface. Ensure backward compatibility (default implementation falls back to G+jωC).
