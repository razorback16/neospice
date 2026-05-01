# neospice Roadmap

## Current State

neospice is a modern C++ SPICE simulator with 32 device models and 10 analysis types,
validated against ngspice across 970+ C++ tests with tolerances as tight as 1e-6,
plus Python binding tests.

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
| JFET/MESFET/HFET | JFET, JFET2, MES, HFET1, HFET2 |
| MOSFET | MOS1, MOS3, MOS9, BSIM3v32, BSIM3, BSIM4v7, BSIMSOI, HiSIM2, HiSIM_HV |

### Netlist Features
`.param` expressions, `.subckt`/`.ends`, `.include`/`.lib`, `.global`, `.ic`, `.nodeset`,
`.options`, `.func`, `.measure`, `.save`, `.step`, SPICE suffixes (k/m/u/n/p/f/T).

### What sets neospice apart
- **Performance**: 1.5–6x faster than ngspice in-process; zero subprocess overhead as a library
- **Embeddable C++ API**: handle-based `Simulator`/`Circuit`/`Result` interface with typed device methods and O(1) result access
- **Auto-differentiation**: B-source expressions get exact Jacobians (no numerical perturbation)
- **Modern codebase**: C++20, modular DeviceRegistry factory pattern (add a device without touching central files), auto-migration tooling for ngspice models
- **Python bindings**: `pip install neospice` — full API with NumPy arrays, typed circuit construction, SPICE notation parser
- **ngspice-compatible output**: raw file format matches ngspice for drop-in tool compatibility

---

## Phase 1: Python Bindings — Done

Exposes the full neospice API to Python via nanobind + scikit-build-core.

### What shipped
- **nanobind C++ bindings** (`python/bindings.cpp`): all enums, options structs, Circuit (with typed device methods), Simulator, and every result type (DC, transient, AC, noise, DC sweep, TF, sensitivity, PZ, SimulationResult, MeasureResult, StepResult)
- **NumPy integration**: all vector results (time, frequency, voltage, current, noise density) returned as `numpy.ndarray`
- **Convenience API** (`python/neospice/__init__.py`): `dc()`, `ac()`, `transient()`, `noise()`, `dc_sweep()`, `tf()`, `sens()`, `run()` — one-liner functions that accept a file path or netlist string
- **Typed Circuit construction**: `ckt.R()`, `ckt.C()`, `ckt.L()`, `ckt.V()`, `ckt.I()`, `ckt.E()`, `ckt.G()` with string node names (auto-converted to handles internally)
- **SPICE notation parser**: `parse_value("4.7k")` → `4700.0`
- **Circuit introspection**: `node_names()`, `device_names()`, `device_info()`, `devices_at_node()`
- **CI/CD** (`.github/workflows/wheels.yml`): cibuildwheel building for Python 3.10–3.13, Linux (x86_64 + aarch64 via QEMU), macOS (x86_64 + arm64), with automatic PyPI publishing on tag push via trusted publishing
- **Python tests** (`tests/python/`, `python/tests/`): enums, options, source specs, load/parse, typed methods, all result types, convenience functions, SPICE notation parsing
- **py.typed** marker for PEP 561 type checker support

### Usage
```python
import neospice as ns

# Convenience: one-liner from file or inline netlist
dc = ns.dc("amplifier.cir")
ac = ns.ac("filter.cir", mode="dec", npoints=100, fstart=1, fstop=1e9)

# Full API
sim = ns.Simulator()
ckt = sim.load("amplifier.cir")
result = sim.run_ac(ckt, ns.ACMode.DEC, 100, 1, 1e9)
gain_db = result.magnitude_db("out")   # numpy.ndarray

# Programmatic circuit building with typed methods
ckt = ns.Circuit()
ckt.V("V1", "in", "0", 0.0, 1.0)      # DC=0, AC=1
ckt.R("R1", "in", "out", 1e3)
ckt.C("C1", "out", "0", 100e-12)

# SPICE engineering notation
from neospice import parse_value
r = parse_value("4.7k")                # 4700.0

# Custom simulator options
result = ns.dc("amp.cir", reltol=1e-4, gmin=1e-14)
```

---

