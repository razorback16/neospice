# API Refresh: Roadmap Update, Typed Results, SimStatus

**Status: COMPLETE** — All 10 tasks implemented and committed.

**Goal:** Modernize the public API surface — refresh the outdated roadmap, complete typed result accessors across all result types, and add structured simulation status reporting.

**Architecture:** Three independent work streams that share no code dependencies. Task 1 is a docs-only update. Tasks 2–5 add convenience methods to existing result structs (header-only, no solver changes). Tasks 6–9 thread a new `SimStatus` struct through the solve pipeline and into result types. Each task produces a compilable, testable commit.

**Tech Stack:** C++20, Google Test, CMake. No new dependencies.

**Note:** After this plan was completed, the R5/R7 refactors changed `AnalysisCommand` to `std::variant` and `SimulationResult` from 8 optionals to `std::variant<std::monostate, DCResult, ...>`. The test code samples below still reference the pre-refactor API (`result.dc.has_value()`, `*result.dc`); actual test code now uses `std::get_if<DCResult>(&result.analysis)` etc.

---

## Task 1: Refresh ROADMAP.md ✓ `9138276`

**Files:**
- Modify: `docs/ROADMAP.md`

The current roadmap describes neospice as having a handful of devices and a few analyses. The actual state: 28 device types, 8 analysis types, 852 tests, `.subckt`/`.param`/`.lib`/`.func`/`.measure`/`.step` all working. The API Vision section's code examples for typed access already partially exist. Phase 7 lists BSIM-SOI and Verilog-A as future — BSIMSOI is already migrated.

- [x] **Step 1: Update "Current State" section**

Replace the current state block (lines 1–17) with:

```markdown
# neospice Roadmap

## Current State

neospice is a modern C++ SPICE simulator with 28 device models and 8 analysis types,
validated against ngspice across 852 tests with tolerances as tight as 1e-6.

### Analyses
DC operating point, DC sweep (nested 2-parameter), transient (adaptive Trap/Gear-2/BE),
AC small-signal, noise (adjoint method), transfer function, sensitivity, pole-zero,
Fourier/THD, parameter sweep (.step), and .measure post-processing.

### Device Models
| Category | Devices |
|----------|---------|
| Passives | R (TC, RAC, flicker noise), C, L, K (mutual) |
| Sources | V, I (DC/PULSE/SIN/PWL/EXP/SFFM/AM) |
| Dependent | E, G, F, H (linear + POLY + TABLE) |
| Behavioral | B (auto-diff Jacobian, DDT, IDT, PWL, TABLE, TEMP) |
| Switches | S (voltage), W (current) — hysteresis |
| T-Line | T (lossless Branin), O (LTRA lossy) |
| Diode/BJT | Diode, BJT (Gummel-Poon), VBIC (level 4/9/12/13) |
| JFET/HFET | JFET, JFET2, HFET1, HFET2 |
| MOSFET | MOS1, MOS3, MOS9, BSIM3v32, BSIM3, BSIM4v7, BSIMSOI, HiSIM2, HiSIM_HV |

### Netlist Features
`.param` expressions, `.subckt`/`.ends`, `.include`/`.lib`, `.global`, `.ic`, `.nodeset`,
`.options`, `.func`, `.measure`, `.save`, `.step`, SPICE suffixes (k/m/u/n/p/f/T).

### What sets neospice apart
- **Performance**: 1.5–6x faster than ngspice in-process; zero subprocess overhead as a library
- **Embeddable C++ API**: clean `Simulator`/`Circuit`/`Result` interface
- **Auto-differentiation**: B-source expressions get exact Jacobians (no numerical perturbation)
- **Modern codebase**: C++20, clean Device abstraction, auto-migration tooling for ngspice models
- **ngspice-compatible output**: raw file format matches ngspice for drop-in tool compatibility
```

- [x] **Step 2: Update API Vision section**

Replace the "Typed Result Access" code block (lines 172–186) to note what already exists:

```markdown
### Typed Result Access (partially implemented)

The main result types already provide typed accessors. DC, transient, AC, and
DC sweep results have `.voltage()`, `.current()` helpers. AC adds
`.magnitude_db()`, `.phase_deg()`, `.magnitude()`, `.diff()`, `.diff_magnitude_db()`.
DC has `.diff()`. Remaining gaps: noise helpers, transient diff, DC sweep
convenience, and current-based magnitude/phase on AC.

```cpp
// Already working today:
auto ac = sim.run_ac(ckt, DEC, 10, 1, 100e6);
auto gain_db = ac.magnitude_db("out");       // vector<double>
auto phase   = ac.phase_deg("out");          // vector<double>
auto vdiff   = ac.diff("out_p", "out_n");    // vector<complex>
double vout  = dc.voltage("out");            // scalar
double ibias = dc.current("v1");             // scalar

