# Remaining Accuracy Fixes — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close remaining ngspice accuracy gaps in custom device models: add TL breakpoints, switch timestep control, ASRC TEMPER/HERTZ/PWL, TL IC=, resistor RAC=, and R/C model cards.

**Architecture:** Three phases — Phase 1 (timestep precision) directly reduces the 10-50% transient errors; Phase 2 (ASRC features) prevents failures on real-world netlists; Phase 3 (parameter completeness) fills out the device model parameter space.

**Tech Stack:** C++20, CMake, Google Test, ngspice as reference

---

## Phase 1: Timestep Precision

### Task 1: Transmission Line Breakpoints at t = k*TD

**Files:**
- Modify: `src/devices/tline.hpp` (add get_breakpoints method declaration)
- Modify: `src/devices/tline.cpp` (implement get_breakpoints)
- Modify: `src/core/transient.cpp:38-48` (wire TL into collect_breakpoints)
- Test: `tests/unit/test_ngspice_compare.cpp` (tighten tolerances if errors improve)

**Context:** ngspice inserts breakpoints at multiples of TD for transmission lines. neospice's `collect_breakpoints()` at transient.cpp:38-48 only queries VSource and ISource. The breakpoints must be added as source breakpoints so that the post-crossing dt reduction at transient.cpp:485-489 triggers.

- [ ] **Step 1: Add get_breakpoints to TransmissionLine**

In `src/devices/tline.hpp`, add the method declaration inside the `public` section, after `double td() const`:

```cpp
std::vector<double> get_breakpoints(double tstart, double tstop) const;
```

In `src/devices/tline.cpp`, implement it:

```cpp
std::vector<double> TransmissionLine::get_breakpoints(double /*tstart*/, double tstop) const {
    std::vector<double> bps;
    if (td_ <= 0.0) return bps;
    double t = td_;
    while (t <= tstop) {
        bps.push_back(t);
        t += td_;
    }
    return bps;
}
```

- [ ] **Step 2: Wire TL breakpoints into collect_breakpoints**

In `src/core/transient.cpp`, add `#include "devices/tline.hpp"` at the top (check if already present), then modify `collect_breakpoints()` to also query TransmissionLine devices. After the existing ISource block (around line 45):

```cpp
} else if (auto* tl = dynamic_cast<TransmissionLine*>(dev.get())) {
    auto bps = tl->get_breakpoints(0.0, tstop);
    for (double bp : bps) ctrl.add_source_breakpoint(bp);
}
```

- [ ] **Step 3: Build and run tests**

Run: `cmake --build build-release -j$(nproc) && build-release/tests/neospice_tests --gtest_filter='NgspiceCompare*'`

Expected: All 24 tests still pass.

- [ ] **Step 4: Commit**

```bash
git add src/devices/tline.hpp src/devices/tline.cpp src/core/transient.cpp
git commit -m "feat: add transmission line breakpoints at t=k*TD"
```

---

### Task 2: Switch Timestep Control (compute_trunc)

**Files:**
- Modify: `src/devices/switch.hpp` (add compute_trunc declarations, add prev_state_changed_ member)
- Modify: `src/devices/switch.cpp` (implement compute_trunc, track previous state_changed_)
- Modify: `src/core/transient.cpp:446-461` (wire switch into device LTE loop)

**Context:** VSwitch and CSwitch have `device_converged()` (returns `!state_changed_`) but no `compute_trunc()`. After a switch state change, the timestep controller doesn't reduce dt for the next step. We need to add compute_trunc that returns a small dt after transitions.

- [ ] **Step 1: Add state tracking members to VSwitch and CSwitch**

In `src/devices/switch.hpp`, add to the `private` section of both VSwitch and CSwitch:

```cpp
bool prev_state_changed_ = false;
```

- [ ] **Step 2: Add compute_trunc declarations**

In `src/devices/switch.hpp`, add to the `public` section of VSwitch (after `device_converged()`):

```cpp
double compute_trunc(const IntegratorCtx& ctx,
                     const SimOptions& opts) const override;
```

Same for CSwitch.

- [ ] **Step 3: Track previous state change**

In `src/devices/switch.cpp`, in `VSwitch::evaluate()`, after the line `current_state_ = new_state;` (line ~183), add:

```cpp
if (mode & (MODEINITTRAN_BIT | MODEINITPRED_BIT))
    prev_state_changed_ = state_changed_;
```

Wait — `state_changed_` is only set in MODEINITFLOAT. We need to track whether the state changed during the *previous* accepted step. The cleanest approach: save state_changed_ at the start of the predictor phase.

