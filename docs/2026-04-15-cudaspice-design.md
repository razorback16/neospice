# CudaSPICE Design Specification

**Date:** 2026-04-15
**Status:** Draft
**Location:** `/Users/subhagato/Development/ideas/circuit-design/cudaspice/`

## Overview

CudaSPICE is a GPU-accelerated SPICE circuit simulator written in C++ with CUDA. It uses ngspice as ground truth for correctness testing during development. The project is designed for future integration with the `circuit-cpp` framework.

## Goals

1. **Correctness first** — every feature validated against ngspice before moving to the next milestone
2. **GPU-accelerated simulation** — CUDA for device evaluation and sparse linear algebra
3. **Fast CPU fallback** — KLU + OpenBLAS + OpenMP + SLEEF when no GPU is available
4. **Integration with circuit-cpp** — clean C++ API consumable by `circuit-cpp`'s `SpiceExporter`

## Milestones

### Milestone 1: CPU-Only Correctness (Phase 1 Devices)

The first milestone is a CPU-only simulator that passes all ngspice comparison tests for Phase 1 devices (R, C, L, V, I, Diode). No GPU code. No BSIM4v7. The goal is to prove that the core simulation engine — parser, MNA matrix assembly, KLU solver, Newton-Raphson with convergence aids, and fixed-step transient — produces correct results.

**Deliverables:**
- Parser handling the documented SPICE subset
- MNA matrix assembly with sparsity pattern / numeric value separation
- KLU-based sparse LU solver (CPU)
- Newton-Raphson with voltage limiting, gmin stepping, source stepping
- DC operating point analysis
- Fixed-step transient analysis (trapezoidal integration)
- AC small-signal analysis
- Test harness with ngspice comparison
- All Phase 1 device tests passing

### Milestone 1.5: Adaptive Transient

Add adaptive time stepping on top of Milestone 1's fixed-step transient:
- LTE-based step size control
- Step rejection and retry with smaller dt
- Breakpoint detection for PULSE/SIN source events
- Gear integration method as alternative to trapezoidal

### Milestone 2: BSIM4v7 + GPU Device Evaluation

Add BSIM4v7 MOSFET model (CPU first, then CUDA kernel). This is the GPU acceleration proof point.

**Deliverables:**
- BSIM4v7 device model (CPU implementation)
- BSIM4v7 ngspice comparison tests passing
- CUDA kernel for batched BSIM4v7 evaluation
- GPU/CPU performance comparison on representative circuits

### Milestone 3: GPU Solver (If Profiling Warrants)

Add GPU sparse solve only if profiling on Milestone 2 circuits shows the solver is the bottleneck. See GPU Phased Introduction below.

## Scope

### Supported Subset

CudaSPICE does **not** aim for full ngspice compatibility. It supports a defined subset of SPICE syntax and device models. Netlists that use only supported features will produce equivalent results in both simulators. Netlists using unsupported features (e.g., behavioral sources, XSPICE, `.measure`, subcircuits beyond Phase 2) will fail with a clear parser error listing the unsupported construct.

### Analyses Supported

| Analysis | Description |
|----------|-------------|
| DC operating point | Newton-Raphson solve for steady-state with convergence aids (see Convergence Strategy) |
| Transient | Time-domain simulation: fixed-step first (M1), adaptive time stepping second (M1.5). Trapezoidal integration, Gear as M1.5 addition. |
| AC small-signal | Frequency sweep using linearized device Jacobians and charge derivatives (see Device Interface) |

### Device Models

**Phase 1 — Basic passives and sources:**
- Resistor (linear)
- Capacitor (linear)
- Inductor (linear)
- Voltage source (DC, AC, PULSE, SIN)
- Current source (DC, AC, PULSE, SIN)
- Diode (Shockley equation: I = Is*(exp(V/nVt) - 1), with junction capacitance for AC)

**Phase 2 — Semiconductor:**
- BSIM4v7 MOSFET (the primary GPU acceleration target)

### Netlist Format and Parser Scope

Standard SPICE netlist format (`.cir` / `.spice`). The parser must handle the following constructs to support the declared device set:

**Required SPICE constructs:**
- Element instance lines (R, C, L, V, I, D, M)
- `.model` statements (for diode and MOSFET parameters)
- `.param` with numeric expressions and parameter substitution
- `.include` and `.lib` (file inclusion, library section selection)
- `.ic` and `.nodeset` (initial conditions)
- `.tran`, `.ac`, `.dc`, `.op` (analysis commands)
- `.save` and `.print` (output variable selection)
- `.options` (simulation options: `reltol`, `abstol`, `vntol`, `gmin`, `temp`, etc.)
- Numeric suffixes (`k`, `meg`, `u`, `n`, `p`, `f`, etc.)
- Line continuations (`+` prefix)
- Comments (`*` prefix, `$` inline)
- Node name mapping (alphanumeric node names, `0` as ground)

