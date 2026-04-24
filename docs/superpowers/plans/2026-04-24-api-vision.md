# API Vision Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement 5 API features from the neospice roadmap — noise accessor, circuit introspection, generic set_param(), analysis chaining, and CircuitBuilder.

**Architecture:** Features are grouped into 3 phases by file-dependency. Phase 1 runs 3 agents in parallel (noise accessor, analysis chaining, CircuitBuilder) since they touch disjoint files. Phase 2 runs 1 agent for introspection + set_param (both modify Device base + all device classes). Phase 3 verifies the full build and test suite.

**Tech Stack:** C++20, Google Test, CMake

**Build command:** `cmake --build build --target neospice_tests -j$(nproc)`
**Test command:** `build/tests/neospice_tests`
**Selective test:** `build/tests/neospice_tests --gtest_filter="TestSuite.TestName"`

---

## Phase 1: Parallel Independent Features

### Task 1: Noise Per-Device Accessor

**Files:**
- Modify: `src/core/noise.hpp:60-71`
- Modify: `tests/unit/test_api.cpp:157-194`

- [ ] **Step 1: Add `device_noise_density()` accessor to NoiseResult**

In `src/core/noise.hpp`, add this method after `device_names()` (after line 65):

```cpp
const std::vector<double>& device_noise_density(const std::string& name) const {
    auto it = device_noise.find(name);
    if (it != device_noise.end()) return it->second;
    throw std::out_of_range("Noise device not found: " + name);
}
```

- [ ] **Step 2: Add test for device_noise_density()**

In `tests/unit/test_api.cpp`, add after the existing `NoiseResultAPI.Helpers` test:

```cpp
TEST(NoiseResultAPI, DeviceNoiseDensity) {
    neospice::Simulator sim;
    std::string netlist = R"(
Noise per-device
V1 in 0 DC 0 AC 1
R1 in out 1k
R2 out 0 1k
.noise v(out) V1 dec 10 1 1meg
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<NoiseResult>(result.analysis));
    auto& nr = std::get<NoiseResult>(result.analysis);

    auto devs = nr.device_names();
    ASSERT_FALSE(devs.empty());
    for (const auto& d : devs) {
        const auto& density = nr.device_noise_density(d);
        ASSERT_EQ(density.size(), nr.frequency.size());
        for (auto v : density)
            EXPECT_GE(v, 0.0);
    }

    EXPECT_THROW(nr.device_noise_density("nonexistent"), std::out_of_range);
}
```

- [ ] **Step 3: Build and run tests**

Run: `cmake --build build --target neospice_tests -j$(nproc) && build/tests/neospice_tests --gtest_filter="NoiseResultAPI*"`
Expected: All pass.

- [ ] **Step 4: Commit**

```bash
git add src/core/noise.hpp tests/unit/test_api.cpp
git commit -m "feat: add NoiseResult::device_noise_density() per-device accessor"
```

---

### Task 2: Analysis Chaining (TransientOptions + ACOptions)

**Files:**
- Modify: `src/api/neospice.hpp:50-77`
- Modify: `src/api/neospice.cpp:74-85`
- Modify: `src/core/transient.hpp:56-59`
- Modify: `src/core/transient.cpp:567-571`
- Modify: `src/core/ac.hpp:114-115`
- Modify: `src/core/ac.cpp:27-28`
- Modify: `tests/unit/test_api.cpp` (append new tests)

- [ ] **Step 1: Add TransientOptions struct and overload to transient.hpp**

In `src/core/transient.hpp`, before the `solve_transient` declaration (before line 58), add:

```cpp
struct TransientOptions {
    const DCResult* ic_from = nullptr;
    bool uic = false;
};
```

And add this include at the top (after the existing includes):

```cpp
#include "core/dc.hpp"
```

Then change the `solve_transient` declaration to add an overload:

```cpp
TransientResult solve_transient(Circuit& ckt, double tstep, double tstop,
                                bool uic = false);
TransientResult solve_transient(Circuit& ckt, double tstep, double tstop,
                                const TransientOptions& opts);
```

- [ ] **Step 2: Implement TransientOptions overload in transient.cpp**

In `src/core/transient.cpp`, add this implementation after the existing `solve_transient`:

```cpp
TransientResult solve_transient(Circuit& ckt, double tstep, double tstop,
                                const TransientOptions& opts) {
    if (opts.ic_from) {
        for (const auto& [key, val] : opts.ic_from->node_voltages) {
            // key is "v(node)" — extract node name
            if (key.size() > 3 && key.front() == 'v' && key[1] == '(') {
                std::string node_name = key.substr(2, key.size() - 3);
                int32_t idx = ckt.node_index(node_name);
                if (idx >= 0) {
                    ckt.ic[idx] = val;
                }
            }
        }
    }
    return solve_transient(ckt, tstep, tstop, opts.uic);
}
```

- [ ] **Step 3: Add ACOptions struct and overload to ac.hpp**

In `src/core/ac.hpp`, before the `solve_ac` declaration, add:

```cpp
struct ACOptions {
    const DCResult* op_from = nullptr;
};
```

And add this include at the top (after existing includes):

```cpp
#include "core/dc.hpp"
```

