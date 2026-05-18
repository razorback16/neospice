# NEOSPICE Design Specification

**Date:** 2026-04-15 (created) · 2026-04-25 (updated)
**Status:** Active — Phases 1–9 complete, Phase 10 (GPU) planned

## Overview

NEOSPICE is a modern SPICE circuit simulator written in C++20. It uses ngspice as ground truth for correctness testing during development. The project is designed for future integration with the `circuit-cpp` framework and eventual GPU acceleration via CUDA.

## Goals

1. **Correctness first** — every feature validated against ngspice before moving to the next milestone
2. **Modern C++ codebase** — clean Device interface, sparse matrix abstraction, auto-migration tooling for ngspice models
3. **GPU-acceleratable** — architecture designed for CUDA device evaluation and sparse solve (deferred until profiling warrants)
4. **Fast CPU fallback** — NeoSolver + OpenBLAS + OpenMP + SLEEF when no GPU is available
5. **Integration with circuit-cpp** — clean C++ API consumable by `circuit-cpp`'s `SpiceExporter`

## Milestone History

### Milestone 1: CPU-Only Correctness ✅

CPU-only simulator passing all ngspice comparison tests for Phase 1 devices (R, C, L, V, I, Diode). Parser, MNA matrix assembly, NeoSolver (custom sparse LU), Newton-Raphson with convergence aids, fixed-step transient, DC operating point, AC small-signal analysis.

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

Built `tools/ngspice_migrate/` — a Python tool that auto-translates ngspice device model source files to neospice C++. Descriptor-driven (YAML). Handles TSTALLOC macros, noise/frontend stripping, compat defines, matrix stamp rewriting, shim/adapter/def generation. Used to re-migrate BSIM4v7 fully automatically (replacing hand-translated code).

### Phase 5: BSIM4v7 Feature Completion ✅

Migrated remaining BSIM4v7 capabilities: AC small-signal load, timestep truncation error, NQS (non-quasi-static) AC, convergence test, noise analysis with correlated noise sources. All BSIM4v7 resistance model variants (RDS, Rgate, Rbody) fully exercised across DC, AC, transient, and noise.

### Phase 6: Controlled Sources + DC Sweep ✅

Implemented all four linear controlled sources (VCVS `E`, VCCS `G`, CCVS `H`, CCCS `F`) with polynomial (POLY) multi-input support, plus nonlinear variants for all four. DC sweep analysis (`.dc`) with 1D and nested 2D parameter sweeps. Voltage-controlled switch (`S`) and current-controlled switch (`W`) with hysteresis. `.save` signal filtering enforcement.

### Phase 7: Subcircuits + Parameter Expressions ✅

`.subckt`/`.ends` definition parsing with parameter defaults. `X` instance flattening with recursive expansion (depth limit 100). `.global` node support. Full `.param` arithmetic expression evaluator (`+`, `-`, `*`, `/`, `**`, functions: `sqrt`, `abs`, `log`, `log10`, `exp`, `sin`, `cos`, `min`, `max`, `pow`, `if`, `gauss`, `agauss`, `unif`, `aunif`). `.func` user-defined functions. `.lib` section selection with circular inclusion detection. `.include` with relative path resolution.

### Phase 8: BJT + Additional Devices ✅

Auto-migrated 15 semiconductor device models from ngspice via the migration tool:

- **MOSFETs:** MOS1 (Level 1), MOS3 (Level 3), MOS9 (Level 9), BSIM3, BSIM3v32, BSIMSOI (Silicon-On-Insulator)
- **BJTs:** Gummel-Poon BJT, VBIC (Vertical Bipolar Inter-Company)
- **JFETs:** JFET (Shichman-Hodges), JFET2 (Level 2)
- **HFETs:** HFET1, HFET2 (High Electron Mobility Transistors)
- **HiSIM:** HiSIM2, HiSIM_HV (High-Voltage)

Also implemented: Coupled inductors (`K`), ideal transmission line (`T`), lossy transmission line (LTRA `O`), behavioral source (ASRC `B`) with expression AST supporting `ddt()`, `idt()`, `temper`, trigonometric functions, and PWL tables. Diode migrated from hand-written to auto-migrated (`dio/`).

### Phase 9: Noise Analysis + Measurement ✅

Noise analysis (`.noise`) using the adjoint method with per-device breakdown, correlated noise source support, and integrated noise over frequency bands. Transfer function (`.tf`) with input/output impedance. Sensitivity analysis (`.sens`) with normalized sensitivities. Pole-zero analysis (`.pz`). Fourier analysis (`.four`) with DFT over last fundamental period and THD calculation. Measurement commands (`.meas`/`.measure`) supporting TRIG/TARG, FIND/WHEN, and statistical (AVG, RMS, MIN, MAX, PP, INTEG). Parameter sweep (`.step`) for sources, parameters, and temperature.

### Refactoring Pass ✅

10-task refactor across 192 files removing ~7,500 lines of code: deduplicated macros/utils/templates into shared headers (`ucb_compat.hpp`, `ucb_utils.hpp`, `ucb_device_init.hpp`, `model_card_utils.hpp`), unified AnalysisCommand and SimulationResult to `std::variant`, eliminated `dynamic_cast` chains in favor of `std::visit`.

