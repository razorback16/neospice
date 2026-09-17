# Validation methods and current limitations

This describes the current comparison methods for the ngspice47 compatibility target.
It does not certify compatibility. See [JOSS progress](joss-progress.md) for
the current failures and [the audit](paper-readiness-audit.md) for historical
measurements.

## Reference and required data

ngspice47 is the reference argument and defines the required signals. Additional
neospice signals do not excuse a missing reference signal. Tests explicitly
exclude some named simulator-private equations where implementations expose
different internal variables; public port voltages and source currents remain
required. A final support/evaluation manifest must enumerate those exclusions.

C++ comparisons reject nonfinite values, unsuccessful status, empty required
signals, malformed vector lengths, and invalid axes. Transient axes must be
strictly increasing and neospice must cover the reference time interval.
AC/noise frequency grids must match point for point, within 64 floating-point
epsilons scaled by the coordinate magnitude. This allowance addresses coordinate
roundoff; it is independent of waveform error tolerances. Transient interpolation
now returns a stored value only at its exact coordinate. Distinct coordinates
are interpolated, including separations below 1e-18 s: the former fixed snap
failed an analytic straight-line, time-scaling regression. Coverage endpoint
allowances are unchanged and independent of the waveform error threshold.
Checkpoint 21 retains both the failing pre-fix probe and the corrected results.

The shared-library wrapper checks API return codes and retains emitted
diagnostics. It additionally rejects explicit errors and terminal analysis
failures even if the command API returns zero. One narrowly
identified exception is the LTRA default-capacitance notice: `ltraset.c` labels
it fatal but deliberately continues with C=0, which is valid for an RG line.
Nonzero status, a subsequent abort, or library-detachment requests still fail.
All 109 formerly skipped required reference failures now fail their tests.
Comprehensive runner vector validation and analysis-completion verification
remain unfinished; diagnostic matching alone is not a completeness proof.

ngspice47 preserves post-solve device-convergence failure flags. Successful
status must not replace required finite-data, completeness and accuracy checks.
The [RFF investigation](rff70n06-investigation.md) retains its nonconvergence.

## Error formulas

For C++ DC comparisons, the normalized error is

`abs(reference - actual) / max(abs(reference), absolute_floor)`.

Passing requires this error to be at most `relative_tolerance` at every compared
value. Despite the `Tolerance.absolute` member name, it is a denominator floor,
not a separate additive absolute allowance. The effective allowance near zero
is `relative_tolerance * absolute_floor`. Tolerances have not been increased.

Transient comparisons use the same formula at the union of both time grids,
restricted to the reference interval. Exact shared coordinates count once;
every sampled excursion within that interval is checked. Each side supplies its
stored value at its own timestamps and an interpolated value elsewhere.
Interpolation considers all contiguous four-point
stencils containing the query bracket and chooses the smallest absolute third
divided difference. With fewer samples, it lowers the polynomial degree. Newton
polynomial evaluation uses locally scaled coordinates and long-double
intermediates. Flat bracketing/neighboring segments retain linear interpolation.
Synthetic tests cover linear/quadratic/cubic functions on nonuniform grids at
three time scales, a continuous function with a curvature corner, and a sine
minimum. Each includes deliberately perturbed values that must still fail.
The original coarse ramp, curvature-corner and five-point sine fixtures are
retained as negative controls: they cannot reconstruct both grids at the stated
limits. Positive controls add samples from the same analytical functions while
retaining the original sample locations and unchanged tolerances. A separate
piecewise-linear ramp regression covers its flat endpoint.