## Phase 2: Parallel Parameter Sweeps

**Priority: High**

The architecture is nearly ready — `Circuit` is value-typed and the solver is
stateless. Running N simulations on N threads enables Monte Carlo yield analysis
at (single-thread speedup) x (core count).

### Goals
- Thread-safe simulation: multiple `Simulator` instances on independent threads
- Built-in Monte Carlo engine: Gaussian/uniform parameter variation with correlation
- Corner analysis: systematic process/voltage/temperature (PVT) sweeps
- Results aggregation: statistical summaries (yield, sigma, histograms)

### Deliverables
- `Simulator::run_sweep()` API with thread pool
- Python integration: `neospice.monte_carlo("circuit.cir", params, n=10000)`
- Automatic detection of optimal thread count

---

## Phase 3: WebAssembly (WASM) Build

**Priority: Medium**

Compile neospice to WebAssembly for browser-based circuit simulation with no
install. The core is pure C++ with no OS dependencies — Emscripten-friendly.

### Goals
- `neospice.wasm` + JS wrapper for browser use
- Sub-second simulation of moderate circuits (50–200 nodes) in the browser
- Integration-ready for web-based EDA tools and educational platforms

### Deliverables
- Emscripten build target in CMake
- JavaScript/TypeScript API wrapper
- Demo web page with interactive circuit editor

---

## Phase 4: Sensitivity & Gradient Computation

**Priority: Medium**

Adjoint sensitivity analysis computes dOutput/dParam for all parameters in a
single extra solve. Essential for optimization and currently absent from ngspice.

### Goals
- Adjoint method for DC and AC sensitivity
- Efficient gradient of any output w.r.t. all component values in O(1) extra solves
- Integration with Python optimization frameworks (SciPy, Optuna, PyTorch)

### Deliverables
- `Simulator::sensitivity()` API returning Jacobian matrices
- Automatic differentiation through the device evaluation chain
- Gradient-based circuit optimization examples

---

## Phase 5: Incremental Re-simulation

**Priority: Medium**

When a component value changes, avoid full re-solve. The NeoSolver refactorize path
already supports partial matrix updates — exploit this for interactive workflows.

### Goals
- Symbolic factorization cached across runs; only numeric refactorize on value change
- Hot-path for single-parameter tweaks: update affected matrix entries, refactorize, solve
- Target: <1ms for re-simulation of a 100-node circuit after a single component change

### Deliverables
- `Circuit::update_param()` + `Simulator::re_solve()` API
- Incremental AC sweep (re-solve only at frequencies where the change is significant)
- GUI integration example (slider-driven component tuning)

---

## Phase 6: GPU-Accelerated Simulation

**Priority: Lower**

For very large circuits (10k+ nodes) or massively parallel sweeps, offload
matrix operations to GPU.

### Goals
- CUDA-accelerated sparse matrix factorization and solve
- Batch simulation: thousands of parameter variations in a single GPU launch
- Device evaluation on GPU (BSIM4v7 is embarrassingly parallel across instances)

### Deliverables
- Optional CUDA build target
- Automatic CPU/GPU selection based on circuit size
- Benchmark suite demonstrating crossover point

---

## Phase 7: Extended Device & Analysis Support

**Priority: Ongoing**

### Devices — Remaining Gaps
- BSIM-CMG (FinFET) model — next-gen compact model, industry demand
- Verilog-A device model compilation (long-term — enables user-defined models)
- Priority 3 legacy devices (MOS2, MOS6, etc.) — low demand, available via migration tool

### Devices — Completed
MOS1, MOS3, MOS9, BSIM3v32, BSIM3, BSIM4v7, BSIMSOI, HiSIM2, HiSIM_HV, BJT,
VBIC, JFET, JFET2, MES, HFET1, HFET2, Diode, LTRA, ASRC, and all passives/sources/switches.

### Analyses — Remaining Gaps
- Distortion analysis (`.disto`)

### Netlist Features — Remaining Gaps
- XSPICE digital/mixed-signal code models
- Full `.param` function library (most common functions done)

---

## API Vision

The API evolves through layers — each builds on the previous. The current API
is netlist-in, struct-out. The target is a fully programmatic, composable,
streaming-capable simulation engine.