**Explicitly NOT supported (Phase 1-2):**
- `.subckt` / `.ends` (subcircuit definitions) — deferred to Phase 3
- Behavioral sources (`B` element, `VALUE=` expressions)
- XSPICE (`.model` with `code_model`)
- `.measure`, `.four`, `.noise`, `.sens`, `.pz`
- Controlled sources (E, F, G, H) — deferred to Phase 3
- `.temp` sweep, `.step` parameter sweep

Unsupported constructs produce a clear error at parse time listing the line number and construct.

## Architecture

### Approach: Hybrid CPU/GPU

The Newton-Raphson iteration has three phases with different computational characteristics. The GPU path accelerates device evaluation and sparse solve. Matrix stamping remains on CPU due to irregular scatter patterns. Control flow (convergence, time stepping) is always CPU.

**Ownership model:** When CUDA is available and the circuit exceeds the size threshold (configurable, default 500+ nodes), the GPU path handles device evaluation and sparse solve. Below threshold or without CUDA, the CPU path handles everything. There is no mixed mode where device eval is on GPU and solve is on CPU within the same simulation — this avoids the marshaling cost of moving results back and forth per Newton iteration.

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
     | GPU mode?        |        |          | GPU mode?        |
     |-- Yes: CUDA kern |        |          |-- Yes: cuDSS /   |
     |-- No:  OpenMP +  |        |          |        cusolverRF|
     |        SLEEF SIMD|        |          |-- No:  KLU +     |
     +--------+---------+        |          |        OpenBLAS  |
              |                  |          +--------+---------+
              v                  v                   v
         (conductances,    (stamp into       (solve Ax=b,
          charges,          matrix - always   update x)
          currents)         CPU)
```

### Data Movement Strategy (GPU Mode)

On NVIDIA GPUs with CUDA, host and device have separate memory. The data movement budget per Newton iteration is:

**Persistent on device (allocated once, reused across iterations):**
- Device instance parameter arrays (Vth0, toxe, W, L, etc.) — read-only, uploaded once at simulation start
- CSC matrix structure arrays (column pointers, row indices) — uploaded once after symbolic analysis
- Matrix value array — updated each iteration
- RHS vector — updated each iteration
- Solution vector — updated each iteration

**Copied host→device each Newton iteration:**
- Node voltage vector (solution from previous iteration) — size: N doubles (N = number of nodes)
- State vector updates from time integration — size: proportional to device count

**Copied device→host each Newton iteration:**
- Device evaluation results (conductances, currents, charges) — these are written directly into the matrix value array on device if both eval and solve are GPU-side
- Solution vector (for convergence check on CPU) — size: N doubles

**Why this is worth it:** When both device eval and solve run on GPU, the only per-iteration host↔device transfer is the solution vector (N doubles for convergence check) and any time-stepping state. The matrix value array stays on device — device eval writes conductances into it, and cuDSS/cusolverRF reads it. For circuits with 1000+ nodes and many devices, the compute savings dominate the transfer cost.

**When it's NOT worth it:** Small circuits (< 500 nodes) where the transfer overhead and kernel launch latency exceed the compute savings. This is why the size threshold exists.

### GPU Path (CUDA) — Phased Introduction

The GPU libraries are introduced incrementally, not all at once:

**GPU Phase 1 (with Phase 2 devices — BSIM4v7):**
- Custom CUDA kernels for batched BSIM4v7 evaluation
- KLU remains on CPU for solve (the device eval is the bigger win)

**GPU Phase 2 (when profiling shows solve is the bottleneck):**
- cusolverRF for sparse refactorization (reuses symbolic pattern from KLU)
- cuSPARSE for SpMV if needed in iterative refinement

**GPU Phase 3 (large circuits, if needed):**
- cuDSS for full GPU-resident symbolic + numeric factorization
- cuBLAS batched for dense BTF diagonal blocks

Each phase is gated by profiling data showing the next bottleneck. We do not add GPU solver complexity until device eval on GPU has been proven and the solver is measured as the dominant cost.

### CPU Path (Optimized)

| Component | Library | What it optimizes |
|-----------|---------|-------------------|
| Sparse LU solver | KLU (SuiteSparse) | Fill-reducing ordering (AMD/COLAMD) + sparse factorization. KLU itself is scalar C code — it does not use SIMD internally. |
| Dense BLAS (called by KLU for dense diagonal blocks) | OpenBLAS | SIMD-optimized dense matrix ops (GEMM, TRSM). AVX2/512 on x86, NEON on Apple Silicon. Multi-threaded. |
| Parallel device eval | OpenMP | Distributes independent device evaluations across CPU cores |
| SIMD transcendentals (in device eval) | SLEEF | Vectorized exp, log, sqrt, tanh, pow. AVX2/512 on x86, NEON on Apple Silicon. |

Clarification: KLU's sparse factorization is not itself SIMD-accelerated. OpenBLAS accelerates the dense sub-operations that KLU delegates to BLAS (primarily within dense diagonal blocks from BTF decomposition). For typical circuit matrices, these dense blocks are small (10-100 rows), so the BLAS contribution is modest. The primary CPU optimization opportunity is in device evaluation (OpenMP + SLEEF).

### Sparse Matrix Abstraction

The matrix system separates **structure** (sparsity pattern) from **values** (numeric entries), allowing different solver backends to work with their preferred format without the core simulation code caring.

**SparsityPattern** (computed once during symbolic analysis):
- Built from device `stamp_pattern()` calls during circuit setup
- Stores the structural non-zero positions as a sorted triplet list
- Immutable after construction — the pattern does not change across Newton iterations or time steps

**NumericMatrix** (updated every Newton iteration):
- Holds only the `values[nnz]` array, indexed by position within the pattern
- Devices stamp into it by pre-assigned offsets (e.g., "your conductance goes at values[47]")
- Cleared and re-stamped each Newton iteration

**Backend-specific views** (created once per simulation, wrap pattern + values):
- `KLUSolverView`: converts pattern to CSC arrays (`col_ptr`, `row_idx`) that KLU expects. The values array is shared by pointer — no copy.
- `CuSolverView` (future): converts pattern to whatever cusolverRF/cuDSS needs. May hold device-side copies of structure arrays.
- Each view is constructed once from the pattern and reuses it. Only the values array changes per iteration.

**Why CSC is the primary backend format:**
- KLU is column-oriented (left-looking LU factorization) and requires CSC
- cuDSS accepts CSC natively
- cusolverRF accepts CSC natively
- cuSPARSE supports both; CSC↔CSR is a metadata swap (transpose) with no data copy

**Why the abstraction matters:** If a future backend needs CSR or a blocked format, only a new view class is needed. The core stamping code, device interface, and Newton-Raphson loop are unaffected. The cost of this abstraction is one level of indirection at stamp time (offset lookup), which is negligible compared to device evaluation cost.

Device stamp functions see only the `NumericMatrix` API:
```cpp
// During symbolic analysis (once):
device.stamp_pattern(builder);  // registers (row, col) positions
auto pattern = builder.build(); // returns SparsityPattern
auto offsets = pattern.get_offsets(device); // pre-computed indices

