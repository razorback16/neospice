# Parser Remaining Issues

Status: 98.1% pass rate (34,254/34,911) on KiCad SPICE Library test suite.
Previous: 96.5% (33,703/34,911). Improved by fixing 6 parser/compatibility gaps.

Remaining 657 failures break down into simulation errors (not parser bugs),
broken library files, timeouts, and a small number of remaining parser gaps.

## Not Parser Bugs

### SIM_ERROR: DC Operating Point Convergence (~510 cases)

Complex subcircuits (voltage regulators, comparators with positive feedback,
op-amps with internal protection diodes) fail to find a DC operating point.
This is a simulation engine issue, not a parser issue. Most of these models
also require GMIN stepping or source stepping in ngspice.

### TIMEOUT (~65 cases)

Models that take >10s to simulate. Likely complex subcircuits with many
internal nodes that stress the solver. Not a parser issue.

### Broken Library Files (~80 cases)

Files with uncommented datasheet text or malformed syntax. The parser
correctly interprets stray text as element definitions. These files would
also fail in ngspice.

---

## Fixed Parser/Engine Gaps (2026-05-23)

### 1. E/G Element VALUE Without Equals Sign (fixed ~219 cases)

PSpice uses `E name np nn VALUE { expr }` without `=` between VALUE and
the expression. Parser now accepts both `VALUE={expr}` and `VALUE {expr}`.

### 2. ASRC Parameter Substitution in Expressions (fixed ~175 cases)

Subcircuit expansion now substitutes parameter values in ASRC/VALUE/B
element expressions. Previously, bare parameter names like `{VTHRESH}`
inside behavioral source expressions were passed through unresolved.

### 3. AKO Cross-Scope Resolution (fixed ~10 cases)

`.model QP AKO:QON` now resolves when QON is in the same subcircuit
instance scope. The resolver tries instance-prefixed names when the
unprefixed lookup fails.

### 4. T Element Z0 Expression Evaluation (fixed ~10 cases)

T element parameters (`Z0=`, `TD=`, `F=`, `NL=`) now evaluate brace
expressions like `Z0={ZCHAR}` using `eval_expression()` instead of
requiring literal numeric values.

### 5. Parenthesized Control Node Pairs (fixed ~24 errors)

E/G elements now accept `(nc+,nc-)` parenthesized control node syntax
in both linear and POLY forms. Also fixed subcircuit expansion to
correctly handle these tokens during hierarchical name prefixing.

### 6. B Element I() Cross-Scope Resolution (verified fixed)

Resolved as a side effect of fix #2 (parameter substitution).

---

## Remaining Parser Gaps

### 1. Expression Evaluator Edge Cases (~5 cases)

A few ASRC expressions still fail:
- `missing '...' for function if` (3x) — complex nested IF() expressions
- `expected '...' in function if` (2x) — unusual IF() syntax variants

### 2. Maximum Subcircuit Nesting Depth (2 cases)

Two models hit the 100-level recursion limit. These are likely genuinely
recursive subcircuit definitions.

### 3. TABLE VCVS Format (3 cases)

A few E element TABLE forms fail: missing table points (2x) or missing
control expression (1x).

### 4. POLY CCCS Unknown Voltage Source (3 cases)

F element POLY forms reference voltage sources that aren't found after
subcircuit expansion.

### 5. Subcircuit Port Count Mismatch (~10 warnings, not failures)

The test harness guesses pin counts from subcircuit port names. When it
guesses wrong, the parser truncates connections with a warning but still
simulates. These are test harness issues, not parser bugs.

---

## Test Command

```bash
python3 tools/test_kicad_models.py                                    # full suite
python3 tools/test_kicad_models.py --errors-only                      # failures only
python3 tools/test_kicad_models.py --errors-only --show-stderr        # with error details
python3 tools/test_kicad_models.py --save results.json                # save to JSON
python3 tools/test_kicad_models.py --error-type SIM_ERROR             # filter by type
python3 tools/test_kicad_models.py --file LinearTech                  # filter by file
python3 tools/test_kicad_models.py --dump-netlist                     # show test netlists
```
