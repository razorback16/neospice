# Model-card syntax and device binding

ngspice 47 is the sole reference. Model parameters may occur before, inside,
or after optional parentheses. `NPN LEVEL=4(IS=1e-14) TD=1u` selects VBIC
and retains both `IS` and `TD`.

The model type is the first token. Whitespace, parentheses, commas and equals
signs delimit tokens. Numeric parameters accept commas and optional groups.
These boundaries follow ngspice 47's `inpdomod.c` and `inpgtok.c`.
They do not establish complete expression, alias or model-language compatibility.

`Parser.ModelParametersBeforeInsideAndAfterParentheses` checks six layouts.
`BJTParser.MixedModelSyntaxInstantiatesVbicAndMatchesNgspice47` checks a nine-cell
NPN/PNP fixture with VBIC levels 4/9 and classical BJT levels 0/1/2.
It checks device classes, delay states, public operating-point values and
nonzero collector supply currents with unchanged tolerances.

The pinned corpus has three VBIC declarations inside BFP780 and BFQ790.
Device inspection confirms one VBIC instance and 74 states in each of their
six primary/driven wrappers. This establishes binding and delay-state allocation,
not full model validation. TL072-R still emits ignored-parameter/expression
warnings despite its improved operating point.

## Explicit errors and remaining scope

Semiconductor registry dispatch for D/M/Q/J/Z propagates `ParseError`.
Q cards fail for absent models, non-BJT models and unsupported BJT levels.
The absent-model and wrong-type controls also fail in ngspice 47.

| BJT level | Behavior |
|---|---|
| 0, 1, 2 | Classical BJT, matching ngspice 47. |
| 4, 9 | VBIC. |
| 8 | Unsupported. ngspice 47 selects HICUM2. |
| 12, 13 | Rejected without fallback. |

Other warning paths, conversion exceptions, fractional/out-of-range levels and
general expression/alias support need audit. Not every malformed input fails.
Previously skipped devices remain in corpus accounting. See the
[VDMOS audit](vdmos-compatibility.md) and dated
[transition triage](evidence/joss/2026-09-11-checkpoint34-transition-triage.json).

<a id="candidate-36-expression-scope"></a>

## Model expressions

Balanced braces and quotes preserve grouping, whitespace and commas.
Known constants and expanded functions are evaluated before model conversion.
Unknown expression identifiers, unbalanced delimiters and non-finite values
raise `ParseError`. Non-numeric metadata and unknown parameters retain warnings.

`TEMPER` is circuit temperature in degrees Celsius. `TEMP` aliases it only in
PSpice mode. Model expressions substitute local constants and defer temperature.
AKO inheritance combines numeric and deferred parameters. Deferred assignments
retain source order and execute in reverse order, matching ngspice 47.
The first deferred assignment wins over later deferred or literal assignments,
including inherited AKO assignments.

Runtime temperature updates support:

| Model | Parameters |
|---|---|
| VDMOS | `VTO`, `KP`, `BV` |
| MOS1, MOS3 | `VTO`, `KP` |
| Classical BJT | `BF` |
| Diode | `IS`, `BV` |

Other recognized runtime updates fail explicitly. A resistance update may require
new internal nodes or setup conductances. Static constant expressions use ordinary
model setup for all model types. This restriction applies to neospice.

Temperature changes invalidate operating points and device temperature caches.
Invalid expressions fail before DC convergence fallbacks. The circuit can recover
at a valid temperature. Regressions use repeated 27 → 0 → 60 → 27 °C changes,
including AC before DC, N/P MOS3, shared instances with `M=2`, and diode breakdown.
Direct precedence tests use the default dialect. AKO tests use explicit PSpice mode.

MOS expansion counts positional terminals and assignments/flags. A global model
named `S` no longer prevents expansion of the formal source node `s`.
Lazy model loading uses model-reference positions. Nodes, source keywords,
instance names and parameter keys cannot activate unused models.
Referenced invalid model expressions still fail.

Dependent `.param` expressions, forward/local parameter scope, ancestor-local
model scope and further instance/nominal-temperature combinations need coverage.
Minimal operating-point agreement does not prove correct expression evaluation.
Dated validation and final-candidate requirements are in the
[progress tracker](joss-progress.md).