// Planned additions:
auto status  = ac.status;                    // SimStatus: converged, method, iterations
auto tran_d  = tran.diff("out_p", "out_n");  // vector<double> differential
auto onoise  = noise.output_noise("r1");     // per-device accessor
auto inoise  = noise.integrated_input(1e3, 1e6); // integrated RMS input noise
```
```

- [x] **Step 3: Update Phase 7 to reflect completed work**

Replace the Phase 7 section (lines 139–158) to remove completed items and add actual remaining gaps:

```markdown
## Phase 7: Extended Device & Analysis Support

**Priority: Ongoing**

### Devices — Remaining Gaps
- BSIM-CMG (FinFET) model — next-gen compact model, industry demand
- Verilog-A device model compilation (long-term — enables user-defined models)
- Priority 3 legacy devices (MOS2, MOS6, etc.) — low demand, available via migration tool

### Devices — Completed
MOS1, MOS3, MOS9, BSIM3v32, BSIM3, BSIM4v7, BSIMSOI, HiSIM2, HiSIM_HV, BJT,
VBIC, JFET, JFET2, HFET1, HFET2, Diode, LTRA, ASRC, and all passives/sources/switches.

### Analyses — Remaining Gaps
- Distortion analysis (`.disto`)

### Netlist Features — Remaining Gaps
- XSPICE digital/mixed-signal code models
- Full `.param` function library (most common functions done)
```

- [x] **Step 4: Update summary table**

Replace the summary table (lines 310–318):

```markdown
## Summary

| Phase | Feature                    | Impact          | Effort   | Status |
|-------|----------------------------|-----------------|----------|--------|
| —     | Typed result access        | Usability       | Low      | Partial |
| —     | SimStatus error model      | Reliability     | Low      | Planned |
| 1     | Python bindings            | Adoption        | Medium   | Planned |
| 2     | WASM build                 | Accessibility   | Low      | Planned |
| 3     | Sensitivity/gradients      | Optimization    | High     | Planned |
| 4     | Parallel sweeps            | Performance     | Medium   | Planned |
| 5     | Incremental re-simulation  | Interactivity   | Medium   | Planned |
| 6     | GPU acceleration           | Large circuits  | High     | Planned |
| 7     | Extended devices/analyses  | Completeness    | Ongoing  | Active  |
```

- [x] **Step 5: Build and verify no regressions**

Run: `cd . && cmake --build build -j$(nproc) 2>&1 | tail -5`
Expected: build succeeds (docs-only change)

- [x] **Step 6: Commit**

```bash
git add docs/ROADMAP.md
git commit -m "docs: refresh roadmap to reflect 28 devices, 8 analyses, 852 tests"
```

---

## Task 2: Typed Accessors — TransientResult ✓ `0550f51`

**Files:**
- Modify: `src/core/transient.hpp`
- Test: `tests/unit/test_api.cpp`

TransientResult has `voltage()` and `current()` but lacks `diff()` (differential voltage) which analog designers use constantly for diff-pair outputs. Also add `signal_names()` for introspection.

- [x] **Step 1: Write the failing tests**

Add to `tests/unit/test_api.cpp`:

```cpp
TEST(TransientResultAPI, DiffVoltage) {
    neospice::Simulator sim;
    std::string netlist = R"(
Diff test
V1 a 0 5
V2 b 0 3
R1 a 0 1k
R2 b 0 1k
.tran 1u 10u
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(result.transient.has_value());
    auto& tran = *result.transient;

    // diff() returns v(a) - v(b) at every time point
    auto d = tran.diff("a", "b");
    ASSERT_EQ(d.size(), tran.time.size());
    for (auto v : d)
        EXPECT_NEAR(v, 2.0, 1e-6);

    // signal_names() lists all available signals
    auto names = tran.signal_names();
    EXPECT_TRUE(std::find(names.begin(), names.end(), "v(a)") != names.end());
    EXPECT_TRUE(std::find(names.begin(), names.end(), "v(b)") != names.end());
}
```

- [x] **Step 2: Run test to verify it fails**

Run: `cd . && cmake --build build -j$(nproc) 2>&1 | tail -3`
Expected: FAIL — `diff` and `signal_names` not found on TransientResult.

- [x] **Step 3: Implement diff() and signal_names()**

In `src/core/transient.hpp`, add inside `struct TransientResult` after `current()`:

```cpp
    std::vector<double> diff(const std::string& node_p, const std::string& node_n) const {
        const auto& vp = voltage(node_p);
        const auto& vn = voltage(node_n);
        std::vector<double> result(vp.size());
        for (std::size_t i = 0; i < vp.size(); ++i)
            result[i] = vp[i] - vn[i];
        return result;
    }

    std::vector<std::string> signal_names() const {
        std::vector<std::string> names;
        names.reserve(voltages.size() + currents.size());
        for (const auto& [k, v] : voltages) names.push_back(k);
        for (const auto& [k, v] : currents) names.push_back(k);
        return names;
    }
```

- [x] **Step 4: Run test to verify it passes**

Run: `cd . && cmake --build build -j$(nproc) && ctest --test-dir build -R SimulatorAPI -V 2>&1 | tail -10`
Expected: PASS