// During Newton iteration (every iteration):
matrix.clear();
device.evaluate(voltages, stamps);
// stamps internally does: matrix.values[offsets.g_diag] += conductance;
```

## Convergence Strategy

Newton-Raphson alone is insufficient for nonlinear circuits. The following convergence aids are implemented, matching ngspice's approach:

### Voltage Limiting

Before each device evaluation, node voltages are clamped to prevent unrealistic jumps that cause numerical overflow. For diodes and MOSFETs:
- Junction voltage limited to prevent exp() overflow: `Vnew = Vold + clamp(Vnew - Vold, -Vcrit, +Vcrit)` where `Vcrit = nVt * ln(nVt / (sqrt(2) * Is))`
- Gate voltage limiting for MOSFETs to prevent unrealistic inversion predictions

### Gmin Stepping

If Newton-Raphson fails to converge at the DC operating point:
1. Add a conductance `gmin` (default 1e-12) from each node to ground
2. If still not converging, increase gmin to `gmin_start` (default 1e-2)
3. Solve, then reduce gmin by factor of 10 each step
4. Use each converged solution as the initial guess for the next gmin value
5. Final solve at `gmin = 1e-12` (effectively zero)

### Source Stepping

If gmin stepping fails:
1. Scale all independent sources to 0
2. Solve (trivial — all zeros)
3. Gradually increase source scaling from 0 to 1 in steps
4. Use each converged solution as initial guess for the next step

### Initial Conditions

- `.ic V(node)=value` — sets initial transient conditions (applied at t=0)
- `.nodeset V(node)=value` — provides initial guess for DC operating point (not enforced)
- Default initial guess: all node voltages = 0

### Convergence Criteria

A Newton iteration has converged when ALL of the following are satisfied:
- `|V_new - V_old| < reltol * max(|V_new|, |V_old|) + vntol` for every node voltage
- `|I_new - I_old| < reltol * max(|I_new|, |I_old|) + abstol` for every branch current
- No device flags non-convergence (e.g., limiting was active)

## Device Interface Contract

Every device model implements the following interface:

```cpp
class Device {
public:
    // --- DC and Transient ---

    // Evaluate device at current operating point.
    // Reads node voltages from the solution vector.
    // Writes: conductance stamps, current contributions, charge values.
    virtual void evaluate(const SolutionVector& voltages,
                          DeviceStamps& stamps) = 0;