Then add the overload:

```cpp
ACResult solve_ac(Circuit& ckt, ACMode mode,
                  int npoints, double fstart, double fstop);
ACResult solve_ac(Circuit& ckt, ACMode mode,
                  int npoints, double fstart, double fstop,
                  const ACOptions& opts);
```

- [ ] **Step 4: Implement ACOptions overload in ac.cpp**

In `src/core/ac.cpp`, add after the existing `solve_ac`:

```cpp
ACResult solve_ac(Circuit& ckt, ACMode mode,
                  int npoints, double fstart, double fstop,
                  const ACOptions& opts) {
    if (opts.op_from) {
        for (const auto& [key, val] : opts.op_from->node_voltages) {
            if (key.size() > 3 && key.front() == 'v' && key[1] == '(') {
                std::string node_name = key.substr(2, key.size() - 3);
                int32_t idx = ckt.node_index(node_name);
                if (idx >= 0) {
                    ckt.nodeset[idx] = val;
                }
            }
        }
    }
    return solve_ac(ckt, mode, npoints, fstart, fstop);
}
```

- [ ] **Step 5: Add Simulator overloads to neospice.hpp**

In `src/api/neospice.hpp`, add these method declarations to the Simulator class (after the existing `run_transient` and `run_ac`):

```cpp
TransientResult run_transient(Circuit& ckt, double tstep, double tstop,
                              const TransientOptions& opts);
ACResult run_ac(Circuit& ckt, ACMode mode,
                int npoints, double fstart, double fstop,
                const ACOptions& opts);
```

Add the includes for the options structs. The `TransientOptions` is in `transient.hpp` and `ACOptions` is in `ac.hpp`, both already included.

- [ ] **Step 6: Implement Simulator overloads in neospice.cpp**

In `src/api/neospice.cpp`, add these after the existing methods:

```cpp
TransientResult Simulator::run_transient(Circuit& ckt, double tstep, double tstop,
                                         const TransientOptions& opts) {
    auto result = solve_transient(ckt, tstep, tstop, opts);
    apply_save_filter(result, ckt.save_signals);
    return result;
}

ACResult Simulator::run_ac(Circuit& ckt, ACMode mode,
                           int npoints, double fstart, double fstop,
                           const ACOptions& opts) {
    auto result = solve_ac(ckt, mode, npoints, fstart, fstop, opts);
    apply_save_filter(result, ckt.save_signals);
    return result;
}
```

- [ ] **Step 7: Add tests for analysis chaining**

In `tests/unit/test_api.cpp`, add:

```cpp
TEST(AnalysisChaining, TransientFromDC) {
    neospice::Simulator sim;
    std::string netlist = R"(
Chain test
V1 in 0 10
R1 in out 1k
C1 out 0 1u
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto dc = sim.run_dc(ckt);
    EXPECT_NEAR(dc.voltage("out"), 10.0, 1e-6);

    // Re-parse for transient (circuit state was consumed by DC)
    auto ckt2 = sim.parse(R"(
Chain transient
V1 in 0 5
R1 in out 1k
C1 out 0 1u
.end
)");
    TransientOptions opts;
    opts.ic_from = &dc;
    auto tran = sim.run_transient(ckt2, 10e-6, 5e-3, opts);

    // First voltage point should be near 10V (from DC IC), not 0
    EXPECT_GT(tran.voltage("out").front(), 5.0);
}

TEST(AnalysisChaining, ACFromDC) {
    neospice::Simulator sim;
    std::string netlist = R"(
AC chain
V1 in 0 DC 5 AC 1
R1 in out 1k
R2 out 0 1k
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto dc = sim.run_dc(ckt);

    auto ckt2 = sim.parse(R"(
AC from DC
V1 in 0 DC 5 AC 1
R1 in out 1k
R2 out 0 1k
.end
)");
    ACOptions opts;
    opts.op_from = &dc;
    auto ac = sim.run_ac(ckt2, ACMode::DEC, 10, 100, 1e6, opts);
    EXPECT_GT(ac.frequency.size(), 10u);
    // Gain should be ~0.5 (-6dB) for voltage divider
    auto gain = ac.magnitude_db("out");
    EXPECT_NEAR(gain[0], -6.02, 0.5);
}
```

- [ ] **Step 8: Build and run tests**

Run: `cmake --build build --target neospice_tests -j$(nproc) && build/tests/neospice_tests --gtest_filter="AnalysisChaining*"`
Expected: All pass.

- [ ] **Step 9: Commit**

```bash
git add src/core/transient.hpp src/core/transient.cpp src/core/ac.hpp src/core/ac.cpp src/api/neospice.hpp src/api/neospice.cpp tests/unit/test_api.cpp
git commit -m "feat: add analysis chaining with TransientOptions and ACOptions"
```

---

### Task 3: CircuitBuilder

**Files:**
- Create: `src/api/circuit_builder.hpp`
- Create: `src/api/circuit_builder.cpp`
- Modify: `src/CMakeLists.txt` (add circuit_builder.cpp to library sources)
- Create: `tests/unit/test_circuit_builder.cpp`
- Modify: `tests/CMakeLists.txt` (add test file)

