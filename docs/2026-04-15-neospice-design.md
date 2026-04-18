# NEOSPICE Design Specification

**Date:** 2026-04-15 (created) · 2026-04-18 (updated)
**Status:** Active — Phases 1–4 complete, Phases 5–10 planned

## Overview

NEOSPICE is a modern SPICE circuit simulator written in C++20. It uses ngspice as ground truth for correctness testing during development. The project is designed for future integration with the `circuit-cpp` framework and eventual GPU acceleration via CUDA.

## Goals

1. **Correctness first** — every feature validated against ngspice before moving to the next milestone
2. **Modern C++ codebase** — clean Device interface, sparse matrix abstraction, auto-migration tooling for ngspice models
3. **GPU-acceleratable** — architecture designed for CUDA device evaluation and sparse solve (deferred until profiling warrants)
4. **Fast CPU fallback** — KLU + OpenBLAS + OpenMP + SLEEF when no GPU is available
5. **Integration with circuit-cpp** — clean C++ API consumable by `circuit-cpp`'s `SpiceExporter`

## Milestone History

### Milestone 1: CPU-Only Correctness ✅

CPU-only simulator passing all ngspice comparison tests for Phase 1 devices (R, C, L, V, I, Diode). Parser, MNA matrix assembly, KLU solver, Newton-Raphson with convergence aids, fixed-step transient, DC operating point, AC small-signal analysis.

### Milestone 1.5: Adaptive Transient ✅

Adaptive time stepping: LTE-based step size control, step rejection/retry, breakpoint detection for PULSE/SIN source events, Gear BDF-2 integration as alternative to trapezoidal.

### Milestone 2: BSIM4v7 Hand-Port ✅

Hand-ported BSIM4v7 MOSFET model (CPU, simplified physics). Enough to prove the Device interface and MNA integration patterns.

### Milestone 2.5: BSIM4v7 Physics Closure ✅

Ported Abulk bulk-charge correction, RDSW series resistance, beta/gche channel-conductance form. Closed 8× drain-current gap to ~2× residual.

### Milestone 3: DC Convergence ✅

Analytical subthreshold gm/gds to prevent Jacobian singularity at zero bias. `.nodeset`/`.ic` seeding for Newton initial guess. MOSFET circuits converge from zero bias.

### Milestone 3.5: Step-Limiting + Residual Physics ✅

MOSFET Newton voltage limiting (DEVfetlim/DEVlimvds-style). VACLM + VADIBL Early-voltage contributions. CMOS inverter DC operating point converges.

### Milestone 4 Phase 1a: UCB Z-Port Scaffolding ✅

Vendored UCB BSIM 4.7.0 source. Demolished hand-port. Built SPICE3 compatibility shim (CKT, SMPmatrix, IF* stubs). Translated all non-per-timestep UCB routines (14K LOC). Golden preprocessing test bit-matches ngspice.

### Milestone 4 Phase 1b: UCB Z-Port Load Path ✅

Translated `b4ld.c` (~5.6K LOC). Extended Device/Circuit interfaces with state machinery (29 state vars, 3-buffer rotation). Shipped BSIM4v7Device adapter. Wired parser M-card (LEVEL=14). 126 tests passing.

### Milestone 4 Phase 2: Internal Nodes ✅

Internal-node allocation for BSIM4v7 resistance models (RDSMOD, RGATEMOD, RBODYMOD). `Device::declare_internal_nodes()` phase in `Circuit::finalize()`. DC tests for all resistance model variants. 135 tests passing.

### Auto-Migration Tool ✅

Built `tools/ngspice_migrate/` — a Python tool that auto-translates ngspice device model source files to neospice C++. Descriptor-driven (YAML). Handles TSTALLOC macros, noise/frontend stripping, compat defines, matrix stamp rewriting, shim/adapter/def generation. Used to re-migrate BSIM4v7 fully automatically (replacing hand-translated code). 135 Python tool tests.

## Current State (2026-04-18)

### Analyses Implemented