### API Refresh ✅

Added typed result accessors (`voltage()`, `current()`, `diff()`, `signal_names()`) to all result types. Embedded `SimStatus` (convergence method, iterations, elapsed time, warnings) in all result types. Added typed device methods on `Circuit` (`R()`, `C()`, `V()`, `E()`, etc.) for programmatic circuit construction. Added circuit introspection (`device_info()`, `devices_at_node()`, `node_names()`, `device_names()`). Added generic `set_param()` for runtime parameter modification. Added analysis chaining via `TransientOptions` and `ACOptions`.

### Device Factory Pattern ✅

Modularized all 17 semiconductor device families via `DeviceRegistry` factory pattern. Each device registers its model card factory, device builder, and element parser in a self-contained factory file. Adding a new device requires zero changes to `circuit_typed.cpp` or `netlist_parser.cpp` — just a factory file, one CMake line, and one `register_all()` call. Reduced `circuit_typed.cpp` from 414 to 295 lines and `netlist_parser.cpp` from 3727 to 2696 lines by extracting semiconductor parsing into 5 shared parser modules (`mosfet_common`, `bjt_common`, `jfet_common`, `hfet_common`, `dio_parser`).

## Current State (2026-05-17)

### Analyses Implemented

| Analysis | Status | Command | API Method |
|----------|--------|---------|------------|
| DC operating point (`.op`) | **Complete** | `.op` | `run_dc()` |
| DC sweep (`.dc`) | **Complete** | `.dc src start stop step` | `run_dc_sweep()` |
| Transient (`.tran`) | **Complete** | `.tran tstep tstop` | `run_transient()` |
| AC small-signal (`.ac`) | **Complete** | `.ac DEC/OCT/LIN np fstart fstop` | `run_ac()` |
| Noise (`.noise`) | **Complete** | `.noise V(out) src mode np fstart fstop` | `run_noise()` |
| Transfer function (`.tf`) | **Complete** | `.tf output input` | `run_tf()` |
| Sensitivity (`.sens`) | **Complete** | `.sens output` | `run_sens()` |
| Pole-zero (`.pz`) | **Complete** | `.pz n1 n2 n3 n4 VOL/CUR POL/ZER/PZ` | via `run()` |
| Fourier (`.four`) | **Complete** | `.four freq signal...` | `compute_fourier()` |
| Measurement (`.meas`) | **Complete** | `.meas type name ...` | `execute_measures()` |
| Parameter sweep (`.step`) | **Complete** | `.step param/src/temp ...` | `run_step_sweep()` |

### Device Models Implemented

**Passive and source devices (root level):**

| Element | Device | DC | Tran | AC | Noise |
|---------|--------|:--:|:----:|:--:|:-----:|
| R | Resistor (with model, temperature) | ✓ | ✓ | ✓ | ✓ |
| C | Capacitor (with model, temperature) | ✓ | ✓ | ✓ | — |
| L | Inductor (with model, temperature) | ✓ | ✓ | ✓ | — |
| K | Coupled inductors | ✓ | ✓ | ✓ | — |
| V | Voltage source (DC, AC, PULSE, SIN, PWL, EXP, SFFM, AM) | ✓ | ✓ | ✓ | — |
| I | Current source (DC, AC, PULSE, SIN, PWL, EXP, SFFM, AM) | ✓ | ✓ | ✓ | — |

**Controlled sources and switches:**

| Element | Device | DC | Tran | AC | Noise |
|---------|--------|:--:|:----:|:--:|:-----:|
| E | VCVS (linear + POLY nonlinear) | ✓ | ✓ | ✓ | — |
| G | VCCS (linear + POLY nonlinear) | ✓ | ✓ | ✓ | — |
| H | CCVS (linear + POLY nonlinear) | ✓ | ✓ | ✓ | — |
| F | CCCS (linear + POLY nonlinear) | ✓ | ✓ | ✓ | — |
| S | Voltage-controlled switch | ✓ | ✓ | ✓ | — |
| W | Current-controlled switch | ✓ | ✓ | ✓ | — |
| B | Behavioral source (ASRC) | ✓ | ✓ | ✓ | — |

**Transmission lines:**

| Element | Device | DC | Tran | AC | Noise |
|---------|--------|:--:|:----:|:--:|:-----:|
| T | Ideal transmission line | ✓ | ✓ | ✓ | — |
| O | Lossy transmission line (LTRA) | ✓ | ✓ | ✓ | — |

**Semiconductor devices (auto-migrated from ngspice):**