- [ ] **Step 1: Find the library source list in CMake**

Run: `grep -n "circuit_builder\|neospice.cpp\|add_library" src/CMakeLists.txt | head -10`

This will show where to add the new source file.

- [ ] **Step 2: Create circuit_builder.hpp**

Create `src/api/circuit_builder.hpp`:

```cpp
#pragma once
#include "core/circuit.hpp"
#include <map>
#include <string>
#include <vector>

namespace neospice {

struct SourceSpec {
    double dc = 0.0;
    double ac_mag = 0.0;
    double ac_phase = 0.0;
};

struct PulseSpec {
    double v1 = 0, v2 = 0, td = 0, tr = 0, tf = 0, pw = 0, per = 0;
};

struct SinSpec {
    double vo = 0, va = 0, freq = 0, td = 0, theta = 0, phase = 0;
};

class CircuitBuilder {
public:
    CircuitBuilder& title(const std::string& t);

    CircuitBuilder& resistor(const std::string& name,
                             const std::string& n1, const std::string& n2,
                             double value);
    CircuitBuilder& capacitor(const std::string& name,
                              const std::string& n1, const std::string& n2,
                              double value);
    CircuitBuilder& inductor(const std::string& name,
                             const std::string& n1, const std::string& n2,
                             double value);

    CircuitBuilder& vsource(const std::string& name,
                            const std::string& np, const std::string& nn,
                            const SourceSpec& spec);
    CircuitBuilder& vsource_pulse(const std::string& name,
                                  const std::string& np, const std::string& nn,
                                  const PulseSpec& spec);
    CircuitBuilder& vsource_sin(const std::string& name,
                                const std::string& np, const std::string& nn,
                                const SinSpec& spec);

    CircuitBuilder& isource(const std::string& name,
                            const std::string& np, const std::string& nn,
                            const SourceSpec& spec);

    CircuitBuilder& diode(const std::string& name,
                          const std::string& anode, const std::string& cathode,
                          const std::string& model_name);

    CircuitBuilder& subcircuit(const std::string& name,
                               const std::string& model,
                               const std::vector<std::string>& ports);

    CircuitBuilder& model(const std::string& name, const std::string& type,
                          const std::map<std::string, double>& params);

    CircuitBuilder& include(const std::string& filepath);

    CircuitBuilder& raw_line(const std::string& line);

    Circuit build();

private:
    std::string title_;
    std::vector<std::string> lines_;
    std::string format_value(double v) const;
};

} // namespace neospice
```

- [ ] **Step 3: Create circuit_builder.cpp**

Create `src/api/circuit_builder.cpp`:

```cpp
#include "api/circuit_builder.hpp"
#include "parser/netlist_parser.hpp"
#include <sstream>
#include <cmath>
#include <iomanip>

namespace neospice {

CircuitBuilder& CircuitBuilder::title(const std::string& t) {
    title_ = t;
    return *this;
}

std::string CircuitBuilder::format_value(double v) const {
    std::ostringstream oss;
    oss << std::setprecision(15) << v;
    return oss.str();
}

CircuitBuilder& CircuitBuilder::resistor(const std::string& name,
                                         const std::string& n1, const std::string& n2,
                                         double value) {
    lines_.push_back(name + " " + n1 + " " + n2 + " " + format_value(value));
    return *this;
}

CircuitBuilder& CircuitBuilder::capacitor(const std::string& name,
                                          const std::string& n1, const std::string& n2,
                                          double value) {
    lines_.push_back(name + " " + n1 + " " + n2 + " " + format_value(value));
    return *this;
}

CircuitBuilder& CircuitBuilder::inductor(const std::string& name,
                                         const std::string& n1, const std::string& n2,
                                         double value) {
    lines_.push_back(name + " " + n1 + " " + n2 + " " + format_value(value));
    return *this;
}

CircuitBuilder& CircuitBuilder::vsource(const std::string& name,
                                        const std::string& np, const std::string& nn,
                                        const SourceSpec& spec) {
    std::string line = name + " " + np + " " + nn;
    if (spec.dc != 0.0) line += " DC " + format_value(spec.dc);
    if (spec.ac_mag != 0.0) {
        line += " AC " + format_value(spec.ac_mag);
        if (spec.ac_phase != 0.0)
            line += " " + format_value(spec.ac_phase);
    }
    lines_.push_back(line);
    return *this;
}

CircuitBuilder& CircuitBuilder::vsource_pulse(const std::string& name,
                                              const std::string& np, const std::string& nn,
                                              const PulseSpec& spec) {
    std::string line = name + " " + np + " " + nn + " PULSE("
        + format_value(spec.v1) + " " + format_value(spec.v2) + " "
        + format_value(spec.td) + " " + format_value(spec.tr) + " "
        + format_value(spec.tf) + " " + format_value(spec.pw) + " "
        + format_value(spec.per) + ")";
    lines_.push_back(line);
    return *this;
}

CircuitBuilder& CircuitBuilder::vsource_sin(const std::string& name,
                                            const std::string& np, const std::string& nn,
                                            const SinSpec& spec) {
    std::string line = name + " " + np + " " + nn + " SIN("
        + format_value(spec.vo) + " " + format_value(spec.va) + " "
        + format_value(spec.freq) + " " + format_value(spec.td) + " "
        + format_value(spec.theta) + " " + format_value(spec.phase) + ")";
    lines_.push_back(line);
    return *this;
}

CircuitBuilder& CircuitBuilder::isource(const std::string& name,
                                        const std::string& np, const std::string& nn,
                                        const SourceSpec& spec) {
    std::string line = name + " " + np + " " + nn;
    if (spec.dc != 0.0) line += " DC " + format_value(spec.dc);
    if (spec.ac_mag != 0.0) {
        line += " AC " + format_value(spec.ac_mag);
        if (spec.ac_phase != 0.0)
            line += " " + format_value(spec.ac_phase);
    }
    lines_.push_back(line);
    return *this;
}

CircuitBuilder& CircuitBuilder::diode(const std::string& name,
                                      const std::string& anode, const std::string& cathode,
                                      const std::string& model_name) {
    lines_.push_back(name + " " + anode + " " + cathode + " " + model_name);
    return *this;
}

CircuitBuilder& CircuitBuilder::subcircuit(const std::string& name,
                                           const std::string& model,
                                           const std::vector<std::string>& ports) {
    std::string line = name;
    for (const auto& p : ports) line += " " + p;
    line += " " + model;
    lines_.push_back(line);
    return *this;
}

CircuitBuilder& CircuitBuilder::model(const std::string& name, const std::string& type,
                                      const std::map<std::string, double>& params) {
    std::string line = ".model " + name + " " + type;
    for (const auto& [k, v] : params)
        line += " " + k + "=" + format_value(v);
    lines_.push_back(line);
    return *this;
}

CircuitBuilder& CircuitBuilder::include(const std::string& filepath) {
    lines_.push_back(".include " + filepath);
    return *this;
}

CircuitBuilder& CircuitBuilder::raw_line(const std::string& line) {
    lines_.push_back(line);
    return *this;
}

Circuit CircuitBuilder::build() {
    std::string netlist;
    netlist += title_.empty() ? "CircuitBuilder" : title_;
    netlist += "\n";
    for (const auto& line : lines_)
        netlist += line + "\n";
    netlist += ".end\n";

    NetlistParser parser;
    return parser.parse(netlist);
}

} // namespace neospice
```