- [x] **Step 5: Commit**

```bash
git add src/core/transient.hpp tests/unit/test_api.cpp
git commit -m "feat: add diff() and signal_names() to TransientResult"
```

---

## Task 3: Typed Accessors — DCSweepResult and DCResult ✓ `d8f648e`

**Files:**
- Modify: `src/core/dc.hpp`
- Test: `tests/unit/test_api.cpp`

DCSweepResult lacks `diff()` and `signal_names()`. DCResult lacks `signal_names()`.

- [x] **Step 1: Write the failing tests**

Add to `tests/unit/test_api.cpp`:

```cpp
TEST(DCResultAPI, SignalNames) {
    neospice::Simulator sim;
    std::string netlist = R"(
DC signals
V1 in 0 10
R1 in out 1k
R2 out 0 1k
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(result.dc.has_value());

    auto names = result.dc->signal_names();
    EXPECT_TRUE(std::find(names.begin(), names.end(), "v(out)") != names.end());
    EXPECT_TRUE(std::find(names.begin(), names.end(), "v(in)") != names.end());
}

TEST(DCSweepResultAPI, DiffAndSignalNames) {
    neospice::Simulator sim;
    std::string netlist = R"(
Sweep diff
V1 in 0 5
R1 in a 1k
R2 in b 2k
R3 a 0 1k
R4 b 0 1k
.dc V1 0 10 1
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(result.dc_sweep.has_value());
    auto& sw = *result.dc_sweep;

    auto d = sw.diff("a", "b");
    ASSERT_EQ(d.size(), sw.sweep_values.size());

    auto names = sw.signal_names();
    EXPECT_TRUE(std::find(names.begin(), names.end(), "v(a)") != names.end());
}
```

- [x] **Step 2: Run test to verify it fails**

Run: `cd . && cmake --build build -j$(nproc) 2>&1 | tail -3`
Expected: FAIL — `signal_names` and `diff` not found.

- [x] **Step 3: Implement helpers on DCResult and DCSweepResult**

In `src/core/dc.hpp`, add inside `struct DCResult` after `diff()`:

```cpp
    std::vector<std::string> signal_names() const {
        std::vector<std::string> names;
        names.reserve(node_voltages.size() + branch_currents.size());
        for (const auto& [k, v] : node_voltages) names.push_back(k);
        for (const auto& [k, v] : branch_currents) names.push_back(k);
        return names;
    }
```

In `src/core/dc.hpp`, add inside `struct DCSweepResult` after `current()`:

```cpp
    std::vector<double> diff(const std::string& node_p, const std::string& node_n) const {
        const auto& vp = voltage(node_p);
        const auto& vn = voltage(node_n);
        std::vector<double> result(vp.size());
        for (std::size_t i = 0; i < vp.size(); ++i)
            result[i] = vp[i] - vn[i];
        return result;
    }

    std::vector<std::string> signal_names() const {
        std::vector<std::string> names;
        names.reserve(voltages.size() + currents.size());
        for (const auto& [k, v] : voltages) names.push_back(k);
        for (const auto& [k, v] : currents) names.push_back(k);
        return names;
    }
```

- [x] **Step 4: Run tests**

Run: `cd . && cmake --build build -j$(nproc) && ctest --test-dir build -R "DCResultAPI|DCSweepResultAPI" -V 2>&1 | tail -10`
Expected: PASS

- [x] **Step 5: Commit**

```bash
git add src/core/dc.hpp tests/unit/test_api.cpp
git commit -m "feat: add diff() and signal_names() to DCResult/DCSweepResult"
```

---

## Task 4: Typed Accessors — ACResult current helpers + signal_names ✓ `935f6c7`

**Files:**
- Modify: `src/core/ac.hpp`
- Test: `tests/unit/test_api.cpp`

ACResult has `magnitude_db()`, `phase_deg()`, `magnitude()` for voltages but not for currents (analog designers need `|I(V1)|` in dB for impedance analysis). Also needs `signal_names()` and `current_magnitude_db()` / `current_phase_deg()`.

- [x] **Step 1: Write the failing tests**

Add to `tests/unit/test_api.cpp`:

```cpp
TEST(ACResultAPI, CurrentMagnitudeAndSignalNames) {
    neospice::Simulator sim;
    std::string netlist = R"(
AC current
V1 in 0 DC 0 AC 1
R1 in 0 1k
.ac dec 10 100 1meg
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(result.ac.has_value());
    auto& ac = *result.ac;

    // Current magnitude in dB
    auto idb = ac.current_magnitude_db("v1");
    ASSERT_EQ(idb.size(), ac.frequency.size());
    // At all frequencies, I(V1) = V/R = 1/1000 => 20*log10(1e-3) = -60 dB
    for (auto v : idb)
        EXPECT_NEAR(v, -60.0, 0.1);

    // Current phase
    auto iph = ac.current_phase_deg("v1");
    ASSERT_EQ(iph.size(), ac.frequency.size());

    // signal_names
    auto names = ac.signal_names();
    EXPECT_FALSE(names.empty());
}
```