### Typed Result Access (done)

All result types provide both string-based and handle-based access. String
accessors work with any circuit; handle-based access provides O(1) dense array
lookup via `NodeId`/`DevId`. DC, transient, AC, DC sweep, and noise results
have `.voltage()`, `.current()` helpers. AC adds `.magnitude_db()`,
`.phase_deg()`, `.magnitude()`, `.diff()`, `.diff_magnitude_db()`, and
current-based variants. Measurement free functions in `neospice::measure`
namespace provide `bandwidth_3db`, `rise_time`, `settling_time`, `overshoot`,
`rms`, `phase_margin`, `gain_margin`, `spot_noise`.

```cpp
// String-based access (works with any circuit):
auto ac = sim.run_ac(ckt, ACMode::DEC, 10, 1, 100e6);
auto gain_db = ac.magnitude_db("out");       // vector<double>
auto phase   = ac.phase_deg("out");          // vector<double>
auto vdiff   = ac.diff("out_p", "out_n");    // vector<complex>
double vout  = dc.voltage("out");            // scalar
double ibias = dc.current("v1");             // scalar

// Handle-based access (O(1) dense array lookup):
NodeId out_id = ckt.find_node("out");
auto gain_h   = ac.magnitude_db(out_id);     // vector<double>
double vout_h = dc.voltage(out_id);          // scalar

// Measurement utilities:
double bw = measure::bandwidth_3db(ac, out_id);
double rt = measure::rise_time(tran, out_id, 0.5, 4.5);

// SimStatus with convergence diagnostics:
auto status = dc.status;                     // converged, iterations, residual, worst_node
```

### Programmatic Circuit Construction (done)

Build circuits in code with typed device methods. Each method returns a `DevId`
handle for the created device.

```cpp
using namespace neospice;

Circuit ckt;
auto in  = ckt.node("in");
auto out = ckt.node("out");

auto v1 = ckt.V("V1", in, GND, 0.0, 1.0);    // DC=0, AC=1
ckt.R("R1", in, out, 1e3);
ckt.C("C1", out, GND, 100e-12);
ckt.E("E1", out2, gnd, in, gnd, 2.0);         // VCVS gain=2
ckt.F("F1", np, nn, v1, 0.5);                 // CCCS
```

### Live Parameter Mutation

Change component values without re-parsing. Triggers NeoSolver refactorize, not full
symbolic re-analysis.

```cpp
auto ckt = sim.load("amp.cir");
auto baseline = sim.run_ac(ckt, DEC, 10, 1, 100e6);

ckt.set_param("r1", 2.2e3);
ckt.set_param("c1", 47e-12);
auto tweaked = sim.run_ac(ckt, DEC, 10, 1, 100e6);
```

### Streaming / Callback Results

For long transient simulations — get data as it's produced, not all at the end.

```cpp
sim.run_transient(ckt, 1e-9, 1e-3, {
    .signals = {"v(out)", "i(v1)"},
    .on_step = [](double t, std::span<const double> values) {
        // stream to file, update plot, etc.
    },
    .stop_when = [](double t, std::span<const double> values) {
        return values[0] > 3.3;  // early termination
    }
});
```

### Circuit Introspection (done)

Query topology and connectivity for validation, visualization, and automation.
Both string-based and handle-based introspection.

```cpp
auto nodes   = ckt.node_names();              // {"in", "out", "vcc", ...}
auto devices = ckt.device_names();            // {"r1", "c1", "x1.q1", ...}
auto info    = ckt.device_info("r1");         // {type, nodes, value}
auto conn    = ckt.devices_at_node("out");    // {"r1", "c1", "x1.ehf"}

// Handle-based introspection
NodeId nid   = ckt.find_node("out");
DevId  did   = ckt.find_device("R1");
auto   name  = ckt.name(nid);                // "out"
auto   dinfo = ckt.device_info(did);         // DeviceInfo struct
```

### Analysis Chaining

Use one analysis result as input to the next — the way analog designers
actually work.