- [ ] **Step 4: Add circuit_builder.cpp to CMake library sources**

In `src/CMakeLists.txt`, find the `add_library(neospice_lib ...)` block and add `api/circuit_builder.cpp` next to `api/neospice.cpp`.

- [ ] **Step 5: Create test file**

Create `tests/unit/test_circuit_builder.cpp`:

```cpp
#include <gtest/gtest.h>
#include "api/circuit_builder.hpp"
#include "api/neospice.hpp"

using namespace neospice;

TEST(CircuitBuilder, ResistorDividerDC) {
    auto ckt = CircuitBuilder()
        .title("Resistor Divider")
        .vsource("V1", "in", "0", {.dc = 10.0})
        .resistor("R1", "in", "out", 1e3)
        .resistor("R2", "out", "0", 1e3)
        .raw_line(".op")
        .build();

    Simulator sim;
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    EXPECT_NEAR(std::get<DCResult>(result.analysis).voltage("out"), 5.0, 1e-6);
}

TEST(CircuitBuilder, RCTransient) {
    auto ckt = CircuitBuilder()
        .vsource("V1", "in", "0", {.dc = 5.0})
        .resistor("R1", "in", "out", 1e3)
        .capacitor("C1", "out", "0", 1e-6)
        .raw_line(".tran 10u 5m")
        .build();

    Simulator sim;
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<TransientResult>(result.analysis));
    auto& tran = std::get<TransientResult>(result.analysis);
    EXPECT_GT(tran.time.size(), 10u);
    EXPECT_NEAR(tran.voltage("out").back(), 5.0, 0.01);
}

TEST(CircuitBuilder, ACLowpass) {
    auto ckt = CircuitBuilder()
        .vsource("V1", "in", "0", {.dc = 0.0, .ac_mag = 1.0})
        .resistor("R1", "in", "out", 1e3)
        .capacitor("C1", "out", "0", 1e-9)
        .raw_line(".ac dec 10 100 10meg")
        .build();

    Simulator sim;
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<ACResult>(result.analysis));
    auto& ac = std::get<ACResult>(result.analysis);
    EXPECT_GT(ac.frequency.size(), 10u);
    auto gain = ac.magnitude_db("out");
    EXPECT_NEAR(gain.front(), 0.0, 1.0);   // low-freq gain ~0 dB
    EXPECT_LT(gain.back(), -20.0);         // high-freq attenuation
}

TEST(CircuitBuilder, PulseSource) {
    auto ckt = CircuitBuilder()
        .vsource_pulse("V1", "in", "0", {.v1 = 0, .v2 = 5, .td = 0,
                                          .tr = 1e-9, .tf = 1e-9,
                                          .pw = 1e-6, .per = 2e-6})
        .resistor("R1", "in", "0", 1e3)
        .raw_line(".tran 10n 10u")
        .build();

    Simulator sim;
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<TransientResult>(result.analysis));
}

TEST(CircuitBuilder, DiodeWithModel) {
    auto ckt = CircuitBuilder()
        .vsource("V1", "in", "0", {.dc = 0.7})
        .diode("D1", "in", "0", "DMOD")
        .model("DMOD", "D", {{"IS", 1e-14}, {"N", 1.0}})
        .raw_line(".op")
        .build();

    Simulator sim;
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
}

TEST(CircuitBuilder, RawLine) {
    auto ckt = CircuitBuilder()
        .raw_line("V1 in 0 10")
        .raw_line("R1 in out 1k")
        .raw_line("R2 out 0 1k")
        .raw_line(".op")
        .build();

    Simulator sim;
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    EXPECT_NEAR(std::get<DCResult>(result.analysis).voltage("out"), 5.0, 1e-6);
}

TEST(CircuitBuilder, ISourceAndInductor) {
    auto ckt = CircuitBuilder()
        .isource("I1", "0", "in", {.dc = 1e-3})
        .resistor("R1", "in", "0", 1e3)
        .inductor("L1", "in", "out", 1e-3)
        .resistor("R2", "out", "0", 1e3)
        .raw_line(".op")
        .build();

    Simulator sim;
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    // At DC, inductor is a short: V(in) = V(out)
    auto& dc = std::get<DCResult>(result.analysis);
    EXPECT_NEAR(dc.voltage("in"), dc.voltage("out"), 1e-6);
}

TEST(CircuitBuilder, SinSource) {
    auto ckt = CircuitBuilder()
        .vsource_sin("V1", "in", "0", {.vo = 0, .va = 1, .freq = 1e3})
        .resistor("R1", "in", "0", 1e3)
        .raw_line(".tran 1u 2m")
        .build();

    Simulator sim;
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<TransientResult>(result.analysis));
    auto& tran = std::get<TransientResult>(result.analysis);
    EXPECT_GT(tran.time.size(), 10u);
}
```

