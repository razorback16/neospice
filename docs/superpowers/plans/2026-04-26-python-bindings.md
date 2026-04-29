# Python Bindings Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship `pip install neospice` with nanobind bindings wrapping the C++ SPICE simulator, publishing manylinux + macOS wheels to PyPI.

**Architecture:** nanobind wraps the public C++ API (`Simulator`, `Circuit`, `CircuitBuilder`, result structs) into a `_core` extension module. A pure-Python `__init__.py` re-exports everything and adds convenience functions (`neospice.dc()`, `neospice.ac()`, etc.). scikit-build-core drives CMake for Python builds. cibuildwheel + GitHub Actions builds wheels for 4 platform/arch combos x 4 Python versions.

**Tech Stack:** nanobind 2.x, scikit-build-core 0.10+, NumPy 1.24+, cibuildwheel, GitHub Actions, pytest

**Spec:** `docs/superpowers/specs/2026-04-26-python-bindings-design.md`

---

## File Map

| Action | Path | Responsibility |
|--------|------|---------------|
| Modify | `CMakeLists.txt` | Generalize SLEEF paths; add `NEOSPICE_BUILD_PYTHON` option with PIC |
| Create | `pyproject.toml` | Python package metadata, scikit-build-core config, cibuildwheel config |
| Create | `python/CMakeLists.txt` | nanobind module target `_core`, links `neospice_lib` |
| Create | `python/bindings.cpp` | All nanobind bindings (enums, structs, Circuit, Simulator, CircuitBuilder, results, SimulationResult) |
| Create | `python/neospice/__init__.py` | Re-exports from `_core` + convenience functions |
| Create | `python/neospice/py.typed` | PEP 561 marker (empty file) |
| Create | `tests/python/test_bindings.py` | pytest suite: smoke, DC, AC, transient, noise, sweep, TF, sens, CircuitBuilder, errors |
| Create | `.github/workflows/wheels.yml` | cibuildwheel CI + PyPI publish via trusted publisher |

---

### Task 1: Generalize CMake SLEEF Discovery

**Files:**
- Modify: `CMakeLists.txt:22-29`

This is a prerequisite for cross-platform builds. The current CMake hardcodes x86_64 Linux paths for SLEEF.

- [ ] **Step 1: Verify current C++ tests pass before changes**

Run:
```bash
cd ./build && cmake --build . -j$(nproc) && ctest -j$(nproc) --output-on-failure 2>&1 | tail -5
```
Expected: all tests pass.

- [ ] **Step 2: Generalize SLEEF find paths**

In `CMakeLists.txt`, replace lines 22-29:

```cmake
# SLEEF
find_path(SLEEF_INCLUDE_DIR sleef.h
    PATHS /usr/include/x86_64-linux-gnu
    REQUIRED
)
find_library(SLEEF_LIBRARY
    NAMES sleef
    PATHS /usr/lib/x86_64-linux-gnu
    REQUIRED
)
```

with:

```cmake
# SLEEF
find_path(SLEEF_INCLUDE_DIR sleef.h REQUIRED)
find_library(SLEEF_LIBRARY NAMES sleef REQUIRED)
```

- [ ] **Step 3: Rebuild and verify tests still pass**

Run:
```bash
cd ./build && cmake .. && cmake --build . -j$(nproc) && ctest -j$(nproc) --output-on-failure 2>&1 | tail -5
```
Expected: all tests pass (SLEEF still found via system search paths on this machine).

- [ ] **Step 4: Commit**

```bash
git add CMakeLists.txt
git commit -m "build: generalize SLEEF discovery for cross-platform support"
```

---

### Task 2: Add Python Build Infrastructure

**Files:**
- Modify: `CMakeLists.txt` (append Python option block)
- Create: `pyproject.toml`
- Create: `python/CMakeLists.txt`
- Create: `python/neospice/__init__.py` (minimal placeholder)
- Create: `python/neospice/py.typed`

- [ ] **Step 1: Add NEOSPICE_BUILD_PYTHON option to CMakeLists.txt**

Append after the `add_executable(neospice ...)` block (after the last line of `CMakeLists.txt`):

```cmake

# Python bindings (opt-in)
option(NEOSPICE_BUILD_PYTHON "Build Python bindings" OFF)
if(NEOSPICE_BUILD_PYTHON)
    set_target_properties(neospice_lib PROPERTIES POSITION_INDEPENDENT_CODE ON)
    foreach(dev bsim4v7 bsim3 bjt jfet jfet2 dio mos1 mos3 mos9
                vbic asrc hfet1 hfet2 bsim3v32 hisim2 hisimhv bsimsoi)
        set_target_properties(${dev}_obj PROPERTIES POSITION_INDEPENDENT_CODE ON)
    endforeach()

    find_package(Python COMPONENTS Interpreter Development.Module REQUIRED)
    find_package(nanobind CONFIG REQUIRED)
    add_subdirectory(python)
endif()
```

- [ ] **Step 2: Create python/CMakeLists.txt**

Create `python/CMakeLists.txt`:

```cmake
nanobind_add_module(_core bindings.cpp)
target_link_libraries(_core PRIVATE neospice_lib)
install(TARGETS _core DESTINATION neospice)
```

- [ ] **Step 3: Create minimal bindings.cpp stub**

Create `python/bindings.cpp`:

```cpp
#include <nanobind/nanobind.h>

namespace nb = nanobind;

NB_MODULE(_core, m) {
    m.doc() = "neospice: fast SPICE circuit simulator";
}
```

- [ ] **Step 4: Create pyproject.toml**

Create `pyproject.toml`:

```toml
[build-system]
requires = ["scikit-build-core>=0.10", "nanobind>=2.0"]
build-backend = "scikit_build_core.build"

[project]
name = "neospice"
version = "0.1.0"
description = "Fast SPICE circuit simulator with a Pythonic API"
requires-python = ">=3.10"
dependencies = ["numpy>=1.24"]
license = {text = "MIT"}
classifiers = [
    "Development Status :: 3 - Alpha",
    "Intended Audience :: Science/Research",
    "Topic :: Scientific/Engineering :: Electronic Design Automation (EDA)",
    "Programming Language :: Python :: 3",
    "Programming Language :: C++",
]

[project.urls]
Homepage = "https://github.com/Razorback16/neospice"

[tool.scikit-build]
cmake.args = ["-DNEOSPICE_BUILD_PYTHON=ON", "-DNEOSPICE_BUILD_TESTS=OFF"]
wheel.packages = ["python/neospice"]
```

- [ ] **Step 5: Create python/neospice/__init__.py placeholder**

Create `python/neospice/__init__.py`:

```python
from neospice._core import *  # noqa: F401,F403

__version__ = "0.1.0"
```

- [ ] **Step 6: Create python/neospice/py.typed**

Create an empty file at `python/neospice/py.typed`.

- [ ] **Step 7: Verify pip install builds and imports**

Run:
```bash
cd . && pip install -v --no-build-isolation -e .
```
Then:
```bash
python -c "import neospice; print(neospice.__version__)"
```
Expected: prints `0.1.0`.