- [x] **Step 2: Run test to verify it fails**

Run: `cd . && cmake --build build -j$(nproc) 2>&1 | tail -3`
Expected: FAIL — `current_magnitude_db`, `current_phase_deg`, `signal_names` not found.

- [x] **Step 3: Implement current helpers and signal_names on ACResult**

In `src/core/ac.hpp`, add inside `struct ACResult` after `diff_magnitude_db()`:

```cpp
    std::vector<double> current_magnitude_db(const std::string& dev) const {
        const auto& c = current(dev);
        std::vector<double> result(c.size());
        for (std::size_t i = 0; i < c.size(); ++i)
            result[i] = 20.0 * std::log10(std::max(std::abs(c[i]), 1e-30));
        return result;
    }

    std::vector<double> current_phase_deg(const std::string& dev) const {
        const auto& c = current(dev);
        std::vector<double> result(c.size());
        for (std::size_t i = 0; i < c.size(); ++i)
            result[i] = std::atan2(c[i].imag(), c[i].real()) * (180.0 / M_PI);
        return result;
    }

    std::vector<double> current_magnitude(const std::string& dev) const {
        const auto& c = current(dev);
        std::vector<double> result(c.size());
        for (std::size_t i = 0; i < c.size(); ++i)
            result[i] = std::abs(c[i]);
        return result;
    }

    std::vector<std::string> signal_names() const {
        std::vector<std::string> names;
        names.reserve(voltages.size() + currents.size());
        for (const auto& [k, v] : voltages) names.push_back(k);
        for (const auto& [k, v] : currents) names.push_back(k);
        return names;
    }
```

- [x] **Step 4: Run tests**

Run: `cd . && cmake --build build -j$(nproc) && ctest --test-dir build -R ACResultAPI -V 2>&1 | tail -10`
Expected: PASS

- [x] **Step 5: Commit**

```bash
git add src/core/ac.hpp tests/unit/test_api.cpp
git commit -m "feat: add current helpers and signal_names() to ACResult"
```

---

## Task 5: Typed Accessors — NoiseResult ✓ `b8575c9`

**Files:**
- Modify: `src/core/noise.hpp`
- Test: `tests/unit/test_api.cpp`

NoiseResult has zero helper methods — only raw vectors. Analog designers need: per-device accessor with friendly name, integrated RMS noise over a band, and spot noise at a frequency.

- [x] **Step 1: Write the failing tests**

Add to `tests/unit/test_api.cpp`:

```cpp
TEST(NoiseResultAPI, Helpers) {
    neospice::Simulator sim;
    std::string netlist = R"(
Noise test
V1 in 0 DC 0 AC 1
R1 in out 1k
R2 out 0 1k
.noise v(out) V1 dec 10 1 1meg
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(result.noise.has_value());
    auto& nr = *result.noise;

    // output_noise_sqrt returns V/sqrt(Hz) (more intuitive than V^2/Hz)
    auto on_sqrt = nr.output_noise_sqrt();
    ASSERT_EQ(on_sqrt.size(), nr.frequency.size());
    for (std::size_t i = 0; i < on_sqrt.size(); ++i)
        EXPECT_NEAR(on_sqrt[i], std::sqrt(nr.output_noise_density[i]), 1e-30);

    // input_noise_sqrt
    auto in_sqrt = nr.input_noise_sqrt();
    ASSERT_EQ(in_sqrt.size(), nr.frequency.size());

    // integrated_output_noise over full band (trapezoidal integration)
    double integrated = nr.integrated_output_noise(nr.frequency.front(), nr.frequency.back());
    EXPECT_GT(integrated, 0.0);

    // device_names lists contributing devices
    auto devs = nr.device_names();
    EXPECT_FALSE(devs.empty());

    // signal_names
    auto names = nr.signal_names();
    EXPECT_TRUE(std::find(names.begin(), names.end(), "onoise") != names.end());
    EXPECT_TRUE(std::find(names.begin(), names.end(), "inoise") != names.end());
}
```

- [x] **Step 2: Run test to verify it fails**

Run: `cd . && cmake --build build -j$(nproc) 2>&1 | tail -3`
Expected: FAIL — methods not found on NoiseResult.

- [x] **Step 3: Implement NoiseResult helpers**

In `src/core/noise.hpp`, add `#include <cmath>` and `#include <algorithm>` to the includes, then add inside `struct NoiseResult` after the `device_noise` field:

