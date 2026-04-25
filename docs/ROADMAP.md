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

---

## Phase 1: Python Bindings

**Priority: Highest**

Expose the neospice API to Python via pybind11. The analog/mixed-signal design
community increasingly works in Python (Jupyter, optimization loops, ML-driven
design). ngspice's Python story (`ngspice_shared` with C callbacks) is painful.

### Goals
- `pip install neospice` with wheels for Linux, macOS, Windows
- `result = neospice.ac("circuit.cir")` returning NumPy arrays directly
- Zero-copy where possible (frequency/time vectors, voltage/current matrices)
- Circuit construction API: build netlists programmatically without string manipulation

### Deliverables
- `python/` directory with pybind11 module
- Typed stubs (`.pyi`) for IDE support
- PyPI package with manylinux/macOS/Windows wheels
- Jupyter notebook examples

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

When a component value changes, avoid full re-solve. The KLU refactorize path
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
VBIC, JFET, JFET2, HFET1, HFET2, Diode, LTRA, ASRC, and all passives/sources/switches.

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

### Programmatic Circuit Construction

Build circuits in code without string formatting or netlist files.

```cpp
auto ckt = neospice::CircuitBuilder()
    .title("my amp")
    .resistor("r1", "in", "out", 1e3)
    .capacitor("c1", "out", "gnd", 100e-12)
    .vsource("vin", "in", "gnd", {.dc = 1.0, .ac = 1.0})
    .subcircuit("x1", "THS4131", {"inp", "inn", "vcc", "vee", "outp", "outn", "vocm"})
    .include("ths4131.lib")
    .build();
```

### Live Parameter Mutation

Change component values without re-parsing. Triggers KLU refactorize, not full
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

### Circuit Introspection

Query topology and connectivity for validation, visualization, and automation.

```cpp
auto nodes   = ckt.node_names();              // {"in", "out", "vcc", ...}
auto devices = ckt.device_names();            // {"r1", "c1", "x1.q1", ...}
auto info    = ckt.device_info("r1");         // {type, nodes, value}
auto conn    = ckt.devices_at_node("out");    // {"r1", "c1", "x1.ehf"}
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

### Python Mirror

Every C++ API maps 1:1 to idiomatic Python with NumPy arrays.

```python
import neospice as ns

ckt = ns.load("amp.cir")
ac = ns.run_ac(ckt, "dec", 10, 1, 100e6)

plt.semilogx(ac.frequency, ac.magnitude_db("out"))

results = ns.sweep(ckt, param="r1", values=np.linspace(100, 10e3, 50),
                   analysis="ac dec 10 1 100e6", parallel=True)
```

### API Implementation Priority

1. **Typed result access** — small effort, immediate usability win
2. **`set_param()` on Circuit** — unlocks sweeps, optimization, interactive use
3. **CircuitBuilder** — enables programmatic circuit construction
4. **Streaming transient** — essential for long simulations and real-time use
5. **Batch/sweep API** — builds on set_param + threading

---

## Summary

| Phase | Feature                    | Impact          | Effort   | Status |
|-------|----------------------------|-----------------|----------|--------|
| —     | Typed result access        | Usability       | Low      | Done |
| —     | SimStatus error model      | Reliability     | Low      | Done |
| —     | Noise per-device accessor  | Usability       | Low      | Done |
| —     | Analysis chaining          | Usability       | Low      | Done |
| —     | CircuitBuilder fluent API  | Usability       | Medium   | Done |
| —     | Circuit introspection      | Usability       | Medium   | Done |
| —     | Generic set_param()        | Optimization    | Medium   | Done |
| 1     | Python bindings            | Adoption        | Medium   | Planned |
| 2     | WASM build                 | Accessibility   | Low      | Planned |
| 3     | Sensitivity/gradients      | Optimization    | High     | Planned |
| 4     | Parallel sweeps            | Performance     | Medium   | Planned |
| 5     | Incremental re-simulation  | Interactivity   | Medium   | Planned |
| 6     | GPU acceleration           | Large circuits  | High     | Planned |
| 7     | Extended devices/analyses  | Completeness    | Ongoing  | Active  |
