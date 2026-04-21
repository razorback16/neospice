# Missing Features — Implementation Plan ✅ COMPLETE

> **Status:** All 14 tasks completed and merged to `main`. 769 tests passing. Executed 2026-04-20.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the most commonly needed missing features in neospice to handle real-world ngspice netlists without parse errors or silent wrong results.

**Architecture:** Four phases ordered by impact — Phase 1 adds the missing source waveform types (PWL/EXP/SFFM/AM) that silently produce wrong results today; Phase 2 adds missing ASRC functions (DDT/IDT/table/db); Phase 3 adds parser features (.func, .global, L model cards, F/H POLY); Phase 4 adds missing analysis types (.sens, .tf).

**Tech Stack:** C++20, CMake, Google Test, ngspice as reference

---

## Phase 1: Source Waveform Types

Currently only DC, PULSE, and SIN are implemented. PWL, EXP, SFFM, and AM silently fall through to DC.

### Task 1: PWL Source Waveform

**Files:**
- Modify: `src/devices/vsource.hpp` (add PWL to SourceFunction enum, add PwlParams struct)
- Modify: `src/devices/vsource.cpp` (implement PWL evaluation in value_at(), add breakpoints)
- Modify: `src/devices/isource.hpp` (add set_pwl)
- Modify: `src/devices/isource.cpp` (implement PWL evaluation)
- Modify: `src/parser/netlist_parser.cpp:130-158` (parse PWL(...) source spec)
- Create: `tests/circuits/pwl_source.cir`
- Modify: `tests/unit/test_ngspice_compare.cpp`

**Context:** PWL(t1 v1 t2 v2 ...) defines a piecewise-linear waveform as time-value pairs. Between points, linear interpolation. Before first point, use first value. After last point, use last value. Each breakpoint time should be registered as a source breakpoint for timestep control.

- [ ] **Step 1: Add PWL structs and enum value**

In `src/devices/vsource.hpp`, add `PWL` to the `SourceFunction` enum:
```cpp
enum class SourceFunction { DC, PULSE, SIN, PWL };
```

Add a PWL params struct:
```cpp
struct PwlParams {
    std::vector<std::pair<double,double>> points;  // (time, value) pairs
};
```

Add to VSource class:
```cpp
void set_pwl(PwlParams p);
```

Add private member:
```cpp
PwlParams pwl_;
```

- [ ] **Step 2: Implement PWL evaluation in VSource**

In `src/devices/vsource.cpp`, in `value_at()`, add a case for PWL:
```cpp
case SourceFunction::PWL: {
    const auto& pts = pwl_.points;
    if (pts.empty()) return dc_value_;
    if (t <= pts.front().first) return pts.front().second;
    if (t >= pts.back().first) return pts.back().second;
    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        if (t >= pts[i].first && t <= pts[i+1].first) {
            double dt = pts[i+1].first - pts[i].first;
            double frac = (dt > 0) ? (t - pts[i].first) / dt : 0.0;
            return pts[i].second + frac * (pts[i+1].second - pts[i].second);
        }
    }
    return pts.back().second;
}
```

Implement `set_pwl()`:
```cpp
void VSource::set_pwl(PwlParams p) {
    func_ = SourceFunction::PWL;
    pwl_ = std::move(p);
}
```

Add PWL breakpoints in `get_breakpoints()`:
```cpp
case SourceFunction::PWL:
    for (const auto& [t, v] : pwl_.points) {
        if (t > tstart && t <= tstop)
            bps.push_back(t);
    }
    break;
```

- [ ] **Step 3: Add PWL to ISource**

Mirror the VSource changes in `src/devices/isource.hpp` and `src/devices/isource.cpp`.

- [ ] **Step 4: Parse PWL in netlist parser**

In `src/parser/netlist_parser.cpp`, in the source function parser (around line 130-158), add after the SIN block:
```cpp
} else if (lower == "pwl" || lower.substr(0, 3) == "pwl") {
    auto vals = parse_paren_params(tokens, i);
    spec.func = SourceFunction::PWL;
    for (size_t j = 0; j + 1 < vals.size(); j += 2) {
        spec.pwl.points.emplace_back(vals[j], vals[j+1]);
    }
}
```

Add `PwlParams pwl;` to whatever struct holds the source spec.

- [ ] **Step 5: Wire PWL into device creation**

In the VSource/ISource creation code (around lines 1152-1171), add:
```cpp
else if (spec.func == SourceFunction::PWL) vs->set_pwl(spec.pwl);
```

- [ ] **Step 6: Create test circuit and comparison test**

Create `tests/circuits/pwl_source.cir`:
```spice
PWL source test
V1 in 0 PWL(0 0 1n 0 2n 1 3n 1 4n 0 5n 0)
R1 in 0 1k
.tran 0.1n 5n
.end
```