```cpp
    std::vector<double> output_noise_sqrt() const {
        std::vector<double> result(output_noise_density.size());
        for (std::size_t i = 0; i < result.size(); ++i)
            result[i] = std::sqrt(std::max(output_noise_density[i], 0.0));
        return result;
    }

    std::vector<double> input_noise_sqrt() const {
        std::vector<double> result(input_noise_density.size());
        for (std::size_t i = 0; i < result.size(); ++i)
            result[i] = std::sqrt(std::max(input_noise_density[i], 0.0));
        return result;
    }

    double integrated_output_noise(double fmin, double fmax) const {
        if (frequency.size() < 2) return 0.0;
        double sum = 0.0;
        for (std::size_t i = 1; i < frequency.size(); ++i) {
            if (frequency[i] < fmin) continue;
            if (frequency[i - 1] > fmax) break;
            double f0 = std::max(frequency[i - 1], fmin);
            double f1 = std::min(frequency[i], fmax);
            sum += 0.5 * (output_noise_density[i - 1] + output_noise_density[i]) * (f1 - f0);
        }
        return std::sqrt(std::max(sum, 0.0));
    }

    double integrated_input_noise(double fmin, double fmax) const {
        if (frequency.size() < 2) return 0.0;
        double sum = 0.0;
        for (std::size_t i = 1; i < frequency.size(); ++i) {
            if (frequency[i] < fmin) continue;
            if (frequency[i - 1] > fmax) break;
            double f0 = std::max(frequency[i - 1], fmin);
            double f1 = std::min(frequency[i], fmax);
            sum += 0.5 * (input_noise_density[i - 1] + input_noise_density[i]) * (f1 - f0);
        }
        return std::sqrt(std::max(sum, 0.0));
    }

    std::vector<std::string> device_names() const {
        std::vector<std::string> names;
        names.reserve(device_noise.size());
        for (const auto& [k, v] : device_noise) names.push_back(k);
        return names;
    }

    std::vector<std::string> signal_names() const {
        std::vector<std::string> names = {"onoise", "inoise"};
        for (const auto& [k, v] : device_noise) names.push_back(k);
        return names;
    }
```

- [x] **Step 4: Run tests**

Run: `cd . && cmake --build build -j$(nproc) && ctest --test-dir build -R NoiseResultAPI -V 2>&1 | tail -10`
Expected: PASS

- [x] **Step 5: Commit**

```bash
git add src/core/noise.hpp tests/unit/test_api.cpp
git commit -m "feat: add typed accessors to NoiseResult (sqrt, integrated, device_names)"
```

---

## Task 6: Define SimStatus Struct ✓ `de2a3d8`

**Files:**
- Create: `src/core/sim_status.hpp`
- Test: `tests/unit/test_sim_status.cpp`
- Modify: `tests/CMakeLists.txt` (or whichever CMake file lists unit tests)

SimStatus is a lightweight struct that every solve function will populate to report how the simulation converged. It replaces "throw on failure" with structured data for the common case while still allowing throws for truly unrecoverable errors (singular matrix, bad netlist).

- [x] **Step 1: Write the test for SimStatus construction**

Create `tests/unit/test_sim_status.cpp`:

```cpp
#include <gtest/gtest.h>
#include "core/sim_status.hpp"

using namespace neospice;

TEST(SimStatus, DefaultIsEmpty) {
    SimStatus s;
    EXPECT_TRUE(s.converged);
    EXPECT_EQ(s.iterations, 0);
    EXPECT_EQ(s.convergence_method, ConvergenceMethod::DIRECT);
    EXPECT_TRUE(s.warnings.empty());
}

TEST(SimStatus, AddWarning) {
    SimStatus s;
    s.warnings.push_back("gmin stepping used");
    EXPECT_EQ(s.warnings.size(), 1u);
    EXPECT_EQ(s.warnings[0], "gmin stepping used");
}

TEST(SimStatus, MethodToString) {
    EXPECT_EQ(convergence_method_name(ConvergenceMethod::DIRECT), "direct");
    EXPECT_EQ(convergence_method_name(ConvergenceMethod::GMIN_STEPPING), "gmin-stepping");
    EXPECT_EQ(convergence_method_name(ConvergenceMethod::SOURCE_STEPPING), "source-stepping");
    EXPECT_EQ(convergence_method_name(ConvergenceMethod::PSEUDO_TRANSIENT), "pseudo-transient");
}
```

- [x] **Step 2: Run test to verify it fails**

Run: `cd . && cmake --build build -j$(nproc) 2>&1 | tail -3`
Expected: FAIL — header not found.

- [x] **Step 3: Create sim_status.hpp**

Create `src/core/sim_status.hpp`:

```cpp
#pragma once
#include <string>
#include <vector>

namespace neospice {

enum class ConvergenceMethod {
    DIRECT,
    GMIN_STEPPING,
    SOURCE_STEPPING,
    PSEUDO_TRANSIENT
};

inline const char* convergence_method_name(ConvergenceMethod m) {
    switch (m) {
    case ConvergenceMethod::DIRECT:             return "direct";
    case ConvergenceMethod::GMIN_STEPPING:      return "gmin-stepping";
    case ConvergenceMethod::SOURCE_STEPPING:    return "source-stepping";
    case ConvergenceMethod::PSEUDO_TRANSIENT:   return "pseudo-transient";
    default:                                     return "unknown";
    }
}

struct SimStatus {
    bool converged = true;
    int iterations = 0;
    ConvergenceMethod convergence_method = ConvergenceMethod::DIRECT;
    std::vector<std::string> warnings;
    double elapsed_seconds = 0.0;
};

} // namespace neospice
```