The former centered cubic crossed curvature corners, and its monotonicity and
bracketing-range guards replaced smooth extrema with inaccurate linear chords.
This correction changes the interpolation estimate, not the acceptance limits.
It is not an error bound on undersampled data, and the flat-segment fallback
still cannot distinguish an unresolved extremum from a plateau.
The normalized denominator now
always uses the reference value. Previously it used the sparser-grid value,
which could be neospice's value. For a reference of 1, results of 1.01 and 0.99
now both have error 0.01; the old calculation gave approximately 0.009901 and
0.010101 when neospice had the sparser grid. Regressions demonstrate both effects.
This correction can change decisions in either direction; old percentages cannot
be reused. Interpolation around switching discontinuities still needs analysis
against the remaining failures; global point count does not establish local
sampling density or bound interpolation error. The former false-pass probe,
reference `[0,1] -> [0,0]` versus actual `[0,0.5,1] -> [0,1,0]`, now fails after
comparing three points. Voltage/current regressions check both reference/actual
directions. At checkpoint 5 the additional samples exposed TlineIC, LTRA RC and
MOS3 transient failures. Checkpoint 6 fixes line initialization and breakpoint
scheduling; TlineIC now passes with the unchanged comparator and tolerance.
The corrected transient driver passes LTRA RC and MOS3 against ngspice47
with unchanged comparisons. See
[the current transient investigation](transient-readiness.md) and the versioned
checkpoint records for the exact candidate and margins.

Transient `CompareResult.signals` retains each required signal's point count,
maximum absolute and normalized errors, time of maximum normalized error, and
both values at that time. Passing signals are included. With debug comparison
enabled, `DETAIL_TRAN|signal|points|max_absolute|max_normalized|time|reference|actual`
lines preserve these records in CTest/JUnit output. These estimates do not yet
bound interpolation uncertainty or prove agreement between all sample times.

## DC-sweep data and status

`validate_dc_sweep_data()` runs before the eight existing reference sweep tests'
numerical checks. It rejects unsuccessful/empty results, nonfinite coordinates,
malformed/nonfinite signal vectors, unequal coordinate counts, misaligned
coordinates and missing reference signals. Coordinates may be negative,
descending or repeat for a nested sweep. Alignment permits only floating-point
roundoff, bounded by `64 * epsilon * max(abs(all_coordinates))`; it does not use
the waveform tolerance. The reference reader rejects invalid real vectors and
recognizes both voltage-source and current-source sweep axes.

This helper validates data integrity, not numerical agreement or completion of
the requested grid. Existing value checks remain unchanged. Some diode/MOS/
BSIM/HiSIM tests use adaptive absolute allowances and permit up to 5% mismatching
points; HFET2, VDMOS and VBIC use worst-error checks. Their acceptance rules and
selected observables must be reported explicitly, not described as universal
pointwise certification. Complete request-aware validation and per-signal
DC-sweep error records remain work items.

The engine now rejects nonfinite/zero/non-advancing steps, reversed ranges,
duplicate sources and more than two sources. With `no_throw`, it stops on the
first failed solve and returns `converged=false` with only successful points;
the former implementation appended failed solutions and reported success.
Source values restore on both normal return and exceptions. A quadratic KCL
regression has a real solution at the first point and none at the second,
verifying the partial-result boundary. Nested sweeps now vary the first source
fastest, matching the reference test; the former implementation reversed the
loop order. A descending current-source fixture also matches ngspice and Ohm's law.

AC comparisons now use the full complex difference:

`abs(reference_complex - actual_complex) / max(abs(reference_complex), absolute_floor)`.

The previous magnitude-only calculation could accept a reversed phase. Equal
unit magnitudes differing by 90 degrees now produce error sqrt(2); opposite
phases produce error 2. Voltage and current responses are both checked.

Noise comparison converts neospice's power spectral density to amplitude
spectral density by taking its square root, then uses the reference-normalized
formula. Negative densities are rejected, as are mismatched grids and lengths.