| Analysis | Status | Notes |
|----------|--------|-------|
| DC operating point (`.op`) | **Complete** | Newton-Raphson + gmin stepping + source stepping |
| Transient (`.tran`) | **Complete** | Fixed + adaptive time stepping, Gear BDF-2, trapezoidal, breakpoint detection |
| AC small-signal (`.ac`) | **Complete** for R/C/L/V/I/Diode | DEC/OCT/LIN sweep modes. BSIM4v7 AC load not yet migrated. |
| DC sweep (`.dc`) | **Not implemented** | Enum declared, driver not wired |
| Noise (`.noise`) | **Not implemented** | |

### Device Models Implemented

**Phase 1 — Basic passives and sources:**
- Resistor (linear) — DC, transient, AC
- Capacitor (linear) — DC, transient, AC (companion model)
- Inductor (linear) — DC, transient, AC (companion model, MNA branch variable)
- Voltage source (DC, AC, PULSE, SIN) — DC, transient, AC
- Current source (DC, AC, PULSE, SIN) — DC, transient, AC
- Diode (Shockley + junction capacitance) — DC, transient, AC

**Phase 2 — Semiconductor:**
- BSIM4v7 MOSFET — DC, transient. AC load, truncation error, noise not yet migrated.
  - Auto-migrated from ngspice via `tools/ngspice_migrate/`
  - 8 translated source files (~21K LOC), shim layer, device adapter
  - Internal-node allocation for resistance models (RDS, Rgate, Rbody)
  - 29 state variables per instance, 3-buffer rotation for transient history

**Not yet implemented:**
- Controlled sources (E, G, F, H)
- BJT (NPN/PNP)
- JFET
- Coupled inductors (K)
- Switches (S, W)
- Behavioral sources (B)
- Transmission lines (T, U)

### Parser Implemented

**Supported constructs:**
- Element instance lines: R, C, L, V, I, D, M
- `.model` statements (diode `d` type, MOSFET `nmos`/`pmos` with LEVEL=14)
- `.param` with scalar numeric substitution
- `.include` (basic file inclusion)
- `.ic` and `.nodeset` (initial conditions)
- `.tran`, `.ac`, `.op` (analysis commands)
- `.save` and `.print` (parsed but not enforced — all variables written)
- `.options` (`reltol`, `abstol`, `vntol`, `gmin`, `temp`, `max_iter`)
- Numeric suffixes (`k`, `meg`, `u`, `n`, `p`, `f`, etc.)
- Line continuations (`+` prefix)
- Comments (`*` prefix, `$` inline)
- Node name mapping (alphanumeric, `0` as ground)

**Not yet supported:**
- `.dc` sweep parameters
- `.subckt` / `.ends` (subcircuit definitions)
- `X` instances (subcircuit instantiation)
- Controlled sources (E, F, G, H element lines)
- Behavioral sources (`B` element, `VALUE=` expressions)
- `.param` arithmetic expressions (only scalar substitution)
- `.lib` section selection
- `.measure`, `.noise`, `.sens`, `.pz`, `.four`
- `.temp` sweep, `.step` parameter sweep
- XSPICE (`.model` with `code_model`)

Unsupported constructs produce a clear error at parse time listing the line number and construct.

### Test Suite

- **135 C++ tests** (Google Test) covering unit, integration, and ngspice comparison
- **135 Python tests** for the auto-migration tool
- **15 golden circuit netlists** validated against ngspice:
  - `resistor_divider.cir` — voltage divider DC
  - `rc_lowpass.cir` — RC filter transient pulse response
  - `rc_ac.cir` — RC frequency response
  - `rc_pulse_fast.cir` — RC fast pulse edges
  - `rlc_series.cir` — RLC impulse response
  - `rlc_underdamped.cir` — underdamped oscillation
  - `diode_iv.cir` — diode I-V characteristic
  - `diode_rectifier.cir` — diode rectifier transient
  - `nmos_iv.cir` — NMOS I-V with BSIM4v7
  - `nmos_rdsmod.cir` — NMOS with drain-source resistance model
  - `nmos_rgatemod.cir` — NMOS with gate resistance model
  - `nmos_rbodymod.cir` — NMOS with body resistance model
  - `cmos_inverter.cir` — CMOS inverter transient
  - `cmos_inverter_resistance.cir` — CMOS inverter with resistance models
  - `ring_osc_5stage.cir` — 5-stage ring oscillator
