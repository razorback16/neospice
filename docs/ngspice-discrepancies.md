# neospice vs ngspice Discrepancies

Collected 2026-04-28 on commit `9ea5868` (926/926 tests passing).

Margins measured with `NEOSPICE_DEBUG_COMPARE=ON`. Margin = tolerance / worst_error.
Tests with margin < 5x are listed — these represent real accuracy gaps, not just loose tolerances.

## How to reproduce

```bash
cmake .. -DNEOSPICE_DEBUG_COMPARE=ON
cmake --build . -j$(nproc)

# Per-test margins:
for exe in ./tests/neospice_tests ./tests/devices/*/test_*; do
  [ -x "$exe" ] || continue
  while IFS= read -r tc; do
    out=$("$exe" --gtest_filter="$tc" 2>&1)
    echo "$out" | grep '^MARGIN_' | sed "s/^/$tc|/"
  done < <("$exe" --gtest_list_tests 2>/dev/null | awk '/^[^ ]/{s=$1;next}{print s $1}')
done | sort -t'|' -k6 -g
```

---

## Tier 1 — Margin < 1.5x (actively at tolerance boundary)

| Test | Type | Worst Signal | Error | Tol | Margin | Root Cause Hypothesis |
|------|------|-------------|-------|-----|--------|----------------------|
| CMOSInverterTransientWithResistance | EDGE | rise_time edge[3] | 2.97e-2 | 1e-3 | 0.0x | Output interpolation at sharp switching edges |
| CMOSInverterTransient | EDGE | rise_time edge[3] | 7.79e-3 | 1e-3 | 0.1x | Same as above |
| BSIM3 NMOS DC | DC | i(v2) | 1e-3 | 1e-3 | 1.0x | Gate current at noise floor; gmin/solver difference |
| BSIM3 PMOS DC | DC | i(v2) | 1e-3 | 1e-3 | 1.0x | Same |
| BSIM3v32 NMOS DC | DC | i(v2) | 1e-3 | 1e-3 | 1.0x | Same |
| BSIM4v7 DC Audit | DC | i(vgs) | 1e-3 | 1e-3 | 1.0x | Same |
| NMOS DC IV | DC | i(v2) | 1e-3 | 1e-3 | 1.0x | Same |
| NMOS DC RBODYMOD | DC | i(v2) | 1e-3 | 1e-3 | 1.0x | Same |
| NMOS DC RDSMOD | DC | i(v2) | 1e-3 | 1e-3 | 1.0x | Same |
| DiodeRectifierTransient | TRAN | i(v1) | 1.41e-1 | 1.5e-1 | 1.1x | Transient integrator / LTE difference |
| VBIC SwitchingTransient | TRAN | v(col) | 2.47e-1 | 2.7e-1 | 1.1x | Transient integrator / charge storage model |
| HFET2 TransientPulse | TRAN | i(vgs) | 2.44e-2 | 3e-2 | 1.2x | Transient integrator |
| Diode TransientSwitching | TRAN | v(out) | 7.99e-2 | 1e-1 | 1.3x | Transient integrator / reverse recovery |
| JFET2 SwitchingTransient | TRAN | i(vg) | 7.88e-2 | 1e-1 | 1.3x | Transient integrator |
| RCLowpassTransient | TRAN | v(in) | 2.26e-5 | 3e-5 | 1.3x | Baseline integrator accuracy |
| RLCSeriesTransient | TRAN | v(n2) | 3.81e-3 | 5e-3 | 1.3x | Same |
| RLCUnderdampedTransient | TRAN | v(n2) | 7.28e-6 | 1e-5 | 1.4x | Same |

## Tier 2 — Margin 1.5x–3x (significant discrepancies)

| Test | Type | Worst Signal | Error | Tol | Margin | Root Cause Hypothesis |
|------|------|-------------|-------|-----|--------|----------------------|
| ExpSourceTransient | TRAN | v(in) | 1.23e-1 | 2e-1 | 1.6x | EXP source eval or integrator |
| JFET2 DC | DC | i(vg) | 5e-4 | 1e-3 | 2.0x | JFET2 gate current model difference |
| BSIM3 CMOS Inverter Tran | TRAN | v(out) | 9.03e-2 | 2e-1 | 2.2x | Transient integrator on CMOS switching |
| TlineIC | TRAN | v(tl_out) | 2.32e-2 | 5e-2 | 2.2x | Transmission line initial condition handling |
| LTRA TransientRC | TRAN | i(v1) | 8.61e-1 | 2.0 | 2.3x | LTRA convolution algorithm |
| BSIM4v7 RGATEMOD DC | DC | i(v2) | 2e-3 | 5e-3 | 2.5x | Gate resistance network gate current |

