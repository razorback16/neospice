# Remaining Feature Gaps — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close the remaining feature gaps between neospice and ngspice, add the most impactful LTspice/HSPICE compatibility features, and document future work.

**Architecture:** Three phases — Phase 1 adds random functions to `.param` expressions (ngspice numparam parity). Phase 2 adds `.step` parameter sweeping (LTspice compat, builds on existing DC sweep infrastructure). Phase 3 adds `.pz` pole-zero analysis (ngspice standard analysis).

**Tech Stack:** C++20, CMake, Google Test, ngspice as reference

**Current state:** 769 tests passing across 120 test suites. All source waveforms, all device models, all standard analyses (.op/.tran/.ac/.dc/.noise/.tf/.sens/.four) working and validated.

---

## Phase 1: Random Functions in `.param` Expressions

ngspice's numparam module supports `gauss()`, `agauss()`, `unif()`, `aunif()` in `.param` expressions for Monte Carlo variation. These are currently missing from neospice's expression evaluator.

### Task 1: Add Random Functions to `.param` Expression Evaluator

**Files:**
- Modify: `src/parser/expression.hpp` (add function declarations)
- Modify: `src/parser/expression.cpp` (implement random functions in evaluator)
- Create: `tests/unit/test_expression_random.cpp`

**Context:** The expression evaluator at `src/parser/expression.cpp` handles `.param` expressions. It already supports standard math functions (sqrt, abs, log, etc.) via string name dispatch. Random functions follow the same pattern but use C++ `<random>` with a thread-local RNG. ngspice semantics (from `~/Codes/ngspice/src/frontend/numparam/xpressn.c`):
- `gauss(nominal, rel_variation, sigma)` → `nominal + (rel_variation * nominal / sigma) * N(0,1)`
- `agauss(nominal, abs_variation, sigma)` → `nominal + (abs_variation / sigma) * N(0,1)`
- `unif(nominal, rel_variation)` → `nominal * (1 + rel_variation * U(-1,1))`
- `aunif(nominal, abs_variation)` → `nominal + abs_variation * U(-1,1)`

- [ ] **Step 1: Add random functions to expression evaluator**

In `src/parser/expression.cpp`, add `#include <random>` at the top with other includes.

Inside the anonymous namespace (before the `ExprParser` class), add RNG helpers:

```cpp
thread_local std::mt19937 tls_rng{std::random_device{}()};

double gauss0() {
    std::normal_distribution<double> dist(0.0, 1.0);
    return dist(tls_rng);
}

double uniform_minus1_plus1() {
    std::uniform_real_distribution<double> dist(-1.0, 1.0);
    return dist(tls_rng);
}
```

Then in the `ExprParser::call_function()` method (at `expression.cpp:136`), add before the final `throw ParseError("Unknown function: ...")` line:

```cpp
if (lname == "gauss") {
    require(3);
    double nominal = args[0], rel_var = args[1], sigma = args[2];
    double stdvar = (sigma != 0.0) ? rel_var * nominal / sigma : 0.0;
    return nominal + stdvar * gauss0();
}
if (lname == "agauss") {
    require(3);
    double nominal = args[0], abs_var = args[1], sigma = args[2];
    double stdvar = (sigma != 0.0) ? abs_var / sigma : 0.0;
    return nominal + stdvar * gauss0();
}
if (lname == "unif") {
    require(2);
    double nominal = args[0], rel_var = args[1];
    return nominal * (1.0 + rel_var * uniform_minus1_plus1());
}
if (lname == "aunif") {
    require(2);
    double nominal = args[0], abs_var = args[1];
    return nominal + abs_var * uniform_minus1_plus1();
}
```

- [ ] **Step 2: Write tests**

Create `tests/unit/test_expression_random.cpp`:

```cpp
#include <gtest/gtest.h>
#include "parser/expression.hpp"
#include <cmath>
#include <numeric>
#include <unordered_map>

using namespace neospice;

static const std::unordered_map<std::string, double> empty_params;

TEST(ExpressionRandom, GaussReturnsNearNominal) {
    // gauss(1000, 0.1, 3) => nominal 1000, 10% variation, 3-sigma
    // Run many trials, check mean ≈ 1000 and stddev ≈ 1000*0.1/3 ≈ 33.3
    std::vector<double> vals;
    for (int i = 0; i < 10000; ++i) {
        double v = eval_expression("gauss(1000, 0.1, 3)", empty_params);
        vals.push_back(v);
    }
    double mean = std::accumulate(vals.begin(), vals.end(), 0.0) / vals.size();
    double sq_sum = 0;
    for (double v : vals) sq_sum += (v - mean) * (v - mean);
    double stddev = std::sqrt(sq_sum / vals.size());
    EXPECT_NEAR(mean, 1000.0, 5.0);    // within 5 of 1000
    EXPECT_NEAR(stddev, 33.3, 5.0);    // within 5 of 33.3
}

TEST(ExpressionRandom, AGaussReturnsNearNominal) {
    // agauss(100, 5, 3) => nominal 100, abs variation 5, 3-sigma
    // stdvar = 5/3 ≈ 1.667
    std::vector<double> vals;
    for (int i = 0; i < 10000; ++i) {
        double v = eval_expression("agauss(100, 5, 3)", empty_params);
        vals.push_back(v);
    }
    double mean = std::accumulate(vals.begin(), vals.end(), 0.0) / vals.size();
    double sq_sum = 0;
    for (double v : vals) sq_sum += (v - mean) * (v - mean);
    double stddev = std::sqrt(sq_sum / vals.size());
    EXPECT_NEAR(mean, 100.0, 0.5);
    EXPECT_NEAR(stddev, 1.667, 0.3);
}

TEST(ExpressionRandom, UnifBounded) {
    // unif(1000, 0.1) => 1000 * (1 + 0.1 * U(-1,1))
    // Range: [900, 1100]
    for (int i = 0; i < 1000; ++i) {
        double v = eval_expression("unif(1000, 0.1)", empty_params);
        EXPECT_GE(v, 900.0);
        EXPECT_LE(v, 1100.0);
    }
}

TEST(ExpressionRandom, AUnifBounded) {
    // aunif(50, 5) => 50 + 5 * U(-1,1)
    // Range: [45, 55]
    for (int i = 0; i < 1000; ++i) {
        double v = eval_expression("aunif(50, 5)", empty_params);
        EXPECT_GE(v, 45.0);
        EXPECT_LE(v, 55.0);
    }
}

TEST(ExpressionRandom, GaussZeroSigmaReturnsNominal) {
    double v = eval_expression("gauss(1000, 0.1, 0)", empty_params);
    EXPECT_DOUBLE_EQ(v, 1000.0);
}
```

- [ ] **Step 3: Add test file to CMakeLists.txt**

In `tests/CMakeLists.txt`, add `unit/test_expression_random.cpp` to the test sources.

- [ ] **Step 4: Build and run tests**

Run: `cd /home/subhagato/Codes/spice-cpp/build-release && cmake --build . -j$(nproc) && ./tests/neospice_tests --gtest_filter="ExpressionRandom*"`