- **37 test source files**, ~2.9K LOC test code
- **Ngspice comparison framework** with configurable tolerances and oscillator comparator

## Planned Phases (5–10)

Detailed task-level plans are in `docs/superpowers/plans/2026-04-18-neospice-roadmap.md`.

### Phase 5: BSIM4v7 Feature Completion

Migrate remaining BSIM4v7 files via the auto-migration tool:
- `b4v7acld.c` — AC small-signal load (enables MOSFET frequency response)
- `b4v7trunc.c` — timestep truncation error (improves transient accuracy)
- `b4v7cvtest.c` — convergence test
- `b4v7getic.c` — initial conditions
- `b4v7ask.c` / `b4v7mask.c` — parameter query (lower priority)

### Phase 6: Controlled Sources + DC Sweep

- VCVS (`E`), VCCS (`G`), CCVS (`H`), CCCS (`F`) — linear controlled sources
- DC sweep analysis (`.dc Vsrc start stop step`)
- `.save` filtering enforcement

### Phase 7: Subcircuits + Parameter Expressions

- `.subckt` / `.ends` definition parsing
- `X` instance expansion (flattening with unique internal node names)
- `.param` arithmetic expression evaluator (`+`, `-`, `*`, `/`, `**`, functions)
- `.lib` section selection
- `.include` with relative path resolution

### Phase 8: BJT + Additional Devices

- BJT (Gummel-Poon) — auto-migrate from `~/Codes/ngspice/src/spicelib/devices/bjt/`
- JFET (optional)
- Coupled inductors (`K` element)

### Phase 9: Noise Analysis + Measurement

- `.noise` frequency-domain analysis with device noise models
- `.measure` post-processing (TRIG/TARG, FIND/WHEN, AVG, RMS, MIN, MAX)
- `.print` / `.plot` output formatting

### Phase 10: GPU Acceleration

Gated by profiling. Only pursue when device evaluation is proven to be the bottleneck on circuits with 500+ MOSFETs.

- GPU Phase 1: CUDA kernel for batched BSIM4v7 evaluation
- GPU Phase 2: cusolverRF for sparse refactorization (if solver is bottleneck)
- GPU Phase 3: cuDSS for full GPU-resident factorization (large circuits)

## Architecture

### Approach: CPU-First with GPU-Ready Design

The simulation engine is CPU-only today. The architecture separates sparsity pattern from numeric values and uses a clean Device interface, making future GPU porting straightforward without restructuring the core.

The Newton-Raphson iteration has three phases:

```
                    +---------------------------+
                    |   Newton-Raphson Loop      |
                    +-------------+-------------+
                                  |
              +-------------------+-------------------+
              v                   v                   v
      Device Evaluation     Matrix Assembly      Sparse Solve
              |                   |                   |
     +--------+--------+         |          +--------+--------+
     | CPU: OpenMP +    |  Always CPU:      | CPU: KLU +      |
     |      SLEEF SIMD  |  scatter into     |      OpenBLAS   |
     +--------+---------+  NumericMatrix    +--------+---------+
              |                  |                   |
              v                  v                   v
         (conductances,    (stamp into       (solve Ax=b,
          charges,          matrix)           update x)
          currents)
```

**Future GPU path:** When CUDA is available and the circuit exceeds the size threshold (configurable, default 500+ nodes), device evaluation and/or sparse solve move to GPU. Matrix stamping remains CPU (irregular scatter patterns). See Phase 10 for details.

### Sparse Matrix Abstraction

The matrix system separates **structure** (sparsity pattern) from **values** (numeric entries):

**SparsityPattern** (computed once during `Circuit::finalize()`):
- Built from device `stamp_pattern()` calls
- Stores structural non-zero positions as sorted CSC (Compressed Sparse Column)
- Immutable after construction

**NumericMatrix** (updated every Newton iteration):
- Holds only the `values[nnz]` array, indexed by pre-assigned offsets
- Devices stamp via `MatrixOffset` (pre-computed during `assign_offsets()`)
- Cleared and re-stamped each iteration

**SparsityBuilder**:
- Accumulates (row, col) entries from devices during symbolic analysis
- Deduplicates and sorts into final SparsityPattern