## Tier 3 — Margin 3x–5x (moderate)

| Test | Type | Worst Signal | Error | Tol | Margin | Root Cause Hypothesis |
|------|------|-------------|-------|-----|--------|----------------------|
| DiodeDC | DC | v(out) | 3.14e-5 | 1e-4 | 3.2x | Minor Newton convergence difference |
| MOS1 TransientPulse | TRAN | v(drain) | 6.15e-2 | 2e-1 | 3.3x | Transient integrator |
| AmSourceTransient | TRAN | v(out) | 3.07e-5 | 1e-4 | 3.3x | AM source eval timing |
| MOS3 TransientPulse | TRAN | v(drain) | 1.32e-2 | 5e-2 | 3.8x | Transient integrator |
| ResistorDividerDC | DC | i(v1) | 2.5e-9 | 1e-8 | 4.0x | Numerical noise floor |
| HFET1 AC | AC | i(vgs) | 2.44e-4 | 1e-3 | 4.1x | HFET1 small-signal model |
| SffmSourceTransient | TRAN | i(v1) | 4.39e-4 | 2e-3 | 4.6x | SFFM source eval |

---

## Root Cause Categories

### 1. Transient integrator / LTE (18 tests, all tiers)

The largest cluster. Even simple RC/RLC passives show 0.07–0.4% error (margin 1.3–1.4x),
which sets the accuracy floor. Device transients (VBIC, diode, JFET2, HFET2, BSIM3, MOS1,
MOS3) amplify this to 1–25%.

The dt_max fix (`min(tstep, tstop/50)`) brought this down from much worse, but the remaining
gap points to differences in:
- LTE (local truncation error) estimation formula
- Predictor polynomial (ngspice uses Gear/Trapezoidal with specific coefficient tables)
- Timestep rejection / backoff logic
- Breakpoint handling at source transitions

**Investigation**: Compare `src/core/transient.cpp` LTE computation against ngspice's
`tran/lterhs.c` and `tran/niditer.c` line by line.

### 2. Gate current at DC noise floor (7 tests, tier 1)

BSIM3, BSIM3v32, and BSIM4v7 all produce i(v2) or i(vgs) errors of exactly 1e-3 at the
DC operating point. The gate current is physically ~0 (MOSFET gate is insulating), so
the "error" is `|neospice_noise - ngspice_noise| / max(|ngspice_noise|, abstol)`.

This isn't a model bug — it's solver floor noise being amplified by the relative_error
formula. Options:
- Increase abstol for these specific tests (masks it)
- Investigate whether neospice's gmin differs from ngspice's
- Check if ngspice strips gate current from output (it may report exactly 0)

**Investigation**: Run both simulators in verbose mode, compare gmin values and gate
current contributions.

### 3. LTRA convolution (1 test, tier 2)

`LTRAValidation.TransientRC` has 86% relative error on i(v1). The LTRA device uses a
convolution-based algorithm with history truncation and delayed-value interpolation.
This is likely an implementation difference in how the convolution integrals are computed,
not a timestepping issue.

**Investigation**: Compare `src/devices/ltra/` against ngspice's `src/spicelib/devices/ltra/`
convolution code, focusing on h2/h3 coefficient computation and history truncation.

### 4. Edge timing extraction (2 tests, tier 1)

The CMOS inverter edge comparator shows 0.8–3% rise time disagreement on edge[3].
This may be an artifact of the comparator's interpolation rather than a real simulator
difference. The sample-wise error on the same circuit (via `compare_transient`) is
much smaller.

**Investigation**: Check whether `extract_edges()` in `comparator.cpp` handles the
4th edge correctly when the waveform is near a rail.

### 5. Device-specific small-signal (HFET1, JFET2)

HFET1 AC i(vgs) at 4.1x margin and JFET2 DC i(vg) at 2.0x margin suggest minor
differences in the small-signal model linearization or parameter handling for these
less-common devices.

**Investigation**: Compare AC stamp routines against ngspice source.