    // Apply voltage limiting before evaluation.
    // Modifies proposed voltages to prevent numerical overflow.
    virtual void limit_voltages(const SolutionVector& old_voltages,
                                SolutionVector& new_voltages) = 0;

    // --- AC small-signal (linearization at DC operating point) ---

    // Return the small-signal conductance matrix (dI/dV Jacobian)
    // and capacitance matrix (dQ/dV) for this device at the current
    // DC operating point. These are used to build G + jwC.
    virtual void ac_stamps(ACStamps& stamps) const = 0;

    // --- Metadata ---

    // Which matrix positions does this device stamp into?
    // Called once during symbolic analysis to build the sparsity pattern.
    virtual void stamp_pattern(SparsityBuilder& builder) const = 0;

    // First-class output currents for this device.
    virtual std::vector<std::string> output_currents() const = 0;
};
```

**AC linearization detail:** After DC operating point converges, each nonlinear device computes its linearized small-signal parameters:
- **Diode:** `gd = dI/dV = (Is/nVt) * exp(Vd/nVt)` (conductance), `Cd = dQ/dV` (junction + diffusion capacitance)
- **BSIM4v7:** Full Jacobian matrix (gm, gds, gmb, Cgs, Cgd, Cgb, etc.) — computed as a byproduct of the DC evaluation

These are stored after DC convergence and reused for all AC frequency points (the linearization point doesn't change during AC sweep).

**Output currents — always available vs. opt-in:**

*Always available (solution variables in MNA):*
- Voltage sources: `I(Vname)` — current is a solution variable, always computed

*Available on request (via `.save` in netlist or API):*
- Current sources: `I(Iname)` — trivially the source value, stored if requested
- Diodes: `I(Dname)` — computed during device evaluation, stored if requested
- MOSFETs: `Id(Mname)`, `Ig(Mname)`, `Is(Mname)` — computed during evaluation, stored if requested

*Not available as direct outputs:*
- Passive elements (R, C, L): current is derived from terminal voltage difference and impedance, not stored as a solution variable. To measure current through a passive, the user inserts a 0V voltage source in series, per standard SPICE convention.

If no `.save` statement is present, the default is to save all node voltages and all voltage source currents (matching ngspice behavior). If `.save` is present, only the listed signals are stored.

## Project Structure

```
cudaspice/
├── CMakeLists.txt
├── src/
│   ├── core/                   # Simulation engine
│   │   ├── circuit.hpp/cpp           # Circuit representation (netlist in memory)
│   │   ├── matrix.hpp/cpp            # SparsityPattern + NumericMatrix + backend views
│   │   ├── solver.hpp/cpp            # Sparse LU solver (dispatches CPU/GPU)
│   │   ├── newton.hpp/cpp            # Newton-Raphson iteration loop
│   │   ├── convergence.hpp/cpp       # Gmin stepping, source stepping, limiting
│   │   ├── transient.hpp/cpp         # Transient analysis (time stepping)
│   │   ├── dc.hpp/cpp                # DC operating point
│   │   └── ac.hpp/cpp                # AC small-signal analysis
│   ├── devices/                # Device models
│   │   ├── device.hpp                # Base device interface (evaluate, limit, ac_stamps)
│   │   ├── resistor.hpp/cpp
│   │   ├── capacitor.hpp/cpp
│   │   ├── inductor.hpp/cpp
│   │   ├── vsource.hpp/cpp
│   │   ├── isource.hpp/cpp
│   │   ├── diode.hpp/cpp
│   │   └── bsim4v7/               # Full BSIM4v7 model
│   │       ├── bsim4v7.hpp/cpp
│   │       └── bsim4v7.cu          # CUDA kernel for batched eval
│   ├── parser/                 # SPICE netlist parser
│   │   ├── netlist_parser.hpp/cpp    # Recursive descent parser
│   │   ├── spice_tokens.hpp          # Tokenizer (handles continuations, suffixes)
│   │   ├── expression.hpp/cpp        # .param expression evaluator
│   │   └── model_cards.hpp/cpp       # .model statement parser
│   ├── gpu/                    # CUDA acceleration layer
│   │   ├── gpu_context.hpp/cpp       # CUDA init, memory management
│   │   ├── device_eval.cu          # Batched device evaluation kernel
│   │   └── sparse_solve.cu         # cusolverRF/cuDSS wrapper
│   ├── output/                 # Results
│   │   ├── vectors.hpp/cpp           # Simulation result vectors
│   │   └── raw_writer.hpp/cpp        # .raw file output (ngspice compatible)
│   └── api/                    # Public C++ API
│       └── cudaspice.hpp/cpp
├── cli/
│   └── main.cpp                # Thin CLI wrapper
├── tests/
│   ├── CMakeLists.txt
│   ├── framework/
│   │   ├── ngspice_runner.hpp/cpp    # Runs ngspice, parses output
│   │   └── comparator.hpp/cpp       # Vector comparison with tolerances
│   ├── circuits/                   # Test netlists (.cir files)
│   │   ├── resistor_divider.cir
│   │   ├── rc_transient.cir
│   │   ├── rlc_ac.cir
│   │   ├── diode_iv.cir
│   │   ├── inverter_bsim4.cir
│   │   └── ring_osc_bsim4.cir
│   └── tests/
│       ├── test_parser.cpp
│       ├── test_dc.cpp
│       ├── test_transient.cpp
│       ├── test_ac.cpp
│       └── test_bsim4v7.cpp
└── third_party/                # External dependencies
```

## Public API

```cpp
namespace cudaspice {

class Simulator {
public:
    struct Options {
        bool use_gpu = true;          // Falls back to CPU if CUDA unavailable
        int gpu_threshold = 500;      // Min nodes to use GPU path
        int max_threads = 0;          // 0 = auto-detect core count
        double abstol = 1e-12;        // Absolute current tolerance
        double reltol = 1e-3;         // Relative tolerance
        double vntol = 1e-6;          // Voltage tolerance
        double trtol = 7.0;           // Transient error tolerance
        double gmin = 1e-12;          // Minimum conductance
    };

