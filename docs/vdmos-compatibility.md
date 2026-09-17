# VDMOS compatibility and known defects

Candidate 35 repairs recognition of three-terminal VDMOS cards such as
`M1 drain gate source POWER M=2`. It follows ngspice 47's `INP2M` rule that
model-name lookup begins after three terminals; only VDMOS may omit the fourth
terminal. Four/five-terminal forms remain accepted, and six-terminal VDMOS
cards now raise a parse error. MOSFET model references also resolve without
regard to case and use the declaration's shared model card.

The eight-cell `vdmos_terminal_forms.cir` regression checks N/P polarity,
three-terminal default and `M=2` instances, four/five-terminal controls,
mixed-case model references and whitespace around instance assignments.
It checks actual VDMOS device classes, all public operating-point observables
against ngspice 47, nonzero drain currents and the expected multiplier scaling.
The standard comparison tolerance is unchanged. The thermal terminals in these
controls are grounded; they do not establish self-heating support.

All four primary/driven fixtures enclosing SGN20N40L and VT6K1 now match the
reference. A separate device inspection confirms one VDMOS instance for
SGN20N40L and two for VT6K1 in both variants. These results repair the missing
instances identified in checkpoint 34. They do not certify the models under
arbitrary bias, temperature or analysis settings.

## Model expressions, scope and ngspice 47 mobility reduction

Candidate 36 repairs three-terminal references to global models during
subcircuit expansion. The six-cell regression covers global/local models,
nested use of a global model, local shadowing and a four-terminal control.
A reduced four-terminal MOS circuit also exposes a new collision: a global
model named `S` is mistaken for the formal source terminal `s`. It passes
candidate 35 and fails candidate 36 while ngspice 47 succeeds. This is a
compatibility regression requiring correction, not successful validation of
all global-model scope. Enclosing-scope lookup needs broader coverage.

Model-card parsing now preserves braces, quotes, grouping and constant
parameters. Supported temperature expressions are evaluated before model setup
and again when circuit temperature changes; cached device temperatures and
operating points are invalidated. Canonical `TEMPER` works in the default
dialect. `TEMP` is an alias only in PSpice compatibility mode. Unsupported
runtime updates raise an error rather than silently using a model default.
See [model-card compatibility](model-card-compatibility.md) for the exact scope.

VDMOS mobility reduction now uses `1 + THETA * vdsat`, following the pinned
ngspice 47 `vdmosload.c`, instead of `1 + THETA * vgs`. A separate N/P regression
checks saturation, triode and reverse operation with a zero-THETA control.
The original regression failed with a normalized current error of 0.1848;
it passes after the implementation correction without changing tolerance.

All six stronger VT6K1 diagnostics now match: drain voltage 2 V, gate voltages
0.7/1.5 V and circuit temperatures 0/27/60 °C. At 27 °C and gate voltage 0.7 V,
both simulators give approximately -2.274588 mA for the drain supply current.
The separate global-expression/literal controls and local/global scope probes
also match. These deliberately chosen diagnostics are not held out.

The repeated-temperature regression runs one loaded native circuit through
27 → 0 → 60 → 27 °C against fresh ngspice 47 references. It checks global/local
expressions, shared instances, N/P polarity, literal controls, changing `KP`
and a body-diode breakdown expression. These checks do not establish general
thermal or model-language compatibility.

| Area | Current status |
|---|---|
| DC and IV sweep | N/P operating points, N-channel sweep, terminal forms, scope, mobility reduction and the specified temperature-expression regressions pass. |
| AC and noise | Explicitly rejected with `SimulationError`; tests include `no_throw`. |
| Temperature expressions | VDMOS `VTO`, `KP` and `BV` supported. Other runtime model-parameter updates fail explicitly. |
| Model scope | Ordinary global three-terminal binding is repaired, but a model/formal-node name collision introduces a demonstrated regression. Ancestor-local scope remains unverified. |
| Thermal and instance options | The adapter is isothermal. `TEMP`, `DTEMP`, `IC`, `OFF`, thermal behavior and operating-point queries need further coverage and correction where ignored. |
| General transient behavior | Not certified by these DC/parser checks. |

