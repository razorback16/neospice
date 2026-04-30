# neospice API Redesign: Handle-Based Architecture for circuit-cpp Integration

## Context

neospice is a C++20 SPICE simulator with 29 device models, 926+ tests, and Python bindings. circuit-cpp is a C++20 EDA framework for type-safe PCB circuit design with compile-time verification. The two libraries need tight integration — circuit-cpp defines circuits, neospice simulates them.

The current neospice API is string-based throughout: node names are strings, result access is via string-keyed maps, and `CircuitBuilder` works by accumulating netlist text and reparsing it. This forces a serialize→parse round-trip for programmatic construction and O(log n) string lookups for every result access. It also prevents circuit-cpp from maintaining a typed bridge between its `Net`/`Component` objects and simulation results.

This redesign makes handles the primary identity mechanism, eliminates the `CircuitBuilder`/`Circuit` split, and restructures the Python API around PyTorch-inspired patterns — while preserving full ngspice netlist compatibility.

## Constraints

- **ngspice netlist compatibility must be preserved** — `Simulator::load()` and `Simulator::parse()` continue to read standard SPICE netlists
- **No backward compatibility required** — both neospice and circuit-cpp can make breaking changes
- **Simulation performance must not regress** — the solver core (Newton-Raphson, matrix factorization, device evaluation) is untouched; API changes affect only the construction and result-access surfaces
- **Device migration tool changes must be minimal** — the tool generates internal device implementation code, insulated from the public API by `Circuit`'s typed methods

## Design

### 1. Core Handle Types

Three opaque handle types replace string-based identity:

```cpp
namespace neospice {

enum class NodeId  : int32_t {};
enum class DevId   : int32_t {};
enum class ModelId : int32_t {};

inline constexpr NodeId GND{0};

}
```

- Lightweight, copyable, comparable, hashable
- `GND` is the ground node constant (replaces string `"0"` / `"gnd"`)
- Handles are valid only within the `Circuit` that created them

### 2. Circuit — Builder and Simulation Target

`CircuitBuilder` is removed. `Circuit` is both the construction API and the object passed to `Simulator` for analysis. Two construction paths:

**Path 1 — Programmatic (primary, for circuit-cpp integration):**

```cpp
Circuit ckt;
auto in  = ckt.node("in");
auto out = ckt.node("out");
auto mod = ckt.model("NMOD", "NMOS", {{"LEVEL",14}, {"VTH0",0.4}, {"TOXE",2e-9}});

auto v1 = ckt.V("V1", in, GND, {.dc = 5.0, .ac = 1.0});
auto r1 = ckt.R("R1", in, out, 1e3);
auto c1 = ckt.C("C1", out, GND, 100e-12);
auto m1 = ckt.M("M1", drain, gate, source, bulk, mod, {{"W",1e-6}, {"L",100e-9}});
```

**Path 2 — Netlist (ngspice compatibility):**

```cpp
Simulator sim;
Circuit ckt = sim.load("amplifier.cir");
auto out = ckt.find_node("out");   // string → handle lookup
auto v1  = ckt.find_device("v1");  // string → handle lookup
```

#### 2.1 Node Management

```cpp
NodeId node(std::string_view name);
NodeId internal_node(std::string_view hint = "");
```

- `node()` creates or retrieves a named external node
- `internal_node()` creates an unnamed internal node (for device subcircuits)
- Calling `node("0")` or `node("gnd")` returns `GND`

#### 2.2 Model Definition

```cpp
ModelId model(std::string_view name, std::string_view type,
              std::initializer_list<Param> params);

ModelId subckt(std::string_view name,
               std::span<const std::string_view> ports,
               std::function<void(Circuit& sub)> define);

ModelId include(std::string_view filepath);
```

- `model()` creates a `.model` card (D, NPN, NMOS, etc.) from parameter key-value pairs
- `subckt()` defines a subcircuit via a lambda that builds the internal circuit
- `include()` parses a `.lib`/`.include` file and registers all models found

#### 2.3 Device Methods

Every device type has a typed method matching its SPICE prefix letter. All methods return `DevId`.