| Element | Device | DC | Tran | AC | Noise |
|---------|--------|:--:|:----:|:--:|:-----:|
| D | Diode (migrated, junction cap + flicker) | ✓ | ✓ | ✓ | ✓ |
| Q | BJT (Gummel-Poon) | ✓ | ✓ | ✓ | ✓ |
| Q | VBIC (Vertical Bipolar Inter-Company) | ✓ | ✓ | ✓ | ✓ |
| J | JFET (Shichman-Hodges) | ✓ | ✓ | ✓ | ✓ |
| J | JFET2 (Level 2) | ✓ | ✓ | ✓ | ✓ |
| Z | MES (GaAs MESFET) | ✓ | ✓ | ✓ | ✓ |
| Z | HFET1 | ✓ | ✓ | ✓ | ✓ |
| Z | HFET2 | ✓ | ✓ | ✓ | ✓ |
| M | MOS1 (Level 1) | ✓ | ✓ | ✓ | ✓ |
| M | MOS3 (Level 3) | ✓ | ✓ | ✓ | ✓ |
| M | MOS9 (Level 9) | ✓ | ✓ | ✓ | ✓ |
| M | BSIM3 | ✓ | ✓ | ✓ | ✓ |
| M | BSIM3v32 | ✓ | ✓ | ✓ | ✓ |
| M | BSIM4v7 | ✓ | ✓ | ✓ | ✓ |
| M | BSIMSOI (Silicon-On-Insulator) | ✓ | ✓ | ✓ | ✓ |
| M | HiSIM2 | ✓ | ✓ | ✓ | ✓ |
| M | HiSIM_HV (High-Voltage) | ✓ | ✓ | ✓ | ✓ |

**Total: 32 device types** (6 passive/source + 7 controlled/switch + 2 transmission line + 17 semiconductor)

### Parser Implemented

**Supported constructs:**
- Element instance lines: R, C, L, V, I, D, M, E, G, H, F, Q, J, Z, K, T, O, S, W, B, X
- `.model` statements (all device types with LEVEL selection for MOSFETs)
- `.param` with full arithmetic expression evaluation (`+`, `-`, `*`, `/`, `**`, functions)
- `.func` user-defined function definitions
- `.include` (file inclusion with relative path resolution, circular detection)
- `.lib` section selection (`.lib filename section`)
- `.subckt` / `.ends` (subcircuit definitions with parameter defaults)
- `X` instances (subcircuit instantiation with recursive flattening, depth limit 100)
- `.global` node declarations
- `.ic` and `.nodeset` (initial conditions)
- `.tran`, `.ac`, `.dc`, `.op`, `.noise`, `.tf`, `.sens`, `.pz` (analysis commands)
- `.four` (Fourier analysis post-processing)
- `.meas` / `.measure` (TRIG/TARG, FIND/WHEN, AVG, RMS, MIN, MAX, PP, INTEG)
- `.step` (parameter, source, and temperature sweeps)
- `.save` and `.print` / `.plot` (output signal selection)
- `.options` (`reltol`, `abstol`, `vntol`, `gmin`, `trtol`, `chgtol`, `temp`, `tnom`, `method`, `itl1`, `itl4`, `lte_ref_mode`, `restart_step_scale`, `interp`)
- Controlled source POLY forms (multi-input polynomial)
- Behavioral expressions (`V=`, `I=`, `ddt()`, `idt()`, `temper`, trig functions, PWL)
- Source waveforms: DC, AC, PULSE, SIN, PWL, EXP, SFFM, AM
- Numeric suffixes (`k`, `meg`, `u`, `n`, `p`, `f`, etc.)
- Line continuations (`+` prefix)
- Comments (`*` prefix, `$` inline)
- Node name mapping (alphanumeric, `0`/`gnd`/`GND` as ground)

**Not yet supported:**
- `.temp` sweep (use `.step temp` instead)
- XSPICE (`.model` with `code_model`)
- Some advanced `.measure` forms

Unsupported constructs produce a clear error at parse time listing the line number and construct.

### Test Suite

- **978 C++ tests** (Google Test) across 105 test source files (~25K LOC test code)
- **173 Python test methods** across 14 test files for the auto-migration tool
- **124 golden circuit netlists** validated against ngspice, covering:
  - Passives: resistor divider, RC/RLC filters, coupled inductors, inductor/capacitor/resistor models
  - Sources: PULSE, PWL, EXP, SFFM, AM waveforms
  - Diodes: I-V, DC sweep, rectifier, transient, AC response, noise (thermal + flicker)
  - MOSFETs: MOS1/MOS3/MOS9/BSIM3/BSIM3v32/BSIM4v7/BSIMSOI/HiSIM2/HiSIM_HV (DC, AC, transient, noise, IV sweep)
  - BJTs: BJT CE noise, VBIC (DC NPN/PNP, AC, Gummel, transient)
  - JFETs: JFET noise, JFET2 (DC, AC, transient)
  - HFETs: HFET1/HFET2 (DC, AC, IV sweep, transient)
  - Controlled sources: POLY VCVS, POLY CCCS, POLY CCVS
  - Switches: VSwitch relay, CSwitch
  - Behavioral: ASRC (AC gain, ddt, idt, hertz, multi-var, PWL, square, tempco, temper, trig, VCCS, voltage doubler)
  - Transmission lines: T-line (DC, AC, IC, matched), LTRA (DC RC/RG, transient RC/LC/RLC)
  - Complex circuits: CMOS inverter, ring oscillator, THS4131 diff amp (with .lib), common-source amp AC
  - Analysis-specific: noise (RC, resistor, MOSFET), sensitivity, transfer function, step sweep, global nodes
- **Ngspice comparison framework** with configurable tolerances and oscillator comparator

## Planned Phases

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
     | CPU: OpenMP +    |  Always CPU:      | CPU: NeoSolver +|
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

