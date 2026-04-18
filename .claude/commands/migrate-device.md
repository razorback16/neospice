# Migrate ngspice Device Model to neospice

Migrate any ngspice device model to the neospice C++ framework. Combines the
auto-migration tool (8-pass C-to-C++ translator) with manual implementation of
features the tool cannot handle.

**Usage:** `/migrate-device <device-name>` (e.g., `/migrate-device dio`, `/migrate-device bjt`)

---

## Phase 0: Orientation

Before anything else, understand the device you are migrating.

### 0.1 Study the ngspice source

Locate the device in the ngspice source tree (typically `src/spicelib/devices/<DEVICE>/`).
Identify:

- **Terminal count and names** (e.g., diode has 2: pos/neg; MOSFET has 4: d/g/s/b; BJT has 3+: c/b/e/s)
- **Internal nodes** — created in the setup function via `CKTnodeName` / node allocation
- **State variable count** — look for `#define <PREFIX>_STATES <N>` or count offsets in the def header
- **Source files** — which .c files exist: setup, load, temp, param, mpar, check, ac, trunc, cvtest, getic, noise, etc.
- **Linked-list structures** — model-level allocations threaded via `pNext` pointers (e.g., size-dependent param caches)
- **Charge-based state variables** — which state offsets store charges (needed for truncation error)
- **Has `ic=` support?** — check getic.c or the load function for `icVDS`/`icVBE`-style fields

### 0.2 Check for an existing descriptor

```bash
ls tools/descriptors/
```

If a YAML descriptor already exists, skip to Phase 2. Otherwise proceed to Phase 1.

### 0.3 Reference: existing migrations

Study these for patterns:
- `src/devices/bsim4v7/` — complex MOSFET (4 terminals, 29 states, internal nodes, full feature set)
- `tools/descriptors/bsim4v7.yaml` — complex descriptor with geometry, version stamp, linked-list cleanup
- `tools/descriptors/dio.yaml` — simpler descriptor (2 terminals, 5 states)

---

## Phase 1: Create Descriptor YAML

Create `tools/descriptors/<device>.yaml`. This drives the entire auto-migration.

### Field Reference