Actually, the correct approach for compute_trunc: if the switch just changed state (state_changed_ was true during the Newton iteration that converged), we want to constrain the *next* timestep. So we track it like this:

In `VSwitch::evaluate()`, at the predictor phase block (line 168-169), before `previous_state_ = current_state_`:

```cpp
prev_state_changed_ = state_changed_;
```

Same for CSwitch::evaluate() at the equivalent location.

- [ ] **Step 4: Implement compute_trunc for VSwitch**

In `src/devices/switch.cpp`:

```cpp
double VSwitch::compute_trunc(const IntegratorCtx& ctx,
                              const SimOptions& /*opts*/) const {
    if (prev_state_changed_) {
        return 0.1 * ctx.dt;
    }
    return 1e30;
}
```

Same for CSwitch.

- [ ] **Step 5: Wire switch into device LTE loop in transient.cpp**

In `src/core/transient.cpp`, find the device LTE loop (around lines 446-461). Add VSwitch and CSwitch after the existing device types:

```cpp
} else if (auto* sw = dynamic_cast<VSwitch*>(dev.get())) {
    double sw_dt = sw->compute_trunc(ic, opts);
    device_dt = std::min(device_dt, sw_dt);
} else if (auto* csw = dynamic_cast<CSwitch*>(dev.get())) {
    double csw_dt = csw->compute_trunc(ic, opts);
    device_dt = std::min(device_dt, csw_dt);
}
```

Also add `#include "devices/switch.hpp"` at the top of transient.cpp if not already present.

- [ ] **Step 6: Build and run tests**

Run: `cmake --build build-release -j$(nproc) && build-release/tests/neospice_tests --gtest_filter='NgspiceCompare*'`

Expected: All 24 tests pass. SwitchHysteresisTransient may show improved timing accuracy.

- [ ] **Step 7: Commit**

```bash
git add src/devices/switch.hpp src/devices/switch.cpp src/core/transient.cpp
git commit -m "feat: add switch compute_trunc for timestep control after state changes"
```

---

### Task 3: Re-measure and Tighten Transient Tolerances

**Files:**
- Modify: `tests/unit/test_ngspice_compare.cpp` (tighten tolerances)

**Context:** After Tasks 1-2, the edge-timing errors in DiodeRectifier, CMOSInverter, and CMOSInverterWithR should be reduced. Measure actual worst-case errors and tighten tolerances to ~2-5x the measured value.

- [ ] **Step 1: Measure actual errors**

Add temporary debug output or run a measurement harness to find the actual worst_error for:
- DiodeRectifierTransient (currently 15% tolerance)
- CMOSInverterTransient (currently 25% tolerance)
- CMOSInverterTransientWithResistance (currently 50% tolerance)

Use the existing `cmp.worst_error` field.

- [ ] **Step 2: Tighten tolerances**

In `tests/unit/test_ngspice_compare.cpp`, update tolerance values based on measured errors. Set each tolerance to roughly 2-5x the measured worst error.

- [ ] **Step 3: Run tests to verify**

Run: `cmake --build build-release -j$(nproc) && build-release/tests/neospice_tests --gtest_filter='NgspiceCompare*'`

Expected: All 24 tests pass with tighter tolerances.

- [ ] **Step 4: Commit**

```bash
git add tests/unit/test_ngspice_compare.cpp
git commit -m "test: tighten transient tolerances after TL breakpoints and switch timestep control"
```

---

## Phase 2: Missing ASRC Features

### Task 4: ASRC TEMPER Variable

**Files:**
- Modify: `src/devices/asrc/expression_ast.cpp` (parse "temper")
- Modify: `src/devices/asrc/asrc_device.hpp` (add temper_var_idx_ member)
- Modify: `src/devices/asrc/asrc_device.cpp` (detect and fill TEMPER)
- Create: `tests/circuits/asrc_temper.cir` (test circuit)
- Modify: `tests/unit/test_ngspice_compare.cpp` (add comparison test)

**Context:** ngspice ASRC expressions can reference `TEMPER` to get simulation temperature in Celsius. neospice handles `TIME` via the `__time__` pattern — TEMPER follows the same approach with `__temper__`.

- [ ] **Step 1: Add TEMPER parsing in expression_ast.cpp**

In `src/devices/asrc/expression_ast.cpp`, in `parse_primary()`, after the TIME block (around line 272), add:

```cpp
if (lname == "temper") {
    VarRef ref;
    ref.kind = VarKind::NODE_VOLTAGE;
    ref.name1 = "__temper__";
    int idx = get_or_add_var(ref);
    auto node = std::make_unique<ASTNode>();
    node->type = NodeType::VARIABLE;
    node->var_idx = idx;
    return node;
}
```

