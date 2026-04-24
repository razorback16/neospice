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
- `tools/descriptors/dio.yaml` — simple (2 terminals, 5 states)
- `tools/descriptors/bsim4v7.yaml` — complex MOSFET (geometry, version stamp, linked-list cleanup)
- `tools/descriptors/bsimsoi.yaml` — 6-terminal with `ac_load_file`, `noise_file`, `levels`, `spice_prefix`

---

## Phase 1: Create Descriptor YAML

Create `tools/descriptors/<device>.yaml`. This drives the entire auto-migration.

Use an existing descriptor as a template — `dio.yaml` for simple devices, `bsimsoi.yaml`
for full-featured ones. The key sections are:

- **Identity**: `ngspice_prefix`, `neospice_name`, struct names, C++ typedefs
- **Terminals**: name + ngspice field for each terminal node
- **State**: `state_count`, `state_base_field`, `charge_states` (offsets of charge variables)
- **Source files**: map roles (`setup`, `load`, `temp`, `param`, `mpar`, `devsup`) to filenames
- **Skip files**: list files the tool should not translate (AC, noise, trunc, etc.)
- **Model types**: drives model card generation (e.g., `{ spice_name: "nmos", flag_field: ..., flag_value: 1 }`)
- **Optional features**: `ac_load_file`, `noise_file`, `levels`, `spice_prefix`, `geometry`, `cleanup_linked_lists`, `version_stamp`

### Tips

- **Finding state_count**: Search the def header for the highest state offset + 1, or look for `#define <PREFIX>_STATES`.
- **Finding terminals**: Look at the setup function's node binding or the def header's node field definitions.
- **Finding cleanup lists**: Search the model struct for pointer fields with `pNext`-linked chains.
- **Geometry**: Look at the instance parameter table for geometric quantities (area, perimeter, width, length, multiplier).
- **AC load file**: The `*acld.c` file. When specified, the tool extracts G/C stamps and generates scaffolded `ac_stamp()` code.
- **Noise file**: The `*noi.c` file. When specified, the tool extracts THERMNOISE/SHOTNOISE/flicker patterns and generates scaffolded `noise_sources()` code.
- **Levels**: Look at the model card registration to find which LEVEL values the device uses (e.g., BSIMSOI uses LEVEL=10,58).
- **Spice prefix**: Element card prefix letter (M for MOSFETs, Q for BJTs, J for JFETs, Z for HFETs, D for diodes).

---

## Phase 2: Run the Auto-Migration Tool

```bash
python -m ngspice_migrate \
    tools/descriptors/<device>.yaml \
    ~/Codes/ngspice/src/spicelib/devices/<DEVICE>/ \
    src/devices/<device>/ \
    --gen-tests
```

Additional CLI flags:
- `--gen-tests` — generate test scaffolding (DC OP + AC comparison tests, SPICE circuits, CMakeLists.txt)
- `--test-dir PATH` — override test output directory (default: `tests/devices/<device>/`)

This produces all `.hpp`/`.cpp` files for the device: def, shim, adapter, translated source,
model card, parser helper, CMakeLists, and (with `--gen-tests`) test scaffolding + circuits.

---

## Phase 3: Build Fix Pass

After auto-migration, the code will likely have compilation errors.

### 3.1 Include fixups

The translated files may reference headers that don't exist. Fix includes:
```cpp
#include "devices/<ns>/<ns>_def.hpp"
#include "devices/<ns>/<ns>_shim.hpp"
```

### 3.2 Sensitivity analysis code

The tool auto-strips `SenCond`, `CKTsenInfo`, `goto next1/next2`, etc. Verify the
output is clean — search for `SenCond`, `CKTsenInfo`, `PERTURBATION`, `TRANSEN` (should be absent).

### 3.3 Untranslated macros

Common macros the tool doesn't handle:
- `TSTALLOC(ptr, row, col)` — matrix pointer reservation in setup
- `DEVfetlim`, `DEVlimvds`, `DEVpnjlim` — voltage limiting functions; implement in the shim or inline

Standard UCB macros (`MAX`, `MIN`, `FABS`, `CHARGE`, `CONSTvt0`, `IOP`/`IOPU`/`IP`/`OP`,
etc.) are provided by `src/devices/ucb_compat.hpp`. The migration tool emits
`#include "devices/ucb_compat.hpp"` in every translated file automatically.

Other macros the tool handles: `NIintegrate`, physical constants (`CONSTKoverQ`,
`CONSTe`, `CONSTboltz`, `REFTEMP`, `OFF`), `IF_COMPLEX`, `CKTtroubleElt`, `CKTsenInfo` stubs.

### 3.4 Build and iterate

```bash
cmake --build build 2>&1 | head -50
```

Fix errors one at a time. The most common are missing includes, undeclared identifiers
from untranslated macros, and type mismatches from C-to-C++ strictness.

---

## Phase 4: AC Stamp

**Status**: Scaffolded when `ac_load_file` is in the descriptor; otherwise manual.