Add ngspice comparison test with tight tolerance (PWL is exact, no numerical differences expected).

- [ ] **Step 7: Build, test, commit**

```bash
git commit -m "feat: add PWL source waveform type"
```

---

### Task 2: EXP Source Waveform

**Files:**
- Modify: `src/devices/vsource.hpp` (add EXP to enum, ExpParams struct)
- Modify: `src/devices/vsource.cpp` (implement EXP evaluation)
- Modify: `src/devices/isource.hpp/cpp` (mirror)
- Modify: `src/parser/netlist_parser.cpp` (parse EXP)
- Create: `tests/circuits/exp_source.cir`
- Modify: `tests/unit/test_ngspice_compare.cpp`

**Context:** EXP(V1 V2 TD1 TAU1 TD2 TAU2) is a two-stage exponential waveform:
- For t < TD1: v = V1
- For TD1 <= t < TD2: v = V1 + (V2-V1)*(1 - exp(-(t-TD1)/TAU1))
- For t >= TD2: v = V1 + (V2-V1)*(1 - exp(-(t-TD1)/TAU1)) + (V1-V2)*(1 - exp(-(t-TD2)/TAU2))

- [ ] **Step 1: Add EXP enum, params struct, and setter**
- [ ] **Step 2: Implement EXP evaluation in value_at()**
- [ ] **Step 3: Add EXP breakpoints (TD1 and TD2)**
- [ ] **Step 4: Mirror in ISource**
- [ ] **Step 5: Parse EXP(...) in netlist parser**
- [ ] **Step 6: Create test circuit and ngspice comparison test**
- [ ] **Step 7: Build, test, commit**

```bash
git commit -m "feat: add EXP source waveform type"
```

---

### Task 3: SFFM Source Waveform

**Files:**
- Modify: `src/devices/vsource.hpp` (add SFFM to enum, SffmParams struct)
- Modify: `src/devices/vsource.cpp` (implement SFFM evaluation)
- Modify: `src/devices/isource.hpp/cpp` (mirror)
- Modify: `src/parser/netlist_parser.cpp` (parse SFFM)
- Create: `tests/circuits/sffm_source.cir`
- Modify: `tests/unit/test_ngspice_compare.cpp`

**Context:** SFFM(VO VA FC MDI FS) — single-frequency FM:
- v(t) = VO + VA * sin(2π·FC·t + MDI * sin(2π·FS·t))

- [ ] **Step 1: Add SFFM enum, params struct, and setter**
- [ ] **Step 2: Implement SFFM evaluation in value_at()**
- [ ] **Step 3: Mirror in ISource**
- [ ] **Step 4: Parse SFFM(...) in netlist parser**
- [ ] **Step 5: Create test circuit and ngspice comparison test**
- [ ] **Step 6: Build, test, commit**

```bash
git commit -m "feat: add SFFM source waveform type"
```

---

### Task 4: AM Source Waveform

**Files:**
- Modify: `src/devices/vsource.hpp` (add AM to enum, AmParams struct)
- Modify: `src/devices/vsource.cpp` (implement AM evaluation)
- Modify: `src/devices/isource.hpp/cpp` (mirror)
- Modify: `src/parser/netlist_parser.cpp` (parse AM)
- Create: `tests/circuits/am_source.cir`
- Modify: `tests/unit/test_ngspice_compare.cpp`

**Context:** AM(VA VO MF FC TD) — amplitude-modulated:
- v(t) = VA * (VO + sin(2π·MF·t)) * sin(2π·FC·t)  for t >= TD
- v(t) = 0 for t < TD

ngspice AM syntax: `AM(sa oc fm fc td)` where sa=signal amplitude, oc=offset, fm=modulating freq, fc=carrier freq, td=delay.

- [ ] **Step 1: Add AM enum, params struct, and setter**
- [ ] **Step 2: Implement AM evaluation in value_at()**
- [ ] **Step 3: Mirror in ISource**
- [ ] **Step 4: Parse AM(...) in netlist parser**
- [ ] **Step 5: Create test circuit and ngspice comparison test**
- [ ] **Step 6: Build, test, commit**

```bash
git commit -m "feat: add AM source waveform type"
```

---

## Phase 2: ASRC Functions

### Task 5: ASRC DDT() — Time Derivative

**Files:**
- Modify: `src/devices/asrc/expression_ast.hpp` (add DDT node type)
- Modify: `src/devices/asrc/expression_ast.cpp` (parse and evaluate DDT)
- Modify: `src/devices/asrc/asrc_device.hpp` (add history for DDT)
- Modify: `src/devices/asrc/asrc_device.cpp` (compute DDT from history)
- Create: `tests/circuits/asrc_ddt.cir`
- Modify: `tests/unit/test_ngspice_compare.cpp`