**NeoSolver view:**
- Wraps pattern's CSC arrays (`col_ptr`, `row_idx`) for sparse LU
- Values array shared by pointer — no copy
- Symbolic factorization (AMD ordering, maximum transversal, Markowitz pivoting with Gilbert-Peierls reach) once, numeric refactorization per iteration

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

    // --- Identity ---
    virtual std::string device_type() const;       // "R", "C", "M", etc.
    virtual std::vector<int32_t> external_nodes() const;
    virtual std::optional<double> primary_value() const;
    virtual bool set_value(double value);

    // --- Circuit construction ---
    virtual void declare_internal_nodes(Circuit& ckt) {}
    virtual void stamp_pattern(SparsityBuilder& builder) const = 0;
    virtual void assign_offsets(const SparsityPattern& pattern) = 0;
    virtual int32_t extra_vars() const { return 0; }   // MNA branch variables
    virtual void assign_branch_index(int32_t& next) {}
    virtual int32_t branch_index() const { return -1; }
    virtual int32_t state_vars() const { return 0; }    // transient state slots
    virtual void set_state_ptrs(double* state0, double* state1,
                                double* state2, int32_t base) {}

    // --- DC and Transient ---
    virtual void evaluate(const std::vector<double>& voltages,
                          NumericMatrix& mat, std::vector<double>& rhs) = 0;
    virtual void limit_voltages(const std::vector<double>& old_v,
                                std::vector<double>& new_v) {}
    virtual bool device_converged() const { return true; }
    virtual double compute_trunc(const IntegratorCtx& ctx,
                                 const SimOptions& opts) const;
    virtual void process_temperature(double sim_temp, double sim_tnom) {}
    virtual void reset() {}
    virtual void reset_temp() {}

    // --- AC small-signal ---
    virtual void ac_stamp(const std::vector<double>& voltages,
                          NumericMatrix& G, NumericMatrix& C) {}
    virtual bool ac_stamp_freq(double omega, std::vector<double>& ax,
                               int32_t nnz,
                               std::vector<std::complex<double>>& ac_rhs);
    virtual void apply_ac_excitation(std::vector<std::complex<double>>& ac_rhs,
                                     int32_t n) {}

    // --- Noise ---
    struct NoiseSource {
        int32_t node_i, node_j;
        double spectral_density;  // A²/Hz
    };
    struct CorrelatedNoiseSource {
        int32_t n1_i, n1_j, n2_i, n2_j;
        double psd1, psd2, phase;
    };
    virtual std::vector<NoiseSource> noise_sources(
        double freq, const std::vector<double>& dc_solution) const;
    virtual std::vector<CorrelatedNoiseSource> correlated_noise_sources(
        double freq, const std::vector<double>& dc_solution) const;
    void set_sim_temp(double t);
    double sim_temp() const;

    // --- Introspection ---
    virtual std::optional<double> query_param(const std::string& name) const;
    virtual std::vector<std::string> output_currents() const { return {}; }
};
```

**AC linearization:** After DC operating point converges, each nonlinear device computes linearized small-signal parameters (conductance G + capacitance C). These are stamped into separate G and C matrices, reused for all AC frequency points. The complex MNA system `(G + jωC) * V = I` is solved at each frequency. For frequency-dependent devices (transmission lines), `ac_stamp_freq()` overrides the per-frequency matrix entries.

**Noise analysis:** After DC operating point, each device reports its noise current sources via `noise_sources()` (uncorrelated) and `correlated_noise_sources()`. The adjoint method solves `Y^T * adj = e_out` to project device noise to the output node. Per-device breakdown and input-referred noise are computed.

**Output currents:**
- *Always available:* Voltage source `I(Vname)` — MNA solution variable
- *Always available:* Any device with a branch variable (inductors, controlled sources)
- *Available on request:* Diode `I(Dname)`, MOSFET `Id(Mname)` — computed during evaluation
- *Derived:* Passive element currents from terminal voltage difference (use series 0V source to probe)

## Convergence Strategy

### Voltage Limiting

Junction voltage clamped before device evaluation to prevent exp() overflow. MOSFET gate voltage limiting via DEVfetlim/DEVlimvds (implemented in shim layer per migrated model).

### Gmin Stepping

If Newton-Raphson fails: add `gmin` conductance from each node to ground, solve with large gmin, progressively reduce to `1e-12`. Each converged solution seeds the next step.

### Source Stepping

If gmin stepping fails: scale all independent sources from 0→1 in steps, using each converged solution as initial guess for the next.

### Pseudo-Transient

If source stepping fails: add C/Δt damping terms to the diagonal, progressively reducing the artificial time constant as the solution stabilizes.

### Initial Conditions

- `.nodeset V(node)=value` — initial guess for DC operating point (preferred)
- `.ic V(node)=value` — fallback DC hint, also applied at transient t=0
- `UIC` option on `.tran` — skip DC operating point, use `.ic` values directly
- Default: all node voltages = 0

### Convergence Criteria

Newton iteration has converged when ALL conditions are met:
- `|V_new - V_old| < reltol * max(|V_new|, |V_old|) + vntol` for every node voltage
- `|I_new - I_old| < reltol * max(|I_new|, |I_old|) + abstol` for every branch current
- `device_converged()` returns true for all devices

### SimStatus

Every analysis result includes a `SimStatus` struct:
```cpp
struct SimStatus {
    bool converged = true;
    int iterations = 0;
    ConvergenceMethod convergence_method;  // DIRECT, GMIN_STEPPING, SOURCE_STEPPING, PSEUDO_TRANSIENT
    std::vector<std::string> warnings;
    double elapsed_seconds = 0.0;
};
```

## Project Structure

```
neospice/
├── CMakeLists.txt
├── cli/
│   └── main.cpp                       # CLI: neospice <file.cir>
├── src/
│   ├── api/
│   │   ├── neospice.hpp/cpp           # Public C++ API (Simulator class)
│   │   └── measure.hpp                # Public measurement utilities
│   ├── core/
│   │   ├── ac.hpp/cpp                 # AC small-signal frequency sweep
│   │   ├── circuit.hpp/cpp            # Circuit representation + finalization
│   │   ├── convergence.hpp/cpp        # Gmin stepping, source stepping, pseudo-transient
│   │   ├── dc.hpp/cpp                 # DC operating point + DC sweep
│   │   ├── fourier.hpp/cpp            # .four Fourier analysis
│   │   ├── freq_utils.hpp             # Frequency sweep generation (shared AC/noise)
│   │   ├── neo_solver.hpp/cpp         # NeoSolver: custom sparse/dense LU with AMD ordering + Markowitz pivoting
│   │   ├── matrix.hpp/cpp             # SparsityPattern + NumericMatrix
│   │   ├── measure.hpp/cpp            # .meas measurement execution
│   │   ├── newton.hpp/cpp             # Newton-Raphson iteration loop
│   │   ├── noise.hpp/cpp              # Noise analysis (adjoint method)
│   │   ├── pz.hpp/cpp                 # Pole-zero analysis
│   │   ├── sens.hpp/cpp               # Sensitivity analysis
│   │   ├── sim_status.hpp             # SimStatus + ConvergenceMethod enum
│   │   ├── tf.hpp/cpp                 # Transfer function analysis
│   │   ├── timestep.hpp/cpp           # Adaptive step controller (LTE)
│   │   ├── transient.hpp/cpp          # Transient analysis driver
│   │   └── types.hpp/cpp              # SimOptions, IntegratorCtx, SPICE number parsing
│   ├── devices/
│   │   ├── device.hpp                 # Base Device interface
│   │   ├── ucb_compat.hpp             # Shared UCB compatibility macros
│   │   ├── ucb_utils.hpp              # Shared UCB utility functions
│   │   ├── device_registry.hpp/cpp     # DeviceRegistry — factory dispatch for model cards, builders, parsers
│   │   ├── ucb_device_init.hpp        # Shared UCB device init template
│   │   ├── model_card_utils.hpp       # Shared model card utilities
│   │   ├── mosfet_common.hpp/cpp      # Shared MOSFET parser (prefix 'm')
│   │   ├── bjt_common.hpp/cpp         # Shared BJT parser (prefix 'q')
│   │   ├── jfet_common.hpp/cpp        # Shared JFET parser (prefix 'j')
│   │   ├── hfet_common.hpp/cpp        # Shared HFET/MES parser (prefix 'z')
│   │   ├── resistor.hpp/cpp           # + resistor_model.hpp
│   │   ├── capacitor.hpp/cpp          # + capacitor_model.hpp
│   │   ├── inductor.hpp/cpp           # + inductor_model.hpp
│   │   ├── coupled_inductor.hpp/cpp
│   │   ├── vsource.hpp/cpp
│   │   ├── isource.hpp/cpp
│   │   ├── vcvs.hpp/cpp               # + vcvs_nonlinear.hpp/cpp
│   │   ├── vccs.hpp/cpp               # + vccs_nonlinear.hpp/cpp
│   │   ├── ccvs.hpp/cpp               # + ccvs_nonlinear.hpp/cpp
│   │   ├── cccs.hpp/cpp               # + cccs_nonlinear.hpp/cpp
│   │   ├── switch.hpp/cpp             # VSwitch (S) and CSwitch (W)
│   │   ├── tline.hpp/cpp              # Ideal transmission line (T)
│   │   ├── ltra.hpp/cpp               # Lossy transmission line (O)
│   │   ├── asrc/                      # Behavioral source (B)
│   │   │   ├── asrc_device.hpp/cpp
│   │   │   └── expression_ast.hpp/cpp
│   │   ├── dio/                       # Diode (auto-migrated)
│   │   ├── bjt/                       # BJT (Gummel-Poon)
│   │   ├── vbic/                      # VBIC
│   │   ├── jfet/                      # JFET
│   │   ├── jfet2/                     # JFET2
│   │   ├── hfet1/                     # HFET1
│   │   ├── hfet2/                     # HFET2
│   │   ├── mos1/                      # MOS1 (Level 1)
│   │   ├── mos3/                      # MOS3 (Level 3)
│   │   ├── mos9/                      # MOS9 (Level 9)
│   │   ├── bsim3/                     # BSIM3
│   │   ├── bsim3v32/                  # BSIM3v3.2
│   │   ├── bsim4v7/                   # BSIM4v7
│   │   ├── bsimsoi/                   # BSIMSOI
│   │   ├── hisim2/                    # HiSIM2
│   │   └── hisimhv/                   # HiSIM_HV
│   ├── output/
│   │   ├── output.hpp/cpp             # Output formatting
│   │   ├── raw_writer.hpp/cpp         # .raw binary output (ngspice-compatible)
│   │   └── vectors.hpp                # Result vector types
│   └── parser/
│       ├── tokenizer.hpp/cpp          # SPICE tokenizer (continuations, suffixes)
│       ├── expression.hpp/cpp         # .param expression evaluator (full arithmetic + functions)
│       ├── model_cards.hpp/cpp        # .model statement parsing
│       ├── subcircuit.hpp             # .subckt definition storage
│       ├── subcircuit_expand.hpp/cpp  # X instance recursive flattening
│       └── netlist_parser.hpp/cpp     # Two-pass netlist parser
├── tools/
│   ├── ngspice_migrate/               # Auto-migration tool (Python)
│   │   ├── __main__.py                #   CLI entry point
│   │   ├── descriptor.py              #   YAML descriptor loader
│   │   ├── transformer.py             #   8-pass C→C++ translation pipeline
│   │   ├── gen_def.py                 #   *_def.hpp generator
│   │   ├── gen_shim.py                #   *_shim.hpp/cpp generator
│   │   ├── gen_adapter.py             #   *_device.hpp/cpp generator
│   │   ├── gen_model_card.py          #   *_model_card.hpp/cpp generator
│   │   ├── gen_parser.py              #   *_parser.hpp generator
│   │   ├── gen_cmake.py               #   CMakeLists.txt generator
│   │   ├── gen_test.py                #   Test scaffolding generator
│   │   └── validation.py              #   Migration validation
│   ├── descriptors/                   #   17 device model descriptors
│   │   ├── asrc.yaml
│   │   ├── bjt.yaml
│   │   ├── bsim3.yaml
│   │   ├── bsim3v32.yaml
│   │   ├── bsim4v7.yaml
│   │   ├── bsimsoi.yaml
│   │   ├── dio.yaml
│   │   ├── hfet1.yaml
│   │   ├── hfet2.yaml
│   │   ├── hisim2.yaml
│   │   ├── hisimhv.yaml
│   │   ├── jfet.yaml
│   │   ├── jfet2.yaml
│   │   ├── ltra.yaml
│   │   ├── mos1.yaml
│   │   ├── mos3.yaml
│   │   ├── mos9.yaml
│   │   └── vbic.yaml
│   └── tests/                         #   Python test suite (14 files, 173 tests)
├── tests/
│   ├── framework/
│   │   ├── ngspice_runner.hpp/cpp     # Runs ngspice, parses .raw output
│   │   └── comparator.hpp/cpp         # Vector comparison with tolerances
│   ├── circuits/                      # 123 golden circuit netlists
│   ├── unit/                          # Unit + integration tests
│   ├── devices/                       # Device comparison tests
│   └── bench/                         # Benchmarks
└── third_party/                       # BSIM4v7 reference, KiCad SPICE models
```

## Public API

```cpp
namespace neospice {

// --- Analysis result variant ---
using AnalysisResult = std::variant<std::monostate,
    DCResult, TransientResult, ACResult,
    DCSweepResult, NoiseResult, TFResult,
    SensResult, PZResult>;

struct SimulationResult {
    AnalysisResult analysis;
    std::optional<MeasureResult> measures;
    std::vector<std::string> print_output;
    std::unique_ptr<StepResult> step;       // non-null when .step sweep ran
};

struct StepResult {
    std::vector<double> step_values;
    std::string step_variable;
    std::vector<SimulationResult> results;
};

// --- Options ---
struct SimulatorOptions {
    double abstol = 1e-12;
    double reltol = 1e-3;
    double vntol  = 1e-6;
    double trtol  = 7.0;
    double gmin   = 1e-12;
};

struct TransientOptions {
    const DCResult* ic_from = nullptr;  // chain DC → transient
    bool uic = false;
};

struct ACOptions {
    const DCResult* op_from = nullptr;  // chain DC → AC
};

// --- Simulator ---
class Simulator {
public:
    using Options = SimulatorOptions;

