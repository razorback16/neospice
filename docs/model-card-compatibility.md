# Model-card syntax and device binding

ngspice 47 is the sole reference. Candidate 34 corrects model cards whose
parameters occur before, inside or after optional parentheses. For example,
`NPN LEVEL=4(IS=1e-14) TD=1u` now selects VBIC and retains both `IS` and `TD`.
Previously the parser interpreted `NPN LEVEL=4` as the model type, and Q-card
handling could silently omit the transistor.

The model type is the first token, with whitespace, parentheses, commas and
equals signs treated as delimiters. Numeric parameters accept whitespace,
commas and optional parenthesized groups. This follows the token boundaries
in ngspice 47's `src/spicelib/parser/inpdomod.c` and `inpgtok.c`; it does not
establish complete expression, alias or model-language compatibility.

The regression `Parser.ModelParametersBeforeInsideAndAfterParentheses`
checks six parameter layouts. `BJTParser.MixedModelSyntaxInstantiatesVbicAndMatchesNgspice47`
loads a nine-cell fixture with NPN/PNP VBIC levels 4/9 and classical BJT levels
0/1/2. It checks the actual device classes and delay-state allocation, compares
all public operating-point observables with ngspice 47, and requires nonzero
collector supply currents in the mixed-syntax cells. Tolerances are unchanged.

The three pinned-corpus VBIC declarations occur inside BFP780 and BFQ790
subcircuits. An explicit device inspection confirms one VBIC instance and 74
state entries in every enclosing primary/driven fixture, six fixtures total.
These checks establish binding and delay-state allocation; minimally excited
operating-point fixtures alone do not validate the full model or its options.
The complete corpus rerun repairs two driven BFQ790 mismatches and both TL072-R
variants. TL072-R still emits ignored-parameter/expression warnings; its improved
operating point is not complete model-language validation.

## Explicit errors and remaining scope

The semiconductor registry dispatch for D/M/Q/J/Z now propagates `ParseError`
instead of warning and continuing with a missing device. Q cards referencing
an absent model, a non-BJT model, or an unsupported BJT level fail explicitly.
The absent-model and wrong-type controls also fail in unmodified ngspice 47.

| BJT level | Candidate behavior |
|---|---|
| 0, 1, 2 | Classical BJT, matching ngspice 47's dispatch. |
| 4, 9 | VBIC. |
| 8 | Explicitly unsupported; ngspice 47 selects HICUM2. |
| 12, 13 | Rejected; no fallback to VBIC or classical BJT. |

Other parser warning paths, model-conversion exceptions, fractional or
out-of-range level handling, and general expression/alias support still need
audit. The change is not a claim that every malformed netlist is rejected.

The full corpus also exposes 456 previously successful native runs whose error
logs showed skipped cards. They now fail explicitly. The reference already
fails 452 of those fixtures; the other four cover SGN20N40L and VT6K1, whose
three-terminal VDMOS cards remain unsupported by the native parser. Three
earlier MATCH results omitted those transistors. All fixtures and failures
remain in the accounting. Candidate 35 repairs those four minimal fixtures. Candidate 36 also corrects
the demonstrated global-model scope and selected temperature-expression paths; see the
[VDMOS audit](vdmos-compatibility.md) and the checkpoint 34
[complete transition triage](evidence/joss/2026-09-11-checkpoint34-transition-triage.json).

Candidate 34 passes 1,248/1,249 Release C++ tests, all 393 Python/tooling tests,
51 parser/BJT sanitizer tests and all 10 VBIC sanitizer tests. Leak detection
is enabled for both sanitizer groups. The existing RFF70N06 C++ failure remains.
All 34 comprehensive, five THS4131 and one TLV benchmark accuracy gates pass;
no new performance measurement is implied. The [progress tracker](joss-progress.md)
records the complete corpus results and remaining publication blockers.

## Candidate 36 expression scope

Balanced brace and quoted model values preserve parentheses, whitespace and
commas within expressions. Known constant parameters and expanded functions
are evaluated before model conversion. Unknown expression identifiers,
unbalanced delimiters and non-finite values produce `ParseError`.
Non-numeric metadata and unknown model parameters retain their existing warning
behavior; this is not complete malformed-input rejection.