**Context:** DDT(expr) returns the time derivative of the expression. Implementation approach: store the previous timestep's value of the expression argument, compute (current - previous) / dt. The AST evaluator returns the value; the derivative w.r.t. circuit variables uses the chain rule with the expression's own gradient.

- [ ] **Step 1: Add DDT node type and parsing**
- [ ] **Step 2: Add history storage to ASRCDevice for DDT nodes**
- [ ] **Step 3: Implement DDT evaluation using backward difference**
- [ ] **Step 4: Create test circuit** (e.g., `B1 out 0 I = 1e-12 * DDT(V(in))` with capacitor-like behavior)
- [ ] **Step 5: Add ngspice comparison test**
- [ ] **Step 6: Build, test, commit**

```bash
git commit -m "feat: add DDT() time derivative function in ASRC expressions"
```

---

### Task 6: ASRC IDT() — Time Integral

**Files:**
- Modify: `src/devices/asrc/expression_ast.hpp` (add IDT node type)
- Modify: `src/devices/asrc/expression_ast.cpp` (parse and evaluate IDT)
- Modify: `src/devices/asrc/asrc_device.hpp` (add accumulator for IDT)
- Modify: `src/devices/asrc/asrc_device.cpp` (compute IDT via trapezoidal accumulation)
- Create: `tests/circuits/asrc_idt.cir`
- Modify: `tests/unit/test_ngspice_compare.cpp`

**Context:** IDT(expr [, ic [, assert]]) returns the time integral of the expression. Syntax: IDT(expr) or IDT(expr, initial_condition). Implementation: trapezoidal accumulation (integral += 0.5*(current+previous)*dt). IC defaults to 0.

- [ ] **Step 1: Add IDT node type and parsing (1-2 args)**
- [ ] **Step 2: Add accumulator state to ASRCDevice for IDT nodes**
- [ ] **Step 3: Implement IDT evaluation using trapezoidal accumulation**
- [ ] **Step 4: Create test circuit** (e.g., `B1 out 0 V = IDT(1)` — should ramp linearly)
- [ ] **Step 5: Add ngspice comparison test**
- [ ] **Step 6: Build, test, commit**

```bash
git commit -m "feat: add IDT() time integral function in ASRC expressions"
```

---

### Task 7: ASRC table() Function

**Files:**
- Modify: `src/devices/asrc/expression_ast.hpp` (add TABLE node type)
- Modify: `src/devices/asrc/expression_ast.cpp` (parse and evaluate table)
- Create: `tests/circuits/asrc_table.cir`
- Modify: `tests/unit/test_ngspice_compare.cpp`

**Context:** `table(expr, x1, y1, x2, y2, ...)` is functionally identical to `PWL(expr, x1,y1, x2,y2, ...)` but with different syntax — ngspice supports both. Parsing is the same as PWL. Can likely reuse the PWL evaluation code.

- [ ] **Step 1: Add TABLE as an alias for PWL in the parser**

In `parse_function()`, add:
```cpp
if (name == "table") {
    // Same as PWL — first arg is expression, rest are constant pairs
    // Reuse PWL parsing and node type
}
```

- [ ] **Step 2: Create test circuit and ngspice comparison test**
- [ ] **Step 3: Build, test, commit**

```bash
git commit -m "feat: add table() function as PWL alias in ASRC expressions"
```

---

### Task 8: ASRC db() Function

**Files:**
- Modify: `src/devices/asrc/expression_ast.hpp` (add DB node type)
- Modify: `src/devices/asrc/expression_ast.cpp` (parse and evaluate db)

**Context:** `db(x)` = 20*log10(abs(x)). Simple unary function, no state needed.

- [ ] **Step 1: Add DB to NodeType enum**
- [ ] **Step 2: Add parser case**: `if (name == "db") return make_unary(NodeType::DB);`
- [ ] **Step 3: Add evaluator case**: `val = 20.0 * log10(max(abs(a.val), 1e-300))`, derivative = `20.0 / (a.val * log(10))` (chain rule with sign)
- [ ] **Step 4: Build, test, commit**

```bash
git commit -m "feat: add db() function in ASRC expressions"
```

---

## Phase 3: Parser Features

### Task 9: .func User-Defined Functions

**Files:**
- Modify: `src/parser/netlist_parser.cpp` (parse .func, expand in expressions)

**Context:** `.func myfunc(x,y) {x*y+1}` defines a function that can be used in expressions. Implementation approach: during pass 1, collect `.func` definitions into a map. During expression parsing (source values, ASRC expressions, .param expressions), expand function calls by textual substitution before parsing.

- [ ] **Step 1: Collect .func definitions in pass 1**

Parse `.func name(arg1,arg2,...) {body}` and store in a map: `func_name -> {arg_names, body_template}`.