**KLU solver view:**
- Wraps pattern's CSC arrays (`col_ptr`, `row_idx`) for KLU
- Values array shared by pointer — no copy
- Symbolic factorization once, numeric refactorization per iteration

### State Management (Transient)

Three rotating state buffers support multi-step integration methods:
- `state0` — current iterate (written during device evaluation)
- `state1` — previous accepted timestep
- `state2` — two timesteps back (for Gear BDF-2)

Devices declare `state_vars()` count during finalization. `Circuit` allocates contiguous buffers and calls `set_state_ptrs()` to bind base offsets. Rotation happens at timestep acceptance.

`IntegratorCtx` (thread-local via `tls_integrator_ctx`) publishes integration coefficients, mode flags, temperature, and timestep info to devices without threading extra parameters through the Device interface.

## Device Interface Contract

```cpp
class Device {
public:
    explicit Device(std::string name);
    virtual ~Device() = default;
    const std::string& name() const;

    // --- Circuit construction ---
    virtual void declare_internal_nodes(Circuit& ckt) {}
    virtual void stamp_pattern(SparsityBuilder& builder) const = 0;
    virtual void assign_offsets(const SparsityPattern& pattern) = 0;
    virtual int32_t extra_vars() const { return 0; }   // MNA branch variables
    virtual int32_t state_vars() const { return 0; }    // transient state slots
    virtual void set_state_ptrs(double* state0, double* state1,
                                double* state2, int32_t base) {}

    // --- DC and Transient ---
    virtual void evaluate(const std::vector<double>& voltages,
                          NumericMatrix& mat, std::vector<double>& rhs) = 0;
    virtual void limit_voltages(const std::vector<double>& old_v,
                                std::vector<double>& new_v) {}

    // --- AC small-signal ---
    virtual void ac_stamp(const std::vector<double>& voltages,
                          NumericMatrix& G, NumericMatrix& C) {}

    // --- Output ---
    virtual std::vector<std::string> output_currents() const { return {}; }
};
```

**AC linearization:** After DC operating point converges, each nonlinear device computes linearized small-signal parameters (conductance G + capacitance C). These are stamped into separate G and C matrices, reused for all AC frequency points. The complex MNA system `(G + jωC) * V = I` is solved at each frequency.

**Output currents:**
- *Always available:* Voltage source `I(Vname)` — MNA solution variable
- *Available on request:* Diode `I(Dname)`, MOSFET `Id(Mname)` — computed during evaluation
- *Derived:* Passive element currents from terminal voltage difference (use series 0V source to probe)

## Convergence Strategy

### Voltage Limiting

Junction voltage clamped before device evaluation to prevent exp() overflow. MOSFET gate voltage limiting via DEVfetlim/DEVlimvds (implemented in shim layer per migrated model).

### Gmin Stepping

If Newton-Raphson fails: add `gmin` conductance from each node to ground, solve with large gmin, progressively reduce to `1e-12`. Each converged solution seeds the next step.

### Source Stepping

If gmin stepping fails: scale all independent sources from 0→1 in steps, using each converged solution as initial guess for the next.

### Initial Conditions

- `.nodeset V(node)=value` — initial guess for DC operating point (preferred)
- `.ic V(node)=value` — fallback DC hint, also applied at transient t=0
- Default: all node voltages = 0

### Convergence Criteria

Newton iteration has converged when ALL conditions are met:
- `|V_new - V_old| < reltol * max(|V_new|, |V_old|) + vntol` for every node voltage
- `|I_new - I_old| < reltol * max(|I_new|, |I_old|) + abstol` for every branch current

## Project Structure