    explicit Simulator(Options opts = Options{});

    Circuit load(const std::string& filepath);
    Circuit parse(const std::string& netlist_text);

    DCResult      run_dc(Circuit& ckt);
    TransientResult run_transient(Circuit& ckt, double tstep, double tstop);
    TransientResult run_transient(Circuit& ckt, double tstep, double tstop,
                                  const TransientOptions& opts);
    ACResult      run_ac(Circuit& ckt, ACMode mode, int npoints,
                         double fstart, double fstop);
    ACResult      run_ac(Circuit& ckt, ACMode mode, int npoints,
                         double fstart, double fstop, const ACOptions& opts);
    DCSweepResult run_dc_sweep(Circuit& ckt, const std::vector<DCSweepParam>& params);
    NoiseResult   run_noise(Circuit& ckt, const std::string& output_node,
                            const std::string& input_src, ACMode mode,
                            int npoints, double fstart, double fstop);
    TFResult      run_tf(Circuit& ckt, const std::string& output_var,
                         const std::string& input_src);
    SensResult    run_sens(Circuit& ckt, const std::string& output_var);

    SimulationResult run(Circuit& ckt);
    SimulationResult run_step_sweep(Circuit& ckt);
};

// --- Result types (all include SimStatus) ---

struct DCResult {
    std::map<std::string, double> node_voltages;
    std::map<std::string, double> branch_currents;
    double voltage(const std::string& node) const;
    double current(const std::string& dev) const;
    double diff(const std::string& node_p, const std::string& node_n) const;
    std::vector<std::string> signal_names() const;
    SimStatus status;
};

struct TransientResult {
    std::vector<double> time;
    std::map<std::string, std::vector<double>> voltages;
    std::map<std::string, std::vector<double>> currents;
    int rejected_steps = 0;
    const std::vector<double>& voltage(const std::string& node) const;
    const std::vector<double>& current(const std::string& dev) const;
    std::vector<double> diff(const std::string& p, const std::string& n) const;
    std::vector<std::string> signal_names() const;
    SimStatus status;
};

struct ACResult {
    std::vector<double> frequency;
    std::map<std::string, std::vector<std::complex<double>>> voltages;
    std::map<std::string, std::vector<std::complex<double>>> currents;
    const std::vector<std::complex<double>>& voltage(const std::string& node) const;
    const std::vector<std::complex<double>>& current(const std::string& dev) const;
    std::vector<double> magnitude_db(const std::string& node) const;
    std::vector<double> phase_deg(const std::string& node) const;
    std::vector<double> magnitude(const std::string& node) const;
    std::vector<std::complex<double>> diff(const std::string& p, const std::string& n) const;
    std::vector<double> diff_magnitude_db(const std::string& p, const std::string& n) const;
    std::vector<double> current_magnitude_db(const std::string& dev) const;
    std::vector<double> current_phase_deg(const std::string& dev) const;
    std::vector<double> current_magnitude(const std::string& dev) const;
    std::vector<std::string> signal_names() const;
    SimStatus status;
};

struct DCSweepResult {
    std::string sweep_var;
    std::vector<double> sweep_values;
    std::map<std::string, std::vector<double>> voltages;
    std::map<std::string, std::vector<double>> currents;
    const std::vector<double>& voltage(const std::string& node) const;
    const std::vector<double>& current(const std::string& dev) const;
    std::vector<double> diff(const std::string& p, const std::string& n) const;
    std::vector<std::string> signal_names() const;
    SimStatus status;
};

struct NoiseResult {
    std::vector<double> frequency;
    std::vector<double> output_noise_density;  // V²/Hz
    std::vector<double> input_noise_density;   // V²/Hz
    std::map<std::string, std::vector<double>> device_noise;
    std::vector<double> output_noise_sqrt() const;
    std::vector<double> input_noise_sqrt() const;
    double integrated_output_noise(double fmin, double fmax) const;
    double integrated_input_noise(double fmin, double fmax) const;
    std::vector<std::string> device_names() const;
    const std::vector<double>& device_noise_density(const std::string& name) const;
    std::vector<std::string> signal_names() const;
    SimStatus status;
};

struct TFResult {
    std::string output_var, input_src;
    double transfer_function;
    double input_impedance;   // Ohms
    double output_impedance;  // Ohms
    SimStatus status;
};

struct SensResult {
    std::string output_var;
    double output_value;
    struct Entry {
        std::string element, parameter;
        double sensitivity, normalized;
    };
    std::vector<Entry> entries;
    SimStatus status;
};

struct PZResult {
    std::vector<std::complex<double>> poles, zeros;
    PZType type;              // POLES, ZEROS, BOTH
    PZTransferType transfer;  // VOLTAGE, CURRENT
    SimStatus status;
};

struct FourierResult {
    std::string signal_name;
    double fundamental_freq;
    std::vector<FourierComponent> components;  // DC + 9 harmonics
    double thd;  // Total Harmonic Distortion (%)
};

struct MeasureResult {
    std::unordered_map<std::string, double> values;
};

enum class ACMode { DEC, OCT, LIN };

// --- Circuit introspection ---
struct DeviceInfo {
    std::string name, type;
    std::vector<std::string> nodes;
    std::optional<double> value;
};

class Circuit {
    // ...
    std::vector<std::string> node_names() const;
    std::vector<std::string> device_names() const;
    DeviceInfo device_info(const std::string& name) const;
    std::vector<std::string> devices_at_node(const std::string& node) const;
    Device* find_device(const std::string& name);
    bool set_param(const std::string& device_name, double value);
};

// --- Circuit typed device methods (programmatic construction) ---
// Each method returns a DevId handle for the created device.
//   ckt.V("V1", in, GND, 0.0, 1.0);   // DC=0, AC=1
//   ckt.R("R1", in, out, 1e3);
//   ckt.C("C1", out, GND, 100e-12);
//   ckt.E("E1", out, gnd, in, gnd, 2.0);  // VCVS
//   auto v1 = ckt.V("V1", in, GND, 1.0);
//   ckt.F("F1", np, nn, v1, 0.5);         // CCCS
// Also: G(), H(), L(), K(), I(), D(), Q(), M(), add_dev()

} // namespace neospice
```

### Analysis Command Precedence

1. API parameters take precedence (explicit caller intent)
2. Netlist `.tran`/`.ac`/`.dc` commands are defaults when API parameters are not specified
3. `run()` uses only netlist commands; `run_transient()` etc. use API parameters with netlist as fallback

### Analysis Chaining

```cpp
neospice::Simulator sim;
auto ckt = sim.parse(netlist);
auto dc = sim.run_dc(ckt);                    // DC operating point
auto ac = sim.run_ac(ckt, ACMode::DEC, 10,    // AC from custom DC
                     1, 1e6, ACOptions{&dc});