```cpp
// Passives
DevId R(std::string_view name, NodeId a, NodeId b, double ohms);
DevId C(std::string_view name, NodeId a, NodeId b, double farads);
DevId L(std::string_view name, NodeId a, NodeId b, double henries);
DevId K(std::string_view name, DevId L1, DevId L2, double coupling);

// Independent sources
DevId V(std::string_view name, NodeId p, NodeId n, SourceSpec spec);
DevId V_pulse(std::string_view name, NodeId p, NodeId n, PulseSpec spec);
DevId V_sin(std::string_view name, NodeId p, NodeId n, SinSpec spec);
DevId I(std::string_view name, NodeId p, NodeId n, SourceSpec spec);
DevId I_pulse(std::string_view name, NodeId p, NodeId n, PulseSpec spec);
DevId I_sin(std::string_view name, NodeId p, NodeId n, SinSpec spec);

// Dependent sources
DevId E(std::string_view name, NodeId op, NodeId on,
        NodeId cp, NodeId cn, double gain);                        // VCVS
DevId G(std::string_view name, NodeId op, NodeId on,
        NodeId cp, NodeId cn, double gm);                          // VCCS
DevId F(std::string_view name, NodeId op, NodeId on,
        DevId vsense, double gain);                                // CCCS
DevId H(std::string_view name, NodeId op, NodeId on,
        DevId vsense, double transresistance);                     // CCVS

// Behavioral
DevId B(std::string_view name, NodeId p, NodeId n,
        std::string_view expr);
DevId B(std::string_view name, NodeId p, NodeId n,
        std::function<double(const EvalContext&)> fn);

// Semiconductors
DevId D(std::string_view name, NodeId a, NodeId k,
        ModelId model, DeviceParams params = {});
DevId Q(std::string_view name, NodeId c, NodeId b, NodeId e,
        ModelId model, DeviceParams params = {});
DevId Q(std::string_view name, NodeId c, NodeId b, NodeId e, NodeId s,
        ModelId model, DeviceParams params = {});
DevId J(std::string_view name, NodeId d, NodeId g, NodeId s,
        ModelId model, DeviceParams params = {});
DevId M(std::string_view name, NodeId d, NodeId g, NodeId s, NodeId b,
        ModelId model, DeviceParams params = {});
DevId Z(std::string_view name, NodeId d, NodeId g, NodeId s,
        ModelId model, DeviceParams params = {});                  // HFET

// Switches
DevId S(std::string_view name, NodeId p, NodeId n,
        NodeId cp, NodeId cn, ModelId model);                      // V-switch
DevId W(std::string_view name, NodeId p, NodeId n,
        DevId vsense, ModelId model);                              // I-switch

// Transmission lines
DevId T(std::string_view name, NodeId p1, NodeId n1,
        NodeId p2, NodeId n2, TLineParams params);

// Subcircuit instance
DevId X(std::string_view name, ModelId subckt,
        std::span<const NodeId> ports, DeviceParams params = {});

// Custom device injection
DevId add(std::unique_ptr<Device> dev);

// Raw netlist passthrough (for anything not covered above)
void raw(std::string_view line);
```

Where:
- `Param` is `std::pair<std::string_view, double>` (used in `model()`)
- `DeviceParams` is `std::initializer_list<Param>` (used in device methods)
- `SourceSpec`, `PulseSpec`, `SinSpec`, `TLineParams` are plain structs with designated initializer support
- `SweepSpec` is `struct { DevId src; double start, stop, step; }` (used in 2D DC sweep)

#### 2.4 Mutation

```cpp
void set_value(DevId dev, double value);
void set_param(DevId dev, std::string_view param, double value);
```

- Changes device values after construction
- No structural mutation (add/remove devices) after first simulation
- Topology is frozen lazily at first `Simulator::run_*()` call, not at a separate `finalize()` step

#### 2.5 Introspection

```cpp
std::string_view name(NodeId) const;
std::string_view name(DevId) const;
NodeId  find_node(std::string_view name) const;
DevId   find_device(std::string_view name) const;
ModelId find_model(std::string_view name) const;
std::vector<std::string> node_names() const;
std::vector<std::string> device_names() const;
DeviceInfo device_info(DevId dev) const;
int32_t num_nodes() const;

SimOptions& options();
```

### 3. Simulator

Lightweight instance carrying the device factory registry, default options, and (future) thread pool config, RNG state for Monte Carlo, and topology cache for resimulation.