```
neospice/
├── CMakeLists.txt
├── cli/
│   └── main.cpp                       # CLI: neospice <file.cir>
├── src/
│   ├── api/
│   │   └── neospice.hpp/cpp           # Public C++ API (Simulator class)
│   ├── core/
│   │   ├── ac.hpp/cpp                 # AC small-signal frequency sweep
│   │   ├── circuit.hpp/cpp            # Circuit representation + finalization
│   │   ├── convergence.hpp/cpp        # Gmin stepping, source stepping
│   │   ├── dc.hpp/cpp                 # DC operating point solve
│   │   ├── klu_solver.hpp/cpp         # KLU sparse LU wrapper
│   │   ├── matrix.hpp/cpp             # SparsityPattern + NumericMatrix
│   │   ├── newton.hpp/cpp             # Newton-Raphson iteration loop
│   │   ├── timestep.hpp/cpp           # Adaptive step controller (LTE)
│   │   ├── transient.hpp/cpp          # Transient analysis driver
│   │   └── types.hpp/cpp              # SimOptions, SPICE number parsing
│   ├── devices/
│   │   ├── device.hpp                 # Base Device interface
│   │   ├── resistor.hpp/cpp
│   │   ├── capacitor.hpp/cpp
│   │   ├── inductor.hpp/cpp
│   │   ├── vsource.hpp/cpp
│   │   ├── isource.hpp/cpp
│   │   ├── diode.hpp/cpp
│   │   └── bsim4v7/                   # Auto-migrated UCB BSIM4v7
│   │       ├── bsim4v7_def.hpp        #   Struct definitions (generated)
│   │       ├── bsim4v7_shim.hpp/cpp   #   SPICE3 compatibility shim
│   │       ├── bsim4v7_device.hpp/cpp #   Neospice Device adapter
│   │       ├── bsim4v7_setup.cpp      #   Model initialization
│   │       ├── bsim4v7_temp.cpp       #   Temperature processing
│   │       ├── bsim4v7_load.cpp       #   Main evaluation (~5.6K LOC)
│   │       ├── bsim4v7_check.cpp      #   Parameter validation
│   │       ├── bsim4v7_mpar.cpp       #   Model parameter dispatch
│   │       ├── bsim4v7_param.cpp      #   Instance parameter dispatch
│   │       ├── bsim4v7_devsup.cpp     #   Device support + param tables
│   │       ├── bsim4v7_geo.cpp        #   Geometry calculations
│   │       └── CMakeLists.txt
│   ├── output/
│   │   ├── raw_writer.hpp/cpp         # .raw binary output (ngspice-compatible)
│   │   └── vectors.hpp                # Result vector types
│   └── parser/
│       ├── tokenizer.hpp/cpp          # SPICE tokenizer (continuations, suffixes)
│       ├── expression.hpp/cpp         # .param expression evaluator (scalar)
│       ├── model_cards.hpp/cpp        # .model statement parsing
│       └── netlist_parser.hpp/cpp     # Two-pass netlist parser
├── tools/
│   ├── ngspice_migrate/               # Auto-migration tool (Python)
│   │   ├── __main__.py                #   CLI entry point
│   │   ├── descriptor.py              #   YAML descriptor loader
│   │   ├── transformer.py             #   8-pass C→C++ translation pipeline
│   │   ├── gen_def.py                 #   *_def.hpp generator
│   │   ├── gen_shim.py                #   *_shim.hpp/cpp generator
│   │   └── gen_adapter.py             #   *_device.hpp/cpp generator
│   ├── descriptors/
│   │   └── bsim4v7.yaml               #   BSIM4v7 model descriptor
│   └── tests/                         #   Python test suite (135 tests)
├── tests/
│   ├── framework/
│   │   ├── ngspice_runner.hpp/cpp     # Runs ngspice, parses .raw output
│   │   └── comparator.hpp/cpp         # Vector comparison with tolerances
│   ├── circuits/                      # 15 golden circuit netlists
│   └── unit/                          # 37 C++ test files (135 tests)
└── third_party/                       # KLU, SuiteSparse, OpenBLAS, SLEEF
```

## Public API