- [ ] **Step 2: Expand .func calls before expression parsing**

Before parsing any expression string (source specs, ASRC, .param), scan for known function names and perform textual substitution of arguments.

- [ ] **Step 3: Create test circuit using .func**
- [ ] **Step 4: Build, test, commit**

```bash
git commit -m "feat: add .func user-defined function support"
```

---

### Task 10: .global Statement

**Files:**
- Modify: `src/parser/netlist_parser.cpp` (parse .global, mark nodes)
- Modify: `src/core/circuit.hpp` (add global node set)

**Context:** `.global vdd gnd` declares nodes that are shared across all subcircuit hierarchies. Currently silently ignored, which causes incorrect connections in hierarchical netlists.

- [ ] **Step 1: Parse .global and collect node names**
- [ ] **Step 2: During subcircuit expansion, don't prefix global node names**
- [ ] **Step 3: Create test circuit with nested subcircuits using .global**
- [ ] **Step 4: Build, test, commit**

```bash
git commit -m "feat: add .global node declaration support"
```

---

### Task 11: L Model Cards (.model LMOD L)

**Files:**
- Create: `src/devices/inductor_model.hpp` (InductorModel struct)
- Modify: `src/parser/model_cards.hpp/cpp` (add to_inductor_model)
- Modify: `src/parser/netlist_parser.cpp` (parse L model references)
- Create: `tests/circuits/inductor_model.cir`
- Modify: `tests/unit/test_ngspice_compare.cpp`

**Context:** Same pattern as R/C model cards (Task 9 of previous plan). `.model LMOD L(TC1=... TC2=...)` with instance params overriding model defaults.

- [ ] **Step 1: Create InductorModel struct**
- [ ] **Step 2: Add dispatcher in model_cards**
- [ ] **Step 3: Wire into L-element parser with two-pass override**
- [ ] **Step 4: Create test circuit and comparison test**
- [ ] **Step 5: Build, test, commit**

```bash
git commit -m "feat: add L model card support (.model LMOD L)"
```

---

### Task 12: F/H POLY Controlled Sources

**Files:**
- Modify: `src/parser/netlist_parser.cpp` (parse POLY for F and H elements)
- Modify: `src/devices/cccs.hpp/cpp` (support polynomial multi-input)
- Modify: `src/devices/ccvs.hpp/cpp` (support polynomial multi-input)
- Create: `tests/circuits/cccs_poly.cir`
- Modify: `tests/unit/test_ngspice_compare.cpp`

**Context:** E and G elements already support POLY(N). F (CCCS) and H (CCVS) need the same. POLY(N) takes N control-branch pairs and polynomial coefficients: `F1 np nn POLY(2) Vs1 Vs2 c0 c1 c2 ...`

- [ ] **Step 1: Add POLY parsing for F element** (mirror E/G POLY parser)
- [ ] **Step 2: Add POLY parsing for H element**
- [ ] **Step 3: Implement multi-input polynomial evaluation in CCCS/CCVS**
- [ ] **Step 4: Create test circuits and comparison tests**
- [ ] **Step 5: Build, test, commit**

```bash
git commit -m "feat: add POLY support for F and H controlled sources"
```

---

## Phase 4: Analysis Types

### Task 13: .tf Transfer Function Analysis

**Files:**
- Modify: `src/parser/netlist_parser.cpp` (parse .tf)
- Create: `src/core/tf.cpp` (implement .tf analysis)
- Modify: `src/api/neospice.hpp/cpp` (expose TF results)

**Context:** `.tf V(out) Vin` computes the DC small-signal transfer function, input resistance, and output resistance. It's essentially a DC analysis + perturbation. Straightforward to implement since the MNA framework is already in place.

- [ ] **Step 1: Parse .tf statement**
- [ ] **Step 2: Implement TF analysis** (DC solve + small-signal perturbation)
- [ ] **Step 3: Create test circuit and comparison test**
- [ ] **Step 4: Build, test, commit**

```bash
git commit -m "feat: add .tf transfer function analysis"
```

---

### Task 14: .sens Sensitivity Analysis

**Files:**
- Modify: `src/parser/netlist_parser.cpp` (parse .sens)
- Create: `src/core/sens.cpp` (implement .sens analysis)
- Modify: `src/api/neospice.hpp/cpp` (expose sensitivity results)

**Context:** `.sens V(out)` computes the DC sensitivity of an output variable with respect to all circuit parameters. Uses the adjoint method: one extra LU solve per output.

- [ ] **Step 1: Parse .sens statement**
- [ ] **Step 2: Implement sensitivity analysis**
- [ ] **Step 3: Create test circuit and comparison test**
- [ ] **Step 4: Build, test, commit**

```bash
git commit -m "feat: add .sens sensitivity analysis"
```