The noise analysis itself uses ngspice's squared-gain lower bound `1e-20` when
referring output noise to input, including at zero gain. This is separate from
the comparator's denominator floor. `Noise.InputReferredGainFloorAnalytical`
and `Noise.InputReferredGainFloorMatchesNgspice` cover zero, below-boundary,
boundary and above-boundary gains with explicit controlled-source/divider
fixtures; both tests failed before the correction. See
[compatibility details](source-compatibility.md#input-referred-noise-at-very-small-or-zero-gain).

DC, sweep, AC and noise comparisons also retain passing and failing per-signal records in
`CompareResult.signals`. For AC and noise, `coordinate_unit` is `Hz`; the worst coordinate and
associated values refer to the maximum normalized error, which need not occur
at the maximum absolute error. AC records retain real and imaginary components,
and their absolute error is the magnitude of the complex difference. Noise
records use amplitude spectral density after the square-root conversion.
For DC, the coordinate unit is `OP` and zero is an axis placeholder. Sweeps use
the reference source coordinate at the maximum normalized error (`V` or `A`),
and retain the point count and maximum absolute error independently. The sweep
comparator validates data/axes and applies the same DC formula to every point;
benchmark request-completion checks remain in addition to that comparison.
Debug output preserves these fields as:

```text
DETAIL_AC|signal|points|max_absolute|max_normalized|frequency|ref_real|ref_imag|actual_real|actual_imag
DETAIL_NOISE|signal|points|max_absolute|max_normalized|frequency|reference|actual
```

Invalid data can fail before records are produced. Multiple calls in a test
produce successive groups of records, not independent population samples.
Regression tests check both failing records and exact passing controls; adding
these records does not change the comparison formula or acceptance thresholds.

`MesValidation.NoiseSpectraAcrossPolarityGeometryAndTemperature` runs nine
fixtures in the order thermal, flicker, area, multiplier, hot, scaled_hot,
area_last, pmf and state_offset. Each checks 28 frequencies from 1 Hz to 1 GHz,
with relative tolerance 1e-8 and amplitude-density floor 1e-20. The comparator
checks both output and input spectra; additional assertions require finite,
positive device contributions and exact agreement of the `area`/`m` queries.
The fixtures include nonzero RD/RS, AF=1.3, 27/85 degrees Celsius, both polarities,
both orders of named AREA/M assignments, and a diode/second MES ahead of the
measured device in the state storage. They do not certify integrated noise,
every bias/parameter combination or all geometry spellings. Checkpoint 19 adds
native DC/AC/transient and intrinsic-noise checks of a multiplied instance against
three explicit parallel instances, plus a registry-builder geometry regression.

The candidate follows ngspice 47: named `AREA` and `M` set independent parameters.
Its core load, AC and noise equations already applied the parallel factor;
checkpoint 19 corrects the parser and registry routing. All nine unchanged noise
fixtures pass against 47, at the original tolerance. A direct CLI probe confirms
that 47's public `area` and redundant `m` queries both return effective area.

`LTRAValidation.ACFrequencyResponseAllLineTypes` runs fixtures in the order LC,
RC, RLC, RG, floating LC ports and RG with `gmin=1e-4`. Each has 29 frequency
points from 1 kHz to 10 GHz. Full complex public voltages and source currents
use relative tolerance 1e-8 and absolute floor 1e-9; only the two explicitly
named private LTRA branch-equation vectors are excluded. Both LC fixtures
also check the matched-line response `0.5*exp(-j*2*pi*f*5 ns)` within 1e-10 V.
These are finite fixture checks, not certification of every line parameter.

`Noise.LosslessLinePropagatesExternalResistorNoise` runs the matched T line,
then matched LC LTRA fixture, using relative tolerance 1e-8 and amplitude-density
floor 1e-15. Independent analytical assertions check output PSD `4*k*T*25`
and input PSD four times larger. Before the frequency-dependent matrix repair,
the T-line fixture returned half the expected output noise and zero input noise.
The test covers external resistor noise propagation through lossless lines;
intrinsic noise of lossy lines and dedicated BSIM4 NQS noise checks remain outside
this evidence. The noise solver now invokes the same per-frequency device
matrix updates as AC before constructing both the gain and transpose systems.

The Python corpus harness uses a different, existing acceptance rule:
`abs(neo-ng) <= RELTOL*max(abs(neo),abs(ng)) + absolute_tolerance`, with RELTOL=1e-3,
voltage tolerance 1e-6 V and current tolerance 1e-9 A. Required variables come
from ngspice after the explicit internal-node filter. Empty, disjoint, missing,
nonfinite and overflow comparisons fail. Successful per-variable evidence is
now retained alongside failures. The OP path requires a real operating-point
plot and no longer substitutes the first transient or AC point.

Checkpoint 29 corrects the formula in this documentation; the Python comparator
and its thresholds have not changed. Earlier versions of this paragraph omitted
the maximum over both magnitudes. The dedicated RFF70N06 test uses the stricter
reference-magnitude allowance and remains unchanged.

## Specialized waveform checks

`validate_transient_data(reference, actual)` exposes the shared status,
finite-vector, axis and reference-coverage checks without assigning a numerical
tolerance or selecting required signals. Specialized callers must supply those
requirements themselves. TLV3201 requires four named port voltages and the full
0–30 us request, then applies its original strict crossing and DC-port policy.
Its qualification report uses fractions of the respective allowances; a separate
full pointwise comparison retains failures against both reference versions.
See [TLV3201 validation](tlv3201-validation.md). A specialized pass does not
establish pointwise waveform agreement.

The oscillator comparator measures voltage period, peak-to-peak amplitude,
midpoint, and DC level rather than phase-aligned samples. It requires a positive
minimum number of periods and rejects invalid tolerances or nonfinite derived
statistics. Its existing late window is selected by sample index, and its DC
mean is sample-weighted; those choices still need review for unequal adaptive
grids. It does not certify branch-current waveforms. Its single worst-error
field mixes relative and absolute metrics, so it is insufficient by itself for
paper evidence.

Edge comparisons check 50% crossing time, signed 10–90% transition duration,
settled value and overshoot. The settling window starts two absolute transition
durations after the midpoint crossing. Falling edges previously used a different
start based on the configured window; this is corrected. Missing threshold
crossings, incomplete or empty settling windows, and nonfinite metrics now fail
instead of receiving default zero values. A fall lasting one second is a valid
negative duration, not the old `-1` missing-data sentinel. Edge directions and
time ordering must agree. The reported count is edges, not individual metrics.

## Solver/output separation

The candidate follows ngspice 47's AC/noise frequency rules. AC DEC distributes
`floor(log10(stop/start)*points_per_decade)` intervals across ranges of at least
one decade; narrower ranges use a fixed exponential ratio, as noise DEC does.
Both advance frequencies incrementally. Equal start/stop and narrow AC ranges
return one point. AC LIN with one
or two requested points produces one point; noise LIN with two produces two.
Regression tests compare actual ngspice47 vectors.
See [source/frequency compatibility](source-compatibility.md).

Transient `.option interp` now changes output only: linear interpolation between
accepted steps, an index-derived output grid, and the final accepted point even
when the stop is not a grid multiple. It no longer inserts solver breakpoints or
implicitly enables global voltage LTE. The selected integration method remains
in force; automatic Trap-to-Gear switching has been removed. Comparisons use the
same netlist options in both simulators; 20 tests previously forced interpolation
only in neospice. Dedicated tests verify that interpolation leaves iteration and
rejection counts unchanged and that sampled values match the raw trajectory.

The initial transient timestep now applies ngspice's breakpoint reduction even
when no source event precedes the stop time: time zero is itself a breakpoint.
For the constant-source regression with `.tran 0.01 0.5`, the first nonzero
time is 0.00005 s. No reference options or tolerances were changed.

Order promotion no longer immediately undoes the order reduction at a source
breakpoint. The first subsequent step uses backward Euler, as in ngspice. A
1 F capacitor driven by a piecewise-linear voltage now has the analytical
piecewise-constant source current, instead of an approximately 1 A alternating
error after slope changes. The new regression failed before this correction.
The RLC, JFET2 and VBIC regression trajectories now pass against47 at their
original tolerances. This does not certify all switching models.

Behavioral `DDT()` follows the tested ngspice47 `PTddt` history semantics and zero
parse-tree Jacobian. It samples the first evaluation at each increasing
transient time, returns zero at the first two such times, and subsequently
divides the argument difference by the preceding time interval. Same-time
Newton iterations and backward timestep retries retain the last derivative.
Outside transient analysis the result is zero. History belongs to the AST node
and restarts at time zero; rerunning a circuit resets source time before its DC
preamble. This replaces the former accepted-step backward difference and
`df/dx / dt` Jacobian. The existing ramp comparison now matches ngspice at all
59 timestamps, including startup, at the unchanged tolerance. Unit tests cover
nonuniform intervals, repeated evaluations, retries, separate nodes and reset;
the reference comparison also runs the same circuit twice.

The reference wrapper initializes libngspice once per process and retains
callback state for the process lifetime. It destroys plots and removes loaded
circuits during reset/destruction. It rejects unreadable source files before
calling ngspice, after removing any previous circuit. These changes removed
the repeated-initialization and missing-file leaks observed at checkpoint 2.
A reference T-line leak reproduced during ngspice47 noise validation remains
recorded in checkpoint24 evidence. Passing focused diode sanitizer tests do
not certify that separate path.

## Sensitivity and unsupported-analysis checks

Sensitivity currently computes forward differences for effective resistor
resistance and independent-source DC values only. The perturbation remains
`max(abs(parameter) * 1e-4, 1e-10)`; normalization remains
`sensitivity * parameter / baseline_output`, with the existing zero convention
when `abs(baseline_output) <= 1e-30`. This is not AC sensitivity, an adjoint
gradient, or sensitivity to semiconductor model parameters.

Baseline and perturbed DC statuses are now checked before extracting outputs.
Nonfinite outputs/derivatives and invalid perturbations fail. The first failed
perturbation stops the analysis. With `no_throw`, the result retains only
previously completed entries and reports failure; it is not a complete gradient.
Exceptions and early returns restore the perturbed parameter. All exit paths
clear the operating-point cache so a later AC analysis cannot reuse a solution
for the perturbed circuit. Regressions cover failed baselines, resistor/voltage/
current-source folds, both error modes, partial entries and stale cached bias.

DC caches now contain only converged operating points. A separate regression
previously obtained an apparently successful AC result from a failed cached
DC solve of `v(out)^2 + v(out) + 1 = 0`; it now rejects the invalid bias.
DC and DC-sweep exits also restore temporary diagonal conductance to `gshunt`,
including exception paths. Previously, the Python sensitivity restoration test
found a residual 1e-12 S term that changed a subsequent valid solve. Its original
voltage assertions remain unchanged.

The former `Sens.NgspiceComparison` executed ngspice but ignored both its status
and its output. It now uses the checked shared-library wrapper, reads the
sensitivity plot and requires finite scalar values for `r1`, `r2` and `v1`.
All three are compared with an absolute derivative allowance of 1e-6; the
pre-existing analytical checks remain. ngspice's additional geometry/model
sensitivities are explicitly outside this entry point's scope. Records use
`DETAIL_SENS|parameter|reference|actual|absolute_error|allowance`. This establishes
the divider fixture's agreement, not universal sensitivity accuracy.

VDMOS AC/noise fixtures now demonstrate successful, nontrivial reference runs
and explicit neospice rejection of unimplemented device contributions. These
are unsupported-operation tests, not numerical-agreement tests. Rejection is
required in both error modes, including through Python. The direct VDMOS noise
provider also throws instead of returning an empty source list. Keep these
fixtures in experiment totals and exclude them from accuracy-qualified timings.
Existing VDMOS DC/IV comparisons retain their original limits.

## Remaining evidence work

Complete request-aware status/coverage checks, DC-sweep numerical reporting,
local interpolation diagnostics, per-signal records for other analyses, and benchmark
validation still need completion. Frozen fixture identities, primary/rescue
accounting, provenance, held-out cases, multi-analysis results and final corpus
tables remain goal requirements. The current formulas and checkpoint results
must not be presented as completed JOSS validation.