- [x] **Step 4: Wire test into CMake**

Find the CMake target that builds `test_api.cpp` and add `test_sim_status.cpp` to the same target's sources list. The file is likely `tests/CMakeLists.txt` or `tests/unit/CMakeLists.txt`.

- [x] **Step 5: Run tests**

Run: `cd . && cmake --build build -j$(nproc) && ctest --test-dir build -R SimStatus -V 2>&1 | tail -10`
Expected: PASS

- [x] **Step 6: Commit**

```bash
git add src/core/sim_status.hpp tests/unit/test_sim_status.cpp tests/CMakeLists.txt
git commit -m "feat: add SimStatus struct for structured convergence reporting"
```

---

## Task 7: Embed SimStatus in Result Types ✓ `5bc38f8`

**Files:**
- Modify: `src/core/dc.hpp`
- Modify: `src/core/transient.hpp`
- Modify: `src/core/ac.hpp`
- Modify: `src/core/noise.hpp`
- Modify: `src/core/tf.hpp`
- Modify: `src/core/sens.hpp`
- Modify: `src/core/pz.hpp`
- Test: `tests/unit/test_api.cpp`

Add a `SimStatus status` field to every result type. Initially it will have the default (converged=true, iterations=0). The next task wires solve functions to populate it.

- [x] **Step 1: Write the failing test**

Add to `tests/unit/test_api.cpp`:

```cpp
TEST(SimStatusIntegration, DCResultHasStatus) {
    neospice::Simulator sim;
    std::string netlist = R"(
Status test
V1 in 0 10
R1 in out 1k
R2 out 0 1k
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(result.dc.has_value());
    // Status field must exist and report convergence
    EXPECT_TRUE(result.dc->status.converged);
    EXPECT_GT(result.dc->status.iterations, 0);
}

TEST(SimStatusIntegration, TransientResultHasStatus) {
    neospice::Simulator sim;
    std::string netlist = R"(
RC status
V1 in 0 5
R1 in out 1k
C1 out 0 1u
.tran 10u 5m
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(result.transient.has_value());
    EXPECT_TRUE(result.transient->status.converged);
    EXPECT_GT(result.transient->status.iterations, 0);
}

TEST(SimStatusIntegration, ACResultHasStatus) {
    neospice::Simulator sim;
    std::string netlist = R"(
AC status
V1 in 0 DC 0 AC 1
R1 in out 1k
C1 out 0 1n
.ac dec 10 100 10meg
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(result.ac.has_value());
    EXPECT_TRUE(result.ac->status.converged);
}
```

- [x] **Step 2: Run test to verify it fails**

Run: `cd . && cmake --build build -j$(nproc) 2>&1 | tail -3`
Expected: FAIL — no `status` member.

- [x] **Step 3: Add SimStatus field to all result types**

Add `#include "core/sim_status.hpp"` and `SimStatus status;` field to each struct:

In `src/core/dc.hpp`, add include at top, and inside `struct DCResult` add `SimStatus status;` as the last field. Same for `struct DCSweepResult`.

In `src/core/transient.hpp`, add include at top, and inside `struct TransientResult` add `SimStatus status;` as the last field.

In `src/core/ac.hpp`, add include at top, and inside `struct ACResult` add `SimStatus status;` as the last field.

In `src/core/noise.hpp`, add include at top, and inside `struct NoiseResult` add `SimStatus status;` as the last field.

In `src/core/tf.hpp`, add include at top, and inside `struct TFResult` add `SimStatus status;` as the last field.

In `src/core/sens.hpp`, add include at top, and inside `struct SensResult` add `SimStatus status;` as the last field.

In `src/core/pz.hpp`, add include at top, and inside `struct PZResult` add `SimStatus status;` as the last field.

- [x] **Step 4: Build and run full test suite**

Run: `cd . && cmake --build build -j$(nproc) && ctest --test-dir build -j$(nproc) 2>&1 | tail -5`
Expected: all existing tests pass (SimStatus has sane defaults, so nothing breaks). The new tests will fail on `iterations > 0` since solve functions don't populate status yet — that's Task 8. Mark these tests as `DISABLED_` for now:

Rename the new tests:
- `DISABLED_DCResultHasStatus`
- `DISABLED_TransientResultHasStatus`
- `DISABLED_ACResultHasStatus`

Then run: `ctest --test-dir build -j$(nproc) 2>&1 | tail -5`
Expected: all pass (disabled tests skipped).

- [x] **Step 5: Commit**

```bash
git add src/core/dc.hpp src/core/transient.hpp src/core/ac.hpp src/core/noise.hpp \
        src/core/tf.hpp src/core/sens.hpp src/core/pz.hpp tests/unit/test_api.cpp
git commit -m "feat: embed SimStatus field in all result types"
```