`TEMPER` represents circuit temperature in degrees Celsius. `TEMP` is normalized
to it only in PSpice compatibility mode. Model expressions keep local constant
substitutions while deferring temperature. AKO inheritance merges numeric and
deferred parameters. Its numeric-versus-temperature-expression precedence still
differs from ngspice 47, as described below.

Runtime parameter updates currently support:

| Model | Temperature-dependent parameters |
|---|---|
| VDMOS | `VTO`, `KP`, `BV` |
| MOS1 | `VTO`, `KP` |
| Classical BJT | `BF` |
| Diode | `IS` |

Other recognized runtime updates fail explicitly. In particular, a resistance
expression may change internal-node allocation or setup-derived conductances;
updating its number without rebuilding those dependencies would be incorrect.
Static constant expressions still use ordinary model setup for all model types.
This restriction does not claim that the corresponding ngspice feature is
unsupported by ngspice itself.

Circuit temperature changes invalidate cached operating points and device
temperature preprocessing. Invalid model expressions surface before DC
convergence fallbacks and leave the circuit able to recover at a valid
temperature. A combined BJT/diode/MOS1 regression runs AC before DC through
27 → 0 → 60 → 27 °C, checking that an old DC solution is not reused. Its BJT
capacitance comparison also exposed and corrected use of the actual rather
than nominal junction-potential correction in temperature preprocessing.

Dependent `.param` expressions, forward/local parameter scope, ancestor-local
model scope, other temperature-dependent model parameters, and further
instance/nominal-temperature combinations remain compatibility work. The
full-corpus accounting retains cases that now report previously hidden input
errors. A previously matching minimal operating point is not evidence that its
model expressions were evaluated correctly.

A stronger assignment-precedence diagnostic remains failing. With
`VTO={.65-.001*(TEMP-27)} VTO=.6` under PSpice compatibility, ngspice 47 applies
the deferred temperature expression; this candidate keeps the later literal.
An AKO-derived literal override of the base's temperature expression has the
same discrepancy. At 27 °C, the diagnostic drain currents are -35.520163 mA
(native) versus -27.410104 mA (reference). Both variants fail at 0/27/60 °C;
expression inheritance without the literal override and derived expressions
over a literal base agree. The new parser unit test's numeric-override assertion
currently describes native behavior and must be corrected together with this
implementation defect. Passing suite totals do not resolve this counterexample.

The completed corpus also reproduces a new model/node scope collision. A global
model named `S` must not prevent a four-terminal MOS card's formal source node
`s` from being expanded to its connected node. The reduced reference fixture
passes candidate 35 and ngspice 47 but fails candidate 36. This and the literal/
temperature precedence counterexamples are blockers in the new implementation,
not merely additional untested features.

## Candidate 37 development repairs

The current implementation retains deferred model assignments in source order
and applies them in reverse order, matching ngspice 47's temperature-update
list. The first deferred assignment therefore wins over later deferred or
literal assignments, including inherited AKO assignments. Direct assignments
are tested in the default dialect and AKO assignments in explicit PSpice mode,
at 27 → 0 → 60 → 27 °C. The earlier parser assertion was corrected with this
reference-tested implementation change.

Runtime expression support now also includes MOS3 `KP`/`VTO` and diode `BV`.
The new regression checks DC and AC through repeated temperature changes,
including N/P MOS3 instances, a shared model with `M=2`, and diode breakdown.
Other runtime parameters retain the explicit unsupported-parameter error.

MOS terminal counting during expansion now follows positional fields and
instance assignments/flags rather than looking up node names as models.
Another repair limits lazy model loading to model-reference positions: source
keywords, nodes, instance names and parameter keys cannot activate unused
models. Actually referenced invalid model expressions still fail explicitly.

All new reference regressions pass in the rebuilt 1,266-test C++ suite; its
sole failure remains RFF70N06. These repairs still require the broader candidate
37 validation described in the [progress tracker](joss-progress.md).