```yaml
model:
  # --- Identity ---
  ngspice_prefix: "DIO"                    # C struct/function prefix in ngspice
  neospice_name: "dio"                     # lowercase name for file naming
  neospice_namespace: "dio"                # C++ namespace under neospice::
  instance_struct: "DIOinstance"            # ngspice instance struct typedef
  model_struct: "DIOmodel"                 # ngspice model struct typedef
  instance_tag: "sDIOinstance"             # struct tag (for forward decl)
  model_tag: "sDIOmodel"                   # struct tag
  cpp_instance: "DIOInstance"              # PascalCase name for C++ typedef
  cpp_model: "DIOModel"                    # PascalCase name for C++ typedef
  gen_instance: "GENinstance"              # always "GENinstance"
  gen_model: "GENmodel"                    # always "GENmodel"

  # --- Terminals ---
  terminals:
    - { name: "pos", field: "DIOposNode" }
    - { name: "neg", field: "DIOnegNode" }

  # --- State ---
  state_count: 5                           # total state variable slots
  state_base_field: "DIOstate"             # instance field holding state base offset

  # --- Linked-list navigation ---
  next_instance_field: "DIOnextInstance"
  instances_field: "DIOinstances"
  next_model_field: "DIOnextModel"
  model_ptr_field: "DIOmodPtr"
  name_field: "DIOname"
  matrix_ptr_suffix: "Ptr"                 # suffix on matrix pointer fields

  # --- Source files to translate ---
  # Keys are roles; values are filenames in the ngspice device directory
  source_files:
    setup: "diosetup.c"       # REQUIRED: node allocation, matrix pointer reservation
    load: "dioload.c"         # REQUIRED: stamp evaluation (DC + transient)
    temp: "diotemp.c"         # REQUIRED: temperature-dependent parameter setup
    param: "dioparam.c"       # instance parameter setting
    mpar: "diompar.c"         # model parameter setting
    # check: "diocheck.c"     # optional: parameter checking
    # geo: "diogeo.c"         # optional: geometry processing
    devsup: "dio.c"           # device support / registration table

  # --- Files to SKIP (handled manually or not needed) ---
  skip_files:
    - "dioacld.c"             # AC load — manual G/C split needed
    - "diotrunc.c"            # Truncation — manual LTE implementation
    - "dioconv.c"             # Convergence test — manual CKTnoncon wiring
    - "diogetic.c"            # Initial conditions — manual ic= parsing
    - "dioask.c"              # Parameter ask — manual query_param()
    - "diomask.c"             # Model ask — not needed
    - "diodel.c"              # Delete — C++ RAII handles this
    - "diodest.c"             # Destroy — C++ RAII handles this
    - "dionoise.c"            # Noise — not yet supported in neospice
    - "diopzld.c"             # Pole-zero — not yet supported
    - "diosoachk.c"           # SOA check — not yet supported
    - "dioinit.c"             # Init table — not needed (C++ registration)

  # --- Preprocessor defines ---
  defines:
    - "PREDICTOR"             # enables predictor integration path

  # --- Features ---
  has_internal_nodes: true     # set true if setup creates internal MNA nodes

  # --- UCB entry-point functions ---
  setup_function: "DIOsetup"
  temp_function: "DIOtemp"
  load_function: "DIOload"

  # --- Cleanup (model-level linked lists to free in destructor) ---
  # Omit if none exist
  cleanup_linked_lists:
    - { field: "pSizeDependParamKnot", next_field: "pNext" }

  # --- Version stamp (omit if not applicable) ---
  version_stamp:
    field: "DIOversion"
    given_field: "DIOversionGiven"
    value: "1.0.0"

  # --- Geometry parameters (instance card fields like W, L, area, m) ---
  geometry:
    - { name: "area", field: "DIOarea", given: "DIOareaGiven", default: "1.0" }
    - { name: "pj",   field: "DIOpj",   given: "DIOpjGiven",   default: "0.0" }
    # always_given: true means the field's Given flag is always set (e.g., W and L)
```

### Tips

- **Finding state_count**: Search the def header for the highest state offset + 1, or look for `#define <PREFIX>_STATES`.
- **Finding terminals**: Look at the setup function's node binding or the def header's node field definitions.
- **Finding cleanup lists**: Search the model struct for pointer fields with `pNext`-linked chains. If the model struct has `pSizeDependParamKnot` or similar, it needs cleanup.
- **Geometry**: Look at the instance parameter table for geometric quantities (area, perimeter, width, length, multiplier).

---

## Phase 2: Run the Auto-Migration Tool

```bash
python -m ngspice_migrate \
    tools/descriptors/<device>.yaml \
    /path/to/ngspice/src/spicelib/devices/<DEVICE>/ \
    src/devices/<device>/
```

This produces:
- `<ns>_def.hpp` — translated struct definitions (from `*def*.h`)
- `<ns>_shim.hpp` / `<ns>_shim.cpp` — compatibility shim (CKT, matrix wrappers)
- `<ns>_device.hpp` / `<ns>_device.cpp` — adapter skeleton (Device interface bridge)
- `<ns>_setup.cpp`, `<ns>_load.cpp`, `<ns>_temp.cpp`, etc. — translated C source
- `CMakeLists.txt` — build integration

### 8-Pass Translation Pipeline

The tool applies these transformations:
1. **Strip OMP** — remove OpenMP pragmas and `#pragma omp` blocks
2. **Split banner** — separate leading comment blocks
3. **Protect literals** — shield string/char literals from token substitution
4. **Token substitution** — `GENinstance` -> `<CppInstance>`, struct tags, pointer casts
5. **Rewrite stamps** — `*(ckt->CKTmatrix->...)` -> `ckt->mat->add(off, val)`
6. **K&R to ANSI** — convert old-style function declarations
7. **Unprotect** — restore protected literals
8. **Wrap namespace** — add `namespace neospice::<ns> { ... }`

