# neospice capabilities

Updated October 6, 2026. This guide covers the native engine and links to
workflow-specific API contracts. For installation, see [building](building.md).
For browser features, see [Circuit Lab](circuit-lab.md) and [WebAssembly](webassembly.md).

### Declared scope

The generated [support matrix](support-matrix.md) defines reference coverage:

- `V`: asserted comparisons with ngspice 47.
- `V*`: asserted comparisons at a relative tolerance above 1e-3, specified in the matrix.
- `X`: tested rejection of an unsupported combination.
- `~`: execution without a reference comparison.
- `-`: no test coverage.

Coverage applies to tested device/analysis pairs, not every parameter, geometry,
temperature or topology. Twelve device types have circuit-level tests but no
per-device row: R, C, L, V, I, E, G, F, H, S, W and lossless T.

The matrix has no per-device reference cells for `.tf`, `.sens`, `.pz` or `.four`.
Analytical tests cover those entry points. A separate circuit test compares
adjoint gradients with ngspice 47 `.sens`. It does not cover finite-difference
`run_sens()`. The lists below describe implementation, not reference coverage.

### Analysis entry points

- **DC Operating Point** (.op) and **DC Sweep** (.dc): one or two distinct voltage/current sources. The first source varies fastest. Invalid ranges fail. Failed sweeps throw. With `no_throw`, they return `status.converged=false` and only successful points.
- **Transient** (.tran) — adaptive timestepping with Trap/Gear-2/BE integration
- **AC Small-Signal** (.ac) — DEC/OCT/LIN frequency sweeps with G/C matrix caching. NQS-compatible via per-frequency device hooks
- **Noise** (.noise) — adjoint-method output/input-referred spectral density with correlated source support
- **Transfer Function** (.tf) — gain + input/output impedance
- **Sensitivity** (.sens): finite-difference DC derivatives for effective resistor resistance and independent-source DC values. Semiconductor model parameters and AC sensitivity are unsupported. Failed baselines or perturbations stop analysis. With `no_throw`, results contain failure status and completed entries only. Check `status.converged` before using them. Parameters restore after analysis, and the perturbed operating-point cache clears.
- **Pole-Zero** (.pz) — transfer function poles and zeros
- **Fourier** (.four) — harmonic decomposition + THD
- **Parameter Sweep** (.step) — nested sweeps of any parameter or temperature

### Device models

| Category | Devices |
| --- | --- |
| Passives | R (TC, RAC, thermal/flicker noise), C, L, K (mutual) |
| Sources | V, I (DC/PULSE/SIN/PWL/EXP/SFFM/AM) |
| Dependent | E (VCVS), G (VCCS), F (CCCS), H (CCVS), with linear/POLY(N)/TABLE forms |
| Behavioral | B (ASRC), expression derivatives, DDT, IDT, PWL, TABLE |
| Switches | S (voltage), W (current), with hysteresis |
| Transmission lines | T (lossless Branin), O (LTRA: RC/RG/LC/RLC) |
| Diode/BJT | Diode, Gummel-Poon BJT, VBIC (levels 4/9). See [VBIC scope](vbic-compatibility.md). |
| JFET/MESFET/HFET | JFET, JFET2 (Parker-Skellern), MES (GaAs NMF/PMF), HFET1 (Curtice cubic), HFET2 (Chalmers) |
| MOS classics | MOS1, MOS2, MOS3, MOS9 (levels 1/2/3/9) |
| BSIM bulk | BSIM3v32 (level 49/v3.24), BSIM3 (level 49/v3.3), BSIM4v7 (level 14, AC NQS and correlated gate noise) |
| SOI/HiSIM | BSIMSOI (levels 10/58, six terminals), HiSIM2 (levels 61/68), HiSIM_HV (level 73, five terminals with self-heating) |

The lossless T line uses delayed-wave port equations with branch currents.
Without `UIC`, its history starts from the DC operating point. With `UIC`, it
uses `IC=` values, defaulting to zero. Tests cover DC, AC, delayed pulses,
reflections and explicit initial conditions.

[VDMOS](vdmos-compatibility.md) supports DC operating points and sweeps.
AC, noise and self-heating requests fail explicitly.

### Convergence & Numerical Features

- **Operating-point recovery**: DC, AC and noise share the operating-point solver, including dynamic/true gmin, source stepping and transient continuation. A successful fallback is not independent proof of a physical equilibrium.
- **Explicit integration method selection**: the transient driver retains the requested trapezoidal or Gear method, using backward Euler during startup and after breakpoints. It does not automatically switch to Gear when ringing appears.
- **Breakpoint classification** (HARD vs SOFT) with adaptive step recovery
- **Configurable LTE reference modes** (per-node, max-all, max-per-signal)
- **Device-level LTE** on charge/flux state variables

### Netlist Features

`.param` expressions, `.subckt`/`.ends`, `.include`/`.lib`, `.global`, `.ic`, `.nodeset`, `.options`, `.func`, `.measure`, `.save`, `.step`, SPICE suffixes (k/m/u/n/p/f/T)

### Implementation features