- [ ] **Step 2: Detect temper_var_idx_ in ASRCDevice constructor**

In `src/devices/asrc/asrc_device.hpp`, add member:

```cpp
int temper_var_idx_ = -1;
```

In `src/devices/asrc/asrc_device.cpp` constructor, after the TIME detection loop:

```cpp
for (int i = 0; i < nv; ++i) {
    if (expr_.var_refs()[i].kind == asrc::VarKind::NODE_VOLTAGE &&
        expr_.var_refs()[i].name1 == "__temper__") {
        temper_var_idx_ = i;
        break;
    }
}
```

- [ ] **Step 3: Fill TEMPER in fill_var_values()**

Find the method that fills var_values_ (in asrc_device.cpp, the evaluate method). Where `time_var_idx_` is filled with `ctx.current_time`, add:

```cpp
if (temper_var_idx_ >= 0) {
    double temp_celsius = 27.0;
    if (tls_integrator_ctx && tls_integrator_ctx->options)
        temp_celsius = tls_integrator_ctx->options->temp - 273.15;
    var_values_[temper_var_idx_] = temp_celsius;
}
```

- [ ] **Step 4: Create test circuit**

Create `tests/circuits/asrc_temper.cir`:

```spice
ASRC TEMPER test
V1 in 0 1.0
B1 out 0 V = V(in) * (1 + 0.003 * (TEMPER - 27))
R1 out 0 1k
.dc V1 1 1 1
.end
```

- [ ] **Step 5: Add ngspice comparison test**

In `tests/unit/test_ngspice_compare.cpp`, add:

```cpp
TEST_F(NgspiceCompareTest, AsrcTemper) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/asrc_temper.cir";
    auto ng_result = ngspice_->run_dc(path);
    auto ckt = sim_.load(path);
    auto cs_result = sim_.run_dc(ckt);
    auto cmp = compare_dc(ng_result, cs_result, {1e-6, 1e-12});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}
```

- [ ] **Step 6: Build and test**

Run: `cmake --build build-release -j$(nproc) && build-release/tests/neospice_tests --gtest_filter='NgspiceCompare*AsrcTemper'`

Expected: PASS

- [ ] **Step 7: Commit**

```bash
git add src/devices/asrc/expression_ast.cpp src/devices/asrc/asrc_device.hpp \
        src/devices/asrc/asrc_device.cpp tests/circuits/asrc_temper.cir \
        tests/unit/test_ngspice_compare.cpp
git commit -m "feat: add TEMPER variable support in ASRC expressions"
```

---

### Task 5: ASRC HERTZ Variable

**Files:**
- Modify: `src/devices/asrc/expression_ast.cpp` (parse "hertz")
- Modify: `src/devices/asrc/asrc_device.hpp` (add hertz_var_idx_ member)
- Modify: `src/devices/asrc/asrc_device.cpp` (detect and fill HERTZ)
- Modify: `src/core/circuit.hpp` (add ac_freq to IntegratorCtx)
- Modify: `src/core/ac.cpp` (set ac_freq in the AC sweep loop)
- Create: `tests/circuits/asrc_hertz.cir` (test circuit)
- Modify: `tests/unit/test_ngspice_compare.cpp` (add comparison test)

**Context:** HERTZ is the AC frequency variable. It's set to 0 in transient, and to the current sweep frequency during AC analysis. Follows same `__hertz__` sentinel pattern as TIME and TEMPER.

- [ ] **Step 1: Add HERTZ parsing**

In `src/devices/asrc/expression_ast.cpp`, after the TEMPER block, add:

```cpp
if (lname == "hertz") {
    VarRef ref;
    ref.kind = VarKind::NODE_VOLTAGE;
    ref.name1 = "__hertz__";
    int idx = get_or_add_var(ref);
    auto node = std::make_unique<ASTNode>();
    node->type = NodeType::VARIABLE;
    node->var_idx = idx;
    return node;
}
```

- [ ] **Step 2: Add ac_freq to IntegratorCtx**

In `src/core/circuit.hpp` (or wherever IntegratorCtx is defined), add:

```cpp
double ac_freq = 0.0;
```

- [ ] **Step 3: Set ac_freq in AC solver**

Find the AC sweep loop (in `src/core/ac.cpp` or similar). Where the frequency is set for each sweep point, also set:

```cpp
ic.ac_freq = freq;
```