    explicit Simulator(Options opts = {});
    ~Simulator();

    // Load a SPICE netlist from file or string.
    // Parses the netlist and extracts analysis commands.
    // Throws ParseError listing unsupported constructs with line numbers.
    Circuit load(const std::string& filepath);
    Circuit parse(const std::string& netlist_text);

    // Run analyses.
    // If the Circuit contains analysis commands (.tran, .ac, etc.),
    // these API parameters override them. If no analysis commands
    // exist in the netlist and no API parameters are given, throws.
    DCResult run_dc(const Circuit& ckt);
    TransientResult run_transient(const Circuit& ckt, double tstep, double tstop);
    ACResult run_ac(const Circuit& ckt, ACMode mode, int npoints, double fstart, double fstop);

    // Run all analyses declared in the netlist.
    // Returns results keyed by analysis type.
    SimulationResult run(const Circuit& ckt);
};

// --- Result types ---
// Signal names follow ngspice convention:
//   Node voltages: "v(nodename)" (lowercase)
//   Branch currents: "i(vsourcename)" for voltage sources,
//                    "id(mname)" for MOSFET drain, etc.
// Internally indexed by integer for performance; string map is the public API.

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

// Combined result from run()
struct SimulationResult {
    std::optional<DCResult> dc;
    std::optional<TransientResult> transient;
    std::optional<ACResult> ac;
};

} // namespace cudaspice
```

### Analysis Command Precedence

When both the netlist and API specify analysis parameters:
1. API parameters take precedence (explicit caller intent)
2. Netlist `.tran`/`.ac`/`.dc` commands are used as defaults when API parameters are not specified
3. `run()` uses only netlist commands; `run_transient()` etc. use API parameters with netlist as fallback

This allows circuit-cpp to call `sim.run(ckt)` (use whatever the netlist says) or `sim.run_transient(ckt, 1e-6, 1e-3)` (override explicitly).

### Signal Naming Convention

Signal names in result maps follow ngspice convention for interoperability:
- `"v(net1)"` — node voltage (lowercase `v`, node name preserved)
- `"i(v1)"` — current through voltage source V1
- `"id(m1)"` — drain current of MOSFET M1

Internally, the simulator uses integer indices for all vectors. The string-keyed maps are constructed at result return time. For performance-critical paths (e.g., comparing millions of time points), a future API extension may expose index-based access.

### Integration with circuit-cpp

The `parse(const std::string&)` method is the integration point. circuit-cpp's `SpiceExporter` generates netlist strings, which are passed directly to CudaSPICE:

```cpp
// In circuit-cpp (future integration layer)
SpiceExporter exporter;
exporter.auto_export(my_circuit);       // Circuit object → SPICE netlist
std::string netlist = exporter.to_string();

cudaspice::Simulator sim;
auto ckt = sim.parse(netlist);          // No temp files needed
auto result = sim.run(ckt);             // Run analyses from netlist commands

