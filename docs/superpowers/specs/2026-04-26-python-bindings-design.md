# Python Bindings Design — neospice on PyPI

## Summary

Expose the neospice C++ SPICE simulator to Python via nanobind, published as
`neospice` on PyPI with pre-built wheels for Linux and macOS.

**Decisions:**
- Package name: `neospice` (`pip install neospice`, `import neospice`)
- Binding tech: nanobind (smaller binaries, faster compile, native NumPy zero-copy)
- Build backend: scikit-build-core (CMake integration, pyproject.toml-native)
- Wheel builder: cibuildwheel (GitHub Actions)
- Platforms: Linux (x86_64, aarch64) + macOS (x86_64, arm64). Windows deferred.
- Python: 3.10+
- NumPy: required dependency
- API style: convenience module-level functions + full Simulator/CircuitBuilder API

---

## 1. Project Layout

New files alongside the existing C++ project:

```
spice-cpp/
├── pyproject.toml                  # Package metadata + scikit-build-core config
├── python/
│   ├── neospice/
│   │   ├── __init__.py             # Convenience API + re-exports from _core
│   │   ├── _core.pyi              # Type stubs for C++ extension (auto-generated)
│   │   └── py.typed               # PEP 561 marker
│   └── bindings.cpp               # nanobind module definition
├── tests/
│   └── python/                    # pytest tests for the Python package
├── .github/
│   └── workflows/
│       └── wheels.yml             # cibuildwheel CI
```

Existing files are unchanged except for conditional PIC flags (see Section 2).
The C++ library target `neospice_lib` is reused — linked into the Python extension.

---

## 2. Build Infrastructure

### CMake Integration

Top-level `CMakeLists.txt` gets a conditional block:

```cmake
option(NEOSPICE_BUILD_PYTHON "Build Python bindings" OFF)
if(NEOSPICE_BUILD_PYTHON)
    # Python extension is a shared library — all linked code must be PIC.
    # Enable PIC on neospice_lib and all device OBJECT libraries.
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

`python/CMakeLists.txt` (paths relative to this subdirectory):

```cmake
nanobind_add_module(_core bindings.cpp)
target_link_libraries(_core PRIVATE neospice_lib)
install(TARGETS _core DESTINATION neospice)
```

Normal C++ builds (`cmake .. && make`) are unaffected — `NEOSPICE_BUILD_PYTHON`
defaults to OFF, and no PIC overhead is added.

### pyproject.toml

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

[tool.scikit-build]
cmake.args = ["-DNEOSPICE_BUILD_PYTHON=ON", "-DNEOSPICE_BUILD_TESTS=OFF"]
wheel.packages = ["python/neospice"]
```

---

## 3. C++ Binding Layer (`python/bindings.cpp`)

Single file wrapping the public API via nanobind. Seven binding groups:

### 3a. Circuit (move-only opaque type)

`Circuit` is move-only (deleted copy ctor/assignment). Bound as a nanobind class with
move semantics. Users obtain `Circuit` objects from `Simulator.load()`,
`Simulator.parse()`, or `CircuitBuilder.build()` and pass them to `run_*` methods.

```cpp
nb::class_<Circuit>(m, "Circuit")
    .def_ro("title", &Circuit::title)
    .def("node_names", &Circuit::node_names)
    .def("device_names", &Circuit::device_names)
    .def("set_param", &Circuit::set_param);
```

Introspection accessors (`node_names`, `device_names`, `device_info`, `set_param`) are
exposed. Internal mutation methods (`finalize`, `rotate_state`, `add_device`) are not —
circuits are constructed via `Simulator.parse/load` or `CircuitBuilder`.

### 3b. Simulator

```cpp
nb::class_<Simulator>(m, "Simulator")
    .def(nb::init<>())
    .def(nb::init<SimulatorOptions>())
    .def("load", &Simulator::load)
    .def("parse", &Simulator::parse)
    .def("run_dc", &Simulator::run_dc)
    .def("run_transient", ...)     // both overloads via nb::overload_cast
    .def("run_ac", ...)            // both overloads
    .def("run_noise", &Simulator::run_noise)
    .def("run_dc_sweep", &Simulator::run_dc_sweep)
    .def("run_tf", &Simulator::run_tf)
    .def("run_sens", &Simulator::run_sens)
    .def("run", &Simulator::run)
    .def("run_step_sweep", &Simulator::run_step_sweep);
```

### 3c. CircuitBuilder

All methods return `nb::rv_policy::reference` to preserve fluent chaining in Python:

```python
ckt = (neospice.CircuitBuilder()
    .title("RC filter")
    .vsource("V1", "in", "0", neospice.SourceSpec(dc=1.0, ac_mag=1.0))
    .resistor("R1", "in", "out", 1e3)
    .capacitor("C1", "out", "0", 1e-9)
    .build())
```

### 3d. Result Types — NumPy Arrays

Two distinct strategies depending on data ownership:

**Borrowed views (zero-copy)** — for data stored directly in the result struct:
Vector fields (`time`, `frequency`, `sweep_values`) and map lookups (`.voltage(node)`,
`.current(dev)`) return `nb::ndarray` views into the C++ `std::vector` memory. The
ndarray holds a reference to the owning Python result object via `nb::handle(self)`,
preventing the result from being garbage-collected while the array is alive.

```cpp
.def_prop_ro("time", [](nb::handle_t<TransientResult> self) {
    auto& r = nb::cast<TransientResult&>(self);
    return nb::ndarray<nb::numpy, double, nb::shape<nb::any>>(
        r.time.data(), {r.time.size()}, self);
})
```

**Owned copies (heap-allocated)** — for computed/derived data:
Methods that return new `std::vector` values (`magnitude_db()`, `phase_deg()`,
`diff()`, etc.) allocate a new NumPy array and copy the data. These are Python-owned
and need no special lifetime management. Implemented via lambda wrappers that call the
C++ method, allocate an ndarray, and `memcpy` the result.

AC complex data maps to `numpy.complex128` arrays (same two strategies — borrowed for
stored, copied for computed).

`signal_names()` returns `list[str]`.

`status` field exposed as a read-only `SimStatus` object with fields: `converged`,
`iterations`, `convergence_method`, `warnings`, `elapsed_seconds`.

### 3e. SimulationResult (variant wrapper)

`SimulationResult` contains `std::variant<monostate, DCResult, ...>`,
`std::optional<MeasureResult>`, and `std::unique_ptr<StepResult>`. These don't map
directly to nanobind. Expose via Python-friendly accessor properties:

```python
result = sim.run(ckt)
result.analysis_type   # str: "dc", "transient", "ac", etc. or None
result.dc              # DCResult or None (raises AttributeError via __getattr__)
result.transient       # TransientResult or None
result.ac              # ACResult or None
# ... one property per variant alternative

result.measures        # MeasureResult or None (from std::optional)
result.step            # StepResult or None (from std::unique_ptr)
```

Implemented via lambda getters that `std::get_if` the variant and return `None` or the
typed result. `StepResult` is bound with `results` as a `list[SimulationResult]` and
`step_values` as an ndarray.

### 3f. Spec/Options Structs

All get keyword-argument constructors via `nb::init()` with named args:

- `SimulatorOptions(abstol=, reltol=, vntol=, trtol=, gmin=)`
- `SourceSpec(dc=, ac_mag=, ac_phase=)`
- `PulseSpec(v1=, v2=, td=, tr=, tf=, pw=, per=)`
- `SinSpec(vo=, va=, freq=, td=, theta=, phase=)`
- `TransientOptions(uic=)`
- `ACOptions()`
- `DCSweepParam(source_name=, start=, stop=, step=)`

### 3g. Enums

`ACMode` and `IntegrationMethod` become Python enums:

```python
neospice.ACMode.DEC
neospice.ACMode.LIN
neospice.ACMode.OCT
```

### 3h. Exception Mapping

Scoped to specific accessors, not registered globally (to avoid misclassifying
unrelated bounds errors):

- `.voltage()` / `.current()` / `.diff()` on all result types: wrap in a try/catch
  that translates `std::out_of_range` → Python `KeyError` with the signal name
- All other C++ exceptions: nanobind's default mapping (`std::runtime_error` →
  `RuntimeError`, etc.)

---

## 4. Python Convenience Layer (`python/neospice/__init__.py`)

Module-level functions that create a Simulator internally and run a single analysis:

```python
neospice.dc(netlist, **opts) -> DCResult
neospice.ac(netlist, *, mode, npoints, fstart, fstop, **opts) -> ACResult
neospice.transient(netlist, *, tstep, tstop, **opts) -> TransientResult
neospice.noise(netlist, *, output, input_src, mode, npoints, fstart, fstop, **opts) -> NoiseResult
neospice.dc_sweep(netlist, params, **opts) -> DCSweepResult
neospice.tf(netlist, *, output, input_src, **opts) -> TFResult
neospice.sens(netlist, *, output, **opts) -> SensResult
neospice.run(netlist, **opts) -> SimulationResult
```

Each function:
1. Creates `Simulator(SimulatorOptions(**opts))` if opts provided, else default
2. Detects file vs string: `os.path.exists(netlist)` → `load()`, else → `parse()`
3. Calls the appropriate `run_*` method
4. Returns the typed result

The `mode` parameter in `ac()` and `noise()` accepts both strings (`"dec"`, `"lin"`,
`"oct"`) and `ACMode` enum values.

All C++ types are re-exported so `from neospice import CircuitBuilder` works.

**Not included (YAGNI):**
- No `.plot()` methods — users call matplotlib directly
- No DataFrame export — trivial with `pd.DataFrame()`
- No async API
- No config file loading

---

## 5. Wheel Building & PyPI Publishing

### Build Matrix