ngspice stamps into a single complex matrix `(Y = G + jωC)`. Neospice uses separate
`G` and `C` matrices. When `ac_load_file` is specified, the tool extracts all
`*(here->XXXPtr) += expr` entries, classifies them as G (real) or C (imaginary), and
generates commented scaffolding in `ac_stamp()`. You need to:

1. Map the extracted ngspice expressions to actual neospice instance field names
2. Divide C-matrix entries by omega (ngspice stamps `cap * omega`; neospice multiplies later)
3. Scale all stamps by device multiplier `m`

Handle NQS/frequency-dependent conductances as unsupported with a warning (see BSIM4v7).

**References**: `bsim3v32_device.cpp:295`, `hisimhv_device.cpp:331`, `bsimsoi_device.cpp:345`, `bjt_device.cpp:258`

---

## Phase 4b: Noise Model

**Status**: Scaffolded when `noise_file` is in the descriptor; otherwise manual.

When `noise_file` is specified, the tool extracts THERMNOISE, SHOTNOISE, N_GAIN, and
flicker patterns and generates commented scaffolding with node pairs, noise types, and
PSD expressions. You need to:

1. Map extracted expressions to actual instance fields
2. Uncomment the `ns.push_back(...)` calls
3. Replace placeholder values with correct field references
4. Multiply by device multiplier `m`
5. Use `sim_temp()` for temperature — **never** declare a local `sim_temp_` member (shadows base class)
6. Convert UCB 1-based nodes to neospice 0-based: `neo_node = ucb_node - 1`

**References**: `bjt_device.cpp:368`, `bsim3v32_device.cpp:729`, `hisimhv_device.cpp:796`, `bsimsoi_device.cpp:1175`

---

## Phase 5: Truncation Error

**Status**: Auto-generated. Rarely needs manual work.

The tool extracts charge state offsets from `NIintegrate()` calls in the load source and
generates a complete `compute_trunc()` using the Gear-2 LTE formula with divided
differences. The descriptor can also override via `charge_states: [3]`.

Only override manually if the device has conditional charge logic or mode-dependent
integration (rare). Verify the auto-generated version against the ngspice `*trunc.c`
or load function for completeness.

**References**: `bsim3v32_device.cpp:604`, `hisim2_device.cpp:804`, `bjt_device.cpp:322`

---

## Phase 6: Convergence Test

**Status**: Fully auto-generated. No manual work needed.

The tool generates `device_converged()` and `last_noncon_` automatically. The framework
calls `device_converged()` after node-voltage convergence (`newton.cpp:151`).
The framework also invokes `branch_index()` for MNA-branch-bearing devices and
`process_temperature()` for temperature-dependent devices via virtual dispatch.

Only override if the device uses a separate `*cvtest.c` with `#ifdef NEWCONV` logic
that goes beyond the standard `CKTnoncon` check.

---

## Phase 7: Initial Conditions

**Status**: Manual.

### 7.1 Identify IC fields

Look in the def header for fields like `<PREFIX>icVDS` / `<PREFIX>icVDSGiven`.

### 7.2 Implement `set_ic()`

A simple setter that writes the IC values + Given flags onto the UCB instance struct.

### 7.3 Parser integration for `ic=`

Parse `ic=V1,V2,V3` on the element line. Handle empty fields (`ic=0.5,,0.1` means V2
not given). Split on comma, parse each non-empty field with `std::stod`, call `set_ic()`.

**References**: `bsim3v32_device.cpp:278`, `bjt_device.cpp:241`, `bsimsoi_device.cpp:1502`

---

## Phase 8: Parameter Query

**Status**: Skeleton auto-generated; fill in TODO stubs.

The tool generates a `query_param()` skeleton with direct field references for geometry
params and TODO stubs for operating-point params. Fill in the stubs with the correct
field mappings from the ngspice `*ask.c` switch table.

### Multiplier rules

- **Currents, conductances, capacitances**: scale by `m`
- **Voltages, geometry (W, L, area)**: do NOT scale
- **Resistances**: scale by `1/m`

**References**: `bsim3v32_device.cpp:666`, `hisimhv_device.cpp:722`, `hisim2_device.cpp:866`

---

## Phase 9: Parser Integration

**Status**: Model card + parser helper auto-generated when `model_types` defined. Element parsing is manual.

The tool generates `<ns>_model_card.hpp/cpp` and `<ns>_parser.hpp`. The model card
conversion uses `convert_model_card_params<>()` from `src/devices/model_card_utils.hpp` —
each device only provides a traits struct (param table pointer, size, mParam function,
type info) and the template handles the lookup + dispatch.

### Manual wiring needed

1. **Model dispatch**: Add the include and `to_<ns>_card()` call in the main parser's
   model type dispatch (see `netlist_parser.cpp:2762` for BSIM3v32 example)
2. **Element parsing**: Add parsing for the device's element card prefix in `netlist_parser.cpp`
   (parse terminal nodes, look up model card, parse geometry + `ic=`, create device, add to circuit)
3. **Model card ownership**: Call `ckt.add_model_card(std::move(card))` — the generic
   template method handles any model card type

### SPICE element prefixes