// Or override analysis parameters explicitly:
auto tran = sim.run_transient(ckt, 1e-6, 1e-3);
```

The standard SPICE netlist format is the interface contract. circuit-cpp must only generate constructs from the supported subset. The SpiceExporter already maps to basic passives and `.model` statements, which aligns with our Phase 1-2 device set.

## Test Harness

### Strategy

Every CudaSPICE feature is validated against ngspice output. The test harness:

1. Runs a `.cir` netlist through ngspice (via CLI subprocess), parses the `.raw` output
2. Runs the same `.cir` through CudaSPICE's C++ API
3. Compares node voltages and declared branch currents within configurable tolerance

**What is compared:**
- All node voltages
- Branch currents from voltage sources (`I(Vname)`) — these are always solution variables in MNA
- Device output currents as declared by each device's `output_currents()` method
- Passive element currents are NOT compared (they are derived quantities, not first-class outputs in either simulator)

### NgspiceRunner

Wraps the ngspice CLI binary. Located via CMake option (`-DNGSPICE_BINARY=/path/to/ngspice`) or `PATH` lookup.

```cpp
class NgspiceRunner {
public:
    explicit NgspiceRunner(const std::string& ngspice_binary_path);
    DCResult run_dc(const std::string& cir_path);
    TransientResult run_transient(const std::string& cir_path);
    ACResult run_ac(const std::string& cir_path);
};
```

Generates a temporary ngspice batch script, runs it, parses the binary `.raw` output file into the same result structs.

### Comparator

```cpp
struct Tolerance {
    double relative = 1e-3;    // Default relative tolerance
    double absolute = 1e-9;    // Floor for near-zero values
};

struct CompareResult {
    bool passed;
    std::string worst_signal;  // Which signal had the largest error
    double worst_error;        // The largest relative error observed
    int num_points_compared;
};

CompareResult compare(const TransientResult& expected,   // ngspice
                      const TransientResult& actual,     // cudaspice
                      Tolerance tol = {});
```

### Transient Comparison Methodology

Comparing transient results between two simulators requires care, especially when time grids differ.

**Milestone 1 (fixed-step):** CudaSPICE uses a fixed time step specified by the user. The comparison is simpler:
1. CudaSPICE output is on a uniform grid (t=0, tstep, 2*tstep, ..., tstop)
2. ngspice uses adaptive stepping, so its output is on a non-uniform grid
3. Interpolate ngspice results onto CudaSPICE's uniform grid (or vice versa)
4. Compare at each interpolated point using the tolerance model
5. The fixed step must be small enough to capture the circuit dynamics — if a test fails, the first diagnostic is to halve tstep

**Milestone 1.5 (adaptive):** Both simulators use adaptive stepping with different internal decisions:
1. **Use ngspice's time points as the reference grid.** Ngspice is ground truth.
2. **Interpolate CudaSPICE results onto ngspice's time grid** using linear interpolation.
3. **Compare at each ngspice time point** using the tolerance model.
4. **Breakpoint handling:** Both simulators hit the same source event breakpoints (PULSE edges, SIN transitions) because the same netlist defines them. CudaSPICE must implement breakpoint detection to avoid stepping over discontinuities.

**What "matches" means:** For a transient test to pass, the interpolated CudaSPICE waveform must be within tolerance of ngspice at every reference time point. This is a functional equivalence test (same waveform shape), not a numerical identity test (same internal computation path). It does NOT validate that the same time steps were taken or that the same steps were accepted/rejected.

### Tolerance Model

- Default: relative error < 1e-3
- Per-test overrides via Google Test parameterized tests
- Simple circuits (resistor divider): tightened to 1e-6 or better
- Complex nonlinear circuits (BSIM4v7 transient): relaxed to 1e-3
- AC analysis: magnitude compared with relative tolerance, phase compared with absolute tolerance (degrees)

### Example Test

```cpp
TEST_P(TransientTest, CompareAgainstNgspice) {
    auto ngspice_result = ngspice.run_transient(GetParam().circuit_path);
    auto ckt = sim.load(GetParam().circuit_path);
    auto cuda_result = sim.run_transient(ckt, GetParam().tstep, GetParam().tstop);

    auto cmp = compare(ngspice_result, cuda_result, GetParam().tolerance);
    EXPECT_TRUE(cmp.passed)
        << "Worst signal: " << cmp.worst_signal
        << " error: " << cmp.worst_error;
}

INSTANTIATE_TEST_SUITE_P(Basic, TransientTest, testing::Values(
    TestCase{"circuits/rc_transient.cir",    1e-6, 1e-3, {.relative=1e-6}},
    TestCase{"circuits/rlc_transient.cir",   1e-7, 1e-3, {.relative=1e-4}},
    TestCase{"circuits/inverter_bsim4.cir",  1e-9, 1e-6, {.relative=1e-3}}
));
```

## Simulation Data Flow

### Transient Analysis

```
.cir file
    |
    v
Parser → Circuit object (nodes, devices, analysis commands, .model cards, .param values)
    |
    v
DC Operating Point:
    |
    1. Build MNA matrix sparsity pattern (symbolic analysis, once)
    2. Newton-Raphson with convergence aids:
       a. Try direct Newton (max 100 iterations)
       b. If fails → gmin stepping
       c. If fails → source stepping
       d. If fails → error (circuit does not converge)
    |
    v