```cpp
class Simulator {
public:
    Simulator();  // registers all 29 built-in device types

    // Netlist input
    Circuit load(const std::filesystem::path& file);
    Circuit parse(std::string_view netlist);

    // Analysis methods
    DCResult        run_dc(Circuit& ckt);
    DCSweepResult   run_dc_sweep(Circuit& ckt, DevId src,
                                 double start, double stop, double step);
    DCSweepResult   run_dc_sweep(Circuit& ckt, SweepSpec outer, SweepSpec inner);
    ACResult        run_ac(Circuit& ckt, ACMode mode, int npoints,
                           double fstart, double fstop);
    TransientResult run_transient(Circuit& ckt, double tstep, double tstop,
                                  TranOptions opts = {});
    NoiseResult     run_noise(Circuit& ckt, NodeId output, DevId input_src,
                              ACMode mode, int npoints, double fstart, double fstop);
    TFResult        run_tf(Circuit& ckt, NodeId output, DevId input_src);
    SensResult      run_sens(Circuit& ckt, NodeId output);
    PZResult        run_pz(Circuit& ckt, NodeId inp, NodeId inn,
                           NodeId outp, NodeId outn, PZMode mode);

    // Netlist-driven dispatch
    SimulationResult run(Circuit& ckt);

    // Device factory registration (for custom device types)
    using DeviceFactory = std::function<
        std::unique_ptr<Device>(std::string_view name,
                                std::span<const NodeId> nodes,
                                ModelId model,
                                const DeviceParams& params)>;
    void register_device(std::string_view prefix, DeviceFactory factory);
};
```

- Default-constructed `Simulator` has all 29 built-in devices registered
- `register_device()` adds custom device types by SPICE prefix
- Non-static methods — instance carries registry and future Monte Carlo / resimulation state
- Thread-safe: multiple threads can use independent `Circuit` objects with the same `Simulator`

### 4. Result Types

Dual access: handle-based (O(1) array index) and string-based (O(log n) name lookup). Array data returned as `std::span` views into internal storage — zero-copy.

#### 4.1 DCResult

```cpp
class DCResult {
public:
    double voltage(NodeId node) const;
    double voltage(std::string_view node) const;
    double current(DevId dev) const;
    double current(std::string_view dev) const;
    double diff(NodeId p, NodeId n) const;

    std::vector<std::string> signal_names() const;
    SimStatus status() const;
};
```

#### 4.2 ACResult

```cpp
class ACResult {
public:
    std::span<const double> frequency() const;

    std::span<const std::complex<double>> voltage(NodeId node) const;
    std::span<const std::complex<double>> voltage(std::string_view node) const;
    std::span<const double> magnitude_db(NodeId node) const;
    std::span<const double> magnitude_db(std::string_view node) const;
    std::span<const double> phase_deg(NodeId node) const;
    std::span<const double> phase_deg(std::string_view node) const;
    std::span<const double> magnitude(NodeId node) const;
    std::span<const double> magnitude(std::string_view node) const;

    std::span<const std::complex<double>> current(DevId dev) const;
    std::span<const std::complex<double>> current(std::string_view dev) const;
    std::span<const double> current_magnitude_db(DevId dev) const;
    std::span<const double> current_magnitude_db(std::string_view dev) const;
    std::span<const double> current_phase_deg(DevId dev) const;
    std::span<const double> current_phase_deg(std::string_view dev) const;

    std::span<const std::complex<double>> diff(NodeId p, NodeId n) const;
    std::span<const double> diff_magnitude_db(NodeId p, NodeId n) const;

    std::vector<std::string> signal_names() const;
    SimStatus status() const;
};
```

#### 4.3 TransientResult

```cpp
class TransientResult {
public:
    std::span<const double> time() const;
    std::span<const double> voltage(NodeId node) const;
    std::span<const double> voltage(std::string_view node) const;
    std::span<const double> current(DevId dev) const;
    std::span<const double> current(std::string_view dev) const;
    std::span<const double> diff(NodeId p, NodeId n) const;

    int rejected_steps() const;
    std::vector<std::string> signal_names() const;
    SimStatus status() const;
};
```

#### 4.4 Other Result Types

`NoiseResult`, `DCSweepResult`, `TFResult`, `SensResult`, `PZResult` follow the same pattern: handle overloads added alongside existing string overloads, vector returns changed to span returns for array data. Scalar fields remain unchanged.

#### 4.5 SimStatus (enriched)

```cpp
struct SimStatus {
    bool converged;
    int iterations;
    ConvergenceMethod convergence_method;
    double residual;                   // final Newton residual norm
    NodeId worst_node;                 // largest residual contributor
    int gmin_steps;                    // 0 if direct convergence
    int source_steps;
    double elapsed_seconds;
    std::optional<double> min_timestep;  // transient only
    std::vector<std::string> warnings;
};
```