```cpp
auto dc   = sim.run_dc(ckt);
auto tran = sim.run_transient(ckt, 1e-9, 1e-6, {.ic_from = dc});

ckt.set_param("vin", 2.5);
auto dc2 = sim.run_dc(ckt);
auto ac  = sim.run_ac(ckt, DEC, 10, 1, 1e9, {.op_from = dc2});
```

### Batch / Sweep API

First-class support for parameter sweeps and Monte Carlo analysis.

```cpp
auto sweep = sim.sweep(ckt, {
    .param = "r1",
    .values = linspace(100, 10e3, 50),
    .analysis = ACSweep{DEC, 10, 1, 100e6},
    .parallel = true
});

auto mc = sim.monte_carlo(ckt, {
    .variations = {{"r1", Gaussian{1e3, 0.05}},
                   {"c1", Gaussian{100e-12, 0.10}}},
    .n_runs = 10000,
    .analysis = Transient{1e-9, 1e-6},
    .measure = [](const TransientResult& r) {
        return r.voltage("out").back();
    }
});
// mc.mean(), mc.sigma(), mc.yield(spec_min, spec_max)
```

### Python Mirror (implemented)

The C++ API is exposed 1:1 to Python via nanobind, with NumPy arrays for all
vector results. Convenience functions provide one-liner access to every analysis.
Circuit construction uses typed methods with string node names.

```python
import neospice as ns

# Convenience one-liners (file path or inline netlist)
dc = ns.dc("amplifier.cir")
ac = ns.ac("filter.cir", mode="dec", npoints=100, fstart=1, fstop=1e9)
tran = ns.transient("osc.cir", tstep=1e-9, tstop=1e-6)

# Full API with Simulator + Circuit objects
sim = ns.Simulator()
ckt = sim.load("amp.cir")
result = sim.run_ac(ckt, ns.ACMode.DEC, 10, 1, 100e6)
plt.semilogx(result.frequency, result.magnitude_db("out"))

# Programmatic circuit building with typed methods
ckt = ns.Circuit()
ckt.V("V1", "in", "0", 0.0, 1.0)
ckt.R("R1", "in", "out", 1e3)
ckt.C("C1", "out", "0", 100e-12)

# SPICE engineering notation
from neospice import parse_value
r = parse_value("4.7k")    # 4700.0
```

### API Implementation Priority

1. ~~Typed result access~~ — **Done** (string + handle-based access on all result types)
2. **`set_param()` on Circuit** — unlocks sweeps, optimization, interactive use (basic version done)
3. ~~Programmatic circuit construction~~ — **Done** (typed device methods replace CircuitBuilder)
4. **Streaming transient** — essential for long simulations and real-time use
5. **Batch/sweep API** — builds on set_param + threading

---

## Summary

| Phase | Feature                    | Impact          | Effort   | Status |
|-------|----------------------------|-----------------|----------|--------|
| —     | Handle types (NodeId/DevId/ModelId) | Type safety | Low  | Done |
| —     | Typed result access (string + handle) | Usability | Low  | Done |
| —     | Dense array result storage | Performance     | Medium   | Done |
| —     | SimStatus error model + SimulationError | Reliability | Low | Done |
| —     | Measurement utilities (8 functions) | Usability | Medium | Done |
| —     | Typed device methods (R/C/L/V/I/E/G/F/H/K) | Usability | Medium | Done |
| —     | Circuit state machine      | Safety          | Low      | Done |
| —     | Handle-based introspection | Usability       | Medium   | Done |
| —     | Noise per-device accessor  | Usability       | Low      | Done |
| —     | Analysis chaining          | Usability       | Low      | Done |
| —     | Circuit introspection      | Usability       | Medium   | Done |
| —     | Generic set_param()        | Optimization    | Medium   | Done |
| 1     | Python bindings            | Adoption        | Medium   | Done |
| 2     | WASM build                 | Accessibility   | Low      | Planned |
| 3     | Sensitivity/gradients      | Optimization    | High     | Planned |
| 4     | Parallel sweeps            | Performance     | Medium   | Planned |
| 5     | Incremental re-simulation  | Interactivity   | Medium   | Planned |
| 6     | GPU acceleration           | Large circuits  | High     | Planned |
| 7     | Extended devices/analyses  | Completeness    | Ongoing  | Active  |