---

## Phase 3: Build Fix Pass

After auto-migration, the code will likely have compilation errors. Common issues:

### 3.1 Include fixups

The translated files may reference headers that don't exist. Fix includes:
```cpp
// Typical includes needed in translated .cpp files
#include "devices/<ns>/<ns>_def.hpp"
#include "devices/<ns>/<ns>_shim.hpp"
```

### 3.2 Untranslated macros

Some ngspice macros may not be handled by the tool. Common ones:
- `TSTALLOC(ptr, row, col)` — should become matrix pointer reservation in setup
- `NIintegrate(ckt, ...)` — state integration; handled by the shim's `Shim::Ckt::integrate()`
- `DEVfetlim`, `DEVlimvds`, `DEVpnjlim` — voltage limiting functions; implement in the shim or inline
- `MAX`, `MIN` — replace with `std::max`, `std::min`

### 3.3 State array access

Verify that state accesses use absolute offsets (base + relative):
```cpp
// CORRECT: state0_[inst.DIOstates + DIO_OFFSET]
// WRONG:   state0_[DIO_OFFSET]  (missing base)
```

### 3.4 Ground node convention

The auto-tool converts node indices but verify:
- neospice: `GROUND_INTERNAL = -1`, real nodes `>= 0`
- UCB/ngspice: ground = `0`, real nodes `>= 1`
- Conversion: `neo_to_ucb(neo)` returns `(neo < 0) ? 0 : (neo + 1)`

### 3.5 Build and iterate

```bash
cmake --build build 2>&1 | head -50
```

Fix errors one at a time. The most common are missing includes, undeclared identifiers from macros the tool didn't handle, and type mismatches from C-to-C++ strictness.

---

## Phase 4: AC Stamp (Manual)

**Why manual**: ngspice stamps into a single complex matrix `(Y = G + jωC)`. Neospice uses separate real `G` and `C` matrices combined per-frequency as `(G + jωC)`. The auto-tool cannot split these.

### 4.1 Find the AC load source

Look at the skipped AC file (e.g., `dioacld.c`, `b4v7acld.c`). This contains the small-signal model stamps.

### 4.2 Implement `ac_stamp()` override

```cpp
void <Prefix>Device::ac_stamp(const std::vector<double>& voltages,
                               NumericMatrix& G, NumericMatrix& C) override;
```

**Pattern**: For each `*(ptr) += value` in the AC load:
- If the value is a conductance (real, frequency-independent) -> stamp into `G`
- If the value is a capacitance (multiplied by `omega` in ngspice, or stored as `Cxx`) -> stamp into `C`

### 4.3 G/C classification rules

| ngspice pattern | Matrix | What it is |
|---|---|---|
| `gm`, `gds`, `gmbs`, `gbd`, `gbs` | G | Transconductance/output conductance |
| `Cgs`, `Cgd`, `Cgb`, `Cds`, `Cbd`, `Cbs` | C | Gate/junction capacitances |
| Resistance reciprocal (`1/Rds`) | G | Conductance |
| `Charge derivative` | C | dQ/dV = capacitance |

### 4.4 Matrix offset resolution

Use the same `MatrixOffset` fields from `assign_offsets()`:
```cpp
auto add_G = [&](MatrixOffset off, double val) {
    if (off >= 0) G.add(off, val);
};
auto add_C = [&](MatrixOffset off, double val) {
    if (off >= 0) C.add(off, val);
};
```

### 4.5 NQS / frequency-dependent conductance

If the model has NQS (non-quasi-static) modes where conductances depend on omega,
this cannot be represented in the G/C split. Options:
- Implement as unsupported with a warning (as done for BSIM4v7 `acnqsMod`)
- Or implement frequency-dependent evaluation (requires framework extension)