```cpp
namespace neospice {

class Simulator {
public:
    struct Options {
        int max_threads = 0;          // 0 = auto-detect core count
        double abstol = 1e-12;        // Absolute current tolerance
        double reltol = 1e-3;         // Relative tolerance
        double vntol = 1e-6;          // Voltage tolerance
        double trtol = 7.0;           // Transient error tolerance
        double gmin = 1e-12;          // Minimum conductance
    };

    explicit Simulator(Options opts = {});
    ~Simulator();

    Circuit load(const std::string& filepath);
    Circuit parse(const std::string& netlist_text);

    DCResult run_dc(const Circuit& ckt);
    TransientResult run_transient(const Circuit& ckt, double tstep, double tstop);
    ACResult run_ac(const Circuit& ckt, ACMode mode, int npoints,
                    double fstart, double fstop);
    SimulationResult run(const Circuit& ckt);
};

struct DCResult {
    std::unordered_map<std::string, double> node_voltages;
    std::unordered_map<std::string, double> branch_currents;
};

struct TransientResult {
    std::vector<double> time;
    std::unordered_map<std::string, std::vector<double>> voltages;
    std::unordered_map<std::string, std::vector<double>> currents;
};

struct ACResult {
    std::vector<double> frequency;
    std::unordered_map<std::string, std::vector<std::complex<double>>> voltages;
    std::unordered_map<std::string, std::vector<std::complex<double>>> currents;
};

enum class ACMode { DEC, OCT, LIN };

struct SimulationResult {
    std::optional<DCResult> dc;
    std::optional<TransientResult> transient;
    std::optional<ACResult> ac;
};

} // namespace neospice
```

### Analysis Command Precedence

1. API parameters take precedence (explicit caller intent)
2. Netlist `.tran`/`.ac`/`.dc` commands are defaults when API parameters are not specified
3. `run()` uses only netlist commands; `run_transient()` etc. use API parameters with netlist as fallback

### Signal Naming Convention

- `"v(net1)"` — node voltage (lowercase `v`, node name preserved)
- `"i(v1)"` — current through voltage source V1
- `"id(m1)"` — drain current of MOSFET M1

### Integration with circuit-cpp

```cpp
SpiceExporter exporter;
exporter.auto_export(my_circuit);
std::string netlist = exporter.to_string();

neospice::Simulator sim;
auto ckt = sim.parse(netlist);
auto result = sim.run(ckt);
```

## Auto-Migration Tool

The `tools/ngspice_migrate/` Python package translates ngspice device model C source code into neospice-compatible C++. It is descriptor-driven: each device model has a YAML file (`tools/descriptors/<model>.yaml`) describing its struct names, terminals, source files, and feature flags.

### Pipeline

1. **Descriptor loading** — YAML → `ModelDescriptor` with all naming and structural info
2. **8-pass transformer** — regex-based C→C++ translation:
   - Strip ngspice includes and frontend code (noise analysis, `ft_curckt`)
   - Protect literals (strings/comments) from regex mangling
   - Rewrite `*(here->FieldPtr) += expr` → `mat.add(here->FieldPtr, expr)`
   - Replace ngspice types with neospice equivalents
   - Inject compat defines (CONSTvt0, CHARGE, FABS, IOP macros, etc.)
   - Wrap in namespace with includes
3. **Generator passes** — produce:
   - `*_def.hpp` — struct definitions with `MatrixOffset` fields, forward declarations
   - `*_shim.hpp/cpp` — SPICE3 compatibility layer (Ckt, Matrix, DEVfetlim, etc.)
   - `*_device.hpp/cpp` — neospice Device adapter (make/stamp/evaluate lifecycle)
   - `CMakeLists.txt`
4. **Validation** — TSTALLOC count vs. mat.add count, compilation check

### Usage

```bash
PYTHONPATH=tools python3 -m ngspice_migrate \
    tools/descriptors/bsim4v7.yaml \
    ~/Codes/ngspice/src/spicelib/devices/bsim4v7 \
    /tmp/bsim4v7_migrated
```

### Adding a New Device Model

1. Create `tools/descriptors/<model>.yaml` with struct names, terminals, source files
2. Run the migration tool
3. Copy generated files to `src/devices/<model>/`
4. Wire parser (element line recognition, `.model` card routing)
5. Add test circuits comparing against ngspice

## Test Harness

### Strategy

Every NEOSPICE feature is validated against ngspice output:
1. Run a `.cir` netlist through ngspice (via CLI subprocess), parse the `.raw` output
2. Run the same `.cir` through NEOSPICE's C++ API
3. Compare node voltages and branch currents within configurable tolerance

### Tolerance Model