(where `ic` is the IntegratorCtx and `freq` is the current AC frequency in Hz).

- [ ] **Step 4: Detect hertz_var_idx_ and fill in evaluate**

In `src/devices/asrc/asrc_device.hpp`, add:

```cpp
int hertz_var_idx_ = -1;
```

In the ASRCDevice constructor (asrc_device.cpp), detect `__hertz__` like `__temper__`.

In the evaluate method, where var_values are filled:

```cpp
if (hertz_var_idx_ >= 0) {
    double freq = 0.0;
    if (tls_integrator_ctx)
        freq = tls_integrator_ctx->ac_freq;
    var_values_[hertz_var_idx_] = freq;
}
```

- [ ] **Step 5: Create test circuit**

Create `tests/circuits/asrc_hertz.cir`:

```spice
ASRC HERTZ test - frequency-dependent impedance
V1 in 0 AC 1
B1 out 0 I = V(in) * HERTZ * 6.2832e-9
R1 out 0 1k
.ac dec 10 1k 1meg
.end
```

- [ ] **Step 6: Add ngspice comparison test**

In `tests/unit/test_ngspice_compare.cpp`:

```cpp
TEST_F(NgspiceCompareTest, AsrcHertz) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/asrc_hertz.cir";
    auto ng_result = ngspice_->run_ac(path);
    auto ckt = sim_.load(path);
    auto cs_result = sim_.run(ckt);
    ASSERT_TRUE(cs_result.ac.has_value());
    auto cmp = compare_ac(ng_result, *cs_result.ac, {1e-3, 1e-9});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}
```

- [ ] **Step 7: Build and test**

Run: `cmake --build build-release -j$(nproc) && build-release/tests/neospice_tests --gtest_filter='NgspiceCompare*AsrcHertz'`

Expected: PASS

- [ ] **Step 8: Commit**

```bash
git add src/devices/asrc/expression_ast.cpp src/devices/asrc/asrc_device.hpp \
        src/devices/asrc/asrc_device.cpp src/core/circuit.hpp src/core/ac.cpp \
        tests/circuits/asrc_hertz.cir tests/unit/test_ngspice_compare.cpp
git commit -m "feat: add HERTZ variable support in ASRC expressions"
```

---

### Task 6: ASRC PWL Function

**Files:**
- Modify: `src/devices/asrc/expression_ast.hpp` (add PWL node type, PWL data storage)
- Modify: `src/devices/asrc/expression_ast.cpp` (parse and evaluate PWL)
- Create: `tests/circuits/asrc_pwl.cir` (test circuit)
- Modify: `tests/unit/test_ngspice_compare.cpp` (add comparison test)

**Context:** PWL(x, x1,y1, x2,y2, ...) is piecewise-linear interpolation. The x-argument is an expression (typically V(node) or TIME), and the breakpoints are numeric constants.

- [ ] **Step 1: Add PWL AST node type**

In `src/devices/asrc/expression_ast.hpp`, add to `NodeType` enum:

```cpp
PWL,            // PWL(x, x1,y1, x2,y2, ...) — piecewise linear
```

Modify `ASTNode` to store PWL breakpoint data:

```cpp
struct ASTNode {
    NodeType type;
    double   value = 0.0;
    int      var_idx = -1;
    std::unique_ptr<ASTNode> left;
    std::unique_ptr<ASTNode> mid;
    std::unique_ptr<ASTNode> right;
    std::vector<std::pair<double,double>> pwl_points;  // for PWL node
};
```

- [ ] **Step 2: Parse PWL in expression_ast.cpp**

In `parse_function()`, add a case for `"pwl"`:

```cpp
if (name == "pwl") {
    auto x_arg = parse_additive();
    std::vector<std::pair<double,double>> points;
    while (true) {
        skip_ws();
        if (peek() == ')') break;
        if (!match(','))
            throw ParseError("PWL: expected ',' between arguments");
        skip_ws();
        auto x_node = parse_additive();
        if (x_node->type != NodeType::CONSTANT)
            throw ParseError("PWL: breakpoint x-values must be constants");
        if (!match(','))
            throw ParseError("PWL: expected ',' between x and y");
        auto y_node = parse_additive();
        if (y_node->type != NodeType::CONSTANT)
            throw ParseError("PWL: breakpoint y-values must be constants");
        points.emplace_back(x_node->value, y_node->value);
    }
    if (!match(')'))
        throw ParseError("PWL: missing closing ')'");
    auto node = std::make_unique<ASTNode>();
    node->type = NodeType::PWL;
    node->left = std::move(x_arg);
    node->pwl_points = std::move(points);
    return node;
}
```