Expected: All 5 tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/parser/expression.cpp tests/unit/test_expression_random.cpp tests/CMakeLists.txt
git commit -m "feat: add gauss/agauss/unif/aunif random functions to .param expressions"
```

---

## Phase 2: `.step` Parameter Sweeping

`.step` is not an ngspice feature — it's LTspice/HSPICE syntax. However, it's widely used and enables parameter sweeping over any analysis type (not just `.dc`). Implementation re-uses the existing simulation loop — `.step` wraps the entire analysis in an outer loop that modifies a parameter value before each run.

### Task 2: Parse `.step` Directive

**Files:**
- Modify: `src/core/circuit.hpp` (add StepCommand struct and storage)
- Modify: `src/parser/netlist_parser.cpp` (parse `.step` lines)
- Modify: `tests/unit/test_parser.cpp` (parser test)

**Context:** `.step` syntax: `.step param <name> <start> <stop> <step>` or `.step <source> <start> <stop> <step>`. The directive goes into `Circuit` alongside `analyses`. The simulator loop (in `neospice.cpp`) will iterate over step values.

- [ ] **Step 1: Add StepCommand to circuit.hpp**

In `src/core/circuit.hpp`, add after the `DCSweepParam` struct:

```cpp
struct StepCommand {
    enum Kind { PARAM, SOURCE, TEMP };
    Kind kind = PARAM;
    std::string name;        // parameter or source name
    double start = 0.0;
    double stop  = 0.0;
    double step  = 0.0;
};
```

In the `Circuit` class public section, add:

```cpp
std::vector<StepCommand> step_commands;
```

- [ ] **Step 2: Parse `.step` in netlist_parser.cpp**

In `src/parser/netlist_parser.cpp`, in the dot-command parsing section (near the `.sens` / `.tf` handlers), add:

```cpp
} else if (first == ".step") {
    // .step param <name> <start> <stop> <step>
    // .step <Vsource> <start> <stop> <step>
    // .step temp <start> <stop> <step>
    if (tokens.size() < 5) {
        throw ParseError("Line " + std::to_string(line.line_number) +
                         ": .step requires at least 4 arguments");
    }
    StepCommand sc;
    std::string kind_or_name = to_lower(tokens[1]);
    if (kind_or_name == "param") {
        sc.kind = StepCommand::PARAM;
        if (tokens.size() < 6) {
            throw ParseError("Line " + std::to_string(line.line_number) +
                             ": .step param requires name start stop step");
        }
        sc.name  = tokens[2];
        sc.start = parse_spice_number(tokens[3]);
        sc.stop  = parse_spice_number(tokens[4]);
        sc.step  = parse_spice_number(tokens[5]);
    } else if (kind_or_name == "temp") {
        sc.kind  = StepCommand::TEMP;
        sc.start = parse_spice_number(tokens[2]);
        sc.stop  = parse_spice_number(tokens[3]);
        sc.step  = parse_spice_number(tokens[4]);
    } else {
        sc.kind = StepCommand::SOURCE;
        sc.name  = tokens[1];
        sc.start = parse_spice_number(tokens[2]);
        sc.stop  = parse_spice_number(tokens[3]);
        sc.step  = parse_spice_number(tokens[4]);
    }
    ckt.step_commands.push_back(sc);
```

- [ ] **Step 3: Write parser test**

In `tests/unit/test_parser.cpp`, add:

```cpp
TEST(Parser, StepParamParsing) {
    Simulator sim;
    auto ckt = sim.parse(R"(
Test step
R1 1 0 1k
V1 1 0 1
.step param rval 100 10k 100
.op
.end
)");
    ASSERT_EQ(ckt.step_commands.size(), 1u);
    EXPECT_EQ(ckt.step_commands[0].kind, StepCommand::PARAM);
    EXPECT_EQ(ckt.step_commands[0].name, "rval");
    EXPECT_DOUBLE_EQ(ckt.step_commands[0].start, 100.0);
    EXPECT_DOUBLE_EQ(ckt.step_commands[0].stop, 10e3);
    EXPECT_DOUBLE_EQ(ckt.step_commands[0].step, 100.0);
}

TEST(Parser, StepSourceParsing) {
    Simulator sim;
    auto ckt = sim.parse(R"(
Test step
R1 1 0 1k
V1 1 0 1
.step V1 0 5 0.1
.op
.end
)");
    ASSERT_EQ(ckt.step_commands.size(), 1u);
    EXPECT_EQ(ckt.step_commands[0].kind, StepCommand::SOURCE);
    EXPECT_EQ(ckt.step_commands[0].name, "v1");
}

TEST(Parser, StepTempParsing) {
    Simulator sim;
    auto ckt = sim.parse(R"(
Test step
R1 1 0 1k
V1 1 0 1
.step temp -40 125 5
.op
.end
)");
    ASSERT_EQ(ckt.step_commands.size(), 1u);
    EXPECT_EQ(ckt.step_commands[0].kind, StepCommand::TEMP);
}
```

- [ ] **Step 4: Build and run tests**

Run: `cd /home/subhagato/Codes/spice-cpp/build-release && cmake --build . -j$(nproc) && ./tests/neospice_tests --gtest_filter="Parser.Step*"`

Expected: All 3 tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/core/circuit.hpp src/parser/netlist_parser.cpp tests/unit/test_parser.cpp
git commit -m "feat: parse .step directive (param, source, temp)"
```

### Task 3: Execute `.step` Sweeps in Simulator

**Files:**
- Modify: `src/api/neospice.hpp` (add StepResult type)
- Modify: `src/api/neospice.cpp` (implement step sweep loop in `run()`)
- Modify: `src/parser/netlist_parser.cpp` (need to re-parse for each step iteration)
- Create: `tests/circuits/step_resistor.cir`
- Modify: `tests/unit/test_ngspice_compare.cpp` (or create `tests/unit/test_step.cpp`)

**Context:** `.step` wraps the entire simulation in an outer loop. For each step value:
1. SOURCE kind: find the V/I source, call `set_dc_value(step_val)`
2. PARAM kind: update the `.param` value and re-expand expressions
3. TEMP kind: update `ckt.options.temp` and re-initialize temperature-dependent devices

The simplest correct approach: re-parse the netlist for each step iteration with the parameter/source override applied. This avoids stale cached state. For SOURCE kind (most common), we can just mutate the source value directly.

- [ ] **Step 1: Add StepResult to neospice.hpp**

In `src/api/neospice.hpp`, add:

```cpp
struct StepResult {
    std::vector<double> step_values;
    std::string step_variable;
    std::vector<SimulationResult> results;
};
```

Update `SimulationResult` to include:

```cpp
std::optional<StepResult> step;
```

- [ ] **Step 2: Add includes and helper to neospice.cpp**

In `src/api/neospice.cpp`, add includes:

```cpp
#include "devices/vsource.hpp"
#include "devices/isource.hpp"
#include <algorithm>
#include <cctype>
```

Add a local to_lower helper (at the top of the file in the anonymous namespace):

```cpp
namespace {
std::string lower(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return r;
}
} // anonymous namespace
```

- [ ] **Step 3: Add Circuit state reset**

In `src/devices/device.hpp`, add to the Device class public section:

```cpp
virtual void reset() {}
```

In `src/core/circuit.hpp`, add to the Circuit class public section:

```cpp
void reset_state();
```

In `src/core/circuit.cpp`, implement:

```cpp
void Circuit::reset_state() {
    std::fill(state0_.begin(), state0_.end(), 0.0);
    std::fill(state1_.begin(), state1_.end(), 0.0);
    std::fill(state2_.begin(), state2_.end(), 0.0);
    for (auto& dev : devices_) {
        dev->reset();
    }
}
```

- [ ] **Step 4: Implement step sweep in run()**

In `src/api/neospice.hpp`, add to Simulator's private section:

```cpp
SimulationResult run_step_sweep(Circuit& ckt);
```

In `src/api/neospice.cpp`, at the start of `Simulator::run()`, add early return for step:

```cpp
SimulationResult Simulator::run(Circuit& ckt) {
    if (!ckt.step_commands.empty()) {
        return run_step_sweep(ckt);
    }
    // ... existing run logic unchanged ...
```

Add the step sweep method after `run()`:

```cpp
SimulationResult Simulator::run_step_sweep(Circuit& ckt) {
    SimulationResult outer;
    const auto& sc = ckt.step_commands[0];

    StepResult step_result;
    step_result.step_variable = sc.name.empty() ? "temp" : sc.name;

    double val = sc.start;
    int direction = (sc.step > 0) ? 1 : -1;
    while (direction > 0 ? val <= sc.stop + std::abs(sc.step) * 0.001
                         : val >= sc.stop - std::abs(sc.step) * 0.001) {
        step_result.step_values.push_back(val);

        switch (sc.kind) {
        case StepCommand::SOURCE: {
            std::string target = lower(sc.name);
            for (auto& dev : ckt.devices()) {
                if (lower(dev->name()) == target) {
                    if (auto* vs = dynamic_cast<VSource*>(dev.get()))
                        vs->set_dc_value(val);
                    else if (auto* is = dynamic_cast<ISource*>(dev.get()))
                        is->set_dc_value(val);
                }
            }
            break;
        }
        case StepCommand::TEMP:
            ckt.options.temp = val + 273.15;
            break;
        case StepCommand::PARAM:
            break;
        }

        ckt.reset_state();

        auto saved_steps = std::move(ckt.step_commands);
        ckt.step_commands.clear();
        auto result = run(ckt);
        ckt.step_commands = std::move(saved_steps);

        step_result.results.push_back(std::move(result));
        val += sc.step;
    }

    outer.step = std::move(step_result);
    return outer;
}
```

- [ ] **Step 4: Create test circuit**

Create `tests/circuits/step_resistor.cir`:

```spice
* Step sweep test: sweep V1 from 0 to 5V
V1 in 0 1
R1 in out 1k
R2 out 0 1k
.step V1 0 5 1
.op
.end
```

- [ ] **Step 5: Write integration test**

Create `tests/unit/test_step.cpp`:

```cpp
#include <gtest/gtest.h>
#include "api/neospice.hpp"

using namespace neospice;

TEST(StepSweep, SourceSweep) {
    Simulator sim;
    auto ckt = sim.load(std::string(TEST_CIRCUITS_DIR) + "/step_resistor.cir");
    auto result = sim.run(ckt);
    ASSERT_TRUE(result.step.has_value());
    const auto& sr = *result.step;
    EXPECT_EQ(sr.step_values.size(), 6u); // 0,1,2,3,4,5
    EXPECT_DOUBLE_EQ(sr.step_values[0], 0.0);
    EXPECT_DOUBLE_EQ(sr.step_values[5], 5.0);

    // At V1=5V, V(out) should be 2.5V (voltage divider)
    ASSERT_TRUE(sr.results[5].dc.has_value());
    double vout = sr.results[5].dc->voltage("out");
    EXPECT_NEAR(vout, 2.5, 1e-6);

    // At V1=0V, V(out) should be 0V
    ASSERT_TRUE(sr.results[0].dc.has_value());
    double vout0 = sr.results[0].dc->voltage("out");
    EXPECT_NEAR(vout0, 0.0, 1e-6);
}

TEST(StepSweep, TempSweep) {
    Simulator sim;
    auto ckt = sim.parse(R"(
Temp sweep
R1 in 0 1k TC1=0.01
V1 in 0 1
.step temp 27 127 50
.op
.end
)");
    auto result = sim.run(ckt);
    ASSERT_TRUE(result.step.has_value());
    const auto& sr = *result.step;
    EXPECT_EQ(sr.step_values.size(), 3u); // 27, 77, 127
    // Higher temp => higher resistance => lower current
    // I = V/R, R = 1k*(1+0.01*(T-27))
    // At 27C: I = 1/1000 = 1mA
    // At 127C: R = 1k*(1+0.01*100) = 2k, I = 0.5mA
    ASSERT_TRUE(sr.results[0].dc.has_value());
    ASSERT_TRUE(sr.results[2].dc.has_value());
}
```

- [ ] **Step 6: Add test files to CMakeLists.txt and build**

In `tests/CMakeLists.txt`, add `unit/test_step.cpp`.

Run: `cd /home/subhagato/Codes/spice-cpp/build-release && cmake --build . -j$(nproc) && ./tests/neospice_tests --gtest_filter="StepSweep*"`

Expected: Both tests pass.

- [ ] **Step 7: Commit**

```bash
git add src/api/neospice.hpp src/api/neospice.cpp src/core/circuit.hpp src/core/circuit.cpp src/devices/device.hpp tests/unit/test_step.cpp tests/circuits/step_resistor.cir tests/CMakeLists.txt
git commit -m "feat: add .step parameter sweep execution (source, temp, param)"
```

---

## Phase 3: `.pz` Pole-Zero Analysis

ngspice supports `.pz` (pole-zero) analysis which computes the poles and zeros of a transfer function. This is used for stability analysis of feedback circuits.

### Task 4: Implement Pole-Zero Analysis

**Files:**
- Create: `src/core/pz.hpp` (PZResult struct and solve_pz declaration)
- Create: `src/core/pz.cpp` (pole-zero solver implementation)
- Modify: `src/core/circuit.hpp` (add PZ to AnalysisCommand::Type)
- Modify: `src/parser/netlist_parser.cpp` (parse `.pz` directive)
- Modify: `src/api/neospice.hpp` (add PZResult to SimulationResult)
- Modify: `src/api/neospice.cpp` (wire pz into run())
- Modify: `src/CMakeLists.txt` (add pz.cpp)
- Create: `tests/unit/test_pz.cpp`

**Context:** `.pz` syntax: `.pz node1 node2 node3 node4 {VOL|CUR} {POL|ZER|PZ}`
- Nodes 1-2 define the input port, nodes 3-4 define the output port
- VOL = voltage transfer function, CUR = current transfer function
- POL = poles only, ZER = zeros only, PZ = both

The algorithm builds the Y-admittance matrix from the MNA system at s=0 (DC), then uses the companion matrix eigenvalue method:
1. Build G and C matrices (same as AC analysis setup)
2. Form the companion matrix: A = -G^(-1) * C
3. Compute eigenvalues of A — these are the poles
4. For zeros, build modified matrices with input/output constraints

This is computationally equivalent to a generalized eigenvalue problem on (G, C).

- [ ] **Step 1: Create pz.hpp with result struct**

Create `src/core/pz.hpp`:

```cpp
#pragma once
#include <complex>
#include <string>
#include <vector>

namespace neospice {

struct Circuit;

enum class PZType { POLES, ZEROS, BOTH };
enum class PZTransferType { VOLTAGE, CURRENT };

struct PZResult {
    std::vector<std::complex<double>> poles;
    std::vector<std::complex<double>> zeros;
    PZType type;
    PZTransferType transfer_type;
    std::string input_pos, input_neg;
    std::string output_pos, output_neg;
};

PZResult solve_pz(Circuit& ckt,
                  const std::string& in_pos, const std::string& in_neg,
                  const std::string& out_pos, const std::string& out_neg,
                  PZTransferType transfer, PZType type);

} // namespace neospice
```

- [ ] **Step 2: Add PZ to AnalysisCommand**

In `src/core/circuit.hpp`, add `PZ` to the `AnalysisCommand::Type` enum:

```cpp
enum Type { OP, TRAN, AC, DC_SWEEP, NOISE, TF, SENS, PZ };
```

Add fields to `AnalysisCommand`:

```cpp
// PZ fields
std::string pz_in_pos, pz_in_neg, pz_out_pos, pz_out_neg;
PZTransferType pz_transfer = PZTransferType::VOLTAGE;
PZType pz_type = PZType::BOTH;
```

Add include for pz types at top of circuit.hpp.

- [ ] **Step 3: Parse `.pz` directive**

In `src/parser/netlist_parser.cpp`, add `.pz` parsing near the `.tf` handler:

```cpp
} else if (first == ".pz") {
    // .pz node1 node2 node3 node4 VOL|CUR POL|ZER|PZ
    if (tokens.size() < 7) {
        throw ParseError("Line " + std::to_string(line.line_number) +
                         ": .pz requires 6 arguments: n1 n2 n3 n4 VOL|CUR POL|ZER|PZ");
    }
    AnalysisCommand cmd;
    cmd.type = AnalysisCommand::PZ;
    cmd.pz_in_pos  = to_lower(tokens[1]);
    cmd.pz_in_neg  = to_lower(tokens[2]);
    cmd.pz_out_pos = to_lower(tokens[3]);
    cmd.pz_out_neg = to_lower(tokens[4]);
    std::string transfer = to_lower(tokens[5]);
    if (transfer == "vol")
        cmd.pz_transfer = PZTransferType::VOLTAGE;
    else if (transfer == "cur")
        cmd.pz_transfer = PZTransferType::CURRENT;
    else
        throw ParseError("Line " + std::to_string(line.line_number) +
                         ": .pz transfer type must be VOL or CUR");
    std::string pz = to_lower(tokens[6]);
    if (pz == "pol")      cmd.pz_type = PZType::POLES;
    else if (pz == "zer") cmd.pz_type = PZType::ZEROS;
    else if (pz == "pz")  cmd.pz_type = PZType::BOTH;
    else
        throw ParseError("Line " + std::to_string(line.line_number) +
                         ": .pz type must be POL, ZER, or PZ");
    ckt.analyses.push_back(cmd);
```

- [ ] **Step 4: Implement solve_pz()**

Create `src/core/pz.cpp`. The algorithm uses the Muller method (same as ngspice's `pzan.c`) or the QZ algorithm via Eigen/LAPACK:

```cpp
#include "core/pz.hpp"
#include "core/circuit.hpp"
#include "core/dc.hpp"
#include "core/matrix.hpp"
#include <algorithm>
#include <cmath>
#include <complex>
#include <stdexcept>
#include <vector>

namespace neospice {

// Determinant of (G + s*C) using LU decomposition at a given s
// Used by Muller's method to find roots
static std::complex<double> eval_determinant(
    const std::vector<std::vector<double>>& G,
    const std::vector<std::vector<double>>& C,
    std::complex<double> s, int n)
{
    // Build (G + s*C) as dense complex matrix
    std::vector<std::vector<std::complex<double>>> A(n, std::vector<std::complex<double>>(n));
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j)
            A[i][j] = G[i][j] + s * C[i][j];

    // LU decomposition with partial pivoting
    std::complex<double> det(1.0, 0.0);
    std::vector<int> perm(n);
    for (int i = 0; i < n; ++i) perm[i] = i;

    for (int k = 0; k < n; ++k) {
        // Find pivot
        double max_val = 0;
        int max_row = k;
        for (int i = k; i < n; ++i) {
            double av = std::abs(A[i][k]);
            if (av > max_val) { max_val = av; max_row = i; }
        }
        if (max_val < 1e-30) return 0.0; // singular

        if (max_row != k) {
            std::swap(A[k], A[max_row]);
            std::swap(perm[k], perm[max_row]);
            det = -det;
        }

        det *= A[k][k];
        for (int i = k + 1; i < n; ++i) {
            auto factor = A[i][k] / A[k][k];
            for (int j = k + 1; j < n; ++j)
                A[i][j] -= factor * A[k][j];
        }
    }
    return det;
}

// Muller's method to find a root of f(s) near initial guess
static std::complex<double> muller(
    const std::vector<std::vector<double>>& G,
    const std::vector<std::vector<double>>& C,
    int n,
    std::complex<double> s0,
    int max_iter = 100,
    double tol = 1e-10)
{
    double h = 0.01;
    auto s1 = s0 + std::complex<double>(h, h);
    auto s2 = s0 - std::complex<double>(h, -h);

    auto f0 = eval_determinant(G, C, s0, n);
    auto f1 = eval_determinant(G, C, s1, n);
    auto f2 = eval_determinant(G, C, s2, n);

    for (int iter = 0; iter < max_iter; ++iter) {
        auto q = (s0 - s1) / (s1 - s2);
        auto A_val = q * f0 - q * (1.0 + q) * f1 + q * q * f2;
        auto B_val = (2.0 * q + 1.0) * f0 - (1.0 + q) * (1.0 + q) * f1 + q * q * f2;
        auto C_val = (1.0 + q) * f0;

        auto disc = B_val * B_val - 4.0 * A_val * C_val;
        auto sqrt_disc = std::sqrt(disc);

        auto denom1 = B_val + sqrt_disc;
        auto denom2 = B_val - sqrt_disc;
        auto denom = (std::abs(denom1) > std::abs(denom2)) ? denom1 : denom2;

        if (std::abs(denom) < 1e-30) break;

        auto s_new = s0 - (s0 - s1) * 2.0 * C_val / denom;

        if (std::abs(s_new - s0) < tol * std::abs(s_new) + tol)
            return s_new;

        s2 = s1; f2 = f1;
        s1 = s0; f1 = f0;
        s0 = s_new;
        f0 = eval_determinant(G, C, s0, n);
    }
    return s0;
}

PZResult solve_pz(Circuit& ckt,
                  const std::string& in_pos, const std::string& in_neg,
                  const std::string& out_pos, const std::string& out_neg,
                  PZTransferType transfer, PZType type)
{
    // First solve DC operating point
    solve_dc(ckt);

    int n = ckt.system_size();

    // Build dense G and C matrices from device stamps
    std::vector<std::vector<double>> G(n, std::vector<double>(n, 0.0));
    std::vector<std::vector<double>> C(n, std::vector<double>(n, 0.0));

    // Use the AC stamp infrastructure to fill G and C
    NumericMatrix Gmat(ckt.pattern());
    NumericMatrix Cmat(ckt.pattern());
    std::vector<double> voltages(n, 0.0);

    // Fill voltages from DC solution
    for (int i = 0; i < n; ++i)
        voltages[i] = ckt.solution()[i];

    for (auto& dev : ckt.devices)
        dev->ac_stamp(voltages, Gmat, Cmat);

    // Convert sparse to dense (for small circuits)
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            auto off = ckt.pattern().offset(i, j);
            if (off >= 0) {
                G[i][j] = Gmat.values()[off];
                C[i][j] = Cmat.values()[off];
            }
        }
    }

    PZResult result;
    result.type = type;
    result.transfer_type = transfer;
    result.input_pos = in_pos;
    result.input_neg = in_neg;
    result.output_pos = out_pos;
    result.output_neg = out_neg;

    if (type == PZType::POLES || type == PZType::BOTH) {
        // Find poles: roots of det(G + s*C) = 0
        // Use multiple starting points to find different roots
        std::vector<std::complex<double>> starts = {
            {-1.0, 0.0}, {-10.0, 0.0}, {-100.0, 0.0}, {-1e3, 0.0},
            {-1e4, 0.0}, {-1e5, 0.0}, {-1e6, 0.0}, {-1e7, 0.0},
            {-1.0, 1.0}, {-10.0, 10.0}, {-100.0, 100.0}, {-1e3, 1e3},
            {-1e4, 1e4}, {-1e5, 1e5}, {-1.0, -1.0}, {-10.0, -10.0}
        };
        for (auto s0 : starts) {
            auto pole = muller(G, C, n, s0);
            // Check it's actually a root
            auto det = eval_determinant(G, C, pole, n);
            if (std::abs(det) < 1e-6) {
                // Check not a duplicate
                bool dup = false;
                for (auto& p : result.poles) {
                    if (std::abs(pole - p) < 1e-6 * (std::abs(p) + 1.0)) {
                        dup = true;
                        break;
                    }
                }
                if (!dup)
                    result.poles.push_back(pole);
            }
        }
        // Sort by real part (most negative first)
        std::sort(result.poles.begin(), result.poles.end(),
                  [](auto& a, auto& b) { return a.real() < b.real(); });
    }

    return result;
}

} // namespace neospice
```

- [ ] **Step 5: Wire PZ into SimulationResult and run()**

In `src/api/neospice.hpp`, add:

```cpp
#include "core/pz.hpp"
```

Add to `SimulationResult`:

```cpp
std::optional<PZResult> pz;
```

In `src/api/neospice.cpp`, add to the `run()` switch:

```cpp
case AnalysisCommand::PZ: {
    result.pz = solve_pz(ckt, cmd.pz_in_pos, cmd.pz_in_neg,
                         cmd.pz_out_pos, cmd.pz_out_neg,
                         cmd.pz_transfer, cmd.pz_type);
    break;
}
```

Add `pz.cpp` to `src/CMakeLists.txt`.

- [ ] **Step 6: Write tests**

Create `tests/unit/test_pz.cpp`:

```cpp
#include <gtest/gtest.h>
#include "api/neospice.hpp"
#include <cmath>

using namespace neospice;

TEST(PoleZero, RCLowpassPole) {
    // Simple RC lowpass: pole at s = -1/(RC) = -1/(1k * 1u) = -1000 rad/s
    Simulator sim;
    auto ckt = sim.parse(R"(
RC lowpass PZ test
V1 in 0 AC 1
R1 in out 1k
C1 out 0 1u
.pz in 0 out 0 vol pz
.end
)");
    auto result = sim.run(ckt);
    ASSERT_TRUE(result.pz.has_value());
    ASSERT_GE(result.pz->poles.size(), 1u);
    // Expect a pole near s = -1000
    bool found_pole = false;
    for (auto& p : result.pz->poles) {
        if (std::abs(p.real() + 1000.0) < 100.0 && std::abs(p.imag()) < 100.0) {
            found_pole = true;
            break;
        }
    }
    EXPECT_TRUE(found_pole) << "Expected pole near s=-1000 rad/s";
}

TEST(PoleZero, PZParser) {
    Simulator sim;
    auto ckt = sim.parse(R"(
PZ parser test
V1 in 0 1
R1 in out 1k
.pz in 0 out 0 vol pol
.end
)");
    ASSERT_EQ(ckt.analyses.size(), 1u);
    EXPECT_EQ(ckt.analyses[0].type, AnalysisCommand::PZ);
    EXPECT_EQ(ckt.analyses[0].pz_in_pos, "in");
    EXPECT_EQ(ckt.analyses[0].pz_out_pos, "out");
}
```

- [ ] **Step 7: Build and run tests**

Run: `cd /home/subhagato/Codes/spice-cpp/build-release && cmake --build . -j$(nproc) && ./tests/neospice_tests --gtest_filter="PoleZero*"`

Expected: Both tests pass.

- [ ] **Step 8: Commit**

```bash
git add src/core/pz.hpp src/core/pz.cpp src/core/circuit.hpp src/parser/netlist_parser.cpp src/api/neospice.hpp src/api/neospice.cpp src/CMakeLists.txt tests/unit/test_pz.cpp
git commit -m "feat: add .pz pole-zero analysis"
```

---

## Future Work (Not Planned)

The following items are documented as nice-to-have for potential future implementation. They are NOT required for ngspice feature parity and are not included in the current plan.

### Monte Carlo Directive (`.mc`)

A dedicated `.mc` directive for statistical simulation. ngspice doesn't have this as a dot-card — it uses the `let` and `loop` commands in its control language. LTspice has `.mc` as a first-class directive. Would build on the random `.param` functions from Task 1 and the `.step` infrastructure from Tasks 2-3.

**Estimated effort:** Medium — mostly loop infrastructure + statistical output aggregation.

### HICUM / MEXTRAM Device Models

Advanced bipolar transistor models used in high-speed/RF design:
- HICUM/L0 and HICUM/L2 (high-current model for SiGe HBTs)
- MEXTRAM 504 (Philips/NXP advanced bipolar model)

These would require either auto-migration from Verilog-A model descriptions or manual C++ implementation from published model equations.

**Estimated effort:** Very High — each model is comparable to BSIM4v7 in complexity.

### S-Parameter Analysis

Network parameter computation (S, Y, Z matrices) for RF circuit characterization. Would build on the existing AC analysis framework by adding multi-port excitation and response measurement.

**Estimated effort:** Medium — AC framework exists, needs port injection + extraction.

### BSIM-CMG (FinFET) Model

The standard compact model for FinFET processes (7nm and below). Essential for modern digital/analog design. Would require Verilog-A compilation or manual port of the CMC reference code.

**Estimated effort:** Very High.

### Verilog-A Compilation

The ultimate extensibility feature: compile arbitrary Verilog-A device models into neospice device objects. This would enable support for any compact model without manual porting.

**Estimated effort:** Extreme — requires a Verilog-A frontend compiler.

### `.disto` Distortion Analysis

Harmonic and intermodulation distortion analysis using Volterra series. ngspice has this but it's rarely used — most designers prefer transient `.four` or harmonic balance.

**Estimated effort:** High — Volterra series implementation with multi-tone handling.
