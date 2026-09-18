## neospice Capabilities

### Declared scope

This section is the claim the paper makes; everything below it is an
implementation overview. The claim is defined by the generated
[support matrix](support-matrix.md), cell by cell, and nothing else:

- **Claimed.** Cells marked `V` or `V*`: a test compares that device and
  analysis against ngspice 47 and asserts the result. `V*` additionally means
  the comparison runs at a tolerance looser than 1e-3 relative, and the matrix
  gives the value -- these are claimed at their stated tolerance, not at the
  corpus tolerance.
- **Documented as unsupported.** Cells marked `X`: the combination fails
  explicitly, by assertion. VDMOS AC and noise, and VDMOS self-heating as a
  model form.
- **Outside the claim.** Cells marked `~` (run, but never compared against the
  reference) and `-` (never exercised). These are not asserted to work and not
  asserted to fail. A `-` cell may agree, may fail loudly, or may return a
  plausible wrong answer; nothing establishes which.
- **Outside verified scope entirely.** `.tf`, `.sens`, `.pz` and `.four`. The
  test harness cannot obtain a reference result for these analyses, so no
  output from any of them has ever been compared against ngspice 47. They are
  implemented and tested against analytic expectations only.

Two further limits belong to the claim rather than to a footnote. Twelve device
types (R, C, L, V, I, E, G, F, H, S, W and the lossless T line) have no row in
the matrix, because their only coverage is inside multi-device circuit tests
that support no per-device claim. And the matrix covers device/analysis pairs,
not the full parameter space of any model: a claimed cell does not certify
every option, geometry or temperature path of that device.


neospice is a C++ circuit simulator with C++ and Python APIs. The following is
an implementation overview, not certification of every device/analysis
combination. The [JOSS progress tracker](joss-progress.md) records current
verification and unresolved defects.

### Analysis entry points
- **DC Operating Point** (.op) and **DC Sweep** (.dc) — one or two distinct voltage/current sources; the first source varies fastest in a nested sweep, matching ngspice. Invalid ranges are rejected. Failed sweeps throw, or return `status.converged=false` with only successful points when `no_throw` is enabled.
- **Transient** (.tran) — adaptive timestepping with Trap/Gear-2/BE integration
- **AC Small-Signal** (.ac) — DEC/OCT/LIN frequency sweeps with G/C matrix caching; NQS-compatible via per-frequency device hooks
- **Noise** (.noise) — adjoint-method output/input-referred spectral density with correlated source support
- **Transfer Function** (.tf) — gain + input/output impedance
- **Sensitivity** (.sens) — finite-difference DC sensitivity to effective resistor resistance and independent voltage/current-source DC values. Semiconductor model parameters and AC sensitivity are not implemented by this entry point. Failed baselines or perturbations stop the analysis; `no_throw` returns failure status and only completed entries. Parameters restore on success or failure, and the perturbed operating-point cache is cleared.
- **Pole-Zero** (.pz) — transfer function poles and zeros
- **Fourier** (.four) — harmonic decomposition + THD
- **Parameter Sweep** (.step) — nested sweeps of any parameter or temperature

### Device models
| Category          | Devices                                                                                                                             |
| ----------------- | ----------------------------------------------------------------------------------------------------------------------------------- |
| Passives          | R (with TC, RAC, noise, flicker), C, L, K (mutual)                                                                                  |
| Sources           | V, I (DC/PULSE/SIN/PWL/EXP/SFFM/AM)                                                                                                 |
| Dependent         | E (VCVS), G (VCCS), F (CCCS), H (CCVS) — linear + POLY(N) + TABLE                                                                   |
| Behavioral        | B (ASRC) — expression-based with auto-differentiation for exact Jacobians, DDT, IDT, PWL, TABLE                                     |
| Switches          | S (voltage), W (current) — hysteresis                                                                                               |
| Transmission Line | T (lossless Branin model), O (LTRA lossy -- RC/RG/LC/RLC)                                                                          |
| Diode/BJT         | Diode, BJT (Gummel-Poon), **VBIC** (ngspice levels 4/9; see [option scope and known gaps](vbic-compatibility.md)) |
| JFET/MESFET/HFET  | JFET, **JFET2** (Parker-Skellern), **MES** (GaAs MESFET -- NMF/PMF), **HFET1** (Curtice cubic), **HFET2** (Chalmers)               |
| MOSFET            | MOS1 (level 1), **MOS3** (level 3), **MOS9** (level 9), **BSIM3v32** (level 49/v3.24), BSIM3 (level 49/v3.3), **BSIM4v7** (level 14, full UCB port: AC NQS, full noise with correlated gate noise), **BSIMSOI** (level 10/58, 6-terminal SOI), **HiSIM2** (level 61/68), **HiSIM_HV** (level 73, 5-terminal with self-heating) |

The lossless T line uses independent delayed-wave port equations with branch
currents. Without `UIC`, its initial history comes from the DC operating point;
under `UIC`, it uses the line's `IC=` values (zero for unspecified values).
Delayed-wave slope changes determine breakpoints and timestep limits. The
transient driver also inserts ngspice's UIC startup breakpoint at the requested
output interval. Existing tests cover DC, AC, delayed pulses/reflections and
explicit initial conditions; this does not establish arbitrary-line-network
coverage.

### Convergence & Numerical Features
- **Operating-point recovery**: DC, AC and noise share the operating-point solver, including dynamic/true gmin, source stepping and transient continuation. A successful fallback is not independent proof of a physical equilibrium.
- **Explicit integration method selection**: the transient driver retains the requested trapezoidal or Gear method, using backward Euler during startup and after breakpoints. It does not automatically switch to Gear when ringing appears.
- **Breakpoint classification** (HARD vs SOFT) with adaptive step recovery
- **Configurable LTE reference modes** (per-node, max-all, max-per-signal)
- **Device-level LTE** on charge/flux state variables