| Platform | Arch | Pythons | Wheel tag |
|----------|------|---------|-----------|
| Linux | x86_64 | 3.10–3.13 | manylinux_2_28 |
| Linux | aarch64 | 3.10–3.13 | manylinux_2_28 |
| macOS | x86_64 | 3.10–3.13 | macosx_11_0 |
| macOS | arm64 | 3.10–3.13 | macosx_14_0 |

16 wheels per release.

### Dependency Bundling

SuiteSparse (KLU, AMD, COLAMD, BTF), OpenBLAS, and SLEEF are linked into `neospice_lib`
and bundled into wheels:

- **Linux:** Build in manylinux_2_28 container. Install deps via yum/dnf where
  available; build SLEEF from source (not packaged in manylinux). `auditwheel repair`
  copies .so files into wheel and patches RPATH.
- **macOS:** Install via Homebrew. `delocate` bundles .dylib files into wheel.

Users do not need any C libraries installed.

### CMake SLEEF path generalization

The current top-level CMake hardcodes SLEEF paths to `/usr/include/x86_64-linux-gnu`
and `/usr/lib/x86_64-linux-gnu`. This breaks on aarch64 and macOS. Generalize to:

```cmake
find_path(SLEEF_INCLUDE_DIR sleef.h)
find_library(SLEEF_LIBRARY NAMES sleef)
```

Drop the `PATHS` constraints and let CMake search standard system locations. This is a
prerequisite for cross-platform wheel builds.

### cibuildwheel Config

```toml
[tool.cibuildwheel]
build = "cp310-* cp311-* cp312-* cp313-*"
skip = "*-musllinux_* *-win*"

[tool.cibuildwheel.linux]
before-all = """
    yum install -y suitesparse-devel openblas-devel cmake &&
    git clone --depth 1 --branch 3.6 https://github.com/shibatch/sleef.git /tmp/sleef &&
    cmake -S /tmp/sleef -B /tmp/sleef/build -DCMAKE_INSTALL_PREFIX=/usr/local \
        -DSLEEF_BUILD_TESTS=OFF -DSLEEF_BUILD_DFT=OFF &&
    cmake --build /tmp/sleef/build --target install
"""
manylinux-x86_64-image = "manylinux_2_28"
manylinux-aarch64-image = "manylinux_2_28"

[tool.cibuildwheel.macos]
before-all = "brew install suite-sparse openblas sleef"
```

### GitHub Actions Workflow

Triggered on git tags `v*`:

1. **build_wheels** job: matrix over `[ubuntu-latest, macos-13, macos-14]`, runs
   cibuildwheel, uploads artifacts
2. **publish** job: downloads all wheel artifacts, publishes to PyPI via trusted
   publisher (OIDC — no API token)

### Versioning

Single source of truth: `version` field in `pyproject.toml`. Manual bump before
tagging. No dynamic versioning.

### PyPI Setup

One-time: configure the GitHub repo as a trusted publisher on pypi.org for the
`neospice` package name.

---

## 6. Type Stubs & Developer Experience

### Stub Generation

nanobind's `stubgen` auto-generates `_core.pyi` from the compiled module. Run as a
post-build step, output checked into the repo so IDEs work without building.

### py.typed Marker

Empty `python/neospice/py.typed` file signals PEP 561 compliance. mypy and pyright
pick up types automatically.

### Convenience Layer Types

`__init__.py` has full inline type annotations — no separate stubs needed for the
pure Python layer.

---

## 7. Testing

### Python Test Suite (`tests/python/`)

pytest tests run against the installed wheel:

- **Smoke:** `neospice.dc("resistor.cir")` returns `DCResult`
- **NumPy:** result arrays have correct dtype (`float64`, `complex128`) and shape
- **CircuitBuilder:** build → simulate → verify known result (e.g., voltage divider)
- **Error handling:** bad netlist → `RuntimeError`, bad node name → `KeyError`
- **Convenience parity:** module-level functions match `Simulator` method results
- **All analyses:** at least one test per analysis type (DC, AC, transient, noise,
  DC sweep, TF, sensitivity)

Tests run in CI after wheel build, on a clean virtualenv with only the wheel installed.

---

## 8. End-User Experience

After `pip install neospice`:

```python
import neospice
import numpy as np

# One-liner simulation
result = neospice.ac("filter.cir", mode="dec", npoints=100, fstart=1, fstop=1e9)
print(result.frequency)          # numpy array
print(result.magnitude_db("out"))  # numpy array

# Programmatic circuit construction
ckt = (neospice.CircuitBuilder()
    .title("Low-pass RC")
    .vsource("V1", "in", "0", neospice.SourceSpec(dc=0, ac_mag=1.0))
    .resistor("R1", "in", "out", 1e3)
    .capacitor("C1", "out", "0", 1e-9)
    .build())

sim = neospice.Simulator()
ac = sim.run_ac(ckt, neospice.ACMode.DEC, 100, 1, 1e9)

import matplotlib.pyplot as plt
plt.semilogx(ac.frequency, ac.magnitude_db("out"))
plt.xlabel("Frequency (Hz)")
plt.ylabel("Gain (dB)")
plt.show()
```