- `worst_node` as `NodeId` — circuit-cpp can map this back to its own typed net for meaningful diagnostics
- `gmin_steps` / `source_steps` — circuit-cpp can flag "simulation converged but struggled" conditions

#### 4.6 Derived Measurements

Free functions in a separate header, not methods on result types. Result types remain pure data containers.

```cpp
namespace neospice::measure {
    double bandwidth_3db(const ACResult& r, NodeId node);
    double settling_time(const TransientResult& r, NodeId node,
                         double final_val, double tolerance);
    double rise_time(const TransientResult& r, NodeId node,
                     double low, double high);
    double overshoot(const TransientResult& r, NodeId node, double final_val);
    double rms(const TransientResult& r, NodeId node, double tstart, double tstop);
    std::pair<double,double> phase_margin(const ACResult& r, NodeId node);
    std::pair<double,double> gain_margin(const ACResult& r, NodeId node);
    double spot_noise(const NoiseResult& r, double freq);
}
```

### 5. Device Interface

Single `Device` base class for all devices — UCB-migrated, hand-written, and future behavioral models. No two-tier split. The interface is essentially unchanged from the current implementation:

```cpp
class Device {
public:
    virtual ~Device() = default;

    // Required
    virtual void stamp_pattern(SparsityBuilder& builder) const = 0;
    virtual void assign_offsets(const SparsityPattern& pattern) = 0;
    virtual void evaluate(const std::vector<double>& voltages,
                          NumericMatrix& mat, std::vector<double>& rhs) = 0;

    // Optional — topology and setup
    virtual void declare_internal_nodes(Circuit& ckt) {}
    virtual int32_t extra_vars() const { return 0; }
    virtual void assign_branch_index(int32_t& next) {}
    virtual int32_t branch_index() const { return -1; }

    // Optional — analysis-specific
    virtual void ac_stamp(const std::vector<double>& voltages,
                          NumericMatrix& G, NumericMatrix& C) {}
    virtual void apply_ac_excitation(std::vector<std::complex<double>>&, int32_t) {}
    virtual std::vector<NoiseSource> noise_sources(double freq,
        const std::vector<double>& dc_solution) const { return {}; }

    // Optional — convergence and integration
    virtual void limit_voltages(const std::vector<double>& old_v,
                                std::vector<double>& new_v) {}
    virtual void process_temperature(double sim_temp, double sim_tnom) {}
    virtual int32_t state_vars() const { return 0; }
    virtual void set_state_ptrs(double*, double*, double*, int32_t) {}
    virtual double compute_trunc(const IntegratorCtx&, const SimOptions&) const { return 1e30; }
    virtual bool device_converged() const { return true; }

    // Optional — introspection
    virtual std::optional<double> query_param(const std::string&) const { return {}; }
    virtual void reset() {}
    virtual void reset_temp() {}
};
```

Custom device authors (for circuit-cpp behavioral models) implement the same interface. When mixed-signal event engine work begins in the future, additional virtual methods (`on_event`, `schedule_events`) may be added with empty defaults — no breaking change.

### 6. Python Bindings

#### 6.1 Design Principles

- **Handles are not exposed** — Python is string-only; bindings convert internally
- **PyTorch-inspired construction** — components as objects, circuits as containers, subcircuits as subclasses
- **SPICE engineering notation** — `"1k"`, `"100p"`, `"10meg"` accepted for values
- **Kwargs replace spec objects** — `dc=5, ac=1` instead of `SourceSpec()`
- **Three tiers**: one-liner convenience, statement-based, Module-style subclassing

#### 6.2 Circuit Construction

**Tier 1 — One-liner convenience (unchanged):**

```python
result = ns.dc("V1 in 0 DC 10\nR1 in out 1k\nR2 out 0 1k\n.op\n.end")
print(result.voltage("out"))
```

**Tier 2 — Statement-based:**

```python
ckt = ns.Circuit("RC Filter")
ckt.V("V1", "in", "0", dc=5, ac=1)
ckt.R("R1", "in", "out", "1k")
ckt.C("C1", "out", "0", "100p")
```

**Tier 3 — Declarative constructor:**