auto tran = sim.run_transient(ckt, 1e-9, 1e-6,
                              TransientOptions{&dc});  // transient from custom DC
```

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

**17 device descriptors** currently available: asrc, bjt, bsim3, bsim3v32, bsim4v7, bsimsoi, dio, hfet1, hfet2, hisim2, hisimhv, jfet, jfet2, ltra, mos1, mos3, mos9, vbic.

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
   - `*_model_card.hpp/cpp` — model card with parameter table
   - `*_parser.hpp` — parser integration header
   - `CMakeLists.txt`
   - Test scaffolding (AC/noise/DC/transient test circuits)
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
2. Run the migration tool — generates device, model card, and factory files
3. Copy generated files to `src/devices/<model>/`
4. Create `<model>_factory.cpp` registering model card factory + device builder with `DeviceRegistry`
5. Add one `register_<model>(*this)` call in `DeviceRegistry::register_all()`
6. Add test circuits comparing against ngspice

No changes to `circuit_typed.cpp` or `netlist_parser.cpp` are needed — the registry
dispatches model card creation, device construction, and element parsing automatically
for existing prefixes (M, D, Q, J, Z).

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
- Noise analysis: spectral density relative tolerance

### Transient Comparison

Both simulators may use different adaptive time steps. Comparison interpolates NEOSPICE results onto ngspice's time grid (ngspice is ground truth) and checks tolerance at every reference point. This validates functional equivalence (same waveform shape), not numerical identity.

## Build System

CMake 3.20+ with C++20. CUDA support is optional and not yet enabled.

### Dependencies

| Dependency | Purpose | License | Required? |
|------------|---------|---------|-----------|
| OpenBLAS | Dense BLAS for NeoSolver | BSD-3 | Yes |
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

Deferred until profiling on large circuits shows device evaluation is the dominant cost for circuits with 500+ MOSFETs. The design is GPU-ready:

- SparsityPattern/NumericMatrix separation allows GPU solver views without changing stamping code
- Device evaluation is embarrassingly parallel (one kernel thread per instance)
- State arrays are contiguous (easy to transfer host↔device)

**GPU Phase 1:** Custom CUDA kernels for batched BSIM4v7 evaluation, NeoSolver stays on CPU.
**GPU Phase 2:** cusolverRF for sparse refactorization if solver is bottleneck.
**GPU Phase 3:** cuDSS for full GPU-resident factorization on large circuits.

Each phase gated by profiling data.

## Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| **Auto-migration tool gaps** — new device models may have patterns the transformer doesn't handle | Build errors on migrated code | Tool is extensible (add regex passes). 17 models successfully migrated. |
| **Convergence differences** — different stepping/limiting than ngspice may produce different paths | Tests fail due to convergence, not math | Match ngspice's convergence aids. Accept wider tolerances on sensitive circuits. |
| **GPU break-even uncertainty** — circuit matrices may be too small for GPU advantage | GPU investment without speedup | Defer GPU until profiling on real circuits. CPU path is the product. |
| **PDK netlist compatibility** — real PDKs use complex `.lib`/`.param`/`.subckt` patterns | Can't run production netlists | Subcircuit flattening, expression evaluator, and `.lib` section selection all implemented. |
| **Linear solver accuracy** | Sparse LU on ill-conditioned matrices | Custom NeoSolver with AMD ordering, maximum transversal, and Markowitz pivoting. No external LGPL dependencies. |

## Resolved Design Decisions

| # | Decision | Rationale |
|---|----------|-----------|
| 1 | Strict documented subset, not full ngspice compat | Clear scope prevents unbounded parser work |
| 2 | Correctness first, GPU later | Fast wrong answers are useless |
| 3 | SparsityPattern + NumericMatrix + views | Decouples stamping from solver format |
| 4 | V-source currents always; others opt-in via `.save` | Matches ngspice default behavior |
| 5 | Fixed-step first, adaptive layered on | Simpler to debug; adaptive added in M1.5 |
| 6 | Auto-migration tool for ngspice models | Hand-porting is error-prone; tool used for 17 models |
| 7 | Flatten subcircuits (no hierarchy preservation) | Simpler implementation; matches ngspice behavior |
| 8 | CPU-only until profiling justifies GPU | Avoids premature optimization; GPU design is preserved in architecture |
| 9 | `std::variant`-based result types | Type-safe multi-analysis dispatch without `dynamic_cast` |
| 10 | `SimStatus` embedded in every result | Convergence diagnostics always available without separate query |
| 11 | Adjoint method for noise analysis | Efficient for single-output noise; matches ngspice approach |