### Netlist Features
`.param` expressions, `.subckt`/`.ends`, `.include`/`.lib`, `.global`, `.ic`, `.nodeset`, `.options`, `.func`, `.measure`, `.save`, `.step`, SPICE suffixes (k/m/u/n/p/f/T)

### Implementation features
- **Expression derivatives** in B-source expressions; stateful functions such as DDT have separate history and derivative rules. This is not a claim that differentiation or behavioral sources originate in neospice.
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

Results are returned as structured data (maps keyed by signal name like `"v(out)"`, `"i(v1)"`).

### Validation

See [validation methods](validation-methods.md) for the actual error formulas,
required signals, interpolation, thresholds and limitations, and
[JOSS progress](joss-progress.md) for versioned test results. Passing a small
operating-point fixture does not certify a model's transient, AC or noise behavior.

VDMOS AC and noise are **explicitly unsupported**: their adapter raises
`SimulationError` naming the device and missing capability. The error also
applies with `no_throw`; that option handles numerical nonconvergence, not an
unimplemented analysis. DC operating-point and IV-sweep tests remain enabled.
Unsupported AC/noise fixtures must remain in experiment accounting and must not
be counted as accuracy matches or accepted performance samples.

PSpice **digital primitives are explicitly unsupported**. ngspice 47 simulates
them under `ngbehavior=psa`, translating `U` cards to its own digital primitives
and inserting automatic interface bridges; neospice has no digital simulation
engine. A `U` card naming a documented PSpice primitive type is rejected at
parse time with an error naming the instance and the primitive. Previously such
a card was dropped silently, so the deck solved as an analog circuit with its
digital devices missing -- see
[corpus mismatch triage](corpus-mismatch-triage.md) for the 441 corpus cases
that behaved this way. The rejection keys on the primitive keyword rather than
the leading letter, because vendor libraries carry uncommented prose.

VDMOS self-heating is **explicitly unsupported** on the same terms. ngspice 47
activates its thermal network only when an instance carries the `thermal` flag
and the model gives `Rthjc`; that combination is rejected at parse time with an
error naming `docs/vdmos-compatibility.md`. Every isothermal form still runs,
including a five-terminal instance supplying Tj and Tcase, because ngspice
grounds those nodes too.

Candidate 36's Release suite passes 1,261 of 1,262 tests against ngspice
47, including the JFET2, MOS3 and VBIC transient fixtures. ngspice47 is the sole
reference. The remaining supported-scope corpus discrepancies and RFF70N06
regression remain release blockers. See
[transient investigation](transient-readiness.md) for the driver corrections and
reference sampling differences. A timestep that cannot advance time now reports
failure; an early partial trace must not be treated as a completed analysis.
The required [RFF70N06 operating-point regression](rff70n06-investigation.md)
also remains nonconvergent. ngspice47 fails this operating point too. The
required regression remains failing, and neospice's device checks remain enabled.
Diode capacitance preserves the grading coefficient and its temperature
adjustment as ngspice 47 does, including values above 0.9. Instance `TEMP` and
`DTEMP` affect diode evaluation and series-resistance noise; explicit `TEMP=0`
is honored. Analytical, AC, transient and noise fixtures cover these corrections.
This does not certify every diode parameter: exact `M=1`, geometry through the
registry builder, and the wider option matrix still need review. See the
[diode and CoolMOS investigation](diode-coolmos-investigation.md).
MES noise includes series-resistance, channel thermal and flicker sources.
Nine unchanged noise fixtures now pass against ngspice 47 across NMF/PMF polarity,
named area/multiplier parameters, temperature and prior device state offsets.
The candidate uses independent `AREA` and `M` inputs, matching 47. Public `area`
and redundant `m` queries both report effective area (`AREA*M`) in that reference.
Native regressions compare a multiplied instance with explicit parallel devices
for DC, AC, transient and intrinsic noise; the registry builder also retains both
geometry parameters. These inputs remain independent and their noise comparisons pass47.
The candidate now follows 47 for AM sources and sub-decade AC frequency grids;
see the [migration notes](source-compatibility.md) and
[reference behavior](ngspice47-reference.md).
See [validation methods](validation-methods.md) for the finite tested scope;
this is not certification of every MES parameter or analysis.
LTRA AC now uses complex propagation equations for LC, RC and RLC lines;
RG retains its frequency-independent distributed equations and honors `gmin`.
Reference tests cover these four cases, floating LC ports and a nondefault RG
`gmin`. Noise analysis now applies device frequency-dependent matrix updates
to both gain and adjoint solves. Matched lossless T/LTRA fixtures verify
propagation of external resistor noise; they do not validate intrinsic thermal
noise in lossy lines.

The device-by-analysis support matrix is no longer pending: see
[support matrix](support-matrix.md), which is generated from the test suite by
`tools/support_matrix.py` rather than maintained by hand. Read it alongside this
page: the lists above say what is *implemented*, the matrix says what is
*verified against ngspice 47*, and those are not the same claim. In particular,
no `.tf`, `.sens`, `.pz` or `.four` result has ever been compared against the
reference, because the test harness has no path to obtain one.
The Python `SimulatorOptions.no_throw` property exposes the same
numerical-failure option as C++. Sensitivity entries after a failed perturbation
are partial results: always inspect `result.status.converged` before using them
as a complete gradient.

Model-language support has additional boundaries: see the
[temperature-expression parameter table](model-card-compatibility.md#candidate-36-expression-scope).
A supported device family does not imply every model expression or option is supported.