- **Expression derivatives** in B-source expressions. Stateful functions such as DDT have separate history and derivative rules.
- **IDT()** function in B-sources (time integral with initial condition)
- **RAC** parameter on resistors (separate AC resistance)
- **AC G/C pre-caching** with NQS device hooks (matrices built once, per-frequency corrections via `ac_stamp_freq()`)
- **3 LTE reference modes** (inspired by Xyce's NEWLTE)

### C++ API

```cpp
neospice::Simulator sim;
auto ckt = sim.load("circuit.cir");   // or sim.parse(netlist_string)
auto result = sim.run(ckt);            // runs all analyses in the netlist
// Or individually: run_dc(), run_transient(), run_ac(), run_noise(), etc.
```

Results expose named accessors such as `voltage("out")` and `current("v1")`,
plus typed-handle access. Waveform vectors use dense storage and Python NumPy
arrays. See [programmatic circuits](programmatic-hierarchy-api.md).

### Validation

[Validation methods](validation-methods.md) defines error formulas, required
signals, interpolation and thresholds. The [progress tracker](joss-progress.md)
records evidence and unresolved findings. Unsupported fixtures remain in
experiment accounting and cannot count as accuracy matches or performance samples.

VDMOS AC/noise raise `SimulationError`, including with `no_throw`.
`no_throw` handles numerical nonconvergence, not unsupported analyses.
VDMOS self-heating requires an instance `thermal` flag and model `Rthjc` in
ngspice 47. neospice rejects that combination. Isothermal five-terminal forms
remain available. See [VDMOS scope](vdmos-compatibility.md).

The parser rejects documented PSpice digital primitives by their keyword.
neospice has no digital engine. See [corpus triage](corpus-mismatch-triage.md).

### BJT and VBIC initialization

`OFF`, including mixed-case and numeric `off=1`/`off=0`, controls junction
initialization. It does not permanently disable a transistor. Parser and
registry values reach the BJT/VBIC adapters. Explicit ICs take precedence over
OFF during VBIC UIC initialization. See [VBIC scope](vbic-compatibility.md).

### Transient behavior

Transient tests cover JFET2, MOS3, VBIC, RLC, and LTRA fixtures against ngspice 47.
UIC loads prescribed state without a DC solve, retains the flag during time
stepping, and starts output at the first accepted step. Capacitor node `.ic`
values supply omitted instance ICs. Output interpolation changes sampling,
not the integration trajectory. A timestep that cannot advance reports failure.

[Validation methods](validation-methods.md) defines adaptive-grid comparison.
TLV3201 retains a [strict pointwise discrepancy](tlv3201-validation.md).
CoolMOS source-step and transient-OP behavior still require diagnosis.
The [RFF70N06 test](rff70n06-investigation.md) asserts that both engines fail
its fixture. It passes as a classification test, not as a numerical match.

### Diode and other device behavior

- Diode grading coefficients retain values above 0.9. Instance `TEMP`/`DTEMP`
  affect device evaluation and series-resistance noise. Explicit `TEMP=0` applies.
  Exact `M=1`, registry geometry and wider option coverage still need review.
  See [diode and CoolMOS findings](diode-coolmos-investigation.md).
- MES keeps `AREA` and `M` independent. Its noise includes series-resistance,
  channel thermal and flicker sources. Tests cover polarity, geometry,
  temperature and prior device-state offsets.
- AM sources and AC frequency grids follow ngspice 47.
  See [source compatibility](source-compatibility.md).
- LTRA AC uses complex propagation for LC, RC and RLC lines.
  RG uses frequency-independent distributed equations and honors `gmin`.
  Noise applies frequency-dependent updates to gain and adjoint solves.
  Tests cover external resistor noise, not intrinsic lossy-line thermal noise.

[Model-card compatibility](model-card-compatibility.md#model-expressions)
defines expression and option boundaries. A supported family does not imply
support for every model expression.

## Parallel parameter studies

Independent netlist jobs can run in a bounded worker pool through
`Simulator::run_sweep()` / Python `sweep()`. Seeded Monte Carlo supports Gaussian
and uniform top-level parameters, Gaussian correlation, and explicit scalar
statistics. Each deck needs one analysis. Nested `.step` is rejected. See
[parallel studies](parallel-studies.md) for the complete contract and limits.

## Gradients and incremental re-simulation

The [adjoint API](adjoint-gradients.md) returns DC Jacobians for nominal R/C/L
and independent-source DC values, and linear-circuit complex AC derivatives.
Nonlinear AC and semiconductor model-parameter differentiation are unsupported.
The existing `.sens` finite-difference interface remains unchanged.

[Incremental DC and AC](incremental-simulation.md) retain symbolic solver state
within one circuit, with checked value updates and cache counters.
AC recomputes all frequencies. Incremental transient resume is unavailable.

## Browser build

The [WebAssembly module](webassembly.md) exposes DC, AC, transient, supported
gradients and incremental updates through JavaScript/TypeScript. Its Worker
demo runs entirely locally. The initial browser interface excludes external
includes, `.step`, parallel studies and pole-zero. See its documented scope.