- Default: relative error < 1e-3
- Simple circuits (resistor divider): tightened to 1e-6
- Complex nonlinear circuits (BSIM4v7 transient): 1e-3
- AC analysis: magnitude relative tolerance, phase absolute tolerance

### Transient Comparison

Both simulators may use different adaptive time steps. Comparison interpolates NEOSPICE results onto ngspice's time grid (ngspice is ground truth) and checks tolerance at every reference point. This validates functional equivalence (same waveform shape), not numerical identity.

## Build System

CMake 3.20+ with C++20. CUDA support is optional and not yet enabled.

### Dependencies

| Dependency | Purpose | License | Required? |
|------------|---------|---------|-----------|
| KLU (SuiteSparse) | Sparse LU solver | LGPL-2.1+ | Yes |
| OpenBLAS | Dense BLAS for KLU internals | BSD-3 | Yes |
| OpenMP | Parallel device evaluation | Compiler-provided | No (optional) |
| SLEEF | SIMD transcendentals | Boost | Yes |
| Google Test | Testing framework | BSD-3 | Tests only |
| ngspice | Ground truth reference | BSD-3 | Tests only |
| PyYAML | Migration tool descriptor loading | MIT | Migration tool only |

### Build Configurations

| Configuration | Use Case |
|---------------|----------|
| `Release` | Production (O2, SIMD) |
| `Debug` | Development (O0, sanitizers) |
| `RelWithDebInfo` | Profiling (O1, debug info) |

## GPU Acceleration (Phase 10 — Future)

Deferred until profiling on Phase 5–9 circuits shows device evaluation is the dominant cost for large circuits. The design is GPU-ready:

- SparsityPattern/NumericMatrix separation allows GPU solver views without changing stamping code
- Device evaluation is embarrassingly parallel (one kernel thread per instance)
- State arrays are contiguous (easy to transfer host↔device)

**GPU Phase 1:** Custom CUDA kernels for batched BSIM4v7 evaluation, KLU stays on CPU.
**GPU Phase 2:** cusolverRF for sparse refactorization if solver is bottleneck.
**GPU Phase 3:** cuDSS for full GPU-resident factorization on large circuits.

Each phase gated by profiling data.

## Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| **Subcircuit complexity** — hierarchical expansion, parameter scoping, recursive instances | Phase 7 is the hardest parser work | Flatten-only approach (no preservation of hierarchy). Follow ngspice's expansion model. |
| **Auto-migration tool gaps** — new device models may have patterns the transformer doesn't handle | Build errors on migrated code | Tool is extensible (add regex passes). BSIM4v7 exercised ~15 pattern categories. |
| **Convergence differences** — different stepping/limiting than ngspice may produce different paths | Tests fail due to convergence, not math | Match ngspice's convergence aids. Accept wider tolerances on sensitive circuits. |
| **GPU break-even uncertainty** — circuit matrices may be too small for GPU advantage | GPU investment without speedup | Defer GPU until profiling on real circuits. CPU path is the product. |
| **PDK netlist compatibility** — real PDKs use complex `.lib`/`.param`/`.subckt` patterns | Can't run production netlists | Phase 7 prioritizes the subset needed for common PDK patterns. |
| **SuiteSparse LGPL license** | Static linking restrictions | Dynamic linking for KLU. Evaluate BSD alternatives if needed. |

## Resolved Design Decisions

| # | Decision | Rationale |
|---|----------|-----------|
| 1 | Strict documented subset, not full ngspice compat | Clear scope prevents unbounded parser work |
| 2 | Correctness first, GPU later | Fast wrong answers are useless |
| 3 | SparsityPattern + NumericMatrix + views | Decouples stamping from solver format |
| 4 | V-source currents always; others opt-in via `.save` | Matches ngspice default behavior |
| 5 | Fixed-step first, adaptive layered on | Simpler to debug; adaptive added in M1.5 |
| 6 | Auto-migration tool for ngspice models | Hand-porting 21K LOC is error-prone; tool is reusable for BJT, JFET, etc. |
| 7 | Flatten subcircuits (no hierarchy preservation) | Simpler implementation; matches ngspice behavior |
| 8 | CPU-only until profiling justifies GPU | Avoids premature optimization; GPU design is preserved in architecture |