Candidate 36 passes 1,261/1,262 Release C++ tests and all 393 Python/tooling
tests. RFF70N06 remains failing. All 58 focused parser/BJT/temperature sanitizer
tests and 11 VDMOS sanitizer tests pass with leak detection enabled. The
[progress tracker](joss-progress.md) records corpus and benchmark validation;
these focused checks are not a claim of complete model or leak-free coverage.

The global `RG`/`RB` reduced diagnostics also identify a comparison-harness
boundary: generated reference nodes `m1#gate` and `m1#body_diode` are currently
required as public outputs by the corpus filter. Those probes fail on missing
internal observables even though public currents agree. The pinned reference's
`vdmosset.c` creates these nodes internally; the filter and its tests need
correction without removing fixtures or weakening numerical tolerances.

Candidate 37 development repairs the model/node collision and direct/AKO
temperature-assignment precedence. All 12 VDMOS suite tests pass against 47,
including the new precedence regression across repeated temperatures. The
rebuilt full C++ suite passes 1,265/1,266 tests with RFF70N06 still failing.
Candidate 37's full-corpus, fresh-wheel, sanitizer and benchmark checks remain
pending; the completed results above belong to checkpoint 36.

## Generated internal observables, resolved in checkpoint 38

The `RG`/`RB` harness boundary described above is closed. `is_internal_var` now
classifies `v(<inst>#gate)` and `v(<inst>#body_diode)` as generated internals, so
the reduced `rg`, `rb` and combined probes match instead of failing on missing
observables that neospice deliberately keeps private. No fixture was removed and
no tolerance changed: the public signals in those probes already agreed, and the
`theta` residual was closed earlier by the ngspice 47 mobility correction. The
rule, its empirical enumeration over the VDMOS option space and its boundary are
documented with the [experiment](kicad-experiment.md), and
`VDMOSValidation.GeneratedInternalNodesAreModeledButPrivate` keeps both halves of
the argument under test.

## Thermal VDMOS self-heating is rejected explicitly

ngspice 47 solves a VDMOS thermal network only when the instance carries the
`thermal` flag **and** the model gives `Rthjc` (`vdmosset.c:401`,
`vdmosload.c:87`). In every other case, including a five-terminal instance that
supplies Tj and Tcase without the flag, ngspice grounds both thermal nodes.

neospice previously ignored the `thermal` instance flag altogether: the M-card
parser skips bare flag tokens, so `VDMOSthermal` was never set, the thermal
terminals were grounded, and the self-heating form returned a *converged*
operating point with the thermal network silently absent:

| Observable | ngspice 47 | neospice (before) |
|---|---|---|
| `v(tj)` | 36.477 | 0 |
| `v(tc)` | 36.172 | 0 |
| `i(vd1)` | -0.15286 | -0.16016 |

That is an apparently valid partial result, which milestone 3 forbids. The
parser now reads `thermal` as an instance flag and rejects the self-heating
combination with an explicit error naming this document. The rejection mirrors
ngspice's activation condition exactly, so it is as narrow as possible: Tj/Tc
terminals without the flag, and the flag without `Rthjc`, both still run and
still agree with ngspice (both engines ground the thermal nodes).
`VDMOSValidation.SelfHeatingFailsExplicitlyAndOnlyThatForm` holds both halves,
checking first that the reference really does solve the rejected form.

ngspice additionally creates `v(<inst>#cktTemp)` and `v(<inst>#VdevTemp)` for the
self-heating form. These are deliberately **not** added to the corpus
internal-observable filter: excluding a generated node is only defensible where
neospice models the same node and merely keeps it private, which is not the case
here. No corpus model declares `Rthjc`/`Rthca`, so no frozen fixture is affected.

Implementing the thermal network remains open; until then the boundary is a
refusal rather than a wrong number.