---

## Task 8: Populate SimStatus in DC and Transient Solvers ✓ `06031ba`

**Files:**
- Modify: `src/core/dc.cpp`
- Modify: `src/core/transient.cpp`
- Test: `tests/unit/test_api.cpp` (re-enable disabled tests)

This is the wiring task — thread SimStatus through the convergence cascade in `solve_dc()` and the transient loop in `solve_transient()`.

- [x] **Step 1: Re-enable the SimStatus integration tests**

In `tests/unit/test_api.cpp`, rename back:
- `DISABLED_DCResultHasStatus` → `DCResultHasStatus`
- `DISABLED_TransientResultHasStatus` → `TransientResultHasStatus`
- `DISABLED_ACResultHasStatus` → `ACResultHasStatus`

- [x] **Step 2: Run tests to confirm they fail**

Run: `cd . && cmake --build build -j$(nproc) && ctest --test-dir build -R SimStatusIntegration -V 2>&1 | tail -15`
Expected: FAIL on `iterations > 0`.

- [x] **Step 3: Populate SimStatus in solve_dc()**

In `src/core/dc.cpp`, add `#include <chrono>` at the top.

In `solve_dc()`, add timing and status tracking. After the existing convergence cascade (lines ~74–103), before building DCResult (line ~105):

1. At the start of `solve_dc()`, add:
```cpp
    auto t_start = std::chrono::steady_clock::now();
    SimStatus sim_status;
```

2. After each convergence attempt, update `sim_status`. Replace the existing cascade structure so that after each successful branch:
```cpp
    // After direct newton_solve succeeds:
    sim_status.convergence_method = ConvergenceMethod::DIRECT;
    sim_status.iterations = result.iterations;

    // After gmin_stepping succeeds:
    sim_status.convergence_method = ConvergenceMethod::GMIN_STEPPING;
    sim_status.iterations = result.iterations;
    sim_status.warnings.push_back("gmin stepping used");

    // After source_stepping succeeds:
    sim_status.convergence_method = ConvergenceMethod::SOURCE_STEPPING;
    sim_status.iterations = result.iterations;
    sim_status.warnings.push_back("source stepping used");

    // After pseudo_transient succeeds:
    sim_status.convergence_method = ConvergenceMethod::PSEUDO_TRANSIENT;
    sim_status.iterations = result.iterations;
    sim_status.warnings.push_back("pseudo-transient continuation used");
```

3. Before returning `dc_result`, set:
```cpp
    auto t_end = std::chrono::steady_clock::now();
    sim_status.elapsed_seconds = std::chrono::duration<double>(t_end - t_start).count();
    sim_status.converged = true;
    dc_result.status = sim_status;
```

The same pattern applies to `solve_dc_sweep()` — add timing and populate the DCSweepResult's status with the total iterations and elapsed time for the full sweep.

- [x] **Step 4: Populate SimStatus in solve_transient()**

In `src/core/transient.cpp`, add `#include <chrono>` at the top.

1. At the start of `solve_transient()`, add:
```cpp
    auto t_start = std::chrono::steady_clock::now();
    SimStatus sim_status;
    int total_iterations = 0;
```

2. After each Newton solve in the transient time-stepping loop, accumulate:
```cpp
    total_iterations += result.iterations;
```

3. Before returning `tran_result`, set:
```cpp
    auto t_end = std::chrono::steady_clock::now();
    sim_status.elapsed_seconds = std::chrono::duration<double>(t_end - t_start).count();
    sim_status.converged = true;
    sim_status.iterations = total_iterations;
    tran_result.status = sim_status;
```

- [x] **Step 5: Populate SimStatus in solve_ac()**

In `src/core/ac.cpp`, add `#include <chrono>` at the top.

The AC solve first does a DC operating point. Capture its status:

1. At the start of `solve_ac()`:
```cpp
    auto t_start = std::chrono::steady_clock::now();
```

2. After AC sweep completes, before returning:
```cpp
    auto t_end = std::chrono::steady_clock::now();
    ac_result.status.converged = true;
    ac_result.status.elapsed_seconds = std::chrono::duration<double>(t_end - t_start).count();
```

- [x] **Step 6: Run tests**

Run: `cd . && cmake --build build -j$(nproc) && ctest --test-dir build -R SimStatusIntegration -V 2>&1 | tail -15`
Expected: all 3 PASS.

- [x] **Step 7: Run full test suite for regressions**

Run: `cd . && ctest --test-dir build -j$(nproc) 2>&1 | tail -5`
Expected: all tests pass.

- [x] **Step 8: Commit**

```bash
git add src/core/dc.cpp src/core/transient.cpp src/core/ac.cpp tests/unit/test_api.cpp
git commit -m "feat: populate SimStatus in DC, transient, and AC solvers"
```

---

## Task 9: Populate SimStatus in Remaining Solvers ✓ `e1b0a70`