- [ ] **Step 6: Add test file to CMake**

In `tests/CMakeLists.txt`, add `unit/test_circuit_builder.cpp` to the `neospice_tests` executable source list.

- [ ] **Step 7: Build and run tests**

Run: `cmake --build build --target neospice_tests -j$(nproc) && build/tests/neospice_tests --gtest_filter="CircuitBuilder*"`
Expected: All 8 tests pass.

- [ ] **Step 8: Commit**

```bash
git add src/api/circuit_builder.hpp src/api/circuit_builder.cpp src/CMakeLists.txt tests/unit/test_circuit_builder.cpp tests/CMakeLists.txt
git commit -m "feat: add CircuitBuilder fluent API for programmatic circuit construction"
```

---

## Phase 2: Device Introspection + set_param

### Task 4: Device Introspection and Generic set_param()

This task adds 4 virtual methods to the `Device` base class and convenience methods to `Circuit`.

**Files:**
- Modify: `src/devices/device.hpp` (add virtual methods)
- Modify: `src/core/circuit.hpp` (add introspection methods + DeviceInfo struct)
- Modify: `src/core/circuit.cpp` (implement methods)
- Modify: `src/devices/resistor.hpp` (override external_nodes, set_value)
- Modify: `src/devices/capacitor.hpp` (override external_nodes, set_value)
- Modify: `src/devices/inductor.hpp` (override external_nodes, set_value)
- Modify: `src/devices/vsource.hpp` (override external_nodes, set_value)
- Modify: `src/devices/isource.hpp` (override external_nodes, set_value)
- Modify: `src/devices/vcvs.hpp` (override external_nodes)
- Modify: `src/devices/vccs.hpp` (override external_nodes)
- Modify: `src/devices/ccvs.hpp` (override external_nodes)
- Modify: `src/devices/cccs.hpp` (override external_nodes)
- Modify: `src/devices/switch.hpp` (override external_nodes on VSwitch and CSwitch)
- Modify: `src/devices/tline.hpp` (override external_nodes)
- Modify: `src/devices/coupled_inductor.hpp` (override external_nodes)
- Create: `tests/unit/test_introspection.cpp`
- Modify: `tests/CMakeLists.txt` (add test file)

- [ ] **Step 1: Add virtual methods to Device base class**

In `src/devices/device.hpp`, add these methods after `name()` (after line 19):

```cpp
virtual std::string device_type() const {
    if (name_.empty()) return "unknown";
    switch (static_cast<unsigned char>(std::toupper(name_[0]))) {
        case 'R': return "R"; case 'C': return "C"; case 'L': return "L";
        case 'V': return "V"; case 'I': return "I"; case 'K': return "K";
        case 'D': return "D"; case 'Q': return "Q"; case 'J': return "J";
        case 'M': return "M"; case 'E': return "E"; case 'G': return "G";
        case 'F': return "F"; case 'H': return "H"; case 'S': return "S";
        case 'W': return "W"; case 'B': return "B"; case 'T': return "T";
        case 'O': return "O"; case 'X': return "X";
        default: return "unknown";
    }
}

virtual std::vector<int32_t> external_nodes() const { return {}; }

virtual std::optional<double> primary_value() const { return std::nullopt; }

virtual bool set_value(double /*value*/) { return false; }
```