- [ ] **Step 8: Verify C++ tests still pass (PIC didn't break anything)**

Run:
```bash
cd ./build && cmake .. -DNEOSPICE_BUILD_PYTHON=OFF && cmake --build . -j$(nproc) && ctest -j$(nproc) --output-on-failure 2>&1 | tail -5
```
Expected: all tests pass.

- [ ] **Step 9: Commit**

```bash
git add CMakeLists.txt pyproject.toml python/CMakeLists.txt python/bindings.cpp python/neospice/__init__.py python/neospice/py.typed
git commit -m "feat: add Python build infrastructure (nanobind + scikit-build-core)"
```

---

### Task 3: Bind Enums, Options Structs, and SimStatus

**Files:**
- Modify: `python/bindings.cpp`

These are leaf types with no dependencies on other bindings — bind them first so later tasks can reference them.

- [ ] **Step 1: Write test for enums and structs**

Create `tests/python/test_bindings.py`:

```python
import neospice


class TestEnums:
    def test_ac_mode_values(self):
        assert hasattr(neospice, "ACMode")
        assert neospice.ACMode.DEC is not None
        assert neospice.ACMode.OCT is not None
        assert neospice.ACMode.LIN is not None

    def test_integration_method_values(self):
        assert hasattr(neospice, "IntegrationMethod")
        assert neospice.IntegrationMethod.TRAPEZOIDAL is not None
        assert neospice.IntegrationMethod.GEAR2 is not None


class TestSimulatorOptions:
    def test_defaults(self):
        opts = neospice.SimulatorOptions()
        assert opts.reltol == 1e-3
        assert opts.abstol == 1e-12
        assert opts.vntol == 1e-6
        assert opts.trtol == 7.0
        assert opts.gmin == 1e-12

    def test_custom_values(self):
        opts = neospice.SimulatorOptions()
        opts.reltol = 1e-4
        assert opts.reltol == 1e-4


class TestSourceSpecs:
    def test_source_spec_defaults(self):
        spec = neospice.SourceSpec()
        assert spec.dc == 0.0
        assert spec.ac_mag == 0.0
        assert spec.ac_phase == 0.0

    def test_pulse_spec_defaults(self):
        spec = neospice.PulseSpec()
        assert spec.v1 == 0.0
        assert spec.v2 == 0.0

    def test_sin_spec_defaults(self):
        spec = neospice.SinSpec()
        assert spec.vo == 0.0
        assert spec.va == 0.0


class TestDCSweepParam:
    def test_fields(self):
        p = neospice.DCSweepParam()
        p.source_name = "V1"
        p.start = 0.0
        p.stop = 5.0
        p.step = 0.1
        assert p.source_name == "V1"
        assert p.stop == 5.0
```

- [ ] **Step 2: Run tests — expect failure**

Run:
```bash
cd . && python -m pytest tests/python/test_bindings.py -v 2>&1 | tail -20
```
Expected: FAIL — `ACMode` not defined in `neospice`.

- [ ] **Step 3: Implement enum and struct bindings**

Replace `python/bindings.cpp` with:

```cpp
#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>
#include "api/neospice.hpp"
#include "api/circuit_builder.hpp"

namespace nb = nanobind;
using namespace neospice;

NB_MODULE(_core, m) {
    m.doc() = "neospice: fast SPICE circuit simulator";

    // --- Enums ---
    nb::enum_<ACMode>(m, "ACMode")
        .value("DEC", ACMode::DEC)
        .value("OCT", ACMode::OCT)
        .value("LIN", ACMode::LIN);

    nb::enum_<IntegrationMethod>(m, "IntegrationMethod")
        .value("TRAPEZOIDAL", IntegrationMethod::TRAPEZOIDAL)
        .value("GEAR2", IntegrationMethod::GEAR2);

    nb::enum_<ConvergenceMethod>(m, "ConvergenceMethod")
        .value("DIRECT", ConvergenceMethod::DIRECT)
        .value("GMIN_STEPPING", ConvergenceMethod::GMIN_STEPPING)
        .value("SOURCE_STEPPING", ConvergenceMethod::SOURCE_STEPPING)
        .value("PSEUDO_TRANSIENT", ConvergenceMethod::PSEUDO_TRANSIENT);

    // --- SimStatus ---
    nb::class_<SimStatus>(m, "SimStatus")
        .def_ro("converged", &SimStatus::converged)
        .def_ro("iterations", &SimStatus::iterations)
        .def_ro("convergence_method", &SimStatus::convergence_method)
        .def_ro("warnings", &SimStatus::warnings)
        .def_ro("elapsed_seconds", &SimStatus::elapsed_seconds);

    // --- Options structs ---
    nb::class_<SimulatorOptions>(m, "SimulatorOptions")
        .def(nb::init<>())
        .def_rw("abstol", &SimulatorOptions::abstol)
        .def_rw("reltol", &SimulatorOptions::reltol)
        .def_rw("vntol", &SimulatorOptions::vntol)
        .def_rw("trtol", &SimulatorOptions::trtol)
        .def_rw("gmin", &SimulatorOptions::gmin);

    nb::class_<SourceSpec>(m, "SourceSpec")
        .def(nb::init<>())
        .def_rw("dc", &SourceSpec::dc)
        .def_rw("ac_mag", &SourceSpec::ac_mag)
        .def_rw("ac_phase", &SourceSpec::ac_phase);

    nb::class_<PulseSpec>(m, "PulseSpec")
        .def(nb::init<>())
        .def_rw("v1", &PulseSpec::v1)
        .def_rw("v2", &PulseSpec::v2)
        .def_rw("td", &PulseSpec::td)
        .def_rw("tr", &PulseSpec::tr)
        .def_rw("tf", &PulseSpec::tf)
        .def_rw("pw", &PulseSpec::pw)
        .def_rw("per", &PulseSpec::per);

    nb::class_<SinSpec>(m, "SinSpec")
        .def(nb::init<>())
        .def_rw("vo", &SinSpec::vo)
        .def_rw("va", &SinSpec::va)
        .def_rw("freq", &SinSpec::freq)
        .def_rw("td", &SinSpec::td)
        .def_rw("theta", &SinSpec::theta)
        .def_rw("phase", &SinSpec::phase);

    nb::class_<TransientOptions>(m, "TransientOptions")
        .def(nb::init<>())
        .def_rw("uic", &TransientOptions::uic);

    nb::class_<ACOptions>(m, "ACOptions")
        .def(nb::init<>());

    nb::class_<DCSweepParam>(m, "DCSweepParam")
        .def(nb::init<>())
        .def_rw("source_name", &DCSweepParam::source_name)
        .def_rw("start", &DCSweepParam::start)
        .def_rw("stop", &DCSweepParam::stop)
        .def_rw("step", &DCSweepParam::step);
}
```

- [ ] **Step 4: Rebuild and run tests**

Run:
```bash
cd . && pip install --no-build-isolation -e . && python -m pytest tests/python/test_bindings.py -v
```
Expected: all tests pass.

- [ ] **Step 5: Commit**

```bash
git add python/bindings.cpp tests/python/test_bindings.py
git commit -m "feat(python): bind enums, SimStatus, and options structs"
```

---

### Task 4: Bind Circuit, Simulator, and CircuitBuilder

**Files:**
- Modify: `python/bindings.cpp`
- Modify: `tests/python/test_bindings.py`

- [ ] **Step 1: Write tests for Circuit, Simulator, and CircuitBuilder**

Append to `tests/python/test_bindings.py`:

```python
import os

CIRCUITS_DIR = os.path.join(os.path.dirname(__file__), "..", "circuits")


class TestSimulatorLoadParse:
    def test_load_file(self):
        sim = neospice.Simulator()
        ckt = sim.load(os.path.join(CIRCUITS_DIR, "resistor_divider.cir"))
        assert isinstance(ckt, neospice.Circuit)
        assert ckt.title == "Resistor Divider"

    def test_parse_string(self):
        sim = neospice.Simulator()
        ckt = sim.parse("Test\nV1 a 0 DC 1\nR1 a 0 1k\n.op\n.end\n")
        assert isinstance(ckt, neospice.Circuit)
        assert "a" in ckt.node_names()

    def test_circuit_introspection(self):
        sim = neospice.Simulator()
        ckt = sim.load(os.path.join(CIRCUITS_DIR, "resistor_divider.cir"))
        nodes = ckt.node_names()
        assert "in" in nodes
        assert "mid" in nodes
        devs = ckt.device_names()
        assert len(devs) == 3  # V1, R1, R2


class TestCircuitBuilder:
    def test_build_and_run_dc(self):
        ckt = (neospice.CircuitBuilder()
            .title("Divider")
            .vsource("V1", "in", "0", neospice.SourceSpec())
            .resistor("R1", "in", "out", 1e3)
            .resistor("R2", "out", "0", 1e3)
            .build())
        assert isinstance(ckt, neospice.Circuit)
        assert ckt.title == "Divider"
```

- [ ] **Step 2: Run tests — expect failure**

Run:
```bash
cd . && python -m pytest tests/python/test_bindings.py::TestSimulatorLoadParse -v 2>&1 | tail -10
```
Expected: FAIL — `Simulator` not defined.

- [ ] **Step 3: Add Circuit, Simulator, and CircuitBuilder bindings**

In `python/bindings.cpp`, add the following includes at the top (after existing includes):

```cpp
#include <nanobind/stl/map.h>
#include <nanobind/stl/optional.h>
```

Then, inside the `NB_MODULE` block, after the `DCSweepParam` binding and before the closing `}`, add:

```cpp
    // --- Circuit (move-only) ---
    nb::class_<Circuit>(m, "Circuit")
        .def_ro("title", &Circuit::title)
        .def("node_names", &Circuit::node_names)
        .def("device_names", &Circuit::device_names)
        .def("device_info", &Circuit::device_info)
        .def("set_param", &Circuit::set_param);

    nb::class_<DeviceInfo>(m, "DeviceInfo")
        .def_ro("name", &DeviceInfo::name)
        .def_ro("type", &DeviceInfo::type)
        .def_ro("nodes", &DeviceInfo::nodes)
        .def_ro("value", &DeviceInfo::value);

    // --- Simulator ---
    nb::class_<Simulator>(m, "Simulator")
        .def(nb::init<>())
        .def(nb::init<SimulatorOptions>())
        .def("load", &Simulator::load)
        .def("parse", &Simulator::parse)
        .def("run_dc", &Simulator::run_dc)
        .def("run_transient",
             nb::overload_cast<Circuit&, double, double>(&Simulator::run_transient))
        .def("run_transient_with_opts",
             nb::overload_cast<Circuit&, double, double, const TransientOptions&>(
                 &Simulator::run_transient))
        .def("run_ac",
             nb::overload_cast<Circuit&, ACMode, int, double, double>(
                 &Simulator::run_ac))
        .def("run_ac_with_opts",
             nb::overload_cast<Circuit&, ACMode, int, double, double, const ACOptions&>(
                 &Simulator::run_ac))
        .def("run_noise", &Simulator::run_noise)
        .def("run_dc_sweep", &Simulator::run_dc_sweep)
        .def("run_tf", &Simulator::run_tf)
        .def("run_sens", &Simulator::run_sens)
        .def("run", &Simulator::run)
        .def("run_step_sweep", &Simulator::run_step_sweep);

    // --- CircuitBuilder (fluent API) ---
    auto cb = nb::class_<CircuitBuilder>(m, "CircuitBuilder");
    cb.def(nb::init<>());
    cb.def("title", &CircuitBuilder::title, nb::rv_policy::reference);
    cb.def("resistor", &CircuitBuilder::resistor, nb::rv_policy::reference);
    cb.def("capacitor", &CircuitBuilder::capacitor, nb::rv_policy::reference);
    cb.def("inductor", &CircuitBuilder::inductor, nb::rv_policy::reference);
    cb.def("vsource", &CircuitBuilder::vsource, nb::rv_policy::reference);
    cb.def("vsource_pulse", &CircuitBuilder::vsource_pulse, nb::rv_policy::reference);
    cb.def("vsource_sin", &CircuitBuilder::vsource_sin, nb::rv_policy::reference);
    cb.def("isource", &CircuitBuilder::isource, nb::rv_policy::reference);
    cb.def("diode", &CircuitBuilder::diode, nb::rv_policy::reference);
    cb.def("subcircuit", &CircuitBuilder::subcircuit, nb::rv_policy::reference);
    cb.def("model", &CircuitBuilder::model, nb::rv_policy::reference);
    cb.def("include", &CircuitBuilder::include, nb::rv_policy::reference);
    cb.def("raw_line", &CircuitBuilder::raw_line, nb::rv_policy::reference);
    cb.def("build", &CircuitBuilder::build);
```

- [ ] **Step 4: Rebuild and run tests**

Run:
```bash
cd . && pip install --no-build-isolation -e . && python -m pytest tests/python/test_bindings.py -v
```
Expected: all tests pass.

- [ ] **Step 5: Commit**

```bash
git add python/bindings.cpp tests/python/test_bindings.py
git commit -m "feat(python): bind Circuit, Simulator, and CircuitBuilder"
```

---

### Task 5: Bind Result Types with NumPy Arrays

**Files:**
- Modify: `python/bindings.cpp`
- Modify: `tests/python/test_bindings.py`

This is the largest binding task. Each result type needs borrowed ndarray views for stored vectors and owned copies for computed vectors.

- [ ] **Step 1: Write tests for DC and transient results**

Append to `tests/python/test_bindings.py`:

```python
import numpy as np


class TestDCResult:
    def test_voltage_divider(self):
        sim = neospice.Simulator()
        ckt = sim.load(os.path.join(CIRCUITS_DIR, "resistor_divider.cir"))
        result = sim.run_dc(ckt)
        assert isinstance(result, neospice.DCResult)
        assert abs(result.voltage("mid") - 5.0) < 0.01
        assert result.status.converged

    def test_signal_names(self):
        sim = neospice.Simulator()
        ckt = sim.load(os.path.join(CIRCUITS_DIR, "resistor_divider.cir"))
        result = sim.run_dc(ckt)
        names = result.signal_names()
        assert isinstance(names, list)
        assert len(names) > 0

    def test_diff(self):
        sim = neospice.Simulator()
        ckt = sim.load(os.path.join(CIRCUITS_DIR, "resistor_divider.cir"))
        result = sim.run_dc(ckt)
        diff = result.diff("in", "mid")
        assert abs(diff - 5.0) < 0.01

    def test_missing_node_raises_key_error(self):
        sim = neospice.Simulator()
        ckt = sim.load(os.path.join(CIRCUITS_DIR, "resistor_divider.cir"))
        result = sim.run_dc(ckt)
        try:
            result.voltage("nonexistent")
            assert False, "Should have raised"
        except KeyError:
            pass

    def test_current(self):
        sim = neospice.Simulator()
        ckt = sim.load(os.path.join(CIRCUITS_DIR, "resistor_divider.cir"))
        result = sim.run_dc(ckt)
        i = result.current("v1")
        assert isinstance(i, float)


class TestTransientResult:
    def test_rc_transient(self):
        sim = neospice.Simulator()
        ckt = sim.load(os.path.join(CIRCUITS_DIR, "rc_lowpass.cir"))
        result = sim.run_transient(ckt, 0.1e-6, 50e-6)
        assert isinstance(result, neospice.TransientResult)
        assert isinstance(result.time, np.ndarray)
        assert result.time.dtype == np.float64
        assert len(result.time) > 10

    def test_voltage_returns_ndarray(self):
        sim = neospice.Simulator()
        ckt = sim.load(os.path.join(CIRCUITS_DIR, "rc_lowpass.cir"))
        result = sim.run_transient(ckt, 0.1e-6, 50e-6)
        v = result.voltage("out")
        assert isinstance(v, np.ndarray)
        assert v.dtype == np.float64
        assert len(v) == len(result.time)

    def test_signal_names(self):
        sim = neospice.Simulator()
        ckt = sim.load(os.path.join(CIRCUITS_DIR, "rc_lowpass.cir"))
        result = sim.run_transient(ckt, 0.1e-6, 50e-6)
        names = result.signal_names()
        assert isinstance(names, list)
        assert len(names) > 0

    def test_missing_node_raises_key_error(self):
        sim = neospice.Simulator()
        ckt = sim.load(os.path.join(CIRCUITS_DIR, "rc_lowpass.cir"))
        result = sim.run_transient(ckt, 0.1e-6, 50e-6)
        try:
            result.voltage("nonexistent")
            assert False, "Should have raised"
        except KeyError:
            pass
```

- [ ] **Step 2: Run tests — expect failure**

Run:
```bash
cd . && python -m pytest tests/python/test_bindings.py::TestDCResult -v 2>&1 | tail -10
```
Expected: FAIL — `DCResult` not defined.

- [ ] **Step 3: Implement DCResult and TransientResult bindings**

In `python/bindings.cpp`, add at the top with the other includes:

```cpp
#include <nanobind/ndarray.h>
#include <nanobind/stl/complex.h>
#include <cstring>
```

Add inside the `NB_MODULE` block, after the Simulator binding:

```cpp
    // --- DCResult ---
    nb::class_<DCResult>(m, "DCResult")
        .def("voltage", [](DCResult& self, const std::string& node) -> double {
            try { return self.voltage(node); }
            catch (const std::out_of_range&) {
                throw nb::key_error(node.c_str());
            }
        })
        .def("current", [](DCResult& self, const std::string& dev) -> double {
            try { return self.current(dev); }
            catch (const std::out_of_range&) {
                throw nb::key_error(dev.c_str());
            }
        })
        .def("diff", [](DCResult& self, const std::string& np, const std::string& nn) {
            try { return self.diff(np, nn); }
            catch (const std::out_of_range& e) {
                throw nb::key_error(e.what());
            }
        })
        .def("signal_names", &DCResult::signal_names)
        .def_ro("status", &DCResult::status);

    // --- TransientResult ---
    nb::class_<TransientResult>(m, "TransientResult")
        .def_prop_ro("time", [](nb::handle_t<TransientResult> self) {
            auto& r = nb::cast<TransientResult&>(self);
            return nb::ndarray<nb::numpy, double, nb::shape<nb::any>>(
                r.time.data(), {r.time.size()}, self);
        })
        .def("voltage", [](nb::handle_t<TransientResult> self, const std::string& node) {
            auto& r = nb::cast<TransientResult&>(self);
            try {
                const auto& v = r.voltage(node);
                return nb::ndarray<nb::numpy, const double, nb::shape<nb::any>>(
                    v.data(), {v.size()}, self);
            } catch (const std::out_of_range&) {
                throw nb::key_error(node.c_str());
            }
        })
        .def("current", [](nb::handle_t<TransientResult> self, const std::string& dev) {
            auto& r = nb::cast<TransientResult&>(self);
            try {
                const auto& v = r.current(dev);
                return nb::ndarray<nb::numpy, const double, nb::shape<nb::any>>(
                    v.data(), {v.size()}, self);
            } catch (const std::out_of_range&) {
                throw nb::key_error(dev.c_str());
            }
        })
        .def("diff", [](TransientResult& self, const std::string& np, const std::string& nn) {
            try {
                auto vec = self.diff(np, nn);
                size_t n = vec.size();
                double* data = new double[n];
                std::memcpy(data, vec.data(), n * sizeof(double));
                nb::capsule owner(data, [](void* p) noexcept { delete[] static_cast<double*>(p); });
                return nb::ndarray<nb::numpy, double, nb::shape<nb::any>>(data, {n}, owner);
            } catch (const std::out_of_range& e) {
                throw nb::key_error(e.what());
            }
        })
        .def("signal_names", &TransientResult::signal_names)
        .def_ro("rejected_steps", &TransientResult::rejected_steps)
        .def_ro("status", &TransientResult::status);
```

- [ ] **Step 4: Rebuild and run DC + transient tests**

Run:
```bash
cd . && pip install --no-build-isolation -e . && python -m pytest tests/python/test_bindings.py::TestDCResult tests/python/test_bindings.py::TestTransientResult -v
```
Expected: all pass.

- [ ] **Step 5: Write tests for AC result**

Append to `tests/python/test_bindings.py`:

```python
class TestACResult:
    def test_rc_ac(self):
        sim = neospice.Simulator()
        ckt = sim.load(os.path.join(CIRCUITS_DIR, "rc_ac.cir"))
        result = sim.run_ac(ckt, neospice.ACMode.DEC, 10, 100, 1e9)
        assert isinstance(result, neospice.ACResult)
        assert isinstance(result.frequency, np.ndarray)
        assert result.frequency.dtype == np.float64
        assert len(result.frequency) > 5

    def test_complex_voltage(self):
        sim = neospice.Simulator()
        ckt = sim.load(os.path.join(CIRCUITS_DIR, "rc_ac.cir"))
        result = sim.run_ac(ckt, neospice.ACMode.DEC, 10, 100, 1e9)
        v = result.voltage("out")
        assert isinstance(v, np.ndarray)
        assert v.dtype == np.complex128
        assert len(v) == len(result.frequency)

    def test_magnitude_db(self):
        sim = neospice.Simulator()
        ckt = sim.load(os.path.join(CIRCUITS_DIR, "rc_ac.cir"))
        result = sim.run_ac(ckt, neospice.ACMode.DEC, 10, 100, 1e9)
        db = result.magnitude_db("out")
        assert isinstance(db, np.ndarray)
        assert db.dtype == np.float64
        assert db[0] > db[-1]  # lowpass: gain drops at high freq

    def test_phase_deg(self):
        sim = neospice.Simulator()
        ckt = sim.load(os.path.join(CIRCUITS_DIR, "rc_ac.cir"))
        result = sim.run_ac(ckt, neospice.ACMode.DEC, 10, 100, 1e9)
        ph = result.phase_deg("out")
        assert isinstance(ph, np.ndarray)
        assert ph.dtype == np.float64

    def test_missing_node_raises_key_error(self):
        sim = neospice.Simulator()
        ckt = sim.load(os.path.join(CIRCUITS_DIR, "rc_ac.cir"))
        result = sim.run_ac(ckt, neospice.ACMode.DEC, 10, 100, 1e9)
        try:
            result.voltage("nonexistent")
            assert False, "Should have raised"
        except KeyError:
            pass
```

- [ ] **Step 6: Implement ACResult binding**

Add inside the `NB_MODULE` block, after the TransientResult binding:

```cpp
    // Helper: copy std::vector<double> to a new owned ndarray
    auto make_owned_double_array = [](const std::vector<double>& vec) {
        size_t n = vec.size();
        double* data = new double[n];
        std::memcpy(data, vec.data(), n * sizeof(double));
        nb::capsule owner(data, [](void* p) noexcept { delete[] static_cast<double*>(p); });
        return nb::ndarray<nb::numpy, double, nb::shape<nb::any>>(data, {n}, owner);
    };

    // --- ACResult ---
    nb::class_<ACResult>(m, "ACResult")
        .def_prop_ro("frequency", [](nb::handle_t<ACResult> self) {
            auto& r = nb::cast<ACResult&>(self);
            return nb::ndarray<nb::numpy, double, nb::shape<nb::any>>(
                r.frequency.data(), {r.frequency.size()}, self);
        })
        .def("voltage", [](nb::handle_t<ACResult> self, const std::string& node) {
            auto& r = nb::cast<ACResult&>(self);
            try {
                const auto& v = r.voltage(node);
                return nb::ndarray<nb::numpy, const std::complex<double>, nb::shape<nb::any>>(
                    v.data(), {v.size()}, self);
            } catch (const std::out_of_range&) {
                throw nb::key_error(node.c_str());
            }
        })
        .def("current", [](nb::handle_t<ACResult> self, const std::string& dev) {
            auto& r = nb::cast<ACResult&>(self);
            try {
                const auto& v = r.current(dev);
                return nb::ndarray<nb::numpy, const std::complex<double>, nb::shape<nb::any>>(
                    v.data(), {v.size()}, self);
            } catch (const std::out_of_range&) {
                throw nb::key_error(dev.c_str());
            }
        })
        .def("magnitude_db", [make_owned_double_array](ACResult& self, const std::string& node) {
            try { return make_owned_double_array(self.magnitude_db(node)); }
            catch (const std::out_of_range&) { throw nb::key_error(node.c_str()); }
        })
        .def("phase_deg", [make_owned_double_array](ACResult& self, const std::string& node) {
            try { return make_owned_double_array(self.phase_deg(node)); }
            catch (const std::out_of_range&) { throw nb::key_error(node.c_str()); }
        })
        .def("magnitude", [make_owned_double_array](ACResult& self, const std::string& node) {
            try { return make_owned_double_array(self.magnitude(node)); }
            catch (const std::out_of_range&) { throw nb::key_error(node.c_str()); }
        })
        .def("diff", [](ACResult& self, const std::string& np, const std::string& nn) {
            try {
                auto vec = self.diff(np, nn);
                size_t n = vec.size();
                auto* data = new std::complex<double>[n];
                std::memcpy(data, vec.data(), n * sizeof(std::complex<double>));
                nb::capsule owner(data, [](void* p) noexcept {
                    delete[] static_cast<std::complex<double>*>(p);
                });
                return nb::ndarray<nb::numpy, std::complex<double>, nb::shape<nb::any>>(
                    data, {n}, owner);
            } catch (const std::out_of_range& e) {
                throw nb::key_error(e.what());
            }
        })
        .def("diff_magnitude_db", [make_owned_double_array](ACResult& self,
                const std::string& np, const std::string& nn) {
            try { return make_owned_double_array(self.diff_magnitude_db(np, nn)); }
            catch (const std::out_of_range& e) { throw nb::key_error(e.what()); }
        })
        .def("current_magnitude_db", [make_owned_double_array](ACResult& self,
                const std::string& dev) {
            try { return make_owned_double_array(self.current_magnitude_db(dev)); }
            catch (const std::out_of_range&) { throw nb::key_error(dev.c_str()); }
        })
        .def("current_phase_deg", [make_owned_double_array](ACResult& self,
                const std::string& dev) {
            try { return make_owned_double_array(self.current_phase_deg(dev)); }
            catch (const std::out_of_range&) { throw nb::key_error(dev.c_str()); }
        })
        .def("current_magnitude", [make_owned_double_array](ACResult& self,
                const std::string& dev) {
            try { return make_owned_double_array(self.current_magnitude(dev)); }
            catch (const std::out_of_range&) { throw nb::key_error(dev.c_str()); }
        })
        .def("signal_names", &ACResult::signal_names)
        .def_ro("status", &ACResult::status);
```

- [ ] **Step 7: Rebuild and run all tests so far**

Run:
```bash
cd . && pip install --no-build-isolation -e . && python -m pytest tests/python/test_bindings.py -v
```
Expected: all pass.

- [ ] **Step 8: Write tests for remaining result types (Noise, DCSweep, TF, Sens)**

Append to `tests/python/test_bindings.py`:

```python
class TestNoiseResult:
    def test_rc_noise(self):
        sim = neospice.Simulator()
        ckt = sim.load(os.path.join(CIRCUITS_DIR, "rc_lowpass_noise.cir"))
        result = sim.run_noise(ckt, "out", "v1", neospice.ACMode.DEC, 10, 100, 10e6)
        assert isinstance(result, neospice.NoiseResult)
        assert isinstance(result.frequency, np.ndarray)
        assert len(result.frequency) > 5
        assert isinstance(result.output_noise_density, np.ndarray)
        assert isinstance(result.input_noise_density, np.ndarray)

    def test_device_names(self):
        sim = neospice.Simulator()
        ckt = sim.load(os.path.join(CIRCUITS_DIR, "rc_lowpass_noise.cir"))
        result = sim.run_noise(ckt, "out", "v1", neospice.ACMode.DEC, 10, 100, 10e6)
        names = result.device_names()
        assert isinstance(names, list)
        assert len(names) > 0

    def test_integrated_noise(self):
        sim = neospice.Simulator()
        ckt = sim.load(os.path.join(CIRCUITS_DIR, "rc_lowpass_noise.cir"))
        result = sim.run_noise(ckt, "out", "v1", neospice.ACMode.DEC, 10, 100, 10e6)
        n = result.integrated_output_noise(100, 10e6)
        assert isinstance(n, float)
        assert n > 0


class TestDCSweepResult:
    def test_diode_sweep(self):
        sim = neospice.Simulator()
        ckt = sim.load(os.path.join(CIRCUITS_DIR, "diode_dc_sweep.cir"))
        p = neospice.DCSweepParam()
        p.source_name = "v1"
        p.start = -1.0
        p.stop = 1.0
        p.step = 0.01
        result = sim.run_dc_sweep(ckt, [p])
        assert isinstance(result, neospice.DCSweepResult)
        assert isinstance(result.sweep_values, np.ndarray)
        assert len(result.sweep_values) > 10
        v = result.voltage("out")
        assert isinstance(v, np.ndarray)
        assert len(v) == len(result.sweep_values)


class TestTFResult:
    def test_resistive_divider_tf(self):
        sim = neospice.Simulator()
        ckt = sim.load(os.path.join(CIRCUITS_DIR, "tf_resistive_divider.cir"))
        result = sim.run_tf(ckt, "v(out)", "v1")
        assert isinstance(result, neospice.TFResult)
        assert isinstance(result.transfer_function, float)
        assert isinstance(result.input_impedance, float)
        assert isinstance(result.output_impedance, float)
        assert result.status.converged


class TestSensResult:
    def test_divider_sensitivity(self):
        sim = neospice.Simulator()
        ckt = sim.load(os.path.join(CIRCUITS_DIR, "sens_divider.cir"))
        result = sim.run_sens(ckt, "v(out)")
        assert isinstance(result, neospice.SensResult)
        assert isinstance(result.output_value, float)
        assert abs(result.output_value - 5.0) < 0.1
        assert len(result.entries) > 0
        e = result.entries[0]
        assert hasattr(e, "element")
        assert hasattr(e, "sensitivity")
        assert hasattr(e, "normalized")
```

- [ ] **Step 9: Implement remaining result type bindings**

Add inside the `NB_MODULE` block, after the ACResult binding:

```cpp
    // --- DCSweepResult ---
    nb::class_<DCSweepResult>(m, "DCSweepResult")
        .def_prop_ro("sweep_values", [](nb::handle_t<DCSweepResult> self) {
            auto& r = nb::cast<DCSweepResult&>(self);
            return nb::ndarray<nb::numpy, double, nb::shape<nb::any>>(
                r.sweep_values.data(), {r.sweep_values.size()}, self);
        })
        .def_ro("sweep_var", &DCSweepResult::sweep_var)
        .def("voltage", [](nb::handle_t<DCSweepResult> self, const std::string& node) {
            auto& r = nb::cast<DCSweepResult&>(self);
            try {
                const auto& v = r.voltage(node);
                return nb::ndarray<nb::numpy, const double, nb::shape<nb::any>>(
                    v.data(), {v.size()}, self);
            } catch (const std::out_of_range&) {
                throw nb::key_error(node.c_str());
            }
        })
        .def("current", [](nb::handle_t<DCSweepResult> self, const std::string& dev) {
            auto& r = nb::cast<DCSweepResult&>(self);
            try {
                const auto& v = r.current(dev);
                return nb::ndarray<nb::numpy, const double, nb::shape<nb::any>>(
                    v.data(), {v.size()}, self);
            } catch (const std::out_of_range&) {
                throw nb::key_error(dev.c_str());
            }
        })
        .def("diff", [make_owned_double_array](DCSweepResult& self,
                const std::string& np, const std::string& nn) {
            try { return make_owned_double_array(self.diff(np, nn)); }
            catch (const std::out_of_range& e) { throw nb::key_error(e.what()); }
        })
        .def("signal_names", &DCSweepResult::signal_names)
        .def_ro("status", &DCSweepResult::status);

    // --- NoiseResult ---
    nb::class_<NoiseResult>(m, "NoiseResult")
        .def_prop_ro("frequency", [](nb::handle_t<NoiseResult> self) {
            auto& r = nb::cast<NoiseResult&>(self);
            return nb::ndarray<nb::numpy, double, nb::shape<nb::any>>(
                r.frequency.data(), {r.frequency.size()}, self);
        })
        .def_prop_ro("output_noise_density", [](nb::handle_t<NoiseResult> self) {
            auto& r = nb::cast<NoiseResult&>(self);
            return nb::ndarray<nb::numpy, double, nb::shape<nb::any>>(
                r.output_noise_density.data(), {r.output_noise_density.size()}, self);
        })
        .def_prop_ro("input_noise_density", [](nb::handle_t<NoiseResult> self) {
            auto& r = nb::cast<NoiseResult&>(self);
            return nb::ndarray<nb::numpy, double, nb::shape<nb::any>>(
                r.input_noise_density.data(), {r.input_noise_density.size()}, self);
        })
        .def("output_noise_sqrt", [make_owned_double_array](NoiseResult& self) {
            return make_owned_double_array(self.output_noise_sqrt());
        })
        .def("input_noise_sqrt", [make_owned_double_array](NoiseResult& self) {
            return make_owned_double_array(self.input_noise_sqrt());
        })
        .def("integrated_output_noise", &NoiseResult::integrated_output_noise)
        .def("integrated_input_noise", &NoiseResult::integrated_input_noise)
        .def("device_names", &NoiseResult::device_names)
        .def("device_noise_density", [](nb::handle_t<NoiseResult> self,
                const std::string& name) {
            auto& r = nb::cast<NoiseResult&>(self);
            try {
                const auto& v = r.device_noise_density(name);
                return nb::ndarray<nb::numpy, const double, nb::shape<nb::any>>(
                    v.data(), {v.size()}, self);
            } catch (const std::out_of_range&) {
                throw nb::key_error(name.c_str());
            }
        })
        .def("signal_names", &NoiseResult::signal_names)
        .def_ro("status", &NoiseResult::status);

    // --- TFResult ---
    nb::class_<TFResult>(m, "TFResult")
        .def_ro("output_var", &TFResult::output_var)
        .def_ro("input_src", &TFResult::input_src)
        .def_ro("transfer_function", &TFResult::transfer_function)
        .def_ro("input_impedance", &TFResult::input_impedance)
        .def_ro("output_impedance", &TFResult::output_impedance)
        .def_ro("status", &TFResult::status);

    // --- SensResult ---
    nb::class_<SensResult::Entry>(m, "SensEntry")
        .def_ro("element", &SensResult::Entry::element)
        .def_ro("parameter", &SensResult::Entry::parameter)
        .def_ro("sensitivity", &SensResult::Entry::sensitivity)
        .def_ro("normalized", &SensResult::Entry::normalized);

    nb::class_<SensResult>(m, "SensResult")
        .def_ro("output_var", &SensResult::output_var)
        .def_ro("output_value", &SensResult::output_value)
        .def_ro("entries", &SensResult::entries)
        .def_ro("status", &SensResult::status);

    // --- PZResult ---
    nb::class_<PZResult>(m, "PZResult")
        .def_prop_ro("poles", [](nb::handle_t<PZResult> self) {
            auto& r = nb::cast<PZResult&>(self);
            return nb::ndarray<nb::numpy, const std::complex<double>, nb::shape<nb::any>>(
                r.poles.data(), {r.poles.size()}, self);
        })
        .def_prop_ro("zeros", [](nb::handle_t<PZResult> self) {
            auto& r = nb::cast<PZResult&>(self);
            return nb::ndarray<nb::numpy, const std::complex<double>, nb::shape<nb::any>>(
                r.zeros.data(), {r.zeros.size()}, self);
        })
        .def_ro("status", &PZResult::status);
```

- [ ] **Step 10: Rebuild and run all tests**

Run:
```bash
cd . && pip install --no-build-isolation -e . && python -m pytest tests/python/test_bindings.py -v
```
Expected: all pass.

- [ ] **Step 11: Commit**

```bash
git add python/bindings.cpp tests/python/test_bindings.py
git commit -m "feat(python): bind all result types with NumPy array support"
```

---

### Task 6: Bind SimulationResult and MeasureResult

**Files:**
- Modify: `python/bindings.cpp`
- Modify: `tests/python/test_bindings.py`

- [ ] **Step 1: Write test for SimulationResult**

Append to `tests/python/test_bindings.py`:

```python
class TestSimulationResult:
    def test_run_returns_simulation_result(self):
        sim = neospice.Simulator()
        ckt = sim.load(os.path.join(CIRCUITS_DIR, "resistor_divider.cir"))
        result = sim.run(ckt)
        assert isinstance(result, neospice.SimulationResult)
        assert result.analysis_type == "dc"
        assert result.dc is not None
        assert result.transient is None
        assert result.ac is None

    def test_run_ac_analysis_type(self):
        sim = neospice.Simulator()
        ckt = sim.load(os.path.join(CIRCUITS_DIR, "rc_ac.cir"))
        result = sim.run(ckt)
        assert result.analysis_type == "ac"
        assert result.ac is not None
        assert result.dc is None

    def test_measures_none_when_absent(self):
        sim = neospice.Simulator()
        ckt = sim.load(os.path.join(CIRCUITS_DIR, "resistor_divider.cir"))
        result = sim.run(ckt)
        assert result.measures is None

    def test_step_none_when_absent(self):
        sim = neospice.Simulator()
        ckt = sim.load(os.path.join(CIRCUITS_DIR, "resistor_divider.cir"))
        result = sim.run(ckt)
        assert result.step is None
```

- [ ] **Step 2: Run tests — expect failure**

Run:
```bash
cd . && python -m pytest tests/python/test_bindings.py::TestSimulationResult -v 2>&1 | tail -10
```
Expected: FAIL — `SimulationResult` not defined.

- [ ] **Step 3: Implement SimulationResult and MeasureResult bindings**

Add inside the `NB_MODULE` block, after the PZResult binding:

```cpp
    // --- MeasureResult ---
    nb::class_<MeasureResult>(m, "MeasureResult")
        .def_ro("values", &MeasureResult::values);

    // --- StepResult ---
    nb::class_<StepResult>(m, "StepResult")
        .def_prop_ro("step_values", [](nb::handle_t<StepResult> self) {
            auto& r = nb::cast<StepResult&>(self);
            return nb::ndarray<nb::numpy, double, nb::shape<nb::any>>(
                r.step_values.data(), {r.step_values.size()}, self);
        })
        .def_ro("step_variable", &StepResult::step_variable)
        .def_ro("results", &StepResult::results);

    // --- SimulationResult ---
    nb::class_<SimulationResult>(m, "SimulationResult")
        .def_prop_ro("analysis_type", [](const SimulationResult& self) -> nb::object {
            return std::visit([](auto&& arg) -> nb::object {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, std::monostate>) return nb::none();
                else if constexpr (std::is_same_v<T, DCResult>) return nb::str("dc");
                else if constexpr (std::is_same_v<T, TransientResult>) return nb::str("transient");
                else if constexpr (std::is_same_v<T, ACResult>) return nb::str("ac");
                else if constexpr (std::is_same_v<T, DCSweepResult>) return nb::str("dc_sweep");
                else if constexpr (std::is_same_v<T, NoiseResult>) return nb::str("noise");
                else if constexpr (std::is_same_v<T, TFResult>) return nb::str("tf");
                else if constexpr (std::is_same_v<T, SensResult>) return nb::str("sens");
                else if constexpr (std::is_same_v<T, PZResult>) return nb::str("pz");
                else return nb::none();
            }, self.analysis);
        })
        .def_prop_ro("dc", [](const SimulationResult& self) -> nb::object {
            auto* p = std::get_if<DCResult>(&self.analysis);
            return p ? nb::cast(*p) : nb::none();
        })
        .def_prop_ro("transient", [](const SimulationResult& self) -> nb::object {
            auto* p = std::get_if<TransientResult>(&self.analysis);
            return p ? nb::cast(*p) : nb::none();
        })
        .def_prop_ro("ac", [](const SimulationResult& self) -> nb::object {
            auto* p = std::get_if<ACResult>(&self.analysis);
            return p ? nb::cast(*p) : nb::none();
        })
        .def_prop_ro("dc_sweep", [](const SimulationResult& self) -> nb::object {
            auto* p = std::get_if<DCSweepResult>(&self.analysis);
            return p ? nb::cast(*p) : nb::none();
        })
        .def_prop_ro("noise", [](const SimulationResult& self) -> nb::object {
            auto* p = std::get_if<NoiseResult>(&self.analysis);
            return p ? nb::cast(*p) : nb::none();
        })
        .def_prop_ro("tf", [](const SimulationResult& self) -> nb::object {
            auto* p = std::get_if<TFResult>(&self.analysis);
            return p ? nb::cast(*p) : nb::none();
        })
        .def_prop_ro("sens", [](const SimulationResult& self) -> nb::object {
            auto* p = std::get_if<SensResult>(&self.analysis);
            return p ? nb::cast(*p) : nb::none();
        })
        .def_prop_ro("pz", [](const SimulationResult& self) -> nb::object {
            auto* p = std::get_if<PZResult>(&self.analysis);
            return p ? nb::cast(*p) : nb::none();
        })
        .def_prop_ro("measures", [](const SimulationResult& self) -> nb::object {
            return self.measures ? nb::cast(*self.measures) : nb::none();
        })
        .def_prop_ro("step", [](const SimulationResult& self) -> nb::object {
            return self.step ? nb::cast(*self.step) : nb::none();
        })
        .def_ro("print_output", &SimulationResult::print_output);
```

- [ ] **Step 4: Rebuild and run all tests**

Run:
```bash
cd . && pip install --no-build-isolation -e . && python -m pytest tests/python/test_bindings.py -v
```
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add python/bindings.cpp tests/python/test_bindings.py
git commit -m "feat(python): bind SimulationResult, MeasureResult, StepResult"
```

---

### Task 7: Python Convenience Layer

**Files:**
- Modify: `python/neospice/__init__.py`
- Modify: `tests/python/test_bindings.py`

- [ ] **Step 1: Write tests for convenience functions**

Append to `tests/python/test_bindings.py`:

```python
class TestConvenienceFunctions:
    def test_dc_from_file(self):
        path = os.path.join(CIRCUITS_DIR, "resistor_divider.cir")
        result = neospice.dc(path)
        assert isinstance(result, neospice.DCResult)
        assert abs(result.voltage("mid") - 5.0) < 0.01

    def test_dc_from_string(self):
        result = neospice.dc(
            "Divider\nV1 in 0 DC 10\nR1 in mid 1k\nR2 mid 0 1k\n.op\n.end\n"
        )
        assert isinstance(result, neospice.DCResult)
        assert abs(result.voltage("mid") - 5.0) < 0.01

    def test_ac(self):
        path = os.path.join(CIRCUITS_DIR, "rc_ac.cir")
        result = neospice.ac(path, mode="dec", npoints=10, fstart=100, fstop=1e9)
        assert isinstance(result, neospice.ACResult)
        assert len(result.frequency) > 5

    def test_ac_with_enum_mode(self):
        path = os.path.join(CIRCUITS_DIR, "rc_ac.cir")
        result = neospice.ac(
            path, mode=neospice.ACMode.DEC, npoints=10, fstart=100, fstop=1e9
        )
        assert isinstance(result, neospice.ACResult)

    def test_transient(self):
        path = os.path.join(CIRCUITS_DIR, "rc_lowpass.cir")
        result = neospice.transient(path, tstep=0.1e-6, tstop=50e-6)
        assert isinstance(result, neospice.TransientResult)
        assert len(result.time) > 10

    def test_noise(self):
        path = os.path.join(CIRCUITS_DIR, "rc_lowpass_noise.cir")
        result = neospice.noise(
            path, output="out", input_src="v1",
            mode="dec", npoints=10, fstart=100, fstop=10e6,
        )
        assert isinstance(result, neospice.NoiseResult)

    def test_dc_sweep(self):
        path = os.path.join(CIRCUITS_DIR, "diode_dc_sweep.cir")
        p = neospice.DCSweepParam()
        p.source_name = "v1"
        p.start = -1.0
        p.stop = 1.0
        p.step = 0.01
        result = neospice.dc_sweep(path, [p])
        assert isinstance(result, neospice.DCSweepResult)

    def test_tf(self):
        path = os.path.join(CIRCUITS_DIR, "tf_resistive_divider.cir")
        result = neospice.tf(path, output="v(out)", input_src="v1")
        assert isinstance(result, neospice.TFResult)

    def test_sens(self):
        path = os.path.join(CIRCUITS_DIR, "sens_divider.cir")
        result = neospice.sens(path, output="v(out)")
        assert isinstance(result, neospice.SensResult)

    def test_run(self):
        path = os.path.join(CIRCUITS_DIR, "resistor_divider.cir")
        result = neospice.run(path)
        assert isinstance(result, neospice.SimulationResult)
        assert result.analysis_type == "dc"

    def test_custom_options(self):
        path = os.path.join(CIRCUITS_DIR, "resistor_divider.cir")
        result = neospice.dc(path, reltol=1e-4)
        assert isinstance(result, neospice.DCResult)
```

- [ ] **Step 2: Run tests — expect failure**

Run:
```bash
cd . && python -m pytest tests/python/test_bindings.py::TestConvenienceFunctions::test_dc_from_file -v 2>&1 | tail -10
```
Expected: FAIL — `neospice.dc` not defined.

- [ ] **Step 3: Implement convenience layer**

Replace `python/neospice/__init__.py` with:

```python
from __future__ import annotations

import os
from typing import Any

from neospice._core import (  # noqa: F401
    ACMode,
    ACOptions,
    ACResult,
    Circuit,
    CircuitBuilder,
    ConvergenceMethod,
    DCResult,
    DCSweepParam,
    DCSweepResult,
    DeviceInfo,
    IntegrationMethod,
    MeasureResult,
    NoiseResult,
    PulseSpec,
    PZResult,
    SensEntry,
    SensResult,
    SimStatus,
    SimulationResult,
    Simulator,
    SimulatorOptions,
    SinSpec,
    SourceSpec,
    StepResult,
    TFResult,
    TransientOptions,
    TransientResult,
)

__version__ = "0.1.0"

_MODE_MAP = {"dec": ACMode.DEC, "oct": ACMode.OCT, "lin": ACMode.LIN}


def _resolve_mode(mode: str | ACMode) -> ACMode:
    if isinstance(mode, ACMode):
        return mode
    return _MODE_MAP[mode.lower()]


def _make_sim(**opts: Any) -> Simulator:
    if opts:
        so = SimulatorOptions()
        for k, v in opts.items():
            setattr(so, k, v)
        return Simulator(so)
    return Simulator()


def _load_or_parse(sim: Simulator, netlist: str) -> Circuit:
    if os.path.exists(netlist):
        return sim.load(netlist)
    return sim.parse(netlist)


def dc(netlist: str, **opts: Any) -> DCResult:
    sim = _make_sim(**opts)
    ckt = _load_or_parse(sim, netlist)
    return sim.run_dc(ckt)


def transient(netlist: str, *, tstep: float, tstop: float, **opts: Any) -> TransientResult:
    sim = _make_sim(**opts)
    ckt = _load_or_parse(sim, netlist)
    return sim.run_transient(ckt, tstep, tstop)


def ac(
    netlist: str,
    *,
    mode: str | ACMode = "dec",
    npoints: int = 100,
    fstart: float = 1.0,
    fstop: float = 1e9,
    **opts: Any,
) -> ACResult:
    sim = _make_sim(**opts)
    ckt = _load_or_parse(sim, netlist)
    return sim.run_ac(ckt, _resolve_mode(mode), npoints, fstart, fstop)


def noise(
    netlist: str,
    *,
    output: str,
    input_src: str,
    mode: str | ACMode = "dec",
    npoints: int = 100,
    fstart: float = 1.0,
    fstop: float = 1e9,
    **opts: Any,
) -> NoiseResult:
    sim = _make_sim(**opts)
    ckt = _load_or_parse(sim, netlist)
    return sim.run_noise(ckt, output, input_src, _resolve_mode(mode), npoints, fstart, fstop)


def dc_sweep(netlist: str, params: list[DCSweepParam], **opts: Any) -> DCSweepResult:
    sim = _make_sim(**opts)
    ckt = _load_or_parse(sim, netlist)
    return sim.run_dc_sweep(ckt, params)


def tf(netlist: str, *, output: str, input_src: str, **opts: Any) -> TFResult:
    sim = _make_sim(**opts)
    ckt = _load_or_parse(sim, netlist)
    return sim.run_tf(ckt, output, input_src)


def sens(netlist: str, *, output: str, **opts: Any) -> SensResult:
    sim = _make_sim(**opts)
    ckt = _load_or_parse(sim, netlist)
    return sim.run_sens(ckt, output)


def run(netlist: str, **opts: Any) -> SimulationResult:
    sim = _make_sim(**opts)
    ckt = _load_or_parse(sim, netlist)
    return sim.run(ckt)
```

- [ ] **Step 4: Rebuild and run all tests**

Run:
```bash
cd . && pip install --no-build-isolation -e . && python -m pytest tests/python/test_bindings.py -v
```
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add python/neospice/__init__.py tests/python/test_bindings.py
git commit -m "feat(python): add convenience functions (dc, ac, transient, noise, etc.)"
```

---

### Task 8: GitHub Actions CI — Wheels + PyPI

**Files:**
- Create: `.github/workflows/wheels.yml`

- [ ] **Step 1: Add cibuildwheel config to pyproject.toml**

Append to `pyproject.toml`:

```toml

[tool.cibuildwheel]
build = "cp310-* cp311-* cp312-* cp313-*"
skip = "*-musllinux_* *-win*"
test-requires = "pytest numpy"
test-command = "pytest {project}/tests/python -v"

[tool.cibuildwheel.linux]
before-all = [
    "yum install -y suitesparse-devel openblas-devel cmake",
    "git clone --depth 1 --branch 3.6 https://github.com/shibatch/sleef.git /tmp/sleef",
    "cmake -S /tmp/sleef -B /tmp/sleef/build -DCMAKE_INSTALL_PREFIX=/usr/local -DSLEEF_BUILD_TESTS=OFF -DSLEEF_BUILD_DFT=OFF",
    "cmake --build /tmp/sleef/build --target install",
]
manylinux-x86_64-image = "manylinux_2_28"
manylinux-aarch64-image = "manylinux_2_28"

[tool.cibuildwheel.macos]
before-all = "brew install suite-sparse openblas sleef"
```

- [ ] **Step 2: Create GitHub Actions workflow**

Create `.github/workflows/wheels.yml`:

```yaml
name: Build wheels and publish to PyPI

on:
  push:
    tags:
      - "v*"
  workflow_dispatch:

jobs:
  build_wheels:
    name: Build wheels on ${{ matrix.os }}
    runs-on: ${{ matrix.os }}
    strategy:
      fail-fast: false
      matrix:
        os: [ubuntu-latest, macos-13, macos-14]

    steps:
      - uses: actions/checkout@v4

      - uses: pypa/cibuildwheel@v2.21
        env:
          CIBW_ARCHS_LINUX: "x86_64 aarch64"
          CIBW_ARCHS_MACOS: "x86_64 arm64"

      - uses: actions/upload-artifact@v4
        with:
          name: wheels-${{ matrix.os }}
          path: ./wheelhouse/*.whl

  build_sdist:
    name: Build source distribution
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - run: pipx run build --sdist
      - uses: actions/upload-artifact@v4
        with:
          name: sdist
          path: dist/*.tar.gz

  publish:
    name: Publish to PyPI
    needs: [build_wheels, build_sdist]
    runs-on: ubuntu-latest
    if: startsWith(github.ref, 'refs/tags/v')
    permissions:
      id-token: write
    steps:
      - uses: actions/download-artifact@v4
        with:
          pattern: wheels-*
          merge-multiple: true
          path: dist/
      - uses: actions/download-artifact@v4
        with:
          name: sdist
          path: dist/
      - uses: pypa/gh-action-pypi-publish@release/v1
```

- [ ] **Step 3: Commit**

```bash
mkdir -p .github/workflows
git add pyproject.toml .github/workflows/wheels.yml
git commit -m "ci: add cibuildwheel workflow for PyPI publishing"
```

---

### Task 9: Final Integration Test and Cleanup

**Files:**
- Modify: `tests/python/test_bindings.py`

- [ ] **Step 1: Add end-to-end integration test matching the spec's end-user example**

Append to `tests/python/test_bindings.py`:

```python
class TestEndToEnd:
    def test_spec_example_circuit_builder_ac(self):
        """End-to-end test matching spec Section 8 example."""
        ckt = (neospice.CircuitBuilder()
            .title("Low-pass RC")
            .vsource("V1", "in", "0", neospice.SourceSpec())
            .resistor("R1", "in", "out", 1e3)
            .capacitor("C1", "out", "0", 1e-9)
            .build())

        sim = neospice.Simulator()
        spec = neospice.SourceSpec()
        spec.ac_mag = 1.0
        ckt_ac = (neospice.CircuitBuilder()
            .title("Low-pass RC")
            .vsource("V1", "in", "0", spec)
            .resistor("R1", "in", "out", 1e3)
            .capacitor("C1", "out", "0", 1e-9)
            .raw_line(".ac dec 100 1 1e9")
            .build())

        result = sim.run_ac(ckt_ac, neospice.ACMode.DEC, 100, 1, 1e9)
        assert isinstance(result.frequency, np.ndarray)
        db = result.magnitude_db("out")
        assert isinstance(db, np.ndarray)
        # RC lowpass: -3dB at f = 1/(2*pi*R*C) ≈ 159kHz
        # At 1 Hz gain should be ~0 dB, at 1 GHz should be very negative
        assert abs(db[0]) < 1.0   # near 0 dB at low freq
        assert db[-1] < -40       # well below 0 dB at high freq

    def test_version(self):
        assert hasattr(neospice, "__version__")
        assert neospice.__version__ == "0.1.0"
```

- [ ] **Step 2: Run the full test suite**

Run:
```bash
cd . && pip install --no-build-isolation -e . && python -m pytest tests/python/test_bindings.py -v
```
Expected: all pass.

- [ ] **Step 3: Verify existing C++ tests still pass**

Run:
```bash
cd ./build && cmake .. -DNEOSPICE_BUILD_PYTHON=OFF && cmake --build . -j$(nproc) && ctest -j$(nproc) --output-on-failure 2>&1 | tail -5
```
Expected: all C++ tests pass.

- [ ] **Step 4: Commit**

```bash
git add tests/python/test_bindings.py
git commit -m "test(python): add end-to-end integration tests"
```

- [ ] **Step 5: Final verification — clean pip install from source**

Run:
```bash
cd /tmp && python -m venv test_neospice && source test_neospice/bin/activate && pip install . && python -c "
import neospice
print('Version:', neospice.__version__)
r = neospice.dc('Div\nV1 a 0 DC 10\nR1 a b 1k\nR2 b 0 1k\n.op\n.end\n')
print('v(b):', r.voltage('b'))
print('OK')
" && deactivate && rm -rf /tmp/test_neospice
```
Expected: prints `Version: 0.1.0`, `v(b): 5.0`, `OK`.