Transient Loop (for each time step):
    |
    +---> Newton-Raphson Iteration (3-10 iterations per step):
    |       |
    |       1. Predict next state (extrapolate from previous steps)
    |       |
    |       2. Voltage limiting (per device, prevents exp overflow)
    |       |
    |       3. Device evaluation (all devices in parallel)
    |       |   +-- GPU mode: pack params → kernel dispatch → results stay on device
    |       |   +-- CPU mode: OpenMP parallel for + SLEEF SIMD batched math
    |       |
    |       4. Matrix assembly (stamp device conductances into matrix)
    |       |   (always CPU — irregular scatter pattern into NumericMatrix)
    |       |
    |       5. Solve Ax=b
    |       |   +-- GPU mode: cusolverRF (reuses pattern from DC)
    |       |   +-- CPU mode: KLU refactorization + OpenBLAS
    |       |
    |       6. Convergence check (voltage + current criteria)
    |       |   +-- converged → exit Newton loop, advance time
    |       |   +-- not converged → back to step 2
    |       |
    +---> Time step control:
    |       M1 (fixed-step): dt = tstep (user-specified), no rejection
    |       M1.5 (adaptive): LTE-based step control:
    |       +-- error too large → reject step, halve dt, retry
    |       +-- error small → accept, potentially increase dt
    |       +-- breakpoint approaching → shrink dt to hit breakpoint exactly
    |
    v
Store results → TransientResult (time, voltages, currents)
```

### AC Analysis

```
.cir file
    |
    v
Parser → Circuit object
    |
    v
DC Operating Point (linearization point) → converged device states
    |
    v
Extract linearized parameters from each device:
    - Diode: gd (conductance), Cd (capacitance)
    - BSIM4v7: gm, gds, gmb, Cgs, Cgd, Cgb, etc.
    (stored during DC evaluation, reused for all frequencies)
    |
    v
For each frequency point:
    |
    1. Build complex-valued MNA matrix:
       A(jw) = G + jw*C
       where G = sum of device conductance stamps
             C = sum of device capacitance stamps
    2. Solve complex Ax=b (KLU supports complex, cuDSS supports complex)
    3. Store voltage/current phasors
    |
    v
ACResult (frequency, complex voltages, complex currents)
```

## Build System

CMake 3.20+ with the following structure:

```cmake
cmake_minimum_required(VERSION 3.20)
project(cudaspice LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)

# Optional CUDA
option(CUDASPICE_USE_CUDA "Enable CUDA GPU acceleration" ON)
if(CUDASPICE_USE_CUDA)
    include(CheckLanguage)
    check_language(CUDA)
    if(CMAKE_CUDA_COMPILER)
        enable_language(CUDA)
        find_package(CUDAToolkit REQUIRED)
        # cuSPARSE and cuSOLVER are part of CUDAToolkit
        # cuDSS is a separate download — found via find_package or bundled
    else()
        message(STATUS "CUDA not found, building CPU-only")
        set(CUDASPICE_USE_CUDA OFF)
    endif()
endif()

# Required CPU dependencies
find_package(SuiteSparse REQUIRED COMPONENTS KLU AMD COLAMD BTF)
find_package(OpenBLAS REQUIRED)
find_package(SLEEF REQUIRED)

# OpenMP — optional on macOS (Apple Clang does not ship it by default)
find_package(OpenMP)
if(NOT OpenMP_FOUND)
    message(STATUS "OpenMP not found — device evaluation will be single-threaded")
    message(STATUS "On macOS, install via: brew install libomp")
endif()

# Testing
option(CUDASPICE_BUILD_TESTS "Build tests" ON)
if(CUDASPICE_BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0)
    FetchContent_MakeAvailable(googletest)

    # ngspice binary for ground truth
    set(NGSPICE_BINARY "ngspice" CACHE FILEPATH "Path to ngspice binary")