**Files:**
- Modify: `src/core/noise.cpp`
- Modify: `src/core/tf.cpp`
- Modify: `src/core/sens.cpp`
- Modify: `src/core/pz.cpp`
- Test: `tests/unit/test_api.cpp`

Same pattern as Task 8 for the remaining analysis types.

- [x] **Step 1: Write tests**

Add to `tests/unit/test_api.cpp`:

```cpp
TEST(SimStatusIntegration, NoiseResultHasStatus) {
    neospice::Simulator sim;
    std::string netlist = R"(
Noise status
V1 in 0 DC 0 AC 1
R1 in out 1k
R2 out 0 1k
.noise v(out) V1 dec 10 1 1meg
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(result.noise.has_value());
    EXPECT_TRUE(result.noise->status.converged);
    EXPECT_GT(result.noise->status.elapsed_seconds, 0.0);
}

TEST(SimStatusIntegration, TFResultHasStatus) {
    neospice::Simulator sim;
    std::string netlist = R"(
TF status
V1 in 0 10
R1 in out 1k
R2 out 0 1k
.tf v(out) V1
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(result.tf.has_value());
    EXPECT_TRUE(result.tf->status.converged);
    EXPECT_GT(result.tf->status.elapsed_seconds, 0.0);
}

TEST(SimStatusIntegration, SensResultHasStatus) {
    neospice::Simulator sim;
    std::string netlist = R"(
Sens status
V1 in 0 10
R1 in out 1k
R2 out 0 1k
.sens v(out)
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(result.sens.has_value());
    EXPECT_TRUE(result.sens->status.converged);
}
```

- [x] **Step 2: Run tests to verify they fail**

Run: `cd . && cmake --build build -j$(nproc) && ctest --test-dir build -R "SimStatusIntegration.*Noise|SimStatusIntegration.*TF|SimStatusIntegration.*Sens" -V 2>&1 | tail -15`
Expected: FAIL on `elapsed_seconds > 0.0`.

- [x] **Step 3: Add timing to noise, TF, sens, and PZ solvers**

In each of `src/core/noise.cpp`, `src/core/tf.cpp`, `src/core/sens.cpp`, `src/core/pz.cpp`:

1. Add `#include <chrono>` at top
2. At start of solve function: `auto t_start = std::chrono::steady_clock::now();`
3. Before returning result:
```cpp
    auto t_end = std::chrono::steady_clock::now();
    result.status.converged = true;
    result.status.elapsed_seconds = std::chrono::duration<double>(t_end - t_start).count();
```

For `solve_tf()` and `solve_sens()`, also capture the DC iterations from the internal DC solve.

- [x] **Step 4: Run tests**

Run: `cd . && cmake --build build -j$(nproc) && ctest --test-dir build -R SimStatusIntegration -V 2>&1 | tail -15`
Expected: all PASS.

- [x] **Step 5: Run full test suite**

Run: `ctest --test-dir build -j$(nproc) 2>&1 | tail -5`
Expected: all tests pass, zero regressions.

- [x] **Step 6: Commit**

```bash
git add src/core/noise.cpp src/core/tf.cpp src/core/sens.cpp src/core/pz.cpp \
        tests/unit/test_api.cpp
git commit -m "feat: populate SimStatus in noise, TF, sensitivity, and PZ solvers"
```

---

## Task 10: Deduplicate apply_save_filter in neospice.cpp ✓ `064cfd6`

**Files:**
- Modify: `src/api/neospice.cpp`

The `apply_save_filter` function is copy-pasted 4 times with identical logic (DCResult, DCSweepResult, TransientResult, ACResult). Replace with a single template.

- [x] **Step 1: Run full test suite to establish baseline**

Run: `cd . && ctest --test-dir build -j$(nproc) 2>&1 | tail -5`
Expected: all pass.

- [x] **Step 2: Replace 4 overloads with one template**

In `src/api/neospice.cpp`, replace all four `apply_save_filter` functions (the block from roughly line 26 to line 92) with:

```cpp
template<typename Result>
static void apply_save_filter(Result& r, const std::vector<std::string>& sigs) {
    if (sigs.empty()) return;
    std::unordered_set<std::string> keep(sigs.begin(), sigs.end());
    auto erase_missing = [&keep](auto& map) {
        for (auto it = map.begin(); it != map.end(); ) {
            if (keep.count(it->first) == 0)
                it = map.erase(it);
            else
                ++it;
        }
    };
    if constexpr (requires { r.node_voltages; }) {
        erase_missing(r.node_voltages);
        erase_missing(r.branch_currents);
    } else {
        erase_missing(r.voltages);
        erase_missing(r.currents);
    }
}
```

- [x] **Step 3: Build and run full test suite**

Run: `cd . && cmake --build build -j$(nproc) && ctest --test-dir build -j$(nproc) 2>&1 | tail -5`
Expected: all pass, zero regressions.

- [x] **Step 4: Commit**

```bash
git add src/api/neospice.cpp
git commit -m "refactor: deduplicate apply_save_filter with template"
```