| Prefix | Device |
|---|---|
| M | MOSFET |  D | Diode |  Q | BJT |  J | JFET |  Z | HFET |
| R | Resistor |  C | Capacitor |  L | Inductor |
| V | Voltage source |  I | Current source |
| S | V-switch |  W | I-switch |  T | Transmission line |  K | Coupled inductor |
| E | VCVS |  G | VCCS |  H | CCVS |  F | CCCS |

---

## Phase 10: Voltage Limiting

If the ngspice load function calls `DEVfetlim()`, `DEVlimvds()`, or `DEVpnjlim()`, the
UCB load code handles limiting internally inside `evaluate()`. No override needed unless
limiting must happen between Newton iterations (implement `limit_voltages()` override).

---

## Phase 11: Testing

**Status**: DC + AC test scaffolding auto-generated with `--gen-tests`.

The tool generates `test_<ns>_compare.cpp` with a GTest fixture, DC/AC comparison tests,
and device-appropriate SPICE circuits in `tests/circuits/`. It infers device category
from `model_types` and uses `levels`/`spice_prefix` for correct LEVEL and element prefix.

### Manual additions

- Additional operating points (subthreshold, saturation, triode)
- Tighter tolerances once verified (default: 1% DC, 5% AC)
- Transient tests (not auto-generated)
- Noise comparison tests (tolerance: 10-20%)

---

## Phase 12: Integration Checklist

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
- [ ] `noise_sources()` returns correct noise types for the device
- [ ] Noise analysis matches ngspice within 20% tolerance
- [ ] No shadowing of base class `sim_temp_` (use `sim_temp()` accessor)

---

## Quick Reference: Auto vs. Manual

| Feature | Auto-Tool | Manual |
|---|---|---|
| Struct definitions (_def.hpp) | Yes | - |
| Shim layer (CKT, matrix, constants) | Yes | - |
| Physical constants, stubs (ucb_compat.hpp) | Yes | - |
| Parameter table macros (ucb_compat.hpp) | Yes | - |
| Adapter skeleton (uses ucb_device_init.hpp) | Yes | - |
| Convergence wiring (last_noncon_) | Yes | - |
| Sensitivity stripping | Yes | Verify output |
| Setup / Load / Temp / Param / Mpar | Yes | Build fixes |
| CMakeLists.txt | Yes | - |
| Model card (uses model_card_utils.hpp) | Yes (when `model_types` defined) | - |
| Truncation error (LTE) | Yes (from charge offsets) | Override if conditional |
| AC stamp (G/C split) | Scaffolded (when `ac_load_file`) | Map expressions to fields |
| Noise model | Scaffolded (when `noise_file`) | Map expressions to fields |
| Parameter query | Skeleton (fill TODOs) | OP param mappings + m-scaling |
| Test scaffolding (DC + AC) | Yes (`--gen-tests`) | Add transient/noise tests |
| Initial conditions (ic=) | - | Parser + set_ic() |
| Parser integration | Model card + helper generated | Element parsing + dispatch wiring |
| Voltage limiting | Inline in load | Override if external |
| Pole-zero / SOA | - | Not yet supported |

---

## Architecture Reference

### Shared Device Headers
- `src/devices/ucb_compat.hpp` — standard UCB macros (constants, math, param table builders)
- `src/devices/ucb_utils.hpp` — `neo_to_ucb()`, `ucb_to_neo()`, `str_tolower()`
- `src/devices/ucb_device_init.hpp` — `ucb_declare_internal_nodes()`, `ucb_stamp_pattern()`, `ucb_compute_offsets()`, `UCB_SPLICE_INSTANCE()`
- `src/devices/model_card_utils.hpp` — `convert_model_card_params<>()` template

### Node Convention
- neospice: `GROUND_INTERNAL = -1`, real nodes >= 0
- UCB/ngspice: ground = 0, real nodes >= 1
- `neo_to_ucb(neo)` from `ucb_utils.hpp` converts: `(neo < 0) ? 0 : (neo + 1)`

### State Ring Buffer
- 3 levels: `state0_` (current), `state1_` (previous), `state2_` (two steps ago)
- Per-device `state_base_` offset into global buffer
- All accesses: `state0_[state_base_ + relative_offset]`

### MatrixOffset Stamping
- Offsets pre-resolved during `assign_offsets()`; stored as `int` fields (the `*Ptr` fields)
- `-1` is ground sentinel (no-op on stamp)

### Device Multiplier `m`
- `inst.<PREFIX>m` (default 1.0)
- Scales all currents, conductances, capacitances in stamps; does NOT scale voltages or geometry

### IntegratorCtx
- Published via `tls_integrator_ctx` during Newton iteration
- Contains: mode, order, delta, ag[8], delta_old[8]
- `mode` bits: MODEDC=0x70, MODETRAN=0x100, MODEINITJCT=0x200, MODEINITFIX=0x400, MODEINITTRAN=0x1000

### Framework Wiring
- Device LTE: `transient.cpp:452` calls `dev->compute_trunc()` (via `evaluate_lte()` helper)
- Device convergence: `newton.cpp:151` calls `dev->device_converged()`
- Both already wired — just override the virtual methods