endif()
```

### Build Configurations

| Configuration | CUDA | CPU Optimization | Use Case |
|---------------|------|-----------------|----------|
| `Release` with CUDA | ON | Full (O2, SIMD) | Production on NVIDIA GPU machines |
| `Release` CPU-only | OFF | Full (O2, SIMD) | Production on Mac / non-GPU machines |
| `Debug` | OFF | Minimal (O0) | Development and debugging |
| `RelWithDebInfo` | Optional | Moderate (O1) | Profiling |

### macOS Build Notes

Apple Clang does not include OpenMP. To build with OpenMP on macOS:
```bash
brew install libomp
cmake -DCMAKE_PREFIX_PATH=$(brew --prefix libomp) ..
```

Without OpenMP, the build succeeds but device evaluation runs single-threaded. All other functionality (KLU, SLEEF, parser, etc.) works without OpenMP.

## Dependencies Summary

| Dependency | Purpose | License | Required? |
|------------|---------|---------|-----------|
| KLU (SuiteSparse) | Sparse LU solver (CPU) | LGPL-2.1+ | Yes |
| OpenBLAS | Dense BLAS for KLU's internal dense blocks | BSD-3 | Yes |
| OpenMP | Parallel device eval (CPU) | Compiler-provided | No (optional, graceful fallback to single-threaded) |
| SLEEF | SIMD transcendentals for device eval | Boost | Yes |
| CUDA Toolkit | GPU runtime (includes cuSPARSE, cuSOLVER, cuBLAS) | NVIDIA EULA | No (optional) |
| cuDSS | GPU sparse LU (separate from toolkit) | NVIDIA EULA | No (GPU Phase 3 only) |
| Google Test | Testing framework | BSD-3 | No (tests only) |
| ngspice | Ground truth reference | BSD-3 | No (tests only) |

Note: MAGMA is removed from the initial dependency set. If profiling shows that batched dense block factorization is a bottleneck (GPU Phase 3), it will be evaluated at that point. cuBLAS batched (part of CUDA Toolkit) is the first choice since it requires no additional dependency.

## Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| **Parser compatibility** — SPICE syntax is complex and poorly standardized. Edge cases in `.model` cards, expressions, and `.lib` sections will cause subtle failures. | Netlists that work in ngspice fail in CudaSPICE | Start with a minimal parser that handles only documented constructs. Use a growing corpus of test netlists. Fail loudly on anything unrecognized. |
| **Convergence robustness** — Nonlinear circuits (especially BSIM4v7) are notoriously hard to converge. Different voltage limiting or stepping strategies than ngspice may produce different convergence paths. | Tests fail not due to wrong math but different convergence behavior | Implement gmin stepping, source stepping, and voltage limiting matching ngspice's approach. Accept wider tolerances on convergence-sensitive circuits. |
| **Host/device transfer overhead** — For small-to-medium circuits, CUDA kernel launch latency and memory transfers may negate compute savings. | GPU path is slower than CPU for typical circuits | Size threshold (default 500 nodes) gates GPU usage. CPU path is the default. Profiling guides threshold tuning. |
| **Adaptive transient complexity** — Time step control with LTE estimation, breakpoint detection, and rejected-step handling is subtle and hard to get right. | Wrong waveforms, missed transient events | Use trapezoidal rule initially (simpler LTE formula). Match ngspice's breakpoint algorithm for PULSE/SIN sources. Extensive test corpus covering edge cases. |
| **macOS build friction** — Apple Clang lacks OpenMP; SuiteSparse and OpenBLAS require Homebrew or manual install. | Developer onboarding friction on Mac | OpenMP is optional (graceful fallback). Document Homebrew setup. Consider vcpkg/Conan for dependency management. |
| **BSIM4v7 porting to CUDA** — 3000+ lines of branchy C with extensive conditionals. | Delays Phase 2 | Start with CPU-only BSIM4v7. Port to CUDA incrementally, testing each subsystem. |
| **cuDSS may not outperform KLU for circuit-sized matrices** — Circuit matrices are small and extremely sparse, which is KLU's sweet spot. | GPU solver adds complexity without speedup | Defer GPU solver (cuDSS) to Phase 3. Only add when profiling on large circuits shows solve is the bottleneck. |
| **Floating-point ordering differences** between CPU and GPU | Numerical differences cause test failures | Configurable tolerance. Document expected precision characteristics per analysis type. |
| **SuiteSparse LGPL license** | May limit static linking options for commercial use | Use dynamic linking for KLU. If needed, evaluate BSD-licensed alternatives (Eigen SparseLU, though less optimized for circuits). |

## Resolved Design Decisions

| # | Question | Decision | Rationale |
|---|----------|----------|-----------|
| 1 | Strict documented subset vs. arbitrary ngspice compatibility? | **Strict documented subset.** Parser fails loudly on unsupported constructs. | We are building a simulator, not an ngspice clone. Clear scope prevents unbounded parser work. |
| 2 | First milestone: correctness or GPU speedups? | **Correctness first (M1 is CPU-only).** | If the math is wrong, fast wrong answers are useless. Test harness and convergence must be solid before GPU complexity. |
| 3 | Single fixed matrix format or abstract pattern + views? | **SparsityPattern + NumericMatrix + backend-specific views.** | Decouples core stamping from solver format. KLU needs CSC, future backends may need CSR or blocked formats. One view class per backend. |
| 4 | Branch currents for every device or just sources/probes? | **Voltage source currents always; others opt-in via `.save`.** | V-source currents are MNA solution variables (free). Device currents cost storage; only save when requested. Matches ngspice default behavior. |
| 5 | Adaptive transient in M1 or fixed-step first? | **Fixed-step in M1, adaptive in M1.5.** | Fixed-step is simpler to implement and debug. Sufficient to validate Newton-Raphson, devices, and integration. Adaptive layers on top once fixed-step passes all tests. |