```python
ckt = ns.Circuit("RC Filter",
    ns.V("V1", "in", "0", dc=5, ac=1),
    ns.R("R1", "in", "out", "1k"),
    ns.C("C1", "out", "0", "100p"),
)
```

**Tier 4 — Module-style subclassing:**

```python
class OpAmp(ns.SubCircuit):
    ports = ["inp", "inn", "out", "vdd", "vss"]

    def __init__(self):
        super().__init__()
        self.Rin  = ns.R("inp", "inn", "1meg")
        self.E1   = ns.E("out", "0", "inp", "inn", gain=1e5)
        self.Rout = ns.R("out", "0", "100")

class Amplifier(ns.Circuit):
    def __init__(self):
        super().__init__("Inverting Amplifier")
        self.V1  = ns.V("in", "0", dc=0, ac=1)
        self.U1  = OpAmp().at("in", "fb", "out", "vdd", "gnd")
        self.Rf  = ns.R("fb", "out", "100k")
        self.Rin = ns.R("in", "fb", "10k")
        self.VDD = ns.V("vdd", "0", dc=15)

ckt = Amplifier()
result = sim.run_ac(ckt, ns.ACMode.DEC, 100, 1, 1e9)
```

- `ns.SubCircuit` is the equivalent of `nn.Module` — attribute assignment registers components
- `.at(...)` maps subcircuit ports to parent node names
- Introspection via attribute access: `ckt.Rf`, `ckt.U1.Rin`

#### 6.3 Source Specifications

Keyword arguments replace separate spec objects:

```python
ckt.V("V1", "in", "0", dc=5, ac=1)
ckt.V_pulse("V1", "in", "0", v1=0, v2=5, tr=1e-9, tf=1e-9, pw=10e-9, per=20e-9)
ckt.V_sin("V1", "in", "0", va=1.0, freq=1e6)
ckt.I("I1", "in", "0", dc=1e-3)
```

`SourceSpec`, `PulseSpec`, `SinSpec` structs are still available for users who prefer them, but kwargs are the primary API.

#### 6.4 Analysis Methods

```python
sim = ns.Simulator()
ckt = ns.Circuit("Test")
# ...

result = sim.run_dc(ckt)
result = sim.run_transient(ckt, tstep=1e-9, tstop=1e-6)
result = sim.run_transient(ckt, tstep=1e-9, tstop=1e-6, uic=True)
result = sim.run_ac(ckt, ns.ACMode.DEC, 100, 1, 1e9)
result = sim.run_dc_sweep(ckt, "v1", -1, 1, 0.01)
result = sim.run_dc_sweep(ckt, ("v1", -1, 1, 0.01), ("v2", 0, 3.3, 0.1))
result = sim.run_noise(ckt, output="out", input_src="v1",
                       mode=ns.ACMode.DEC, npoints=100, fstart=1, fstop=1e9)
result = sim.run_tf(ckt, output="v(out)", input_src="v1")
result = sim.run_sens(ckt, output="v(out)")
```

- No separate `_with_opts` methods — optional kwargs on existing methods
- String-based node/device references (Python has no handles)
- `run_dc_sweep` accepts positional tuples for conciseness

#### 6.5 Result Types

Unchanged from current API — string-based access, numpy array returns. All existing methods remain.

#### 6.6 Convenience Functions

Unchanged:

```python
result = ns.dc("amplifier.cir")
result = ns.ac("filter.cir", mode="dec", npoints=100, fstart=1, fstop=1e9)
result = ns.transient("osc.cir", tstep=1e-9, tstop=1e-6)
```

### 7. Header Organization

Public headers follow standard C++ library convention (`include/<project>/`):

```
include/
  neospice/
    neospice.hpp        // single umbrella include
    types.hpp           // NodeId, DevId, ModelId, GND, enums
    circuit.hpp         // Circuit class
    simulator.hpp       // Simulator class
    results.hpp         // All result types
    options.hpp         // SimOptions, TranOptions, SourceSpec, etc.
    device.hpp          // Device base class (for custom device authors)
    measure.hpp         // Free measurement functions
```

Internal headers remain in `src/`:

```
src/
  core/                 // solver, matrix, newton, integration
  devices/              // 29 device implementations
  parser/               // netlist parser
  output/               // raw file writer
  api/                  // implementation files for public headers
```

CMake exports `neospice::neospice` target. Consumers use:

```cmake
find_package(neospice REQUIRED)
target_link_libraries(my_project PRIVATE neospice::neospice)
```

