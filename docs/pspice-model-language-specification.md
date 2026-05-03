# PSpice Model Language Specification

## Purpose and implementation target

This document describes PSpice model-language constructs that a coding agent should add on top of an existing SPICE parser. PSpice remains SPICE-family syntax, but real vendor macro-models commonly depend on PSpice-specific parameter, expression, tolerance, and analog behavioral modeling forms. Cadence describes PSpice models as either model parameter sets using `.MODEL` syntax or subcircuit netlists using `.SUBCKT` syntax, with both forms saved in model libraries ([Cadence PSpice User Guide](https://resources.pcb.cadence.com/i/1180526/192)). The PSpice A/D Reference Guide defines extensions such as `AKO:` model inheritance, tolerance specifications, `OPTIONAL:`, `PARAMS:`, `TEXT:`, `.FUNC`, and `VALUE` or `TABLE` ABM device syntax ([PSpice A/D Reference Guide](https://www.montana.edu/aolson/ee503/pspcref.pdf)).

The goal is not to implement all PSpice simulator behavior immediately. The goal is to parse and represent enough PSpice model-library syntax to import manufacturer libraries, emit high-quality diagnostics, and lower supported constructs to the existing SPICE backend where possible.

*Source: PSpice Reference Guide, Product Version 16.5, May 2011*

## PSpice constructs to add first

| Priority | Construct | Why it matters |
| --- | --- | --- |
| P0 | Curly-brace expressions `{...}` | Cadence documents braces as the syntax for replacing component values, model parameter values, and other properties with evaluated expressions ([Cadence PSpice User Guide](https://resources.pcb.cadence.com/i/1180526-pspice-user-guide/165)). |
| P0 | `.SUBCKT ... PARAMS:` | PSpice macro-models commonly expose tunable parameters through `PARAMS:`, and the PSpice reference syntax includes `PARAMS:` on `.SUBCKT` definitions ([PSpice A/D Reference Guide](https://www.montana.edu/aolson/ee503/pspcref.pdf)). |
| P0 | `X... PARAMS:` call overrides | PSpice-compatible subcircuit calls use call-site parameters to override defaults, and third-party examples show `Xamp ... PARAMS: Cin=20n Rbias=2.7K` for PSpice-compatible syntax ([YouSpice](https://youspice.com/creating-a-spice-subcircuit-subckt-manually/)). |
| P0 | `.FUNC` | PSpice functions are used inside expressions and may be stored in `.INC` include files ([FlowCAD .FUNC application note](https://www.flowcad.de/AN/FlowCAD-AN-PSpice-Func-Command.pdf)). |
| P0 | `VALUE={...}` and `TABLE {...}` on `E` and `G` | PSpice ABM uses `VALUE` and `TABLE` extensions to `E` and `G` devices for instantaneous behavioral relationships ([Cadence PSpice User Guide](https://resources.pcb.cadence.com/i/1180526-pspice-user-guide/363)). |
| P1 | `.MODEL ... AKO:` and tolerance syntax | The PSpice reference `.MODEL` form includes optional `AKO:` inheritance plus `DEV` and `LOT` tolerance specifications ([PSpice A/D Reference Guide](https://www.montana.edu/aolson/ee503/pspcref.pdf)). |
| P1 | `OPTIONAL:` and `TEXT:` on `.SUBCKT` | The PSpice reference `.SUBCKT` form includes optional interface nodes and text parameters ([PSpice A/D Reference Guide](https://www.montana.edu/aolson/ee503/pspcref.pdf)). |
| P1 | PSpice numeric functions | Vendor models use PSpice functions such as `IF`, `LIMIT`, `STP`, `TABLE`, `DDT`, and `SDT` ([PSpice A/D Reference Guide](https://www.montana.edu/aolson/ee503/pspcref.pdf)). |
| P2 | Capture `PSpiceTemplate` | Capture symbols generate PSpice netlists from `PSpiceTemplate` properties, which matters if importing schematic libraries rather than plain netlists ([Cadence PSpice User Guide](https://resources.pcb.cadence.com/i/1180526-pspice-user-guide/199)). |

---

## Numeric values and scale suffixes

PSpice literal numeric values use standard floating-point notation and may be scaled by suffixes such as `F`, `P`, `N`, `U`, `MIL`, `M`, `K`, `MEG`, `G`, and `T` ([PSpice A/D Reference Guide](https://www.montana.edu/aolson/ee503/pspcref.pdf)). PSpice's `M` suffix means milli, while `MEG` means mega, so the lexer must use longest-match suffix recognition and must not interpret bare `M` as mega ([PSpice A/D Reference Guide](https://www.montana.edu/aolson/ee503/pspcref.pdf)).

Alphabetic characters are **not case sensitive**.

| Suffix | Meaning |
| --- | --- |
| `F` | femto, `1e-15` |
| `P` | pico, `1e-12` |
| `N` | nano, `1e-9` |
| `U` | micro, `1e-6` |
| `MIL` | `25.4e-6` |
| `M` | milli, `1e-3` |
| `K` | kilo, `1e3` |
| `MEG` | mega, `1e6` |
| `G` | giga, `1e9` |
| `T` | tera, `1e12` |

The reference also lists `C` as a clock-cycle scale whose value varies and must be set where applicable, so most analog model importers should preserve it as a symbolic scale unless they implement the relevant digital timing context ([PSpice A/D Reference Guide](https://www.montana.edu/aolson/ee503/pspcref.pdf)).

## Command syntax notation

| Notation | Example | Meaning |
| --- | --- | --- |
| `<item>` | `<model name>` | Required item |
| `<item>*` | `<value>*` | Required, one or more occurrences |
| `[item]` | `[AC]` | Optional item |
| `[item]*` | `[value]*` | Optional, zero or more occurrences |
| `<A \| B>` | `<YES \| NO>` | Exactly one of the choices |
| `[A \| B]` | `[ON \| OFF]` | Zero or one of the choices |

---

## Comments, continuations, and includes

PSpice uses `*` in column 1 for full-line comments, `;` for inline comments, and `+` in column 1 for continuation lines ([PSpice A/D Reference Guide](https://www.montana.edu/aolson/ee503/pspcref.pdf)). The parser should assemble continuation lines before parsing statements and should strip inline semicolon comments only when not inside quoted text or brace expressions.

Cadence describes include files as user-defined files that contain PSpice A/D commands or supplemental comments, and it notes that include files typically use the `.INC` extension ([Cadence PSpice User Guide](https://resources.pcb.cadence.com/i/1180526-pspice-user-guide/50)). Cadence also states that PSpice searches model libraries, stimulus files, and include files according to configured files, scopes, and search order, with files scoped to profile, design, or global use ([Cadence PSpice User Guide](https://resources.pcb.cadence.com/i/1180526-pspice-user-guide/50)).

Implementation guidance:

- Support `.INC`, `.INCLUDE`, and model library files as source units with configurable search paths.
- Preserve file scope metadata when importing from an OrCAD/Capture project if available.
- Detect recursive include graphs and report the include stack.
- Treat include expansion as a parse forest so diagnostics can point to the original file.

---

## PSpice expressions

### Curly-brace expression syntax

Cadence defines a PSpice expression as a mathematical relationship that defines a numeric or boolean value, and it documents `{ expression }` as the syntax used to replace component values, model parameter values, other property values, or IF-test logic ([Cadence PSpice User Guide](https://resources.pcb.cadence.com/i/1180526-pspice-user-guide/165)). PSpice evaluates expressions when it reads a new circuit and when a parameter used by an expression changes during an analysis, such as a DC sweep or parametric analysis ([Cadence PSpice User Guide](https://resources.pcb.cadence.com/i/1180526-pspice-user-guide/165)).

```spice
.PARAM VSUPPLY=14v
VCC vcc 0 DC {VSUPPLY}
RBIAS in out {RBASE*1.05}
```

Cadence's global-parameter workflow also uses `{ global_parameter_name }` to tell PSpice A/D to evaluate a named parameter and use its value in component values, model parameter values, or other properties ([Cadence PSpice User Guide](https://resources.pcb.cadence.com/i/1180526-pspice-user-guide/163)).

### Arithmetic operators

| Operator | Meaning |
| --- | --- |
| `+` | addition (also string concatenation) |
| `-` | subtraction |
| `*` | multiplication |
| `/` | division |
| `**` / `PWR()` / `PWRS()` | exponentiation |

### Relational and logical operators

The PSpice reference lists arithmetic, boolean, and relational operators including `+`, `-`, `*`, `/`, `**`, `~`, `|`, `^`, `&`, `==`, `!=`, `>`, `>=`, `<`, and `<=` ([PSpice A/D Reference Guide](https://www.montana.edu/aolson/ee503/pspcref.pdf)).

### Intrinsic functions

| Function | Meaning | Notes |
| --- | --- | --- |
| `ABS(x)` | \|x\| | |
| `ACOS(x)` | arccos(x) | -1.0 ≤ x ≤ +1.0 |
| `ACOSH(x)` | inverse hyperbolic cosine | result in radians |
| `ARCTAN(x)` | tan⁻¹(x) | result in radians |
| `ASIN(x)` | arcsin(x) | -1.0 ≤ x ≤ +1.0 |
| `ASINH(x)` | inverse hyperbolic sine | result in radians |
| `ATAN(x)` | tan⁻¹(x) | result in radians |
| `ATAN2(y,x)` | arctan(y/x) | result in radians |
| `ATANH(x)` | inverse hyperbolic tangent | result in radians |
| `COS(x)` | cos(x) | x in radians |
| `COSH(x)` | hyperbolic cosine | x in radians |
| `DDT(x)` | time derivative of x | transient analysis only |
| `EXP(x)` | eˣ | |
| `IF(t, x, y)` | x if t is true, y if false | discontinuities between branches can cause convergence issues |
| `IMG(x)` | imaginary part of x | returns 0.0 for real numbers |
| `LIMIT(x, min, max)` | clamp x to [min, max] | |
| `LOG(x)` | ln(x) | log base e |
| `LOG10(x)` | log₁₀(x) | log base 10 |
| `M(x)` | magnitude of x | same as ABS(x) |
| `MAX(x, y)` | maximum of x and y | |
| `MIN(x, y)` | minimum of x and y | |
| `P(x)` | phase of x | returns 0.0 for real numbers |
| `PWR(x, y)` | \|x\|ʸ | interchangeable with `x**y` |
| `PWRS(x, y)` | +\|x\|ʸ if x>0; −\|x\|ʸ if x<0 | |
| `R(x)` | real part of x | |
| `SCHEDULE(x1,y1,...,xn,yn)` | piecewise constant function | must include entry for TIME=0 |
| `SDT(x)` | time integral of x | transient analysis only |
| `SGN(x)` | signum function | |
| `SIN(x)` | sin(x) | x in radians |
| `SINH(x)` | hyperbolic sine | x in radians |
| `STP(x)` | 1 if x ≥ 0; 0 if x < 0 | unit step function |
| `SQRT(x)` | x^(1/2) | |
| `TAN(x)` | tan(x) | x in radians |
| `TANH(x)` | hyperbolic tangent | x in radians |
| `TABLE(x, x1,y1,...,xn,yn)` | piecewise-linear lookup; clips at endpoints | |

Important semantic notes:

- `IF(t,x,y)` returns `x` when the boolean test is true and `y` when it is false, and discontinuities between branches can create convergence problems ([PSpice expression reference excerpt](https://www.scribd.com/document/135630057/Pspice-Expressions)).
- `LIMIT(x,min,max)` clamps below `min` and above `max`, while `STP(x)` is a step function useful for suppressing a value until a time threshold ([Cadence PSpice User Guide](https://resources.pcb.cadence.com/i/1180526-pspice-user-guide/169)).
- `DDT(x)` and `SDT(x)` are transient-analysis-only derivative and integral functions ([Cadence PSpice User Guide](https://resources.pcb.cadence.com/i/1180526-pspice-user-guide/169)).
- `TABLE(x,x1,y1,...,xn,yn)` returns an interpolated y-value and clamps to the nearest endpoint outside the table range ([PSpice A/D Reference Guide](https://www.montana.edu/aolson/ee503/pspcref.pdf)).

### Expression parser recommendation

```ebnf
braced_expr   := "{" expr "}"
expr          := logical_or
logical_or    := logical_xor ("|" logical_xor)*
logical_xor   := logical_and ("^" logical_and)*
logical_and   := equality ("&" equality)*
equality      := relational (("==" | "!=") relational)*
relational    := additive (("<" | "<=" | ">" | ">=") additive)*
additive      := multiplicative (("+" | "-") multiplicative)*
multiplicative:= power (("*" | "/") power)*
power         := unary ("**" unary)*
unary         := ("+" | "-" | "~") unary | primary
primary       := number | identifier | function_call | braced_expr | "(" expr ")"
```

This grammar intentionally treats `PWR(x,y)` and `PWRS(x,y)` as functions rather than operators, while `**` is parsed as the exponentiation operator.

### System variables

These are reserved keywords usable in PSpice expressions. They **cannot** be redefined via `.PARAM` except that `GMIN`, `PI`, and `TEMP` may be declared inside a `.SUBCKT` statement for a local user-defined value.

| Variable | Evaluates to | Notes |
| --- | --- | --- |
| `TEMP` | Temperature (°C) from temperature sweep | Default TNOM = 27°C |
| `TIME` | Current transient simulation time | Undefined if no transient analysis running |
| `RELTOL` | Relative tolerance for voltage and current | From `.OPTIONS` |
| `ABSTOL` | Absolute current tolerance | From `.OPTIONS` |
| `VNTOL` | Voltage tolerance | From `.OPTIONS` |
| `CHGTOL` | Charge tolerance | From `.OPTIONS` |
| `GMIN` | Minimum conductance for any branch | From `.OPTIONS`; can be locally redefined in `.SUBCKT` |
| `PI` | π (3.14159…) | Can be locally redefined in `.SUBCKT` |

---

## `.PARAM` command and global parameters

The PSpice reference defines `.PARAM <<name>=<value>>*` and `.PARAM <<name>={ <expression> }>>*`, where names cannot begin with a number and cannot be predefined parameters such as `TIME`, `TEMP`, `VT`, or `GMIN` ([PSpice A/D Reference Guide](https://www.montana.edu/aolson/ee503/pspcref.pdf)). Cadence's user guide describes global parameters through the Capture `PARAM` part, but the netlist-level importer should represent both schematic-originated and textual `.PARAM` definitions in the same parameter symbol table ([Cadence PSpice User Guide](https://resources.pcb.cadence.com/i/1180526-pspice-user-guide/163)).

```spice
.PARAM RBASE=10k
.PARAM RHI={RBASE*1.10} RLO={RBASE*0.90}
```

Implementation guidance:

- Parse multiple assignments on one `.PARAM` line.
- Store parameters in lexical scopes: global deck scope, subcircuit scope, and call override scope.
- Delay evaluation until after include expansion and after `.FUNC` definitions are known.
- Detect reserved-name collisions but allow permissive mode to preserve questionable vendor models.

## `.FUNC` command

PSpice `.FUNC` defines functions used in expressions with the general form `.FUNC <name>([arg]*) {<body>}` ([FlowCAD .FUNC application note](https://www.flowcad.de/AN/FlowCAD-AN-PSpice-Func-Command.pdf)). The reference allows up to 10 arguments, requires the number of call arguments to match the definition, and treats the body as a normal math expression enclosed in braces ([FlowCAD .FUNC application note](https://www.flowcad.de/AN/FlowCAD-AN-PSpice-Func-Command.pdf)).

```spice
.FUNC my_bv(p1,p2,p3) {LOG10(2*p1+4)+p2+EXP(p3/5)}
.FUNC DECAY(CNST) {EXP(-CNST*TIME)}
.FUNC MIN3(A,B,C) {MIN(A,MIN(B,C))}
```

FlowCAD notes that `.FUNC` definitions can be placed directly in subcircuits, where they have local scope, and that function names cannot be redefined or match predefined functions such as `SIN` or `SQRT` ([FlowCAD .FUNC application note](https://www.flowcad.de/AN/FlowCAD-AN-PSpice-Func-Command.pdf)). FlowCAD also shows `.FUNC` as a workaround for nested parameter-expression problems in model parameters such as `BV={my_bv(p1,p2,p3)}` inside a `.SUBCKT ... PARAMS:` model ([FlowCAD .FUNC application note](https://www.flowcad.de/AN/FlowCAD-AN-PSpice-Func-Command.pdf)).

Implementation guidance:

- Add a `FunctionDef` AST node with name, formal arguments, body expression, and lexical scope.
- Resolve user functions before intrinsic functions only if dialect compatibility says user functions may shadow; otherwise reject predefined-name collisions.
- Inline expansion is optional; semantic checks can treat user functions as expression call nodes.
- Support `.FUNC` inside `.SUBCKT` bodies with lexical scoping.

---

## `.MODEL` statement in PSpice

### General form

The PSpice reference defines a richer `.MODEL` syntax than baseline SPICE, including optional `AKO:` inheritance, model type, parameter assignments, tolerance specifications, and temperature-measurement metadata ([PSpice A/D Reference Guide](https://www.montana.edu/aolson/ee503/pspcref.pdf)).

```spice
.MODEL <model name> [AKO: <reference model name>]
+ <model type>
+ ([<parameter name>=<value> [tolerance specification]]*
+ [T_MEASURED=<value>]
+ [[T_ABS=<value>] or [T_REL_GLOBAL=<value>] or [T_REL_LOCAL=<value>]])
```

The PSpice reference lists model types including `CAP`, `CORE`, `D`, `DINPUT`, `DOUTPUT`, `GASFET`, `IND`, `ISWITCH`, `LPNP`, `NIGBT`, `NJF`, `NMOS`, `NPN`, `PJF`, `PMOS`, `PNP`, `RES`, `TRN`, `UADC`, `UDAC`, `UDLY`, `UEFF`, `UGATE`, `UGFF`, `UIO`, `UTGATE`, and `VSWITCH` ([PSpice A/D Reference Guide](https://www.montana.edu/aolson/ee503/pspcref.pdf)).

### `AKO:` inheritance

`AKO:` appears in the `.MODEL` grammar before the model type and names a reference model inherited by the new model ([PSpice A/D Reference Guide](https://www.montana.edu/aolson/ee503/pspcref.pdf)). A parser should represent inheritance explicitly rather than flattening it during parse, because include order and model-library selection can affect reference resolution.

```spice
.MODEL FASTD AKO: BASED D(IS=1n RS=0.1)
```

### Tolerances and temperature metadata

The PSpice `.MODEL` grammar allows tolerance specifications using `DEV` and `LOT`, optional tracking and distribution information, and distributions such as `UNIFORM`, `GAUSS`, or a user-defined name ([PSpice A/D Reference Guide](https://www.montana.edu/aolson/ee503/pspcref.pdf)). Cadence notes that editing model text to add `DEV` and `LOT` information does not by itself make a model compatible with Advanced Analysis Monte Carlo, because tolerance and smoke information can also live in the device property file associated with the model library ([Cadence PSpice User Guide](https://resources.pcb.cadence.com/i/1180526-pspice-user-guide/199)).

Implementation guidance:

- Parse `DEV` and `LOT` as parameter annotations, not as separate parameters.
- Preserve unknown distribution names and lot tracking IDs.
- Parse `T_MEASURED`, `T_ABS`, `T_REL_GLOBAL`, and `T_REL_LOCAL` as model-level metadata fields.
- Do not assume PSpice Advanced Analysis behavior from textual `.MODEL` alone.

---

## `.SUBCKT` in PSpice

### General form

The PSpice reference defines `.SUBCKT` with ordinary nodes plus optional `OPTIONAL:`, `PARAMS:`, and `TEXT:` sections, followed by a body and `.ENDS [subcircuit name]` ([PSpice A/D Reference Guide](https://www.montana.edu/aolson/ee503/pspcref.pdf)).

```spice
.SUBCKT <name> [node]*
+ [OPTIONAL: <<interface node>=<default value>>*]
+ [PARAMS: <<name>=<value>>*]
+ [TEXT: <<name>=<text value>>*]
...
.ENDS [subcircuit name]
```

Cadence describes subcircuit-syntax models as netlists that describe the structure and function of a part plus variable input parameters used to fine-tune the model ([Cadence PSpice User Guide](https://resources.pcb.cadence.com/i/1180526/192)). FlowCAD examples show a PSpice subcircuit using `PARAMS:` defaults and model parameters such as `BV={my_bv(p1,p2,p3)}` inside the body ([FlowCAD .FUNC application note](https://www.flowcad.de/AN/FlowCAD-AN-PSpice-Func-Command.pdf)).

```spice
.SUBCKT my_diode a k PARAMS:
+ p1=3
+ p2=5
+ p3=0
D1 a k my_D
.MODEL my_D D(BV={my_bv(p1,p2,p3)} IBV=.51729)
.ENDS my_diode
```

### `PARAMS:` syntax

`PARAMS:` introduces named default parameters in a PSpice subcircuit definition, and those parameters are used in brace expressions inside the subcircuit body ([FlowCAD .FUNC application note](https://www.flowcad.de/AN/FlowCAD-AN-PSpice-Func-Command.pdf)). PSpice-compatible call syntax may also include `PARAMS:` before call-site overrides, as in `Xamp 5 4 2 ACamplifier PARAMS: Cin=20n Rbias=2.7K` ([YouSpice](https://youspice.com/creating-a-spice-subcircuit-subckt-manually/)).

Parser recommendation:

```ebnf
pspice_subckt :=
  ".SUBCKT" name node*
  ("OPTIONAL:" assignment*)?
  ("PARAMS:" assignment*)?
  ("TEXT:" text_assignment*)?

pspice_xcall :=
  xname node* subckt_name ("PARAMS:"? assignment*)?
```

Subcircuit-name detection for `X` calls should scan from the right for the first non-assignment token that resolves to a known subcircuit, while treating `PARAMS:` as an optional section marker.

### `OPTIONAL:` and `TEXT:`

The PSpice reference grammar includes `OPTIONAL:` for interface nodes with default values and `TEXT:` for text-valued parameters ([PSpice A/D Reference Guide](https://www.montana.edu/aolson/ee503/pspcref.pdf)). A first implementation can parse and preserve these sections even if the simulator backend ignores them, because dropping them may make round-tripping or library conversion unsafe.

---

## ABM: `VALUE`, `TABLE`, `LAPLACE`, `FREQ`, `CHEBYSHEV`, and related forms

PSpice ABM uses `VALUE` and `TABLE` extensions to `E` and `G` devices to express instantaneous voltage- or current-controlled relationships ([Cadence PSpice User Guide](https://resources.pcb.cadence.com/i/1180526-pspice-user-guide/363)). The PSpice reference gives the ABM forms `E|G<name> <(+)<node> <(-) node> VALUE={ <expression> }` and `E|G<name> <(+)<node> <(-) node> TABLE { <expression> } = <<input>,<output>>*` ([PSpice A/D Reference Guide](https://www.montana.edu/aolson/ee503/pspcref.pdf)).

```spice
EGAIN out 0 VALUE={LIMIT(V(in)*10, 0, 5)}
GTABLE out 0 TABLE {V(ctrl)} = (0,0) (1,1m) (5,10m)
```

Cadence documents EVALUE and GVALUE parts as allowing an instantaneous transfer function in standard mathematical notation, where `EXPR` can contain constants, parameters, voltages, currents, or time ([Cadence PSpice User Guide](https://resources.pcb.cadence.com/i/1180526-pspice-user-guide/363)). Cadence notes that voltage references may be `V(5)` or `V(4,5)`, while current references must be current through a voltage source such as `I(VSENSE)` ([Cadence PSpice User Guide](https://resources.pcb.cadence.com/i/1180526-pspice-user-guide/363)). Cadence also notes that nonlinear devices are linearized around the bias point for AC analysis ([Cadence PSpice User Guide](https://resources.pcb.cadence.com/i/1180526-pspice-user-guide/363)).

The PSpice reference also lists ABM forms for `LAPLACE`, `FREQ`, and `CHEBYSHEV` after `E` or `G` controlled-source declarations ([PSpice A/D Reference Guide](https://www.montana.edu/aolson/ee503/pspcref.pdf)). If the existing SPICE backend cannot simulate these forms, the parser should still preserve them and emit an unsupported-feature diagnostic at semantic or lowering time rather than at parse time.

### ABM table semantics

FlowCAD describes the ABM `TABLE` part as a lookup table mapping input values to output values, with linear interpolation between data points and monotonically increasing input values ([FlowCAD ABM TABLE application note](https://www.flowcad.de/AN/FlowCAD-AN-PSpice-ABM-Model-Table.pdf)). FlowCAD states that out-of-range inputs clamp to the output value associated with the smallest or largest input entry ([FlowCAD ABM TABLE application note](https://www.flowcad.de/AN/FlowCAD-AN-PSpice-ABM-Model-Table.pdf)).

FlowCAD also documents a PSpice line-length issue for ETABLE where a table longer than 132 characters can produce `ERROR - Line too long. Limit is 132 characters`, and it recommends wrapping long definitions in a subcircuit with continuation lines as a workaround ([FlowCAD ABM TABLE application note](https://www.flowcad.de/AN/FlowCAD-AN-PSpice-ABM-Model-Table.pdf)).

Implementation guidance:

- Parse both `TABLE {expr} = (x,y) ...` and property-template-generated `TABLE {expr} row row ...` variants where practical.
- Store table points as expressions, not only numeric literals, because vendor models may use parameters.
- Warn when a generated PSpice output line would exceed 132 characters.
- Lower tables to backend-native PWL or table constructs only after checking endpoint clamp semantics.
- Maximum 2048 input/output pairs per TABLE.

---

## Capture and model-library integration

Cadence states that a `PSpiceTemplate` property attached to a symbol is used for generating the PSpice netlist, while `PORT_ORDER` information in the device property file is also used for netlist generation ([Cadence PSpice User Guide](https://resources.pcb.cadence.com/i/1180526-pspice-user-guide/199)). Cadence also states that the model library and device property file must have the same name and be in the same location for that workflow ([Cadence PSpice User Guide](https://resources.pcb.cadence.com/i/1180526-pspice-user-guide/199)).

This matters only if the coding agent is importing OrCAD/Capture assets rather than plain `.cir`, `.lib`, or `.inc` text. For text-model support, the implementation should not require `PSpiceTemplate`, but it should avoid discarding template-related metadata if it appears in auxiliary files.

---

# Commands Reference

## `.AC` — AC analysis

```
.AC <sweep type> <points value> <start frequency value> <end frequency value>
```

| Parameter | Description |
| --- | --- |
| `LIN` | Linear sweep. `<points value>` is total number of points. |
| `OCT` | Logarithmic by octaves. `<points value>` is points per octave. |
| `DEC` | Logarithmic by decades. `<points value>` is points per decade. |
| `<start frequency value>` | Starting frequency; must be > 0. |
| `<end frequency value>` | Ending frequency; must be ≥ start and > 0. |

Notes:
- AC analysis is linear — the circuit is linearized around the bias point.
- Only sources with `AC` specifications are active; `SIN` specs are ignored.
- Results require `.PRINT`, `.PLOT`, or `.PROBE`.

```spice
.AC LIN 101 100Hz 200Hz
.AC OCT 10  1kHz  16kHz
.AC DEC 20  1MEG  100MEG
```

## `.ALIASES` / `.ENDALIASES`

```
.ALIASES
<device name>  <device alias>  (<<pin>=<node>>)
_              _               (<<net>=<node>>)
.ENDALIASES
```

Used by Capture/Probe to map device-and-pin names to raw node numbers. Associates schematic net names with simulator node names. Block must be terminated with `.ENDALIASES`.

```spice
.ALIASES
R_RBIAS  RBIAS (1=$N_0001  2=VDD)
Q_Q3     Q3    (c=$N_0001  b=$N_0001  e=VEE)
_        _     (OUT=$N_0007)
.ENDALIASES
```

## `.AUTOCONVERGE`

```
.AUTOCONVERGE <option1>=<relaxed value> [<option2>=<relaxed value> ...]
+             [RESTART=0]
```

Options that can be relaxed: `ITL1`, `ITL2`, `ITL4`, `RELTOL`, `ABSTOL`, `VNTOL`, `PIVTOL`. Relaxed values must be numerically more relaxed than the simulator defaults. `RESTART=0` restarts from T=0 with relaxed values if convergence fails.

```spice
.AUTOCONVERGE ITL1=1000 RELTOL=0.05 ABSTOL=.001
```

## `.CHKPT` — generate checkpoints

```
.CHKPT <CheckPoint_name> <time_interval_type> <time_interval_value>
+      [<time_interval_type> <time_interval_value>]
+      [TSTEP <timestep>]
```

| Parameter | Description |
| --- | --- |
| `SINT` | Simulation time interval |
| `RINT` | Real (wall-clock) time interval |
| `TP` | Explicit time points (comma-separated) |

```spice
.CHKPT "D:/simdata/checkset1"  SINT 1ms  RINT 10min  TP 2.5ms,7.5ms
```

## `.DC` — DC analysis

### Linear sweep
```
.DC [LIN] <sweep variable name> <start value> <end value> <increment value>
+   [nested sweep specification]
```

### Logarithmic sweep
```
.DC <DEC|OCT> <sweep variable name> <start value> <end value> <points value>
+   [nested sweep specification]
```

### List sweep
```
.DC <sweep variable name> LIST <value> [<value> ...]
+   [nested sweep specification]
```

Sweep variable types:

| Type | Syntax | Meaning |
| --- | --- | --- |
| Independent source | Source name (e.g. `VIN`) | Source value set to sweep value |
| Model parameter | `MODTYPE MODNAME(PARAM)` | Model parameter set to sweep value |
| Temperature | `TEMP` | Circuit temperature set to sweep value |
| Global parameter | `PARAM <name>` | Global parameter set to sweep value |

Notes:
- `<start value>` can be greater or less than `<end value>` for linear sweeps.
- For logarithmic sweeps, `<start value>` must be positive and less than `<end value>`.
- For nested sweeps, the first sweep is the inner loop.

```spice
.DC VIN -.25 .25 .05
.DC LIN I2 5mA -2mA 0.1mA
.DC VCE 0V 10V .5V  IB 0mA 1mA 50uA
.DC RES RMOD(R) 0.9 1.1 .001
.DC DEC NPN QFAST(IS) 1E-18 1E-14 5
.DC TEMP LIST 0 20 27 50 80 100  PARAM Vsupply 7.5 15 .5
```

## `.DISTRIBUTION` — user-defined distribution

```
.DISTRIBUTION <name>  (<deviation> <probability>)*
```

- `<deviation>`: X-axis value in (-1, +1), monotonically non-decreasing. Up to 100 pairs.
- `<probability>`: relative probability, ≥ 0.
- Used only with Monte Carlo and sensitivity/worst-case analyses.
- Area normalized to 1.0.

```spice
.DISTRIBUTION bi_modal  (-1,1) (-.5,1) (-.5,0) (.5,0) (.5,1) (1,1)
.DISTRIBUTION triangular (-1,0) (0,1) (1,0)
```

## `.DMFACTOR`

```
.DMFACTOR <value>
```

Sets the relative factor by which the minimum transient time step size is scaled. Default minimum time step is 10⁻¹⁸. Only meaningful values are factors of 10 (e.g. 0.1, 0.01).

## `.END` — end of circuit

```
.END
```

Marks the end of the circuit file. Required.

## `.ENDS` — end subcircuit

```
.ENDS [subcircuit name]
```

Marks the end of a `.SUBCKT` definition. The optional subcircuit name, if present, must match the name in the corresponding `.SUBCKT`.

## `.EXTERNAL` — external port

```
.EXTERNAL <attribute> <node-name>*
```

`<attribute>`: `INPUT`, `OUTPUT`, or `BIDIRECTIONAL`. Applies only to nodes with digital devices. Nodes marked `.EXTERNAL` are primary observation points for timing-violation analysis.

```spice
.EXTERNAL INPUT        Data1, Data2, Data3
.EXTERNAL OUTPUT       P1
.EXTERNAL BIDIRECTIONAL BPort1 BPort2
```

## `.FOUR` — Fourier analysis

```
.FOUR <frequency value> [<no. harmonics value>] <output variable>*
```

- Requires `.TRAN`; uses the final 1/`<frequency value>` seconds of transient.
- Default harmonics: 9 (DC + fundamental + harmonics 2–9).
- Results written to `.out` file only — not viewable in Probe.

```spice
.FOUR 10kHz          V(5) V(6,7) I(VSENS3)
.FOUR 60Hz  20       V(17)
```

## `.IC` — initial bias point condition

```
.IC <V(<node> [,<node>])=<value>>*
.IC <I(<inductor>)=<value>>*
```

- Sets node voltages and inductor currents during bias point calculation.
- Overrides `.NODESET` for the same node if both are present.
- PSpice enforces voltage by attaching a source with 0.0002 Ω series resistance.

```spice
.IC V(2)=3.4  V(102)=0  V(3)=-1V  I(L1)=2uAmp
.IC V(InPlus,InMinus)=1e-3  V(100,133)=5.0V
```

## `.INC` — include file

```
.INC <file name>
```

- Included file contents treated as inline in the parent.
- Cannot contain a title line.
- Nesting supported up to 4 levels deep.

```spice
.INC "SETUP.CIR"
.INC "C:\LIB\VCO.CIR"
```

## `.LIB` — library file

```
.LIB [file_name]
```

- Library files may contain only: comments, `.MODEL`, `.SUBCKT`, `.PARAM`, `.FUNC`, and `.LIB`.
- If `file_name` omitted, references the master library `nom.lib`.
- PSpice builds an index file on first use; must be regenerated when library modified.

```spice
.LIB
.LIB linear.lib
.LIB "C:\lib\bipolar.lib"
```

## `.LOADBIAS` — load bias point file

```
.LOADBIAS <"file name">
```

Loads a bias point file (typically from `.SAVEBIAS`) containing `.NODESET` commands.

```spice
.LOADBIAS "SAVETRAN.NOD"
```

## `.MC` — Monte Carlo analysis

```
.MC <#runs> <analysis> <output variable> <function>
+   [LIST]
+   [OUTPUT <ALL | FIRST <N> | EVERY <N> | RUNS <N>*>]
+   [RANGE(<low>, <high>)]
+   [SEED=<value>]
```

| Parameter | Description |
| --- | --- |
| `<#runs>` | Total runs. Max 2000 (printed) / 400 (Probe). |
| `<analysis>` | `DC`, `AC`, or `TRAN` |
| `<function>` | `YMAX`, `MAX`, `MIN`, `RISE_EDGE(<value>)`, `FALL_EDGE(<value>)` |
| `SEED` | Odd integer 1–32767; default 17533 |

Run 1 always uses nominal values; subsequent runs vary `DEV`/`LOT` tolerances.

```spice
.MC 10  TRAN  V(5)   YMAX
.MC 50  DC    IC(Q7)  YMAX  LIST
.MC 20  AC    VP(13,5) YMAX  LIST  OUTPUT ALL
```

## `.NODESET` — set approximate node voltage

```
.NODESET <V(<node> [,<node>])=<value>>*
.NODESET <I(<inductor>)=<value>>*
```

Provides initial guess (not a clamp) for bias point. `.IC` overrides `.NODESET` for the same node.

```spice
.NODESET V(2)=3.4 V(102)=0 V(3)=-1V I(L1)=2uAmp
```

## `.NOISE` — noise analysis

```
.NOISE V(<node> [,<node>]) <name> [interval value]
```

- Requires `.AC`; computed at every frequency in the AC sweep.
- `<name>` is an independent V or I source defining the input reference.
- Optional `[interval value]`: every Nth frequency, print per-device noise table.

```spice
.NOISE V(5)          VIN
.NOISE V(101)        VSRC  20
.NOISE V(4,5)        ISRC
```

## `.OP` — bias point

```
.OP
```

The bias point is always calculated regardless. With `.OP`, small-signal linearized parameters of all nonlinear devices are also printed.

## `.OPTIONS` — analysis options

```
.OPTIONS [option name]* [<option name>=<value>]*
```

### Flag options (default off)

| Flag | Meaning |
| --- | --- |
| `ACCT` | Print summary and accounting info |
| `EXPAND` | List devices from subcircuit expansion |
| `LIBRARY` | List lines used from library files |
| `LIST` | List circuit element summary |
| `NOBIAS` | Suppress bias point node voltages |
| `NODE` | List node connection summary |
| `NOECHO` | Suppress input file listing |
| `NOICTRANSLATE` | Suppress IC attribute translation |
| `NOMOD` | Suppress model parameter listing |
| `NOOUTMSG` | Suppress error messages in output |
| `NOPAGE` | Suppress paging and banners |
| `NOPRBMSG` | Suppress error messages in Probe |
| `NOREUSE` | Suppress auto bias save/restore |
| `OPTS` | List all option values |
| `STEPGMIN` | Enable GMIN stepping for convergence |

### Named-value options

| Option | Description | Default |
| --- | --- | --- |
| `ABSTOL` | Best accuracy of currents (A) | 1.0 pA |
| `CHGTOL` | Best accuracy of charges (C) | 0.01 pC |
| `CPTIME` | CPU time limit (s) | 0 (infinity) |
| `DEFAD` | MOSFET default drain area (m²) | 0.0 |
| `DEFAS` | MOSFET default source area (m²) | 0.0 |
| `DEFL` | MOSFET default length (m) | 100u |
| `DEFW` | MOSFET default width (m) | 100u |
| `DIGFREQ` | Min digital time step = 1/DIGFREQ (Hz) | 10 GHz |
| `DIGDRVF` | Min drive resistance (Ω) | 2.0 |
| `DIGDRVZ` | Max drive resistance (Ω) | 20K |
| `DIGERRDEFAULT` | Default digital constraint error limit | 20.0 |
| `DIGERRLIMIT` | Max digital error message limit | 0 (infinity) |
| `DIGINITSTATE` | Initial state of flip-flops: 0=clear, 1=set, 2=X | 2 |
| `DIGIOLVL` | Default I/O level: 1–4 | 1 |
| `DIGMNTYMX` | Delay selector: 1=min, 2=typ, 3=max, 4=min/max | 2 |
| `DIGMNTYSCALE` | Min delay scale from typical | 0.4 |
| `DIGOVRDRV` | Drive resistance override ratio | 3.0 |
| `DIGTYMXSCALE` | Max delay scale from typical | 1.6 |
| `DISTRIBUTION` | Default Monte Carlo distribution | UNIFORM |
| `GMIN` | Minimum branch conductance (Ω⁻¹) | 1E-12 |
| `ITL1` | DC/bias blind iteration limit | 150 |
| `ITL2` | DC/bias educated-guess limit | 20 |
| `ITL4` | Iterations per transient point | 10 |
| `ITL5` | Total transient iteration limit | 0 (infinity) |
| `LIMPTS` | Max print/plot points | 0 (infinity) |
| `NUMDGT` | Print table digits (max 8) | 4 |
| `PIVREL` | Relative pivot magnitude | 1E-3 |
| `PIVTOL` | Absolute pivot magnitude | 1E-13 |
| `RELTOL` | Relative V/I accuracy | 0.001 |
| `SOLVER` | Solution algorithm: 0=original, 1=advanced | 0 |
| `TNOM` | Nominal temperature (°C) | 27.0 |
| `VNTOL` | Best accuracy of voltages (V) | 1.0 uV |
| `WIDTH` | Output line width (80 or 132) | 80 |

The `.OPTIONS` command is cumulative. `ABSTOL`, `GMIN`, `ITL4`, `RELTOL`, `VNTOL` can use `SCHEDULE()` for time-varying values during transient.

```spice
.OPTIONS NOECHO NOMOD DEFL=12u DEFW=8u
.OPTIONS ACCT RELTOL=.01
.OPTIONS DISTRIBUTION=GAUSS
.OPTIONS RELTOL={SCHEDULE(0s,.001,2s,.005)}
```

## `.PLOT` — plot

```
.PLOT <analysis type> [output variable]*
+     ( [<lower limit>, <upper limit>] )*
```

- `<analysis type>`: `DC`, `AC`, `NOISE`, `TRAN`.
- Max 8 output variables per `.PLOT`.
- Produces text-character line-printer plots in the output file.

```spice
.PLOT DC V(3) V(2,3) V(R1) I(VIN)
.PLOT AC VM(2) VP(2) VM(3,4) VDB(5)
.PLOT TRAN V(3) V(2,3) (0,5V) ID(M2) I(VCC) (-50mA,50mA)
```

## `.PRINT` — print

```
.PRINT[/DGTLCHG] <analysis type> [output variable]*
```

- `/DGTLCHG`: print row on any digital state change (digital only).
- `<analysis type>`: `DC`, `AC`, `NOISE`, `TRAN`.
- No count limit on variables.

```spice
.PRINT DC V(3) V(2,3) V(R1) I(VIN)
.PRINT AC VM(2) VP(2) VDB(5)
.PRINT TRAN V(3) V(2,3) ID(M2) I(VCC)
.PRINT/DGTLCHG TRAN QA QB RESET
```

## `.PROBE` — Probe output

```
.PROBE[/CSDF] [output variable]*
```

- `/CSDF`: Common Simulation Data File (text) format instead of binary.
- If no variables listed, all node voltages and device currents are saved.
- No analysis type prefix needed (unlike `.PRINT`/`.PLOT`).

### Output variable syntax

| Form | Meaning |
| --- | --- |
| `V(<node>)` | Voltage at node |
| `V(<+node>, <-node>)` | Voltage between two nodes |
| `V(<name>)` | Voltage across two-terminal device |
| `Vx(<name>)` | Voltage at terminal x (B/C/D/E/G/S) |
| `Vxy(<name>)` | Voltage across two terminals |
| `I(<name>)` | Current through two-terminal device |
| `Ix(<name>)` | Current into terminal x |
| `W(<name>)` | Power dissipation |
| `D(<name>)` | Digital value (transient only) |
| `V(*)` / `I(*)` / `W(*)` / `D(*)` | All of type |

### AC analysis suffixes (`.PRINT`/`.PLOT` only)

| Suffix | Meaning |
| --- | --- |
| `M` | Magnitude |
| `DB` | Magnitude in dB |
| `P` | Phase in degrees |
| `R` | Real part |
| `I` | Imaginary part |
| `G` | Group delay |

### Noise variables

`INOISE`, `ONOISE`, `DB(INOISE)`, `DB(ONOISE)`

```spice
.PROBE
.PROBE V(3) V(2,3) I(VIN) IB(Q13)
.PROBE/CSDF
```

## `.RESTART` — restart from checkpoint

```
.RESTART <checkPoint_name> <state_number> [0|1]
```

```spice
.restart "D:/simdata/checkset1" state20
```

## `.SAVEBIAS` — save bias point

```
.SAVEBIAS <"file_name"> <[OP] [TRAN] [DC]> [NOSUBCKT]
+         [TIME=<value> [REPEAT]] [TEMP=<value>]
+         [STEP=<value>] [MCRUN=<value>] [DC=<value>]
+         [DC1=<value>] [DC2=<value>]
```

One analysis type per `.SAVEBIAS`; one per analysis type per circuit.

```spice
.SAVEBIAS "OPPOINT" OP
.SAVEBIAS "TRANDATA.BSP" TRAN NOSUBCKT TIME=10u
.SAVEBIAS "SAVEDC.BSP" DC MCRUN=3 DC1=3.5 DC2=100
```

## `.SENS` — sensitivity analysis

```
.SENS <output variable>*
```

DC sensitivity of each output to all device values and model parameters. Results in output file only. Current outputs must be through a voltage source.

```spice
.SENS V(9) V(4,3) V(17) I(VCC)
```

## `.STEP` — parametric analysis

```
.STEP [LIN] <sweep variable name> <start> <end> <increment>
.STEP DEC|OCT <sweep variable name> <start> <end> <points>
.STEP <sweep variable name> LIST <value>*
```

Sweep variable types same as `.DC`: source name, `MODTYPE MODNAME(PARAM)`, `TEMP`, `PARAM <name>`.

All standard analyses repeated for each step value. Cannot step the same variable as `.DC`.

```spice
.STEP VCE 0V 10V .5V
.STEP RES RMOD(R) 0.9 1.1 .001
.STEP DEC NPN QFAST(IS) 1E-18 1E-14 5
.STEP TEMP LIST 0 20 27 50 80 100
.STEP PARAM CenterFreq 9.5kHz 10.5kHz 50Hz
```

## `.STIMLIB` — stimulus library file

```
.STIMLIB <file name[.stl]>
```

References a file containing `.STIMULUS` commands (typically created by StmEd).

## `.STIMULUS` — stimulus

```
.STIMULUS <stimulus name> <type> <type-specific parameters>*
```

Encompasses only the transient waveform portion of V/I device syntax. Types: `PULSE`, `SIN`, `EXP`, `PWL`, `SFFM`, `STIM`.

```spice
.STIMULUS InputPulse PULSE (-1mv 1mv 2ns 2ns 50ns 100ns)
.STIMULUS 50KHZSIN SIN (0 5 50KHZ 0 0 0)
```

## `.TEMP` — temperature

```
.TEMP <temperature value>*
```

One or more temperatures in °C. All analyses repeated for each. Behaves like `.STEP TEMP LIST`.

```spice
.TEMP 0 27 125
```

## `.TEXT` — text parameter

```
.TEXT <name>=<text value>
```

Defines text-valued parameters for use in `.SUBCKT` `TEXT:` sections and for JEDEC file references in PLDs.

## `.TF` — transfer function

```
.TF <output variable> <input source name>
```

Computes small-signal DC gain, input resistance, and output resistance. Results in output file only.

```spice
.TF V(5) VIN
.TF I(VDRIV) ICNTRL
```

## `.TRAN` — transient analysis

```
.TRAN[/OP] <print step> <final time>
+          [<no-print value> [<step ceiling>]] [SKIPBP]
```

| Parameter | Description |
| --- | --- |
| `/OP` | Print detailed bias point info for transient |
| `<print step>` | Output time interval for `.PRINT`/`.PLOT`/`.FOUR` |
| `<final time>` | End time (TSTOP) |
| `<no-print value>` | Suppress output until this time |
| `<step ceiling>` | Override internal step ceiling (default TSTOP/50) |
| `SKIPBP` | Skip transient bias point; use IC= on devices |

Step ceiling can use `SCHEDULE()` for time-varying values:
```
.TRAN 1ns 100ns 0ns {SCHEDULE(0,1ns,25ns,.1ns)}
```

```spice
.TRAN 1ns 100ns
.TRAN/OP 1ns 100ns 20ns SKIPBP
.TRAN 1ns 100ns 0ns .1ns
```

## `.VECTOR` — digital output

```
.VECTOR <number of nodes> <node>*
+       [POS=<column>] [FILE=<filename>]
+       [RADIX="Binary"|"Hex"|"Octal" [BIT=<bit index>]]
+       [SIGNAMES=<signal names>]
```

Output format is identical to digital file stimulus device (FSTIM).

```spice
.VECTOR 1 CLOCK SIGNAMES=SYSCLK
.VECTOR 4 DATA3 DATA2 DATA1 DATA0
```

## `.WATCH` — watch analysis results

```
.WATCH [DC][AC][TRAN]
+      [<output variable> [<lower limit>,<upper limit>]]*
```

Up to 8 variables per `.WATCH`. If range exceeded, simulator pauses.

```spice
.WATCH DC V(3) (-1V,4V) V(2,3)
.WATCH TRAN VBE(Q13) (0V,5V) ID(M2) I(VCC) (0,500mA)
```

## `.WCASE` — sensitivity/worst-case analysis

```
.WCASE <analysis> <output variable> <function> [option]*
```

| Function | Meaning |
| --- | --- |
| `YMAX` | Abs. greatest difference from nominal |
| `MAX` | Maximum value |
| `MIN` | Minimum value |
| `RISE_EDGE(<v>)` | First crossing above threshold |
| `FALL_EDGE(<v>)` | First crossing below threshold |

| Option | Meaning |
| --- | --- |
| `LIST` | Print updated parameters per run |
| `OUTPUT ALL` | Output from all sensitivity runs |
| `RANGE(<low>,<high>)` | Restrict evaluation range |
| `HI` / `LOW` | Worst-case direction |
| `VARY DEV` / `VARY LOT` / `VARY BOTH` | Which tolerances to vary |
| `DEVICES <list>` | Restrict to device types |

Cannot use both `.MC` and `.WCASE` in the same circuit.

```spice
.WCASE TRAN V(5) YMAX
.WCASE DC IC(Q7) YMAX VARY DEV
.WCASE AC VP(13,5) YMAX DEVICES RQ OUTPUT ALL
```

---

# Analog Device Reference

## Device summary

| Device Type | Letter | Model Type(s) |
| --- | --- | --- |
| Bipolar transistor | Q | NPN, PNP, LPNP |
| Capacitor | C | CAP |
| Diode | D | D |
| GaAsFET | B | GASFET |
| IGBT | Z | NIGBT |
| Inductor | L | IND |
| Inductor coupling | K | CORE |
| Junction FET | J | NJF, PJF |
| MOSFET | M | NMOS, PMOS |
| Resistor | R | RES |
| Subcircuit instantiation | X | — |
| Transmission line | T | TRN |
| Voltage-controlled switch | S | VSWITCH |
| Current-controlled switch | W | ISWITCH |
| VCVS | E | — |
| VCCS | G | — |
| CCCS | F | — |
| CCVS | H | — |
| Independent voltage source | V | — |
| Independent current source | I | — |

## R — Resistor

```
R<name> <(+)> <(-)> [model name] <value> [TC=<TC1>[,<TC2>]]
```

Model: `.MODEL <name> RES`

| Parameter | Description | Default |
| --- | --- | --- |
| R | Resistance multiplier | 1.0 |
| TC1 | Linear temperature coefficient (°C⁻¹) | 0.0 |
| TC2 | Quadratic temperature coefficient (°C⁻²) | 0.0 |
| TCE | Exponential temperature coefficient (%/°C) | 0.0 |
| T_ABS / T_MEASURED / T_REL_GLOBAL / T_REL_LOCAL | Temperature params | — |

Value formula: if TCE specified: `value · R · 1.01^(TCE·(T-Tnom))`; else: `value · R · (1 + TC1·ΔT + TC2·ΔT²)`.

Noise: `i² = 4kT/R`. PSpice extension: TCE (exponential TC).

```spice
RLOAD 15 0 2K
R2 1 2 2.4E4 TC=.015,-.003
RFDBCK 3 33 RMOD 10K
```

## C — Capacitor

```
C<name> <(+)> <(-)> [model name] <value> [IC=<initial value>]
```

Model: `.MODEL <name> CAP`

| Parameter | Description | Default |
| --- | --- | --- |
| C | Capacitance multiplier | 1.0 |
| TC1 | Linear temperature coefficient (°C⁻¹) | 0.0 |
| TC2 | Quadratic temperature coefficient (°C⁻²) | 0.0 |
| VC1 | Linear voltage coefficient (V⁻¹) | 0.0 |
| VC2 | Quadratic voltage coefficient (V⁻²) | 0.0 |
| T_ABS / T_MEASURED / T_REL_GLOBAL / T_REL_LOCAL | Temperature params | — |

Value: `value · C · (1+VC1·V+VC2·V²) · (1+TC1·ΔT+TC2·ΔT²)`. Value must not be zero. PSpice extension: VC1/VC2 voltage-dependent capacitance.

```spice
CLOAD 15 0 20pF
C2 1 2 .2E-12 IC=1.5V
CFDBCK 3 33 CMOD 10pF
```

## L — Inductor

```
L<name> <(+)> <(-)> [model name] <value> [IC=<initial value>]
```

Winding form (with core model):
```
L<name> <+> <-> <TURNS> [RESIS=<val>] [IC=<val>]
```

Model: `.MODEL <name> IND`

| Parameter | Description | Default |
| --- | --- | --- |
| L | Inductance multiplier | 1.0 |
| IL1 | Linear current coefficient (A⁻¹) | 0.0 |
| IL2 | Quadratic current coefficient (A⁻²) | 0.0 |
| TC1 | Linear temperature coefficient (°C⁻¹) | 0.0 |
| TC2 | Quadratic temperature coefficient (°C⁻²) | 0.0 |
| T_ABS / T_MEASURED / T_REL_GLOBAL / T_REL_LOCAL | Temperature params | — |

Value: `value · L · (1+IL1·I+IL2·I²) · (1+TC1·ΔT+TC2·ΔT²)`. PSpice extensions: IL1/IL2 (current-dependent inductance), winding form with RESIS.

```spice
LLOAD 15 0 20mH
LSENSE 5 12 2UH IC=2mA
L1 2 0 103 resis=40m   ; 103 turns, 40mΩ winding resistance
```

## D — Diode

```
D<name> <(+)> <(-)> <model name> [area value]
```

Model: `.MODEL <name> D`

[area value] scales IS, ISR, IKF, RS, CJO, IBV. Default = 1.

Key model parameters:

| Parameter | Description | Default |
| --- | --- | --- |
| IS | Saturation current (A) | 1E-14 |
| N | Emission coefficient | 1.0 |
| RS | Parasitic resistance (Ω) | 0.0 |
| BV | Reverse breakdown voltage (V) | infinite |
| IBV | Reverse breakdown current (A) | 1E-10 |
| CJO | Zero-bias capacitance (F) | 0.0 |
| VJ | Junction potential (V) | 1.0 |
| M | Grading coefficient | 0.5 |
| TT | Transit time (s) | 0.0 |
| EG | Bandgap (eV) | 1.11 |
| XTI | IS temperature exponent | 3.0 |
| IKF | High-injection knee current (A) | infinite |
| ISR | Recombination current (A) | 0.0 |
| NR | ISR emission coefficient | 2.0 |
| NBV / NBVL | Breakdown ideality factors | 1.0 |
| TBV1/TBV2/TIKF/TRS1/TRS2 | Temperature coefficients | 0.0 |
| FC | Forward-bias cap coefficient | 0.5 |
| AF / KF | Flicker noise parameters | 1.0 / 0.0 |
| T_ABS / T_MEASURED / T_REL_GLOBAL / T_REL_LOCAL | Temperature params | — |

```spice
DCLAMP 14 0 DMOD
D13 15 17 SWITCH 1.5
```

## Q — Bipolar transistor

```
Q<name> <collector> <base> <emitter> [<substrate>] <model name> [AREA=<value>]
```

Model: `.MODEL <name> NPN|PNP|LPNP`

LEVEL=1: Standard Gummel-Poon (default). LEVEL=2: Extended Mextram-style (PSpice-specific).

Key Level 1 parameters:

| Parameter | Description | Default |
| --- | --- | --- |
| BF | Ideal max forward beta | 100 |
| BR | Ideal max reverse beta | 1.0 |
| IS | Transport saturation current (A) | 1E-16 |
| NF / NR | Forward/reverse emission coefficients | 1.0 |
| VAF (VA) | Forward Early voltage (V) | infinite |
| VAR (VB) | Reverse Early voltage (V) | infinite |
| IKF (IK) | Forward high-current corner (A) | infinite |
| IKR | Reverse high-current corner (A) | infinite |
| ISE (C2) | B-E leakage current (A) | 0.0 |
| ISC (C4) | B-C leakage current (A) | 0.0 |
| NE / NC | Leakage emission coefficients | 1.5 / 2.0 |
| RB | Max base resistance (Ω) | 0.0 |
| RBM | Min base resistance (Ω) | RB |
| RC | Collector resistance (Ω) | 0.0 |
| RE | Emitter resistance (Ω) | 0.0 |
| CJC / CJE / CJS | Junction capacitances (F) | 0.0 |
| VJC/VJE/VJS | Junction potentials (V) | 0.75 |
| MJC/MJE/MJS | Grading factors | 0.33/0.33/0.0 |
| TF / TR | Transit times (s) | 0.0 |
| XCJC | Fraction of CJC internal to Rb | 1.0 |
| PTF | Excess phase at 1/(2·TF) Hz (deg) | 0.0 |
| AF / KF | Flicker noise | 1.0 / 0.0 |
| T_ABS / T_MEASURED / T_REL_GLOBAL / T_REL_LOCAL | Temperature params | — |

PSpice extensions: LPNP model type, Level 2 (Mextram-style), XCJC2, quasi-saturation parameters (RCO, QCO, GAMMA, CN, D, VG, VO), temperature coefficients TRB1/TRB2/TRC1/TRC2/TRE1/TRE2/TRM1/TRM2.

```spice
Q1 14 2 13 QMOD
Q2 C B E SUB QFAST 2.0
```

## J — Junction FET

```
J<name> <drain> <gate> <source> <model name> [area value]
```

Model: `.MODEL <name> NJF|PJF`

[area value] scales RD, RS, IS, ISR, CGS, CGD. Default = 1.0.

Key parameters:

| Parameter | Description | Default |
| --- | --- | --- |
| VTO | Threshold voltage (V) | -2.0 |
| BETA | Transconductance (A/V²) | 1E-4 |
| LAMBDA | Channel-length modulation (V⁻¹) | 0 |
| IS | Gate saturation current (A) | 1E-14 |
| RD / RS | Ohmic resistances (Ω) | 0 |
| CGD / CGS | Gate capacitances (F) | 0 |
| PB | Gate potential (V) | 1.0 |
| AF / KF | Flicker noise | 1 / 0 |
| BETATCE | BETA temp coefficient (%/°C) | 0 |
| T_ABS / T_MEASURED / T_REL_GLOBAL / T_REL_LOCAL | Temperature params | — |

PSpice extensions: BETATCE, ISR/NR, ALPHA/VK (ionization).

```spice
JIN 100 1 0 JFAST
J13 22 14 23 JNOM 2.0
```

## M — MOSFET

```
M<name> <drain> <gate> <source> <bulk> <model name>
+ [L=<value>] [W=<value>]
+ [AD=<value>] [AS=<value>]
+ [PD=<value>] [PS=<value>]
+ [NRD=<value>] [NRS=<value>] [NRG=<value>] [NRB=<value>]
+ [M=<value>]
```

Model: `.MODEL <name> NMOS|PMOS`

### LEVEL parameter

| LEVEL | Model |
| --- | --- |
| 1 | Shichman-Hodges |
| 2 | Grove-Frohman |
| 3 | Semi-empirical |
| 4 | BSIM1 |
| 5 | EKV 2.6 (PSpice-specific) |
| 6 | BSIM3v2 (PSpice-specific) |
| 7 | BSIM3v3.2 (PSpice-specific) |
| 8 | BSIM4v4.1 (PSpice-specific) |

Key common parameters:

| Parameter | Description | Default |
| --- | --- | --- |
| LEVEL | Model index | 1 |
| VTO | Threshold voltage (V) | 0 |
| KP | Transconductance (A/V²) | 2E-5 |
| GAMMA | Bulk threshold (V½) | computed |
| PHI | Surface potential (V) | 0.6 |
| LAMBDA | Channel-length modulation (V⁻¹) | 0 |
| TOX | Oxide thickness (m) | computed |
| UO | Surface mobility (cm²/V·s) | 600 |
| L / W | Channel length/width (m) | DEFL/DEFW |
| RD / RS | Ohmic resistances (Ω) | 0 |
| CBD / CBS | Junction capacitances (F) | 0 |
| CJ | Bottom capacitance/area (F/m²) | 0 |
| CJSW | Sidewall capacitance/length (F/m) | 0 |
| CGDO / CGSO / CGBO | Overlap capacitances | 0 |
| IS / JS / JSSW | Saturation currents | 1E-14 / 0 / 0 |
| AF / KF | Flicker noise | 1 / 0 |
| NLEV | Noise equation selector | 2 |
| T_ABS / T_MEASURED / T_REL_GLOBAL / T_REL_LOCAL | Temperature params | — |

PSpice extensions: LEVEL 5–8, NRG/NRB, device multiplier M, NLEV/GDSNOI.

```spice
M1 14 2 13 0 PMOD L=5u W=50u
M2 3 5 4 4 NMOD W=10u L=1u AD=100p AS=100p
```

## B — GaAsFET

```
B<name> <drain> <gate> <source> <model name> [AREA=<value>]
```

Model: `.MODEL <name> GASFET`

### LEVEL parameter

| LEVEL | Model |
| --- | --- |
| 1 | Curtice |
| 2 | Raytheon/Statz |
| 3 | TOM (PSpice-specific) |
| 4 | Parker-Skellern (PSpice-specific) |
| 5 | TOM-2 (PSpice-specific) |
| 6 | TOM-3 (PSpice-specific) |

Key parameters:

| Parameter | Description | Default |
| --- | --- | --- |
| VTO | Pinchoff voltage (V) | -2.5 |
| BETA | Transconductance (A/V²) | 0.1 |
| ALPHA | Saturation voltage parameter (V⁻¹) | 2.0 |
| LAMBDA | Channel-length modulation (V⁻¹) | 0 |
| RD / RG / RS | Ohmic resistances (Ω) | 0 |
| CGD / CGS / CDS | Capacitances (F) | 0 |
| IS | Gate saturation current (A) | 1E-14 |
| BETATCE | BETA temp coefficient (%/°C) | 0 |
| T_ABS / T_MEASURED / T_REL_GLOBAL / T_REL_LOCAL | Temperature params | — |

```spice
BFET 100 1 0 GMOD
B13 22 14 23 TOM3_MOD AREA=2.0
```

## Z — IGBT (PSpice-specific)

```
Z<name> <collector> <gate> <emitter> <model name>
+ [AREA=<value>] [WB=<value>] [AGD=<value>] [KP=<value>] [TAU=<value>]
```

Model: `.MODEL <name> NIGBT`

Key parameters:

| Parameter | Description | Default |
| --- | --- | --- |
| VTO | Threshold voltage (V) | 1.0 |
| KP | MOS transconductance (A/V²) | 0.38 |
| BF | Max forward beta | 9.0 |
| AREA | Device area (m²) | 1E-5 |
| WB | Base width (m) | 9E-5 |
| AGD | Gate-drain overlap area (m²) | 5E-6 |
| TAU | Recombination lifetime (s) | 7.1E-6 |
| CGS / COXD / CJC / CJE | Capacitances | various |
| NB | Base doping (cm⁻³) | 2E14 |
| MUN / MUP | Mobilities (cm²/V·s) | 1500 / 450 |
| THETA | Transverse field factor (V⁻¹) | 0.02 |

AGD, AREA, KP, TAU, WB can be specified at both device and model level; device-level takes precedence.

```spice
ZDRIVE 1 4 2 IGBTA AREA=10.1u WB=91u AGD=5.1u KP=0.381
```

## E — VCVS (Voltage-Controlled Voltage Source)

```
E<name> <(+)> <(-)> <(+)ctrl> <(-)ctrl> <gain>

E<name> <(+)> <(-)> POLY(<n>) <(+)ctrl> <(-)ctrl>* <coeff>*

E<name> <(+)> <(-)> VALUE = {<expression>}

E<name> <(+)> <(-)> TABLE {<expression>} = <in>,<out>*

E<name> <(+)> <(-)> LAPLACE {<expression>} = {<transform>}

E<name> <(+)> <(-)> FREQ {<expression>} = [KEYWORD]
+ <freq>,<mag>,<phase>*  [DELAY=<value>]

E<name> <(+)> <(-)> CHEBYSHEV {<expression>} =
+ <LP|HP|BP|BR>,<cutoff freqs>*,<attenuation>*

E<name> <(+)> <(-)> F = {<expression>}   ; Flux source (PSpice extension)
```

PSpice extensions: VALUE, TABLE, LAPLACE, FREQ, CHEBYSHEV, flux source (F=), error= callback.

FREQ keywords: `MAG` (raw), `DB` (dB, default), `RAD` (radians), `DEG` (degrees, default), `R_I` (real/imaginary).

```spice
EBUFF 10 11 1 2 1.0
EAMP 13 0 POLY(1) 26 0 0 500
ESQROOT 5 0 VALUE = {5V*SQRT(V(3,2))}
ET2 2 0 TABLE {V(ANODE,CATHODE)} = (0,0) (30,1)
ERC 5 0 LAPLACE {V(10)} = {1/(1+.001*s)}
ELOWPASS 5 0 CHEBYSHEV {V(10)} = LP 800 1.2K .1dB 50dB
```

## G — VCCS (Voltage-Controlled Current Source)

Same syntax as E, substituting G for E. Additionally supports charge source form:

```
G<name> <(+)> <(-)> Q = {<expression>}   ; Charge source (PSpice extension)
```

Output current = dq/dt.

```spice
GBUFF 10 11 1 2 1.0
GPSK 11 6 VALUE = {5MA*SIN(6.28*10kHz*TIME+V(3))}
Gbc 2b 0 Q={(1e-3)*V(2b)}
```

## F — CCCS (Current-Controlled Current Source)

```
F<name> <(+)> <(-)> <controlling V device> <gain>
F<name> <(+)> <(-)> POLY(<n>) <V device>* <coeff>*
```

Controlling source must be an independent voltage source. No ABM forms (VALUE/TABLE/LAPLACE).

```spice
FSENSE 1 2 VSENSE 10.0
FAMP 13 0 POLY(1) VIN 0 500
```

## H — CCVS (Current-Controlled Voltage Source)

```
H<name> <(+)> <(-)> <controlling V device> <transresistance>
H<name> <(+)> <(-)> POLY(<n>) <V device>* <coeff>*
```

Same as F but generates a voltage.

```spice
HFDBCK 5 0 VSENSE 5K
```

## V / I — Independent voltage / current sources

```
V<name> <(+)> <(-)> [[DC] <value>] [AC <mag> [phase]] [transient spec]
I<name> <(+)> <(-)> [[DC] <value>] [AC <mag> [phase]] [transient spec]
```

### Transient stimulus forms

#### EXP
```
EXP(<v1> <v2> <td1> <tc1> <td2> <tc2>)
```

| Parameter | Default |
| --- | --- |
| v1 (initial) | required |
| v2 (peak) | required |
| td1 (rise delay) | 0 |
| tc1 (rise τ) | TSTEP |
| td2 (fall delay) | td1+TSTEP |
| tc2 (fall τ) | TSTEP |

#### PULSE
```
PULSE(<v1> <v2> <td> <tr> <tf> <pw> <per>)
```

| Parameter | Default |
| --- | --- |
| v1 (initial) | required |
| v2 (pulsed) | required |
| td (delay) | 0 |
| tr (rise time) | TSTEP |
| tf (fall time) | TSTEP |
| pw (pulse width) | TSTOP |
| per (period) | TSTOP |

#### SIN
```
SIN(<voff> <vampl> <freq> <td> <df> <phase>)
```

| Parameter | Default |
| --- | --- |
| voff (offset) | required |
| vampl (amplitude) | required |
| freq (Hz) | 1/TSTOP |
| td (delay) | 0 |
| df (damping factor, s⁻¹) | 0 |
| phase (degrees) | 0 |

Waveform: `voff + vampl·sin(2π·(freq·(t-td) + phase/360°))·e^(-(t-td)·df)` for t > td.

#### SFFM (Single-Frequency FM)
```
SFFM(<voff> <vampl> <fc> <mod> <fm>)
```

| Parameter | Default |
| --- | --- |
| voff (offset) | required |
| vampl (amplitude) | required |
| fc (carrier Hz) | 1/TSTOP |
| mod (modulation index) | 0 |
| fm (modulation Hz) | 1/TSTOP |

Waveform: `voff + vampl·sin(2π·fc·t + mod·sin(2π·fm·t))`

#### PWL (Piecewise Linear)
```
PWL [TIME_SCALE_FACTOR=<value>] [VALUE_SCALE_FACTOR=<value>]
+ (<t1>,<v1>) (<t2>,<v2>) ...
+ [FILE <filename>]
+ [REPEAT FOR <n> ... ENDREPEAT]
+ [REPEAT FOREVER ... ENDREPEAT]
```

PSpice extensions: TIME_SCALE_FACTOR, VALUE_SCALE_FACTOR, REPEAT FOR/FOREVER/ENDREPEAT, FILE.

```spice
VIN 1 0 DC 5V AC 1V SIN(0 1V 1kHz)
VPULSE 2 0 PULSE(0 5V 10ns 1ns 1ns 50ns 100ns)
VPWL 3 0 PWL(0,0 1u,5V 2u,0)
```

## S — Voltage-Controlled Switch

```
S<name> <(+)sw> <(-)sw> <(+)ctrl> <(-)ctrl> <model name>
```

Model: `.MODEL <name> VSWITCH`

Variable-resistance model:

| Parameter | Default |
| --- | --- |
| RON (Ω) | 1.0 |
| ROFF (Ω) | 1E+6 |
| VON (V) | 1.0 |
| VOFF (V) | 0.0 |

Short-transition model (S_ST, PSpice-specific):

| Parameter | Default |
| --- | --- |
| RON (Ω) | 1.0 |
| ROFF (Ω) | 1E+12 |
| VT (V) | 0.0 |
| VH (V) | 0.0 |
| TD (s) | 0.0 |

ROFF/RON ratio > 1E+12 not recommended.

```spice
S12 13 17 2 0 SMOD
```

## W — Current-Controlled Switch

```
W<name> <(+)sw> <(-)sw> <controlling V device> <model name>
```

Model: `.MODEL <name> ISWITCH`

Variable-resistance model:

| Parameter | Default |
| --- | --- |
| RON (Ω) | 1.0 |
| ROFF (Ω) | 1E+6 |
| ION (A) | 1E-3 |
| IOFF (A) | 0.0 |

Short-transition model (W_ST, PSpice-specific): IT, IH, RON, ROFF, TD.

```spice
W12 13 17 VC WMOD
```

## T — Transmission Line

### Ideal line
```
T<name> <A+> <A-> <B+> <B-> Z0=<value> [TD=<value>] [F=<value> [NL=<value>]]
+ [IC=<nearV> <nearI> <farV> <farI>]
```

### Lossy line
```
T<name> <A+> <A-> <B+> <B-> LEN=<value> R=<value> L=<value> G=<value> C=<value>
```

Model: `.MODEL <name> TRN`

Notes:
- Both `Z0` and `ZO` accepted.
- Either TD or F required for ideal lines; NL defaults to 0.25 (quarter-wave).
- Lossy R/G may be Laplace expressions for frequency-dependent parameters (PSpice extension).

```spice
T1 1 2 3 4 Z0=220 TD=115ns
T4 1 2 3 4 LEN=1 R=.311 L=.378u G=6.27u C=67.3p
```

## K — Coupling

### Linear mutual coupling
```
K<name> L<ind1> [L<ind2> ...] <coupling value>
```

Coupling value between 0 and 1.0. Multiple inductors in a single K statement is a PSpice extension (SPICE2 requires pairwise).

### Nonlinear magnetic core
```
K<name> L<ind1> [L<ind2> ...] <coupling> <model name> [<size>]
```

When model present, inductor values become turns counts. Core LEVEL=2 (Jiles-Atherton) or LEVEL=3 (Spice Plus). Both are PSpice-specific.

### Transmission line coupling
```
K<name> T<line1> T<line2> Lm=<value> Cm=<value>
```

PSpice extension for coupled lossy transmission lines.

```spice
K1 L1 L2 0.99
KALL L1 L2 L3 L4 1
K1 L1 1 K528T500_3C8
K12 T1 T2 Lm=.04u Cm=6p
```

## X — Subcircuit Instantiation

```
X<name> [node]* <subcircuit name>
+ [PARAMS: <<name>=<value>>*]
+ [TEXT: <<name>=<text value>>*]
```

Node count must match definition. `PARAMS:` and `TEXT:` are PSpice extensions.

```spice
X12 100 101 200 201 DIFFAMP
XFELT 1 2 FILTER PARAMS: CENTER=200kHz
```

## OpAmp behavioral macromodel (PSpice-specific)

Instantiated as X subcircuit. Model type: `OPAMP`.

Key parameters: VOS, IB, IBOS, A0, GBW, SRP/SRM, CMRR, ROUT/ROAC, VPDIFF/VMDIFF, VCC/VSS, ISCP/ISCM, ENW, PSRR.

## Battery model (PSpice-specific)

Implemented as X subcircuit. Types: `awbflooded_cell`, `awbvalve_regulated_cell`.

Parameters: VOC (open-circuit voltage), AH (ampere-hour capacity), SOC (state of charge 0–1).

---

# Digital Device Reference

## Device types

| Device Class | Letter | Description |
| --- | --- | --- |
| Digital primitives | U | Gates, flip-flops, memory, converters, etc. |
| Stimulus generators | U | Digital waveform generators |
| Digital input (interface) | N | Digital-to-analog translation |
| Digital output (interface) | O | Analog-to-digital translation |

## Digital primitive format

```
U<name> <primitive type> [(<parameter>*)]
+ <digital power node> <digital ground node>
+ <node>*
+ <timing model name> <I/O model name>
+ [MNTYMXDLY=<delay select>]
+ [IO_LEVEL=<interface select>]
```

| Argument | Description |
| --- | --- |
| `MNTYMXDLY` | 0=.OPTIONS default, 1=min, 2=typ, 3=max, 4=worst-case |
| `IO_LEVEL` | 0=.OPTIONS default, 1–4=AtoD/DtoA subcircuit level |

### Standard gates

`BUF`, `INV`, `AND`, `NAND`, `OR`, `NOR`, `XOR`, `NXOR`

Array variants: `BUFA`, `INVA`, `ANDA`, `NANDA`, `ORA`, `NORA`, `XORA`, `NXORA`

Compound gates: `AO` (AND-OR), `OA` (OR-AND), `AOI` (AND-NOR), `OAI` (OR-NAND)

### Tristate gates

`BUF3`, `INV3`, `AND3`, `NAND3`, `OR3`, `NOR3`, `XOR3`, `NXOR3`

Array variants: `BUF3A`, `INV3A`, etc.

### Bidirectional transfer gates

`NBTG` (N-channel), `PBTG` (P-channel)

### Flip-flops and latches

`JKFF` (J-K, negative-edge), `DFF` (D-type, positive-edge), `SRFF` (S-R gated latch), `DLTCH` (D gated latch)

### Other digital primitives

`PULLUP`, `PULLDN`, `DLYLINE` (delay line), `ROM`, `RAM`, `ADC`, `DAC`

Programmable logic arrays: `PLAND`, `PLOR`, `PLXOR`, `PLNAND`, `PLNOR`, `PLNXOR` (and complement variants with `C` suffix)

Behavioral primitives: `LOGICEXP` (logic expression), `PINDLY` (pin-to-pin delay), `CONSTRAINT` (constraint checking)

## Timing model

```
.MODEL <name> <model type> ( <parameters>* )
```

Each timing parameter has MN/TY/MX variants. Categories: propagation delays (TP), setup times (TSU), hold times (TH), pulse widths (TW), switching times (TSW).

Unspecified delay extrapolation: `TPxxMN = DIGMNTYSCALE × TPxxTY`, `TPxxMX = DIGTYMXSCALE × TPxxTY`.

## I/O model

```
.MODEL <name> UIO ( <parameters>* )
```

| Parameter | Description | Default |
| --- | --- | --- |
| AtoD1–AtoD4 | AtoD interface subcircuits | AtoDDefault |
| DtoA1–DtoA4 | DtoA interface subcircuits | DtoADefault |
| DIGPOWER | Power supply subcircuit | DIGIFPWR |
| DRVH | Output high resistance (Ω) | 50 |
| DRVL | Output low resistance (Ω) | 50 |
| DRVZ | Z-state leakage resistance (Ω) | 250K |
| INLD | Input load capacitance (F) | 0 |
| INR | Input leakage resistance (Ω) | 30K |
| OUTLD | Output load capacitance (F) | 0 |

## N — Digital input device

```
N<name> <interface node> <low level node> <high level node>
+ <model name>
+ DGTLNET = <digital net name>
+ <I/O model name>
+ [IS = <initial state>]
```

Model: `.MODEL <name> DINPUT`

Modeled as two time-varying resistors (RLO, RHI) with optional capacitors (CLO, CHI). Up to 20 states (S0–S19), each with SnNAME, SnTSW, SnRLO, SnRHI.

## O — Digital output device

```
O<name> <interface node> <reference node> <model name>
+ DGTLNET = <digital net name> <I/O model name>
```

Model: `.MODEL <name> DOUTPUT`

Voltage compared against per-state ranges (SnVLO, SnVHI). Supports hysteresis modeling. Parameters: CLOAD, RLOAD, TIMESTEP, TIMESCALE, CHGONLY.

---

# Behavioral Simulation Functions

These functions are specific to behavioral modeling expressions (E/G sources with VALUE=).

## ZERO(expression)

Evaluates expression, discards result, returns 0. Useful as side-effect carrier.

## ONE(expression)

Evaluates expression, discards result, returns 1.

## CEIL(arg)

Returns ceiling (nearest integer ≥ arg). `CEIL(PI)` = 4, `CEIL(5)` = 5.

## FLOOR(arg)

Returns floor (nearest integer ≤ arg). `FLOOR(PI)` = 3, `FLOOR(5)` = 5.

## INTQ(arg)

Returns 1 if arg is integer, 0 otherwise. `INTQ(PI)` = 0, `INTQ(5)` = 1.

## DELTA(n)

Returns size of one of last three time steps. `DELTA(0)` = current step size. n = 0–3.

## TIME(n)

Returns one of last three simulation time-point values. `TIME(0)` = current time.

## STATE(n, source)

Returns value of a behavioral source from up to three previous time points.

```spice
Estate1 6 0 VALUE={state(1,V(5))}
```

## BREAK(time)

Schedules a simulator breakpoint at specified absolute time. Returns the time value. Commonly wrapped in `ZERO()`:

```spice
EE15 12 0 VALUE={ZERO(BREAK(time+10e-6))}
```

---

# Differences between PSpice and Berkeley SPICE2

PSpice runs any SPICE2G.6 circuit except:

1. **`.DISTO` analysis** not supported. Use transient + Fourier in Probe.
2. **Unavailable `.OPTIONS`**: `LIMTIM`, `LVLCOD`, `METHOD`, `MAXORD`, `LVLTIM`, `ITL3`.
3. **`.WIDTH IN=`** not available; PSpice reads entire input.
4. **Voltage coefficients for capacitors / current coefficients for inductors** must be in `.MODEL`, not on device line.
5. **Nested subcircuit definitions** not allowed. Calls can nest; definitions must be top-level.
6. **`.ALTER`** not supported. Use `.STEP` instead.
7. **POLY form**: PSpice requires explicit `POLY(1)` for one-dimensional; SPICE2 does not.

---

# PSpice-specific extensions summary

| Feature | Device | Description |
| --- | --- | --- |
| ABM VALUE | E, G | `VALUE = {<expression>}` arbitrary expression |
| ABM TABLE | E, G | `TABLE {<expr>} = <pairs>` lookup table |
| ABM LAPLACE | E, G | `LAPLACE {<expr>} = {<transform>}` s-domain filter |
| ABM FREQ | E, G | Frequency-domain tabulated response |
| ABM CHEBYSHEV | E, G | Automatic Chebyshev filter synthesis |
| Flux source | E | `F = {<expression>}` for dφ/dt |
| Charge source | G | `Q = {<expression>}` for dq/dt |
| Current-dependent inductance | L | IL1, IL2 model parameters |
| Inductor winding form | L | `<TURNS> [RESIS=]` with K core model |
| IGBT device | Z | NIGBT model, entire device type |
| OpAmp macromodel | X/OPAMP | Behavioral model with GBW, CMRR, etc. |
| Battery model | X | awbflooded_cell / awbvalve_regulated_cell |
| PWL REPEAT/FILE | I, V | Loop and file-read extensions |
| PWL scale factors | I, V | TIME_SCALE_FACTOR, VALUE_SCALE_FACTOR |
| PARAMS:/TEXT: on X | X | Parameterized subcircuit instantiation |
| Resistor TCE | R | Exponential TC model parameter |
| Capacitor VC1/VC2 | C | Voltage-dependent capacitance |
| Switch VH/TD (S_ST, W_ST) | S, W | Hysteresis and time-delay |
| Lossy T-line Laplace R/G | T | Frequency-dependent parameters |
| error=/warn= callbacks | E, G | Simulation-time conditional messages |
| Multi-inductor K | K | Multiple inductors in single statement |
| Nonlinear core models | K | Jiles-Atherton (LEVEL=2), Spice Plus (LEVEL=3) |
| T-line coupling | K | Coupled lossy transmission lines |
| MOSFET LEVEL 5–8 | M | EKV, BSIM3, BSIM4 |
| GaAsFET LEVEL 3–6 | B | TOM, Parker-Skellern, TOM-2, TOM-3 |
| BJT Level 2 | Q | Extended Mextram-style model |
| LPNP model type | Q | Lateral PNP |
| Temperature params | All | T_ABS, T_MEASURED, T_REL_GLOBAL, T_REL_LOCAL |

---

# PSpice AST extensions

Add these nodes or fields to an existing SPICE AST:

```ts
type PspiceModelStatement = ModelStatement & {
  akoReference?: Identifier;
  toleranceAnnotations?: ModelToleranceAnnotation[];
  temperatureMeta?: {
    tMeasured?: Expression;
    tAbs?: Expression;
    tRelGlobal?: Expression;
    tRelLocal?: Expression;
  };
};

type PspiceSubcktStatement = SubcktStatement & {
  optionalNodes: Assignment[];
  params: Assignment[];
  textParams: TextAssignment[];
};

type FunctionStatement = {
  kind: "func";
  name: Identifier;
  args: Identifier[];
  body: Expression;
  scope: ScopeId;
};

type AbmControlledSource = ElementStatement & {
  abmKind: "VALUE" | "TABLE" | "LAPLACE" | "FREQ" | "CHEBYSHEV";
  controlExpression?: Expression;
  tablePoints?: Array<[Expression, Expression]>;
  transform?: RawText | Expression;
};
```

---

# Compatibility and lowering strategy

- Preserve first, lower second. Parse all PSpice constructs into a loss-preserving AST before deciding whether the existing SPICE engine can simulate them.
- Treat `.FUNC` as scoped symbol definitions. Inline function bodies only after detecting recursion and arity errors.
- Treat `PARAMS:` as a named parameter namespace on subcircuit definitions and `X` instances. Do not flatten defaults into body expressions during parsing.
- Treat `AKO:` as model inheritance. Resolve and merge model parameters during elaboration, not parsing.
- Treat ABM `VALUE` as a behavioral source if the backend supports `B` sources. Otherwise preserve it and emit an unsupported-feature diagnostic.
- Treat ABM `TABLE` as a first-class table expression. Lower to backend table or PWL only after confirming interpolation and clamp behavior.
- Keep dialect flags. PSpice, ngspice, LTspice, and strict SPICE3 differ in subcircuit parameter spelling, expression operators, and behavioral-source syntax.

---

# Minimum conformance tests

### PSpice parameters and expressions

```spice
PSpice params
.PARAM VSUPPLY=14v RBASE=10k
V1 vcc 0 DC {VSUPPLY}
R1 in out {RBASE*1.05}
.END
```

Expected result: `.PARAM` defines `VSUPPLY` and `RBASE`, and both element values parse as brace expressions.

### `.FUNC` inside a parameterized subcircuit

```spice
Function scope
.FUNC my_bv(p1,p2,p3) {LOG10(2*p1+4)+p2+EXP(p3/5)}
.SUBCKT my_diode a k PARAMS:
+ p1=3 p2=5 p3=0
D1 a k my_D
.MODEL my_D D(BV={my_bv(p1,p2,p3)} IBV=.51729)
.ENDS my_diode
X1 n1 0 my_diode PARAMS: p1=4
.END
```

Expected result: `my_bv` resolves in the model parameter expression, default `PARAMS:` are captured, and the call override `p1=4` is associated with `X1`.

### ABM `VALUE`

```spice
ABM value
.PARAM THL=1
E1 out 0 VALUE={IF(V(in)<THL, V(in), V(in)*V(in)/THL)}
.END
```

Expected result: `E1` is parsed as a PSpice ABM voltage-controlled voltage source with a `VALUE` expression and an `IF` call.

### ABM `TABLE`

```spice
ABM table
E1 out 0 TABLE {V(in)} = (0,0) (1,1) (2,4) (4,16)
.END
```

Expected result: `E1` is parsed as a table-based ABM source with four ordered input/output points and endpoint-clamp semantics recorded for lowering.

### `.MODEL AKO:` and tolerances

```spice
Model inheritance
.MODEL BASED D(IS=1n RS=0.2)
.MODEL FASTD AKO: BASED D(IS=2n RS=0.1 DEV/GAUSS 5%)
.END
```

Expected result: `FASTD` stores `BASED` as an inheritance reference, parses `D` as the model type, and preserves the `DEV/GAUSS 5%` tolerance annotation.

---

# Suggested implementation order

- Step 1: Add dialect-aware lexical handling for PSpice comments, continuations, numeric suffixes, and brace expressions.
- Step 2: Extend `.PARAM`, `.SUBCKT`, and `X` parsing for `PARAMS:`, `OPTIONAL:`, and `TEXT:`.
- Step 3: Add `.FUNC` parsing, scoped symbol resolution, intrinsic-function checks, and function-call arity diagnostics.
- Step 4: Extend `.MODEL` for `AKO:`, PSpice model types, tolerances, and temperature metadata.
- Step 5: Add ABM parsing for `VALUE`, `TABLE`, `LAPLACE`, `FREQ`, and `CHEBYSHEV`.
- Step 6: Add lowering passes to the existing SPICE representation and mark unsupported constructs with precise diagnostics.
- Step 7: Parse all analysis commands (`.AC`, `.DC`, `.TRAN`, `.NOISE`, `.OP`, `.TF`, `.SENS`, `.FOUR`).
- Step 8: Parse simulation control commands (`.OPTIONS`, `.STEP`, `.MC`, `.WCASE`, `.TEMP`, `.DISTRIBUTION`).
- Step 9: Parse all analog device forms including PSpice extensions (POLY, flux/charge sources, switches, transmission lines, coupling).
- Step 10: Parse digital device primitives, I/O models, and interface devices.
- Step 11: Parse output/checkpoint commands (`.PROBE`, `.PRINT`, `.PLOT`, `.WATCH`, `.VECTOR`, `.SAVEBIAS`, `.LOADBIAS`, `.CHKPT`, `.RESTART`).
