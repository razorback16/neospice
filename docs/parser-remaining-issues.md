# Parser Remaining Issues

Status: 98.4% pass rate (34,345/34,911) on KiCad SPICE Library test suite.
Previous: 96.5% (33,703/34,911). Improved by fixing 8 parser/compatibility gaps
and enhancing the test harness with pin-mapping comment parsing.

Remaining 566 failures break down into simulation errors (not parser bugs),
broken library files, timeouts, and a small number of edge cases.

## Failure Breakdown

| Category | Count | Notes |
|----------|-------|-------|
| SIM_ERROR (DC convergence) | 419 | Genuine convergence failures |
| ERROR (broken libraries) | 80 | Malformed syntax, missing sources |
| TIMEOUT (>10s) | 65 | Complex subcircuits stressing solver |
| PARSE_ERROR | 2 | Recursive subcircuit definitions |

## SIM_ERROR Analysis (419 cases)

### Convergence Failures (~319 cases)

Complex subcircuits (voltage regulators, comparators with positive feedback,
op-amps with internal protection diodes) fail to find a DC operating point.
Most of these models also require GMIN stepping or source stepping in ngspice.

### Residual-Zero Failures (~77 cases)

Models that report residual=0 but still fail. Most are MOSFET test circuits
where the test harness uses `W=10u L=1u` but the model's minimum channel
length is larger than 1u, causing "effective channel length less than zero"
errors (22 cases). The remaining ~55 are circuits where the solver converges
to a degenerate solution.

### Channel Length Errors (~22 cases)

MOSFET models where the test harness default `L=1u` is below the model's
minimum. These are test harness limitations, not solver bugs.

### Activation Energy (~1 case)

One model triggers "activation energy too small, limited to 0.1".

## ERROR (80 cases)

- **smps_cb.lib** (50x): Broken library with uncommented text parsed as elements
- **tube.lib** (30x): Same issue — malformed library syntax

These files would also fail in ngspice.

## TIMEOUT (65 cases)

Models that take >10s to simulate. Complex subcircuits with many internal
nodes that stress the solver.

## PARSE_ERROR (2 cases)

Two MOV models in `MOV-07D.lib` hit the 100-level subcircuit recursion limit.
These are genuinely recursive subcircuit definitions.

---

## Fixed Parser/Engine Gaps

### Phase 1 (2025-05-23): Parser Compatibility

1. **E/G Element VALUE Without Equals Sign** (fixed ~219 cases)
   PSpice `VALUE { expr }` without `=`. Parser accepts both forms.

2. **ASRC Parameter Substitution** (fixed ~175 cases)
   Subcircuit expansion now substitutes parameters in ASRC/VALUE/B expressions.

3. **AKO Cross-Scope Resolution** (fixed ~10 cases)
   `.model QP AKO:QON` resolves instance-prefixed names.

4. **T Element Z0 Expression Evaluation** (fixed ~10 cases)
   T element parameters evaluate brace expressions like `Z0={ZCHAR}`.

5. **Parenthesized Control Node Pairs** (fixed ~24 cases)
   E/G elements accept `(nc+,nc-)` syntax in linear and POLY forms.

6. **B Element I() Cross-Scope Resolution** (fixed as side effect of #2)

### Phase 2 (2025-05-24): Test Harness & Engine

7. **Pin-Mapping Comment Parsing** (fixed ~90+ cases)
   Test harness now parses pin-mapping comments above `.SUBCKT` definitions
   to generate proper bias circuits with VCC/VEE/input/output connections
   instead of generic 100k-to-ground for all ports. Supports 3 comment
   patterns: pipe-tree, inline tabular, and compact inline. Also detects
   file-level convention headers (e.g., LinearTech.lib).

8. **DC Solver Iteration Reporting** (fix)
   `sim_status.iterations` now set on the all-failed path in dc.cpp.

---

## Remaining Parser Gaps

### 1. Expression Evaluator Edge Cases (~5 cases)

A few ASRC expressions still fail:
- `missing '...' for function if` (3x) — complex nested IF() expressions
- `expected '...' in function if` (2x) — unusual IF() syntax variants

### 2. Maximum Subcircuit Nesting Depth (2 cases)

Two MOV models hit the 100-level recursion limit. Genuinely recursive.

### 3. TABLE VCVS Format (2 cases)

TABLE VCVS with missing table points.

### 4. POLY CCCS Unknown Voltage Source (3 cases)

F element POLY forms reference voltage sources not found after expansion.

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
