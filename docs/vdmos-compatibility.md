# VDMOS compatibility and known defects

The parser accepts three-terminal VDMOS cards such as
`M1 drain gate source POWER M=2`. It follows ngspice 47's `INP2M` rule that
model-name lookup begins after three terminals. Only VDMOS may omit the fourth
terminal. Four/five-terminal forms remain accepted, and six-terminal VDMOS
cards now raise a parse error. MOSFET model references also resolve without
regard to case and use the declaration's shared model card.

The eight-cell `vdmos_terminal_forms.cir` regression checks N/P polarity,
three-terminal default and `M=2` instances, four/five-terminal controls,
mixed-case model references and whitespace around instance assignments.
It checks actual VDMOS device classes, all public operating-point observables
against ngspice 47, nonzero drain currents and the expected multiplier scaling.
The standard comparison tolerance is unchanged. The thermal terminals in these
controls are grounded. They do not establish self-heating support.

All four primary/driven fixtures enclosing SGN20N40L and VT6K1 match the
reference. A separate device inspection confirms one VDMOS instance for
SGN20N40L and two for VT6K1 in both variants. These checks do not certify the models under
arbitrary bias, temperature or analysis settings.

## Model expressions, scope and ngspice 47 mobility reduction

Three-terminal cards resolve global model references during
subcircuit expansion. The six-cell regression covers global/local models,
nested use of a global model, local shadowing and a four-terminal control.
MOS expansion also preserves formal nodes whose names match global models.
Ancestor-local scope needs broader coverage.

Model-card parsing preserves braces, quotes, grouping and constant
parameters. Supported temperature expressions are evaluated before model setup
and again when circuit temperature changes. Cached device temperatures and
operating points are invalidated. Canonical `TEMPER` works in the default
dialect. `TEMP` is an alias only in PSpice compatibility mode. Unsupported
runtime updates raise an error rather than silently using a model default.
See [model-card compatibility](model-card-compatibility.md) for the exact scope.

VDMOS mobility reduction uses `1 + THETA * vdsat`, following the pinned
ngspice 47 `vdmosload.c`. An N/P regression checks saturation, triode and
reverse operation with a zero-THETA control.

All six stronger VT6K1 diagnostics match: drain voltage 2 V, gate voltages
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
| AC and noise | Explicitly rejected with `SimulationError`. Tests include `no_throw`. |
| Temperature expressions | VDMOS `VTO`, `KP` and `BV` supported. Other runtime model-parameter updates fail explicitly. |
| Model scope | Global three-terminal binding and model/formal-node collisions have regressions. Ancestor-local scope remains unverified. |
| Thermal and instance options | The adapter is isothermal. `TEMP`, `DTEMP`, `IC`, `OFF`, thermal behavior and operating-point queries need further coverage and correction where ignored. |
| General transient behavior | Not certified by these DC/parser checks. |

## Generated internal observables

`is_internal_var` classifies `v(<inst>#gate)` and `v(<inst>#body_diode)` as
generated internals. neospice models these nodes but keeps them private.
The public signals in reduced `RG`/`RB` probes agree with ngspice 47.
No fixture or tolerance changes are required.

The [experiment](kicad-experiment.md#generated-internal-observables) documents the
reference option enumeration and filter boundary.
`VDMOSValidation.GeneratedInternalNodesAreModeledButPrivate` checks node allocation,
privacy and the reference's generated names.

## Thermal VDMOS self-heating is rejected explicitly

ngspice 47 solves a VDMOS thermal network only when the instance carries the
`thermal` flag **and** the model gives `Rthjc` (`vdmosset.c:401`,
`vdmosload.c:87`). In every other case, including a five-terminal instance that
supplies Tj and Tcase without the flag, ngspice grounds both thermal nodes.

The parser reads `thermal` as an instance flag and rejects the self-heating
combination with an explicit error naming this document. The rejection mirrors
ngspice's activation condition exactly, so it is as narrow as possible: Tj/Tc
terminals without the flag, and the flag without `Rthjc`, both still run and
still agree with ngspice (both engines ground the thermal nodes).
`VDMOSValidation.SelfHeatingFailsExplicitlyAndOnlyThatForm` holds both halves,
checking first that the reference really does solve the rejected form.

ngspice creates `v(<inst>#cktTemp)` and `v(<inst>#VdevTemp)` for self-heating.
The corpus filter does not exclude these names. neospice does not model their
thermal network, so the private-node justification does not apply.

The exact 2,073-file frozen source inventory contains no `Rthjc` declaration
and no M card with `thermal`. No frozen fixture can activate self-heating.
The explicit rejection therefore changes no outcome in that population.

The flag survives subcircuit expansion:
`mos_terminal_count` (`subcircuit_expand.cpp:49`) treats `thermal` as a
node-list terminator. `tests/circuits/vdmos_thermal_selfheat_subckt.cir` wraps
the same instance in a `.subckt` and is rejected on the expanded name `x1.m1`,
so the boundary cannot silently drop the token and solve isothermally.

Implementing the thermal network remains open. Until then the boundary is a
refusal rather than a wrong number.
