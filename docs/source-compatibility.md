# Source, frequency and geometry compatibility

ngspice47 exclusively defines AM-source parameters, AC/noise frequency grids,
MES geometry and breakpoint scheduling. CI and migration-tool inputs use the
same pinned release. [JOSS progress](joss-progress.md) records the remaining
compatibility failures. The migration notes below concern earlier neospice
inputs; they do not define a second compatibility mode.

## AM voltage and current sources

Both source types accept:

```spice
AM(VO VMO [VMA [FM [FC [TD [PHASEM [PHASEC]]]]]])
```

`VO` is the output offset, `VMO` the modulation offset, and `VMA` the modulation
amplitude. `FM` and `FC` are frequencies in Hz. `TD` is the delay in seconds;
the two phases are in degrees. The first two parameters are required.
Defaults are `VMA=1`, `FM=5/TSTOP`, `FC=500/TSTOP`, and zero delay/phases.
Explicit zero frequencies remain zero. Omitted frequencies are resolved for
each transient duration, including repeated runs using the same source.

Writing `u = t - TD`, the source is zero for `u <= 0`. After the delay:

```text
VO + (VMO + VMA*sin(2*pi*FM*u + PHASEM*pi/180))
     * sin(2*pi*FC*u + PHASEC*pi/180)
```

Values have voltage units for a voltage source and current units for a current
source. See [the paired voltage/current example](../tests/circuits/am_offsets_phases.cir).
Explicit DC-source values continue to govern DC analysis when supplied.

The old five-parameter spelling had a different order and formula. With explicit
nonzero frequencies, convert:

```text
old: AM(SA OC FM FC TD)
new: AM(0 SA*OC SA FM FC TD)
```

For example, old `AM(1 0.5 10k 1meg 0)` becomes
`AM(0 0.5 1 10k 1meg 0)`. Calculate products before inserting them, or use
the netlist's parameter-expression syntax. If an old frequency was omitted or
zero, specify its old effective value (`1/TSTOP`) explicitly to preserve that
waveform; the new defaults differ. Unconverted five-parameter inputs are parsed
according to the new order, without automatic legacy detection.

The internal C++ `AmParams` structure now uses `vo`, `vmo`, `vma`, optional
`fm`/`fc`, `td`, `phasem` and `phasec`. Code using the former `sa`/`oc` fields
or positional aggregates needs the same conversion. This is a candidate API
change, not a claim of backward compatibility.

## AC frequency grids

For a DEC AC sweep covering less than one decade, the candidate uses the fixed
ratio `exp(log(10)/points_per_decade)`. A sweep from 1 to 3 Hz with ten points
per decade returns approximately `1, 1.25893, 1.58489, 1.99526, 2.51189` Hz.
It does not redistribute those points to force a 3 Hz endpoint. Equal start/stop
and very narrow ranges produce one point rather than an undefined grid.

For ranges of at least one decade, ngspice 47 retains the older AC rule:
`floor(log10(stop/start)*points_per_decade)` intervals span the range.
Noise DEC always uses the fixed ratio. Incremental arithmetic and the reference's
endpoint tolerance remain in effect. AC LIN with one or two requested points
still gives one point; noise LIN with two gives two. These behaviors are checked
against reference vectors, not inferred from labels alone.

Source provenance and successful/failed checks are retained in
[checkpoint18](evidence/joss/2026-09-10-validation-18.json). Subsequent full
corpus runs are linked from [progress](joss-progress.md).

## MES geometry

MES netlists now treat `AREA` and `M` independently, as ngspice 47 does.
`AREA` scales each instance and `M` counts parallel copies. Thus
`AREA=2.5 M=3` and `M=3 AREA=2.5` describe the same device; neither assignment
overwrites the other. The DC/transient, AC and noise equations apply the
multiplier. The registry builder also accepts both geometry parameters.

The public `area` and redundant `m` queries both return effective area,
`AREA*M`, matching direct queries to ngspice 47. In the example they return 7.5,
not the raw multiplier 3. This query convention differs from the input meanings.

Earlier neospice checkpoints treated the two named inputs as
aliases. To preserve that behavior explicitly, put the old final assigned value
in `AREA` and set `M=1`. For example, old `AREA=2.5 M=3` becomes `AREA=3 M=1`;
old `M=3 AREA=2.5` becomes `AREA=2.5 M=1`. HFET's separate multiplier semantics
are unchanged.

The unchanged nine noise fixtures pass against 47. Additional regressions compare
a multiplied instance with explicit parallel devices for DC, AC, transient and
intrinsic noise. See [checkpoint 19](evidence/joss/2026-09-10-validation-19.json)
for the tested scope.

## Pulse arithmetic and first-period endpoint

Voltage and current PULSE evaluation now follows ngspice's period subtraction
and ramp arithmetic order. Repetition reduces time by `PER * floor(time/PER)`
only when delayed time is strictly greater than PER. This corrects tiny spurious
voltages/currents from `fmod` at repeated boundaries. At exactly the first PER,
an overlapping pulse (width greater than period) still evaluates in the first
pulse; the next representable time starts the next pulse. An unchanged ngspice47
CLI probe and two pre-fix failing regressions cover voltage and current sources.
No pulse parameters or existing comparison tolerances changed in this correction.

The transient driver now requests the next PULSE corner from each accepted
source state, before choosing its restart step. It uses the reference
shared-library/XSPICE minimum spacing, `10 * delmin`, and resets source request
state for every analysis. The older `get_breakpoints()` helper still returns
nominal corners for callers; the transient driver does not use that static
PULSE list. Non-PULSE corner collection is unchanged.

This resolves the strict long-RC/RLC benchmark comparisons against 47 without
changing their inputs or thresholds. The long RC output matches exactly; the
RLC worst normalized error is approximately 7.48e-12. Both new required regression
cases pass against47. Current suite results are in [progress](joss-progress.md);
RFF70N06 remains unresolved.

This does not certify all PULSE modes. Defaults, optional pulse-count/phase modes
and reusing omitted defaults across analyses still need a final scope audit.
The new reuse test covers explicitly supplied pulse parameters and request-state
reset; it does not claim that omitted defaults are re-resolved correctly.

## Input-referred noise at very small or zero gain

Noise analysis follows ngspice47's `N_MINGAIN` convention:
`input_PSD = output_PSD * (1 / max(abs(gain)^2, 1e-20))`.
Previously neospice divided by arbitrarily small positive squared gains and
returned zero input noise at exactly zero gain. Highly attenuating circuits
could therefore produce much larger input-referred noise than ngspice, and
zero-gain circuits incorrectly reported none.

This bound is part of the reference's analysis semantics, not an accuracy
tolerance or a physical assertion about an amplifier's useful gain. Output
noise is unchanged. The new regressions check an analytically solvable divider
driven by an ideal controlled source at zero gain and below, at and above the
boundary. The larger paired RC/diode-RC noise cases retain their pre-fix failures.