### 4.6 Device multiplier

Scale all stamps by the device multiplier `m`:
```cpp
double m = inst_.<PREFIX>m;  // or default 1.0
// apply: add_G(off, gm * m);
```

---

## Phase 5: Truncation Error (Manual)

**Why manual**: The truncation error calculation uses charge state history to compute
local truncation error (LTE) for timestep control. The ngspice version in `*trunc.c`
calls `CKTterr()` which has framework dependencies the tool cannot translate.

### 5.1 Identify charge state variables

Look in the def header for state offsets associated with charges:
```c
#define DIOcapCharge  3   // junction capacitance charge
#define DIOcapCurrent 4   // capacitance current
```

Charges are the quantities that get integrated (`NIintegrate` calls in the load function).
Look for `NIintegrate(ckt, geq, ceq, cap, state_offset)` calls — the `state_offset` arguments are the charge variables.

### 5.2 Implement `compute_trunc()` override

```cpp
double <Prefix>Device::compute_trunc(const IntegratorCtx& ctx,
                                      const SimOptions& opts) const override;
```

**Gear-2 LTE formula** (matches ngspice CKTterr for order 2):

```cpp
double dt_min = 1e30;
const double h0 = ctx.delta;          // current step
const double h1 = ctx.delta_old[1];   // previous step (h_{n-1})
if (h1 <= 0.0) return 1e30;

for (int charge_offset : charge_offsets) {
    int qcap = state_base_ + charge_offset;

    // Divided differences from state history
    double q0 = state0_[qcap];
    double q1 = state1_[qcap];
    double q2 = state2_[qcap];

    double dd1 = (q0 - q1) / h0;
    double dd2 = ((q0 - q1) / h0 - (q1 - q2) / h1) / (h0 + h1);

    // Tolerance: chgtol + reltol * |charge| (on the charge itself)
    double tol = opts.chgtol + opts.reltol * std::max(std::abs(q0), std::abs(q1));
    if (tol <= 0.0) continue;

    // Gear-2 coefficient = 2/(order+1) = 2/3 ... but ngspice uses trtol/3
    // The standard CKTterr uses: coeff = 2.0 / (order * (order + 1))
    //   -> for order=2: coeff = 2/6 = 1/3, then factor = 1/(coeff * dd2) * tol
    // Simplified: dt = sqrt(tol / (factor * |dd2|))
    double lte_coeff = 2.0 / 9.0;  // 2/(3*(order+1)) for Gear-2
    if (std::abs(dd2) > 1e-30) {
        double dt = std::sqrt(tol / (lte_coeff * std::abs(dd2)));
        dt_min = std::min(dt_min, dt);
    }
}
return dt_min;
```

### 5.3 Key details