- [ ] **Step 3: Evaluate PWL in eval_node**

In `CompiledExpression::eval_node()`, add a case for `NodeType::PWL`:

```cpp
case NodeType::PWL: {
    auto x = eval_node(node->left.get(), var_values, num_vars, need_grad);
    const auto& pts = node->pwl_points;
    if (pts.empty()) return {0.0, std::vector<double>(num_vars, 0.0)};

    double xv = x.val;
    double yv = 0.0;
    double dydx = 0.0;

    if (xv <= pts.front().first) {
        yv = pts.front().second;
        dydx = 0.0;
    } else if (xv >= pts.back().first) {
        yv = pts.back().second;
        dydx = 0.0;
    } else {
        for (size_t i = 0; i + 1 < pts.size(); ++i) {
            if (xv >= pts[i].first && xv <= pts[i+1].first) {
                double dx = pts[i+1].first - pts[i].first;
                double dy = pts[i+1].second - pts[i].second;
                double frac = (dx > 0.0) ? (xv - pts[i].first) / dx : 0.0;
                yv = pts[i].second + frac * dy;
                dydx = (dx > 0.0) ? dy / dx : 0.0;
                break;
            }
        }
    }

    DualNumber result;
    result.val = yv;
    if (need_grad) {
        result.grad.resize(num_vars, 0.0);
        for (int i = 0; i < num_vars; ++i)
            result.grad[i] = dydx * x.grad[i];
    }
    return result;
}
```

- [ ] **Step 4: Create test circuit**

Create `tests/circuits/asrc_pwl.cir`:

```spice
ASRC PWL function test
V1 in 0 1.5
B1 out 0 V = PWL(V(in), 0,0, 1,1, 2,3, 3,3)
R1 out 0 1k
.dc V1 0 3 0.1
.end
```

Wait — this is a DC sweep, but ngspice_runner needs to handle sweeps. Let's use a simpler DC OP test:

```spice
ASRC PWL function test
V1 in 0 1.5
B1 out 0 V = PWL(V(in), 0,0, 1,1, 2,3, 3,3)
R1 out 0 1k
.op
.end
```

- [ ] **Step 5: Add ngspice comparison test**

In `tests/unit/test_ngspice_compare.cpp`:

```cpp
TEST_F(NgspiceCompareTest, AsrcPwl) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/asrc_pwl.cir";
    auto ng_result = ngspice_->run_dc(path);
    auto ckt = sim_.load(path);
    auto cs_result = sim_.run_dc(ckt);
    auto cmp = compare_dc(ng_result, cs_result, {1e-6, 1e-12});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}
```

- [ ] **Step 6: Build and test**

Run: `cmake --build build-release -j$(nproc) && build-release/tests/neospice_tests --gtest_filter='NgspiceCompare*AsrcPwl'`

Expected: PASS

- [ ] **Step 7: Commit**

```bash
git add src/devices/asrc/expression_ast.hpp src/devices/asrc/expression_ast.cpp \
        tests/circuits/asrc_pwl.cir tests/unit/test_ngspice_compare.cpp
git commit -m "feat: add PWL function support in ASRC expressions"
```

---

## Phase 3: Parameter Completeness

### Task 7: Transmission Line IC= Initial Conditions

**Files:**
- Modify: `src/devices/tline.hpp` (add IC fields and setter)
- Modify: `src/devices/tline.cpp` (use IC values in init_dc_state)
- Modify: `src/parser/netlist_parser.cpp:1669-1712` (parse IC=)
- Create: `tests/circuits/tline_ic.cir` (test circuit)
- Modify: `tests/unit/test_ngspice_compare.cpp` (add comparison test)

**Context:** ngspice T-element supports `IC=V1,I1,V2,I2`. The parser currently reads Z0, TD, F, NL but ignores IC. The TransmissionLine already has `init_dc_state()` which seeds the history buffer — we just need to override those values when IC is given.

- [ ] **Step 1: Add IC storage to TransmissionLine**

In `src/devices/tline.hpp`, add to the private section:

```cpp
bool has_ic_ = false;
double ic_v1_ = 0.0, ic_i1_ = 0.0, ic_v2_ = 0.0, ic_i2_ = 0.0;
```

Add public setter:

```cpp
void set_ic(double v1, double i1, double v2, double i2);
bool has_ic() const { return has_ic_; }
```

- [ ] **Step 2: Implement set_ic and modify init_dc_state**

In `src/devices/tline.cpp`:

```cpp
void TransmissionLine::set_ic(double v1, double i1, double v2, double i2) {
    has_ic_ = true;
    ic_v1_ = v1; ic_i1_ = i1;
    ic_v2_ = v2; ic_i2_ = i2;
}
```

Modify `init_dc_state()`: after computing v1/v2 from the DC solution, override with IC values if present:

```cpp
void TransmissionLine::init_dc_state(const std::vector<double>& sol) {
    double v1, i1, v2, i2;
    if (has_ic_) {
        v1 = ic_v1_; i1 = ic_i1_;
        v2 = ic_v2_; i2 = ic_i2_;
    } else {
        double vp1p = (p1p_ >= 0) ? sol[p1p_] : 0.0;
        double vp1n = (p1n_ >= 0) ? sol[p1n_] : 0.0;
        double vp2p = (p2p_ >= 0) ? sol[p2p_] : 0.0;
        double vp2n = (p2n_ >= 0) ? sol[p2n_] : 0.0;
        v1 = vp1p - vp1n;
        v2 = vp2p - vp2n;
        i1 = 0.0;
        i2 = 0.0;
    }
    history_.clear();
    for (int k = 2; k >= 0; --k) {
        HistoryPoint hp;
        hp.time = -static_cast<double>(k) * td_;
        hp.v1 = v1; hp.i1 = i1;
        hp.v2 = v2; hp.i2 = i2;
        history_.push_back(hp);
    }
}
```

- [ ] **Step 3: Parse IC= in netlist parser**

In `src/parser/netlist_parser.cpp`, in the T-element parser (around line 1681-1691), add parsing for IC:

```cpp
double ic_v1 = 0, ic_i1 = 0, ic_v2 = 0, ic_i2 = 0;
bool ic_given = false;
```

In the parameter loop, add:

```cpp
else if (key == "ic") {
    // IC=V1,I1,V2,I2
    std::string ic_str = tokens[i].substr(eq + 1);
    // Parse comma-separated values
    std::vector<double> ic_vals;
    std::stringstream ss(ic_str);
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        ic_vals.push_back(parse_spice_number(tok));
    }
    if (ic_vals.size() >= 1) ic_v1 = ic_vals[0];
    if (ic_vals.size() >= 2) ic_i1 = ic_vals[1];
    if (ic_vals.size() >= 3) ic_v2 = ic_vals[2];
    if (ic_vals.size() >= 4) ic_i2 = ic_vals[3];
    ic_given = true;
}
```

After creating the TransmissionLine object and before `ckt.add_device()`:

```cpp
auto tl = std::make_unique<TransmissionLine>(tname, tp1p, tp1n, tp2p, tp2n, tz0, ttd);
if (ic_given)
    tl->set_ic(ic_v1, ic_i1, ic_v2, ic_i2);
ckt.add_device(std::move(tl));
```

- [ ] **Step 4: Create test circuit**

Create `tests/circuits/tline_ic.cir`:

```spice
Transmission Line IC test
V1 in 0 PULSE(0 1 0 1n 1n 50n 100n)
R1 in tl_in 50
T1 tl_in 0 tl_out 0 Z0=50 TD=10n IC=0.5,0,0.5,0
R2 tl_out 0 50
.tran 0.1n 100n UIC
.end
```

- [ ] **Step 5: Add ngspice comparison test**

In `tests/unit/test_ngspice_compare.cpp`:

```cpp
TEST_F(NgspiceCompareTest, TlineIC) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/tline_ic.cir";
    auto ng_result = ngspice_->run_transient(path);
    auto ckt = sim_.load(path);
    auto cs_result = sim_.run(ckt);
    ASSERT_TRUE(cs_result.transient.has_value());
    auto cmp = compare_transient(*cs_result.transient, ng_result, {1e-2, 1e-3});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}
```

- [ ] **Step 6: Build and test**

Run: `cmake --build build-release -j$(nproc) && build-release/tests/neospice_tests --gtest_filter='NgspiceCompare*TlineIC'`

Expected: PASS

- [ ] **Step 7: Commit**

```bash
git add src/devices/tline.hpp src/devices/tline.cpp src/parser/netlist_parser.cpp \
        tests/circuits/tline_ic.cir tests/unit/test_ngspice_compare.cpp
git commit -m "feat: add IC= initial condition support for transmission lines"
```

---

### Task 8: Resistor AC Resistance (RAC=)

**Files:**
- Modify: `src/devices/resistor.hpp` (add rac_ member and setter)
- Modify: `src/devices/resistor.cpp` (use rac_ in ac_stamp)
- Modify: `src/parser/netlist_parser.cpp` (parse RAC= on R elements)
- Create: `tests/circuits/resistor_rac.cir` (test circuit)
- Modify: `tests/unit/test_ngspice_compare.cpp` (add comparison test)