```cpp
#include <neospice/neospice.hpp>
```

### 8. Device Migration Tool Updates

The migration tool (`tools/ngspice_migrate/`) generates internal device implementation code. It is insulated from the public API redesign by `Circuit`'s typed methods acting as the bridge.

#### 8.1 What Changes

| File | Change | Reason |
|---|---|---|
| `gen_adapter.py` | Update generated `#include` paths | Public headers move from `src/api/` to `include/neospice/` |
| `gen_shim.py` | Update internal header includes | Same path reorganization |
| `gen_test.py` | Update test scaffolding to use new API | Tests should use `Simulator` instance methods and programmatic construction |
| `gen_cmake.py` | Update include directory references | `include/` added to include path |

#### 8.2 What Does Not Change

- The 8-pass transformer (C→C++ translation)
- Descriptor YAML format and all 18 existing descriptors
- Shim layer architecture
- Device struct definitions (`_def.hpp`)
- AC stamp / noise / truncation extraction
- Model card conversion logic (`gen_model_card.py`)
- Parser helper generation (`gen_parser.py`)
- Post-migration validation (`validation.py`)
- The `make()` factory signature on generated device classes

#### 8.3 migrate-device Skill Updates

The `.claude/commands/migrate-device.md` skill needs these updates:

1. **Phase 9 (Parser Integration)**: Update wiring instructions — the device dispatch point moves from `netlist_parser.cpp` prefix switch to `Circuit`'s internal device creation in the typed methods (e.g., `Circuit::M()` calls `BSIM4v7Device::make()`)
2. **Phase 12 (Integration Checklist)**: Add verification that the device works through both programmatic API (`ckt.M(...)`) and netlist parsing (`sim.parse("M1 d g s b NMOD ...")`)
3. **Architecture Reference**: Update header paths for the new `include/neospice/` layout

### 9. circuit-cpp Integration Surface

This section defines what neospice's API must accommodate for circuit-cpp integration. The circuit-cpp changes themselves are a separate design spec.

#### 9.1 What circuit-cpp Needs From neospice

- **Handle types** (`NodeId`, `DevId`, `ModelId`) embeddable in circuit-cpp's `Net`/`Component` types
- **Programmatic `Circuit` construction** without netlist serialization
- **Handle-based result access** for O(1) lookup from circuit-cpp's typed objects
- **`SimStatus::worst_node`** as `NodeId` mappable back to circuit-cpp's type system
- **`register_device()`** for future custom behavioral models
- **Measurement utilities** callable from circuit-cpp's analysis passes

#### 9.2 Expected circuit-cpp Integration Pattern

```cpp
// Each circuit-cpp component implements emit()
void Resistor::emit(neospice::Circuit& ckt) {
    sim_dev_ = ckt.R(name_, pin1_.sim_id(), pin2_.sim_id(), value_);
}

// circuit-cpp's Circuit delegates to neospice
neospice::DCResult CircuitCpp::simulate_dc() {
    neospice::Circuit ckt;
    for (auto& comp : components_)
        comp->emit(ckt);
    neospice::Simulator sim;
    return sim.run_dc(ckt);
}

// Results accessed by handle — O(1)
double v = dc_result.voltage(output_net.sim_id());
```

#### 9.3 What circuit-cpp Will Eventually Change

- Add neospice as a CMake dependency
- `emit(neospice::Circuit&)` method on each component class
- Store `NodeId`/`DevId` alongside existing `Net`/`Component` types
- Simulation methods on `Circuit` (or separate `SimulationContext`)
- `SpiceExporter` becomes optional — direct API replaces netlist round-trip
- `SpiceContext`/`SpiceDirective` may be deprecated in favor of direct `neospice::Simulator` calls

### 10. Performance Impact

The redesign is API-surface only. The solver core is untouched.

| Operation | Before | After | Impact |
|---|---|---|---|
| Programmatic construction | String accumulation → reparse | Direct object creation | Faster (skips parse) |
| Result access | `map<string, vector>` O(log n) | Array index O(1) | Faster |
| Array result data | `vector` copy per access | `span` view | Faster (zero-copy) |
| Device evaluate() | Virtual dispatch | Virtual dispatch | Identical |
| Matrix factorization | NeoSolver LU | NeoSolver LU | Identical |
| Newton iteration | Same algorithm | Same algorithm | Identical |

No simulation performance regression is expected.