Add `#include <cctype>` to the includes at the top of the file if not already present.

- [ ] **Step 2: Override in Resistor**

In `src/devices/resistor.hpp`, add after `set_rac()` (after line 33):

```cpp
std::vector<int32_t> external_nodes() const override { return {np_, nn_}; }
std::optional<double> primary_value() const override { return resistance_eff_; }
bool set_value(double value) override {
    resistance_nom_ = value; resistance_eff_ = value; return true;
}
```

- [ ] **Step 3: Override in Capacitor**

In `src/devices/capacitor.hpp`, add after `capacitance_nom()` (after line 38):

```cpp
std::vector<int32_t> external_nodes() const override { return {np_, nn_}; }
std::optional<double> primary_value() const override { return cap_eff_; }
bool set_value(double value) override {
    cap_nom_ = value; cap_eff_ = value; return true;
}
```

- [ ] **Step 4: Override in Inductor**

In `src/devices/inductor.hpp`, add after `inductance_nom()` (after line 37):

```cpp
std::vector<int32_t> external_nodes() const override { return {np_, nn_}; }
std::optional<double> primary_value() const override { return inductance_eff_; }
bool set_value(double value) override {
    inductance_nom_ = value; inductance_eff_ = value; return true;
}
```

- [ ] **Step 5: Override in VSource**

In `src/devices/vsource.hpp`, add after `dc_value()` (after the existing `set_dc_value` / `dc_value` methods):

```cpp
std::vector<int32_t> external_nodes() const override { return {np_, nn_}; }
std::optional<double> primary_value() const override { return dc_value_; }
bool set_value(double value) override { dc_value_ = value; return true; }
```

- [ ] **Step 6: Override in ISource**

In `src/devices/isource.hpp`, add after the `dc_value()` method:

```cpp
std::vector<int32_t> external_nodes() const override { return {np_, nn_}; }
std::optional<double> primary_value() const override { return dc_value_; }
bool set_value(double value) override { dc_value_ = value; return true; }
```

- [ ] **Step 7: Override external_nodes() in dependent sources**

In `src/devices/vcvs.hpp`, add in the public section:

```cpp
std::vector<int32_t> external_nodes() const override { return {np_, nn_, ncp_, ncn_}; }
```

In `src/devices/vccs.hpp`, add in the public section:

```cpp
std::vector<int32_t> external_nodes() const override { return {np_, nn_, ncp_, ncn_}; }
```

In `src/devices/ccvs.hpp`, add in the public section (note: CCVS has np_, nn_ plus the sense branch, but external nodes are just the output terminals):

```cpp
std::vector<int32_t> external_nodes() const override { return {np_, nn_}; }
```

In `src/devices/cccs.hpp`, add in the public section:

```cpp
std::vector<int32_t> external_nodes() const override { return {np_, nn_}; }
```

- [ ] **Step 8: Override external_nodes() in switches**

In `src/devices/switch.hpp`, add to `VSwitch` public section:

```cpp
std::vector<int32_t> external_nodes() const override { return {np_, nn_, ncp_, ncn_}; }
```

Add to `CSwitch` public section:

```cpp
std::vector<int32_t> external_nodes() const override { return {np_, nn_}; }
```

- [ ] **Step 9: Override external_nodes() in TransmissionLine**

In `src/devices/tline.hpp`, add in the public section:

```cpp
std::vector<int32_t> external_nodes() const override { return {p1p_, p1n_, p2p_, p2n_}; }
```

- [ ] **Step 10: Add DeviceInfo struct and Circuit methods to circuit.hpp**

In `src/core/circuit.hpp`, add before the `Circuit` class declaration (before line 142):

```cpp
struct DeviceInfo {
    std::string name;
    std::string type;
    std::vector<std::string> nodes;
    std::optional<double> value;
};
```

Add these public methods to the `Circuit` class, after `node_index()` (after line 180):

```cpp
std::vector<std::string> node_names() const;
std::vector<std::string> device_names() const;
DeviceInfo device_info(const std::string& name) const;
std::vector<std::string> devices_at_node(const std::string& node) const;
Device* find_device(const std::string& name);
const Device* find_device(const std::string& name) const;
bool set_param(const std::string& device_name, double value);
```

Add `#include <optional>` to the includes at the top if not already present.

- [ ] **Step 11: Implement Circuit introspection methods in circuit.cpp**

In `src/core/circuit.cpp`, add these implementations (find the end of the existing method implementations):