**Context:** ngspice's resistor supports RAC= for a different resistance value during AC analysis. neospice's `ac_stamp()` always uses `resistance_eff_` (the DC value).

- [ ] **Step 1: Add RAC to Resistor**

In `src/devices/resistor.hpp`, add to private:

```cpp
double rac_ = -1.0;  // AC resistance (-1 = use DC resistance)
```

Add to public:

```cpp
void set_rac(double r) { rac_ = r; }
```

- [ ] **Step 2: Use RAC in ac_stamp**

In `src/devices/resistor.cpp`, modify `ac_stamp()`:

```cpp
void Resistor::ac_stamp(const std::vector<double>& /*voltages*/,
                        NumericMatrix& G, NumericMatrix& /*C*/) {
    double r = (rac_ > 0.0) ? rac_ : resistance_eff_;
    const double g = 1.0 / r;
    add_if_valid(G, off_pp_,  g);
    add_if_valid(G, off_pn_, -g);
    add_if_valid(G, off_np_, -g);
    add_if_valid(G, off_nn_,  g);
}
```

- [ ] **Step 3: Parse RAC= in netlist parser**

In `src/parser/netlist_parser.cpp`, in the R-element parser (around line 1016-1030), add:

```cpp
else if (tok.starts_with("rac="))
    r->set_rac(parse_spice_number(tok.substr(4)));
```

- [ ] **Step 4: Create test circuit**

Create `tests/circuits/resistor_rac.cir`:

```spice
Resistor RAC test
V1 in 0 AC 1 DC 1
R1 in out 1k RAC=500
R2 out 0 1k
.ac dec 10 1 1meg
.end
```

- [ ] **Step 5: Add ngspice comparison test**

In `tests/unit/test_ngspice_compare.cpp`:

```cpp
TEST_F(NgspiceCompareTest, ResistorRAC) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/resistor_rac.cir";
    auto ng_result = ngspice_->run_ac(path);
    auto ckt = sim_.load(path);
    auto cs_result = sim_.run(ckt);
    ASSERT_TRUE(cs_result.ac.has_value());
    auto cmp = compare_ac(ng_result, *cs_result.ac, {1e-6, 1e-12});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}
```

- [ ] **Step 6: Build and test**

Run: `cmake --build build-release -j$(nproc) && build-release/tests/neospice_tests --gtest_filter='NgspiceCompare*ResistorRAC'`

Expected: PASS

- [ ] **Step 7: Commit**

```bash
git add src/devices/resistor.hpp src/devices/resistor.cpp \
        src/parser/netlist_parser.cpp tests/circuits/resistor_rac.cir \
        tests/unit/test_ngspice_compare.cpp
git commit -m "feat: add RAC= AC resistance parameter for resistors"
```

---

### Task 9: R/C Model Cards

**Files:**
- Create: `src/devices/resistor_model.hpp` (ResistorModel struct)
- Create: `src/devices/capacitor_model.hpp` (CapacitorModel struct)
- Modify: `src/parser/model_cards.hpp` (add to_resistor_model, to_capacitor_model)
- Modify: `src/parser/model_cards.cpp` (implement dispatchers)
- Modify: `src/parser/netlist_parser.cpp` (parse R/C model references)
- Create: `tests/circuits/resistor_model.cir` (test circuit with .model R)
- Modify: `tests/unit/test_ngspice_compare.cpp` (add comparison test)

**Context:** ngspice supports `.model RMOD R(TC1=3.9e-3 TC2=0)` to define shared resistor model parameters. neospice's parser currently has no R or C model card handling. Instance parameters override model parameters.

- [ ] **Step 1: Create ResistorModel struct**

Create `src/devices/resistor_model.hpp`:

```cpp
#pragma once

namespace neospice {

struct ResistorModel {
    double tc1 = 0.0;
    double tc2 = 0.0;
    double rac = -1.0;  // -1 = not specified
    double kf = 0.0;    // flicker noise coefficient
    double af = 1.0;    // flicker noise exponent
    double tnom = -1.0;  // -1 = use simulation default
};

} // namespace neospice
```

- [ ] **Step 2: Create CapacitorModel struct**

Create `src/devices/capacitor_model.hpp`:

```cpp
#pragma once

namespace neospice {

struct CapacitorModel {
    double tc1 = 0.0;
    double tc2 = 0.0;
    double vc1 = 0.0;   // voltage coefficient 1
    double vc2 = 0.0;   // voltage coefficient 2
    double tnom = -1.0;  // -1 = use simulation default
};

} // namespace neospice
```

- [ ] **Step 3: Add dispatchers in model_cards**

In `src/parser/model_cards.hpp`, add:

```cpp
#include "devices/resistor_model.hpp"
#include "devices/capacitor_model.hpp"
```

And declarations:

```cpp
ResistorModel to_resistor_model(const ModelCard& card);
CapacitorModel to_capacitor_model(const ModelCard& card);
```

In `src/parser/model_cards.cpp`, implement:

```cpp
ResistorModel to_resistor_model(const ModelCard& card) {
    ResistorModel m;
    for (const auto& [key, val] : card.params) {
        if (key == "tc1") m.tc1 = val;
        else if (key == "tc2") m.tc2 = val;
        else if (key == "rac") m.rac = val;
        else if (key == "kf") m.kf = val;
        else if (key == "af") m.af = val;
        else if (key == "tnom") m.tnom = val + 273.15;
    }
    return m;
}

CapacitorModel to_capacitor_model(const ModelCard& card) {
    CapacitorModel m;
    for (const auto& [key, val] : card.params) {
        if (key == "tc1") m.tc1 = val;
        else if (key == "tc2") m.tc2 = val;
        else if (key == "vc1") m.vc1 = val;
        else if (key == "vc2") m.vc2 = val;
        else if (key == "tnom") m.tnom = val + 273.15;
    }
    return m;
}
```

- [ ] **Step 4: Wire R/C model cards into the parser**

In `src/parser/netlist_parser.cpp`:

1. Add storage maps near other model card maps:

```cpp
std::unordered_map<std::string, ResistorModel> res_models;
std::unordered_map<std::string, CapacitorModel> cap_models;
```

2. In the .model processing section, handle "r" and "c" types:

```cpp
if (card.type == "r") {
    res_models[card.name] = to_resistor_model(card);
}
else if (card.type == "c") {
    cap_models[card.name] = to_capacitor_model(card);
}
```

3. In the R-element parser, check if any remaining token matches a model name and apply:

After parsing the R value and instance params, look for a model name token. If found in `res_models`, apply the model's TC1/TC2/RAC as defaults (instance params override):

```cpp
// Check for model reference in remaining tokens
for (size_t k = 4; k < tokens.size(); ++k) {
    std::string tok_lower = to_lower(tokens[k]);
    if (!tok_lower.contains('=')) {
        auto mit = res_models.find(tok_lower);
        if (mit != res_models.end()) {
            const auto& rm = mit->second;
            // Apply model defaults (instance overrides model)
            if (r->tc1() == 0.0 && rm.tc1 != 0.0) r->set_tc1(rm.tc1);
            if (r->tc2() == 0.0 && rm.tc2 != 0.0) r->set_tc2(rm.tc2);
            if (rm.rac > 0.0) r->set_rac(rm.rac);
        }
    }
}
```

Note: This requires adding `tc1()` and `tc2()` getters to Resistor if not already present.

Similarly for C-element and `cap_models`.

- [ ] **Step 5: Create test circuit**

Create `tests/circuits/resistor_model.cir`:

```spice
Resistor model card test
V1 in 0 1.0
R1 in out 1k RMOD
R2 out 0 1k
.model RMOD R(TC1=3.9e-3 TC2=-5.8e-7)
.options temp=85
.op
.end
```

- [ ] **Step 6: Add ngspice comparison test**

In `tests/unit/test_ngspice_compare.cpp`:

```cpp
TEST_F(NgspiceCompareTest, ResistorModelCard) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/resistor_model.cir";
    auto ng_result = ngspice_->run_dc(path);
    auto ckt = sim_.load(path);
    auto cs_result = sim_.run_dc(ckt);
    auto cmp = compare_dc(ng_result, cs_result, {1e-3, 1e-6});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}
```

- [ ] **Step 7: Build and test**

Run: `cmake --build build-release -j$(nproc) && build-release/tests/neospice_tests --gtest_filter='NgspiceCompare*ResistorModel'`

Expected: PASS

- [ ] **Step 8: Commit**

```bash
git add src/devices/resistor_model.hpp src/devices/capacitor_model.hpp \
        src/parser/model_cards.hpp src/parser/model_cards.cpp \
        src/parser/netlist_parser.cpp tests/circuits/resistor_model.cir \
        tests/unit/test_ngspice_compare.cpp
git commit -m "feat: add R/C model card support (.model RMOD R)"
```
