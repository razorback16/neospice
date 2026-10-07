# VBIC compatibility scope and remaining work

The VBIC adapter is a translated four-terminal, isothermal implementation.
The following evidence describes particular tested behaviors, not complete
coverage of ngspice 47's VBIC options. The overall device/analysis/option matrix
for the paper remains unfinished.

## Junction initialization

The ordinary INITJCT branch in `vbic_load.cpp` follows the reference:
ngspice 47 initializes the intrinsic base–collector voltage to the negative of
the intrinsic base–emitter voltage, rather than zero. The external
base–collector junction remains at zero. Reference source:
`src/spicelib/devices/vbic/vbicload.c` in the checksum-pinned release.

`BJTParser.VbicCollectorJunctionInitializationNgspice47` examines device state
after exactly one device load. The UIC branch returns successfully without
a matrix solve. Ordinary initialization still attempts a Newton step. Its 16 combinations cover NPN/PNP,
levels 4/9, OFF on/off and explicit transient initial conditions on/off.
The four ordinary initialization cases fail before the correction and pass
afterward. OFF and explicit IC branches keep their reference behavior.
Explicit ICs take precedence over OFF during UIC initialization.

## Analysis evidence

The five existing `VBICValidation` comparisons cover NPN and PNP operating
points, a Gummel sweep, a small-signal amplifier and switching transient.
Their input circuits are in `tests/circuits/vbic_*.cir`. These original cases do not establish complete option coverage. The additional
delay cases below cover AC, transient and noise.

The switching test now uses the standard C++ comparator tolerance:
relative limit 1e-3 and denominator floor 1e-9. This replaces its old relative
limit 0.27 and floor 0.05. The fixture, requested analysis, reference options,
required public waveforms and comparison algorithm are unchanged. All 877
combined-grid points per signal pass. The worst normalized error is
8.651342538338568e-8 at the collector. Its maximum absolute error is
2.496429896137187e-8 V. These are measured errors for this fixture, not a
general accuracy guarantee or an interpolation-error bound.

## Excess-phase delay and UIC

Positive `TD` creates the two auxiliary unknowns and eight optional state
entries needed for the isothermal ngspice 47 delay equations:

```
TD * d(xf1)/dt + xf2 = Itzf
(TD/3) * d(xf2)/dt + xf2 - xf1 = 0
```

The delayed current drives the collector. Intrinsic junction charges retain
undelayed transport. AC includes the auxiliary capacitances, and collector
shot noise uses delayed forward minus reverse transport. The last auxiliary
conductances are retained for small-signal loading, matching ngspice even
when transient continuation established the operating point. A debugger probe
of the PNP noise fixture records these nonzero reference conductances.

`vbic_delay_ac.cir`, `vbic_delay_transient.cir`, `vbic_delay_gear.cir` and
`vbic_delay_uic.cir` each include eight cells: NPN and PNP, levels 4/9, zero and
positive delay, shared model cards, NPN area/multiplicity variation and models
with intrinsic charge and series resistance. AC compares 81 frequencies from
1 kHz to 10 MHz and independently checks the simple-cell transfer function
`1 / (1 + s*TD + s*s*TD*TD/3)` at every frequency. Transient comparisons cover
6 microseconds, including source edges. Two further noise fixtures cover both
polarities. The PNP case exercises transient-assisted operating-point recovery.
All required public signals use the standard comparator tolerance and floor.

The UIC comparison exposed a core startup error. UIC now performs one initial
device load without a DC solve, retains its mode flag through time stepping,
and emits its first output after the first accepted step. An omitted capacitor
instance IC derives from the initial node voltages. The current-driven
capacitor regression has no DC equilibrium. It checks ngspice agreement, the
analytic voltage ramp, `.ic` precedence over `.nodeset` and repeated analyses.
Noise now calls the same operating-point implementation as `.op` and AC instead
of maintaining an incomplete fallback sequence.

## Option audit: known gaps

| Option or interface | Observed behavior and remaining work |
|---|---|
| `OFF`, `IC` during initialization | Tested as described above and in the [initialization contract](capabilities.md#bjt-and-vbic-initialization). |
| `TD=0` | Retained as controls in the frequency and transient delay regressions. |
| Positive `TD` | Implemented and exercised by the tests above. Coverage remains limited to the stated circuits and options. |
| Fifth thermal terminal | Not supported by the current Q-card parser. The authored five-terminal self-heating example fails parsing in neospice and runs in ngspice 47. No thermal compatibility claim is made. |
| Levels 4 and 9 | Both dispatch to VBIC in ngspice 47 and are covered by the initialization regression. |
| Levels 12 and 13 | Rejected with a parse error. They are not VBIC levels accepted by the reference parser. |

An initial four-terminal `RTH` probe produced matching results because the
reference grounds an omitted thermal terminal. It did not exercise free
self-heating. Supplying the fifth terminal produces a reference temperature
rise of about 6.38348 K and collector supply current about -1.275865 mA.
Neospice rejects that five-terminal card. The initial matching probe must not
be used as thermal-validation evidence.

## Evidence

[Checkpoint 32](evidence/joss/2026-09-11-validation-32.json) retains initialization
and option probes. [Checkpoint 33](evidence/joss/2026-09-11-validation-33.json)
retains delay, startup and noise validation. These records describe their dated
revisions. See [JOSS progress](joss-progress.md) for current publication work.

A [lexical corpus scan](evidence/joss/2026-09-11-checkpoint33-vbic-corpus-scope.json)
verifies all 2,073 pinned source-file hashes and finds three numeric level-4/9
NPN/PNP cards: BFP780 and two BFQ790 declarations, with TD values of 500 fs
and 1 fs. They are nested in three subcircuits. The frozen experiment has no
direct model fixture for those declarations. Each enclosing subcircuit has a
primary and driven fixture. The [binding diagnostic](evidence/joss/2026-09-11-checkpoint33-model-binding-audit.json)
found skipped Q devices caused by mixed model syntax. The corrected parser now
instantiates one VBIC device with 74 states in each wrapper, including delay states.
The lexical scan itself does not resolve expressions
or aliases. See [model-card compatibility](model-card-compatibility.md) for the
regression and the explicit unsupported-level handling.

Further source-audit leads require dedicated regressions: VBIC current query
aliases still expose undelayed forward current, some query/noise expressions
apply an additional instance multiplier to already scaled state, and thermal
noise uses nominal temperature. General node-IC-to-semiconductor-junction
initialization also needs coverage. These are pending correctness work, not
validated options or reasons to exclude failing circuits.