```cpp
std::vector<std::string> Circuit::node_names() const {
    std::vector<std::string> names;
    for (int32_t i = 0; i < next_node_; ++i) {
        if (!is_internal_node(i))
            names.push_back(node_names_[i]);
    }
    return names;
}

std::vector<std::string> Circuit::device_names() const {
    std::vector<std::string> names;
    names.reserve(devices_.size());
    for (const auto& dev : devices_)
        names.push_back(dev->name());
    return names;
}

Device* Circuit::find_device(const std::string& name) {
    std::string lower_name = name;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    for (auto& dev : devices_) {
        std::string dev_lower = dev->name();
        std::transform(dev_lower.begin(), dev_lower.end(), dev_lower.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (dev_lower == lower_name) return dev.get();
    }
    return nullptr;
}

const Device* Circuit::find_device(const std::string& name) const {
    return const_cast<Circuit*>(this)->find_device(name);
}

DeviceInfo Circuit::device_info(const std::string& name) const {
    const Device* dev = find_device(name);
    if (!dev) throw std::out_of_range("Device not found: " + name);

    DeviceInfo info;
    info.name = dev->name();
    info.type = dev->device_type();
    info.value = dev->primary_value();
    for (int32_t idx : dev->external_nodes()) {
        if (idx == GROUND_INTERNAL)
            info.nodes.push_back("0");
        else if (idx >= 0 && idx < next_node_)
            info.nodes.push_back(node_names_[idx]);
    }
    return info;
}

std::vector<std::string> Circuit::devices_at_node(const std::string& node) const {
    int32_t target = node_index(node);
    if (target < 0 && node != "0" && node != "gnd" && node != "GND")
        return {};

    std::vector<std::string> result;
    for (const auto& dev : devices_) {
        auto nodes = dev->external_nodes();
        for (int32_t n : nodes) {
            if (n == target || (target == GROUND_INTERNAL && n == GROUND_INTERNAL)) {
                result.push_back(dev->name());
                break;
            }
        }
    }
    return result;
}

bool Circuit::set_param(const std::string& device_name, double value) {
    Device* dev = find_device(device_name);
    if (!dev) return false;
    return dev->set_value(value);
}
```

Add `#include <algorithm>` and `#include <stdexcept>` to the top of `circuit.cpp` if not already present.

- [ ] **Step 12: Create test file**

Create `tests/unit/test_introspection.cpp`:

```cpp
#include <gtest/gtest.h>
#include <algorithm>
#include "api/neospice.hpp"

using namespace neospice;

class IntrospectionTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::string netlist = R"(
Introspection test
V1 in 0 10
R1 in mid 1k
R2 mid out 2k
C1 out 0 1n
.op
.end
)";
        Simulator sim;
        ckt = sim.parse(netlist);
    }
    Circuit ckt;
};

TEST_F(IntrospectionTest, NodeNames) {
    auto names = ckt.node_names();
    EXPECT_GE(names.size(), 3u);  // in, mid, out
    EXPECT_TRUE(std::find(names.begin(), names.end(), "in") != names.end());
    EXPECT_TRUE(std::find(names.begin(), names.end(), "mid") != names.end());
    EXPECT_TRUE(std::find(names.begin(), names.end(), "out") != names.end());
}

TEST_F(IntrospectionTest, DeviceNames) {
    auto names = ckt.device_names();
    EXPECT_EQ(names.size(), 4u);  // V1, R1, R2, C1
}

TEST_F(IntrospectionTest, DeviceInfoResistor) {
    auto info = ckt.device_info("R1");
    EXPECT_EQ(info.type, "R");
    EXPECT_EQ(info.nodes.size(), 2u);
    EXPECT_TRUE(info.value.has_value());
    EXPECT_NEAR(info.value.value(), 1e3, 1e-6);
}

TEST_F(IntrospectionTest, DeviceInfoVSource) {
    auto info = ckt.device_info("V1");
    EXPECT_EQ(info.type, "V");
    EXPECT_EQ(info.nodes.size(), 2u);
    EXPECT_TRUE(info.value.has_value());
    EXPECT_NEAR(info.value.value(), 10.0, 1e-6);
}

TEST_F(IntrospectionTest, DeviceInfoCapacitor) {
    auto info = ckt.device_info("C1");
    EXPECT_EQ(info.type, "C");
    EXPECT_TRUE(info.value.has_value());
    EXPECT_NEAR(info.value.value(), 1e-9, 1e-15);
}

TEST_F(IntrospectionTest, DeviceInfoCaseInsensitive) {
    auto info = ckt.device_info("r1");
    EXPECT_EQ(info.type, "R");
}

TEST_F(IntrospectionTest, DeviceInfoNotFound) {
    EXPECT_THROW(ckt.device_info("nonexistent"), std::out_of_range);
}

TEST_F(IntrospectionTest, DevicesAtNode) {
    auto devs = ckt.devices_at_node("mid");
    EXPECT_EQ(devs.size(), 2u);  // R1 and R2

    auto devs_out = ckt.devices_at_node("out");
    EXPECT_EQ(devs_out.size(), 2u);  // R2 and C1
}

TEST_F(IntrospectionTest, DevicesAtGround) {
    auto devs = ckt.devices_at_node("0");
    EXPECT_GE(devs.size(), 2u);  // V1 and C1 at minimum
}

TEST_F(IntrospectionTest, FindDevice) {
    auto* dev = ckt.find_device("R1");
    ASSERT_NE(dev, nullptr);
    EXPECT_EQ(dev->name(), "R1");

    EXPECT_EQ(ckt.find_device("nonexistent"), nullptr);
}

TEST(SetParam, ResistorValue) {
    Simulator sim;
    auto ckt = sim.parse(R"(
SetParam test
V1 in 0 10
R1 in out 1k
R2 out 0 1k
.op
.end
)");
    auto dc1 = sim.run_dc(ckt);
    EXPECT_NEAR(dc1.voltage("out"), 5.0, 1e-6);

    EXPECT_TRUE(ckt.set_param("R2", 3e3));
    ckt.reset_state();
    auto dc2 = sim.run_dc(ckt);
    EXPECT_NEAR(dc2.voltage("out"), 7.5, 1e-6);  // 10 * 3k/(1k+3k)
}

TEST(SetParam, VSourceValue) {
    Simulator sim;
    auto ckt = sim.parse(R"(
SetParam source
V1 in 0 10
R1 in 0 1k
.op
.end
)");
    auto dc1 = sim.run_dc(ckt);
    EXPECT_NEAR(dc1.voltage("in"), 10.0, 1e-6);

    EXPECT_TRUE(ckt.set_param("V1", 5.0));
    ckt.reset_state();
    auto dc2 = sim.run_dc(ckt);
    EXPECT_NEAR(dc2.voltage("in"), 5.0, 1e-6);
}

TEST(SetParam, CapacitorValue) {
    Simulator sim;
    auto ckt1 = sim.parse(R"(
SetParam cap
V1 in 0 DC 0 AC 1
R1 in out 1k
C1 out 0 1n
.ac dec 10 100 10meg
.end
)");
    auto ac1 = sim.run_ac(ckt1, ACMode::DEC, 10, 100, 10e6);
    auto f3db_1 = 1.0 / (2.0 * M_PI * 1e3 * 1e-9);  // ~159 kHz

    auto ckt2 = sim.parse(R"(
SetParam cap2
V1 in 0 DC 0 AC 1
R1 in out 1k
C1 out 0 1n
.end
)");
    EXPECT_TRUE(ckt2.set_param("C1", 10e-9));  // 10x larger cap
    auto ac2 = sim.run_ac(ckt2, ACMode::DEC, 10, 100, 10e6);

    // Larger cap = lower bandwidth: gain at 100kHz should be lower with 10nF than 1nF
    auto gain1 = ac1.magnitude_db("out");
    auto gain2 = ac2.magnitude_db("out");
    EXPECT_LT(gain2.back(), gain1.back());
}

TEST(SetParam, NonexistentDevice) {
    Simulator sim;
    auto ckt = sim.parse(R"(
SetParam none
V1 in 0 10
R1 in 0 1k
.op
.end
)");
    EXPECT_FALSE(ckt.set_param("R99", 1e3));
}

TEST(SetParam, InductorValue) {
    Simulator sim;
    auto ckt = sim.parse(R"(
SetParam inductor
V1 in 0 DC 0 AC 1
R1 in out 1k
L1 out 0 1m
.ac dec 10 100 10meg
.end
)");
    EXPECT_TRUE(ckt.set_param("L1", 10e-3));
    auto info = ckt.device_info("L1");
    EXPECT_NEAR(info.value.value(), 10e-3, 1e-9);
}
```