- `state_base_` must be added to charge offsets (they are relative to the instance's state block)
- Use `state0_`, `state1_`, `state2_` (the 3-level ring buffer)
- Guard against `h1 <= 0.0` and `tol <= 0.0` (division by zero)
- The `2/9` coefficient matches ngspice's CKTterr for Gear-2 with `trtol=1`

---

## Phase 6: Convergence Test (Manual)

**Why manual**: ngspice's convergence test (in `*cvtest.c`) is often dead code when `NEWCONV` is not defined. The actual convergence check is inline in the load function, incrementing `CKTnoncon`.

### 6.1 Check if convergence is inline or in cvtest

- If `#ifndef NEWCONV` wraps the load function's convergence checks -> convergence is inline (common case)
- If `#ifdef NEWCONV` and a separate cvtest file -> need to port the cvtest logic

### 6.2 Wire CKTnoncon (inline case)

The load function already increments `ckt->CKTnoncon` when currents don't converge.
Capture this in the evaluate method:

```cpp
// In evaluate(), after the load call:
last_noncon_ = ckt.CKTnoncon;
```

Add the member:
```cpp
mutable int last_noncon_ = 0;
```

Override:
```cpp
bool <Prefix>Device::device_converged() const override {
    return last_noncon_ == 0;
}
```

### 6.3 Framework integration

The Newton solver already calls `device_converged()` after node-voltage convergence passes (see `src/core/newton.cpp` lines 143-156). No framework changes needed.

---

## Phase 7: Initial Conditions (Manual)

**Why manual**: The `getic.c` file sets initial voltages from `.ic` card or instance `ic=` parameters. This is handled differently in neospice's parser.

### 7.1 Identify IC fields

Look in the def header for fields like:
```c
double <PREFIX>icVDS;    int <PREFIX>icVDSGiven;
double <PREFIX>icVGS;    int <PREFIX>icVGSGiven;
double <PREFIX>icVBS;    int <PREFIX>icVBSGiven;
// or for diodes/BJTs:
double <PREFIX>icVD;     int <PREFIX>icVDGiven;
```

### 7.2 Implement `set_ic()`

Add a method to set initial conditions on the underlying UCB instance struct:

```cpp
void <Prefix>Device::set_ic(double v1, bool v1_given,
                             double v2, bool v2_given, ...) {
    if (v1_given) { inst_.<PREFIX>icV1 = v1; inst_.<PREFIX>icV1Given = 1; }
    if (v2_given) { inst_.<PREFIX>icV2 = v2; inst_.<PREFIX>icV2Given = 1; }
    // ...
}
```

### 7.3 Parser integration for ic=

In `src/parser/netlist_parser.cpp`, add ic= parsing for the element card:

```cpp
// After creating the device, check for ic= on the element line
auto ic_it = params.find("ic");
if (ic_it != params.end()) {
    // Parse comma-separated values: ic=V1,V2,V3
    // Handle empty fields: ic=0.5,,0.1 means V2 not given
    auto fields = split(ic_it->second, ',');
    double v1 = 0, v2 = 0, v3 = 0;
    bool v1g = false, v2g = false, v3g = false;
    if (fields.size() > 0 && !fields[0].empty()) { v1 = std::stod(fields[0]); v1g = true; }
    if (fields.size() > 1 && !fields[1].empty()) { v2 = std::stod(fields[1]); v2g = true; }
    // ...
    dev->set_ic(v1, v1g, v2, v2g, ...);
}
```

---

## Phase 8: Parameter Query (Manual)

**Why manual**: ngspice's `*ask.c` uses a switch table that doesn't map to C++ patterns.

### 8.1 Implement `query_param()` override

```cpp
std::optional<double> <Prefix>Device::query_param(const std::string& name) const override;
```

### 8.2 Pattern

```cpp
std::optional<double> <Prefix>Device::query_param(const std::string& name) const {
    // Case-insensitive comparison
    auto ci_eq = [](const std::string& a, const char* b) {
        if (a.size() != std::strlen(b)) return false;
        for (size_t i = 0; i < a.size(); ++i)
            if (std::tolower(a[i]) != std::tolower(b[i])) return false;
        return true;
    };

    double m = inst_.<PREFIX>m;  // device multiplier (default 1.0 if no multiplier)

    // Operating-point parameters
    if (ci_eq(name, "gm"))   return inst_.<PREFIX>gm * m;
    if (ci_eq(name, "gds"))  return inst_.<PREFIX>gds * m;
    if (ci_eq(name, "id"))   return inst_.<PREFIX>cd * m;
    // ... add parameters from the ask.c switch table

    // Geometry (not scaled by m)
    if (ci_eq(name, "w"))    return inst_.<PREFIX>w;
    if (ci_eq(name, "l"))    return inst_.<PREFIX>l;

    // Terminal voltages (from state buffer)
    if (ci_eq(name, "vds"))  return state0_[inst_.<PREFIX>vds];
    if (ci_eq(name, "vgs"))  return state0_[inst_.<PREFIX>vgs];

    return std::nullopt;
}
```

### 8.3 Multiplier rules

- **Currents**: scale by `m` (parallel instances)
- **Conductances/transconductances**: scale by `m`
- **Capacitances**: scale by `m`
- **Voltages**: do NOT scale
- **Geometry (W, L, area)**: do NOT scale
- **Resistances**: scale by `1/m` (parallel reduces resistance)

---

## Phase 9: Parser Integration

### 9.1 Register the element type

In `src/parser/netlist_parser.cpp`, add parsing for the device's element card prefix.

SPICE element prefixes:
| Prefix | Device |
|---|---|
| M | MOSFET |
| D | Diode |
| Q | BJT |
| J | JFET |
| R | Resistor |
| C | Capacitor |
| L | Inductor |
| V | Voltage source |
| I | Current source |

### 9.2 Model card registration

Add model type recognition in the `.model` card parser:
```cpp
// In parse_model_card():
if (type == "nmos" || type == "pmos") { /* MOSFET */ }
if (type == "d")    { /* Diode */ }
if (type == "npn" || type == "pnp") { /* BJT */ }
```

### 9.3 Instance creation

Follow the BSIM4v7 pattern:
1. Parse terminal nodes from the element line
2. Look up (or create) the shared `ModelCard`
3. Parse geometry parameters from the element line
4. Call `<Prefix>Device::make(name, nodes..., geom, model_card)`
5. Parse and apply `ic=` if present
6. Add to circuit via `ckt.add_device()`

---

## Phase 10: Voltage Limiting (If Applicable)

Some devices need voltage limiting to help Newton convergence.

### 10.1 Check if needed

Look in the ngspice load function for calls to:
- `DEVfetlim()` — MOSFET gate voltage limiting
- `DEVlimvds()` — drain-source voltage limiting
- `DEVpnjlim()` — PN junction voltage limiting
- `limvds()`, `pnjlim()` — inline versions

### 10.2 Implement `limit_voltages()` override

If the UCB load function handles limiting internally (as BSIM4v7 does), no override
is needed — the limiting happens inside `evaluate()` via the translated load code.

If limiting needs to happen between Newton iterations:
```cpp
void <Prefix>Device::limit_voltages(const std::vector<double>& old_v,
                                     std::vector<double>& new_v) override;
```

---

## Phase 11: Testing

### 11.1 Basic DC test

Create a simple test circuit and verify DC operating point against ngspice:

```cpp
TEST(DeviceTest, BasicDC) {
    NgspiceRunner ng;
    auto ref = ng.run_dc("test_circuit.cir");

    Circuit ckt;
    // ... build circuit programmatically or parse netlist
    auto result = solve_dc(ckt);

    EXPECT_NEAR(result.solution[node_idx], ref["v(out)"], tolerance);
}
```

### 11.2 AC test (if ac_stamp implemented)

Test AC response at multiple frequencies against ngspice:

```cpp
TEST(DeviceTest, ACResponse) {
    // Compare magnitude/phase at key frequencies
    // Use 25% tolerance for near-threshold or high-gain operating points
}
```

### 11.3 Transient test (validates truncation + convergence)

```cpp
TEST(DeviceTest, Transient) {
    // Step response or pulse response
    // Verify timestep control doesn't explode
    // Compare waveform against ngspice at key time points
}
```

### 11.4 Parameter query test

```cpp
TEST(DeviceTest, QueryParams) {
    // Verify gm, gds, id, vth, capacitances
    // Test case-insensitive lookup
    // Test multiplier scaling
}
```

### 11.5 IC test

```cpp
TEST(DeviceTest, InitialConditions) {
    // Parse ic=V1,V2,V3
    // Verify empty field handling: ic=0.5,,0.1
    // Verify IC fields are set on instance
}
```

### 11.6 Test circuits

Create test circuits in `tests/circuits/`:
- Basic DC bias circuit
- AC small-signal circuit
- Transient switching circuit
- Multi-device circuit (tests multiplier and multi-instance)

---

## Phase 12: Integration Checklist

Before considering the migration complete, verify:

- [ ] `cmake --build build` compiles cleanly
- [ ] All new tests pass
- [ ] DC operating point matches ngspice (< 1% error for well-behaved circuits)
- [ ] AC response matches ngspice (< 25% for gain, < 10 degrees for phase)
- [ ] Transient simulation completes without convergence failures
- [ ] `query_param()` returns correct operating-point values
- [ ] `ic=` parsing works with empty fields
- [ ] Device multiplier `m` scales correctly
- [ ] Multi-device circuits work (shared ModelCard, separate instances)
- [ ] No memory leaks (ModelCard destructor frees linked lists)
- [ ] State buffer accesses use absolute offsets (`state_base_` + relative)

---

## Quick Reference: What the Tool Handles vs. Manual Work

| Feature | Auto-Tool | Manual |
|---|---|---|
| Struct definitions (_def.hpp) | Yes | - |
| Shim layer (CKT, matrix) | Yes | - |
| Adapter skeleton (Device interface) | Yes | - |
| Setup (node alloc, matrix ptrs) | Yes | Build fixes only |
| Load (DC + transient stamps) | Yes | Build fixes only |
| Temp (temperature params) | Yes | Build fixes only |
| Param/Mpar (parameter tables) | Yes | Build fixes only |
| CMakeLists.txt | Yes | - |
| AC stamp (G/C split) | - | **Full implementation** |
| Truncation error (LTE) | - | **Full implementation** |
| Convergence test (CKTnoncon) | - | **Wiring only** |
| Initial conditions (ic=) | - | **Parser + set_ic()** |
| Parameter query | - | **Full implementation** |
| Parser integration | - | **Element/model registration** |
| Voltage limiting | Inline in load | Override if external |
| Noise analysis | - | Not yet supported |
| Pole-zero analysis | - | Not yet supported |
| SOA checking | - | Not yet supported |
| Delete/Destroy | - | C++ RAII (automatic) |

---

## Architecture Reference

### Node Convention
- neospice: `GROUND_INTERNAL = -1`, real nodes >= 0
- UCB/ngspice: ground = 0, real nodes >= 1
- `neo_to_ucb(neo)` converts: `(neo < 0) ? 0 : (neo + 1)`

### State Ring Buffer
- 3 levels: `state0_` (current), `state1_` (previous), `state2_` (two steps ago)
- Per-device `state_base_` offset into global buffer
- All accesses: `state0_[state_base_ + relative_offset]`
- UCB code uses `*(ckt->CKTstate0 + inst->states + offset)` which translates to `state0_[inst.states + offset]` since `inst.states = state_base_`

### MatrixOffset Stamping
- Offsets pre-resolved during `assign_offsets()`
- Stored on instance struct as `int` fields (the `*Ptr` fields)
- `-1` is ground sentinel (no-op on stamp)
- `NumericMatrix::add(off, val)` no-ops when `off == -1`

### Device Multiplier `m`
- `inst.<PREFIX>m` (default 1.0)
- Scales all currents, conductances, capacitances in stamps
- Does NOT scale voltages or geometry

### IntegratorCtx (thread-local)
- Published via `tls_integrator_ctx` during Newton iteration
- Contains: mode, order, delta, ag[8], delta_old[8]
- `mode` bits: MODEDC=0x70, MODETRAN=0x100, MODEINITJCT=0x200, MODEINITFIX=0x400, MODEINITTRAN=0x1000

### Transient Integration Wiring
- Device LTE: `src/core/transient.cpp` calls `dev->compute_trunc()` after Newton convergence
- Device convergence: `src/core/newton.cpp` calls `dev->device_converged()` after node convergence
- Both already wired in the framework — just override the virtual methods