- [ ] **Step 13: Add test file to CMake**

In `tests/CMakeLists.txt`, add `unit/test_introspection.cpp` to the `neospice_tests` executable source list.

- [ ] **Step 14: Build and run tests**

Run: `cmake --build build --target neospice_tests -j$(nproc) && build/tests/neospice_tests --gtest_filter="IntrospectionTest*:SetParam*"`
Expected: All pass.

- [ ] **Step 15: Run full test suite**

Run: `build/tests/neospice_tests`
Expected: All 866+ tests pass (no regressions from adding virtual methods).

- [ ] **Step 16: Commit**

```bash
git add src/devices/device.hpp src/core/circuit.hpp src/core/circuit.cpp src/devices/resistor.hpp src/devices/capacitor.hpp src/devices/inductor.hpp src/devices/vsource.hpp src/devices/isource.hpp src/devices/vcvs.hpp src/devices/vccs.hpp src/devices/ccvs.hpp src/devices/cccs.hpp src/devices/switch.hpp src/devices/tline.hpp tests/unit/test_introspection.cpp tests/CMakeLists.txt
git commit -m "feat: add circuit introspection and generic set_param() API"
```

---

## Phase 3: Integration Verification

### Task 5: Full Build + Test Suite + Push

- [ ] **Step 1: Full rebuild from clean**

Run: `cmake --build build --target neospice_tests -j$(nproc) 2>&1 | tail -5`
Expected: Build succeeds with no errors or warnings.

- [ ] **Step 2: Run full test suite**

Run: `build/tests/neospice_tests`
Expected: All tests pass (866+ existing + new tests).

- [ ] **Step 3: Push to main**

```bash
git push origin main
```

- [ ] **Step 4: Update ROADMAP.md API Vision section**

In `docs/ROADMAP.md`, update the "Typed Result Access" status to reflect completion and the new features:

Change the summary table entries:
- Typed Result Access: `Partial` → `Done`
- SimStatus: already `Done`
- Add new row for Circuit Introspection: `Done`
- Add new row for set_param: `Done`
- Add new row for Analysis Chaining: `Done`
- Add new row for CircuitBuilder: `Done`

- [ ] **Step 5: Commit ROADMAP update**

```bash
git add docs/ROADMAP.md
git commit -m "docs: update ROADMAP with completed API Vision features"
git push origin main
```
