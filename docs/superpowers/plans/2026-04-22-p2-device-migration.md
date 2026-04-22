# Priority 2 Device Migration — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Migrate 9 Priority 2 ngspice device models to neospice with ngspice comparison tests.

**Architecture:** Each device follows the 12-phase migrate-device workflow: create YAML descriptor → run auto-migration tool → fix build → manual AC stamp → noise → truncation → convergence → IC → query → parser → tests → verify. Devices within a tier run as parallel subagents in git worktrees.

**Tech Stack:** C++ (neospice framework), Python (ngspice_migrate tool), CMake, GoogleTest, ngspice (reference simulator)

---

## Common Patterns

All devices follow the same integration points. Each task references these patterns.

### File Structure Per Device

```
src/devices/<dev>/
  CMakeLists.txt          # OBJECT library
  <dev>_def.hpp           # auto-generated: UCB struct definitions
  <dev>_shim.hpp          # auto-generated: shim layer
  <dev>_shim.cpp          # auto-generated: shim implementation
  <dev>_setup.cpp         # auto-generated: node allocation
  <dev>_load.cpp          # auto-generated: stamp evaluation
  <dev>_temp.cpp          # auto-generated: temperature processing
  <dev>_param.cpp         # auto-generated: instance params
  <dev>_mpar.cpp          # auto-generated: model params
  <dev>_devsup.cpp        # auto-generated: device support
  <dev>_device.hpp        # MANUAL: Device subclass header
  <dev>_device.cpp        # MANUAL: AC stamp, noise, trunc, query, IC
tests/devices/<dev>/
  CMakeLists.txt          # test executable
  test_<dev>_compare.cpp  # ngspice comparison tests
tests/circuits/
  <dev>_*.cir             # test netlists
```

### CMakeLists.txt Template (src/devices/<dev>/)

```cmake
add_library(<dev>_obj OBJECT
    <dev>_setup.cpp
    <dev>_load.cpp
    <dev>_temp.cpp
    <dev>_param.cpp
    <dev>_mpar.cpp
    <dev>_devsup.cpp
    <dev>_shim.cpp
    <dev>_device.cpp
)
target_include_directories(<dev>_obj PUBLIC ${CMAKE_SOURCE_DIR}/src)
```

### CMakeLists.txt Template (tests/devices/<dev>/)

```cmake
add_executable(test_<dev>_compare
    test_<dev>_compare.cpp
    ${CMAKE_SOURCE_DIR}/tests/framework/ngspice_runner.cpp
    ${CMAKE_SOURCE_DIR}/tests/framework/comparator.cpp
)
target_link_libraries(test_<dev>_compare PRIVATE gtest_main neospice_lib)
target_include_directories(test_<dev>_compare PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_compile_definitions(test_<dev>_compare PRIVATE
    NGSPICE_BINARY="/usr/bin/ngspice"
    TEST_CIRCUITS_DIR="${CMAKE_SOURCE_DIR}/tests/circuits"
)
gtest_discover_tests(test_<dev>_compare)
```

### Parser Integration Points

- `src/parser/netlist_parser.cpp` — add level dispatch, `to_<dev>_card()`, deferred resolution
- `src/parser/model_cards.cpp` — add `to_<dev>_card()` function
- `src/parser/model_cards.hpp` — declare `to_<dev>_card()`
- `src/core/circuit.hpp` — add `add_<dev>_model_card()` and storage vector
- `src/core/circuit.cpp` — implement `add_<dev>_model_card()`
- `src/CMakeLists.txt` — add `add_subdirectory(devices/<dev>)` and link `<dev>_obj`

### Reference Files

- **Existing MOS1 migration** (Level 1 MOSFET, simplest reference):
  `src/devices/mos1/` — 12 files, 3919 LOC total
- **Existing BSIM3 migration** (Level 8/49 MOSFET, medium reference):
  `src/devices/bsim3/` — similar structure with size-dependent param cleanup
- **Migration tool guide:** `.claude/commands/migrate-device.md`
- **Descriptor examples:** `tools/descriptors/mos1.yaml`, `tools/descriptors/bsim3.yaml`
- **ngspice source:** `~/Codes/ngspice/src/spicelib/devices/<ngspice_dir>/`

---

## Tier 1: Easy Devices (4 parallel subagents)

---

### Task 1: Migrate HFET2

**Device:** HFET2 — heterojunction FET model (GaAs/GaN), 3-terminal, 13 states, 2,196 LOC.

**ngspice source:** `~/Codes/ngspice/src/spicelib/devices/hfet2/`

**Files:**
- Create: `tools/descriptors/hfet2.yaml`
- Create: `src/devices/hfet2/` (all files per template above)
- Create: `tests/devices/hfet2/CMakeLists.txt`
- Create: `tests/devices/hfet2/test_hfet2_compare.cpp`
- Create: `tests/circuits/hfet2_nfet_dc.cir`, `tests/circuits/hfet2_nfet_ac.cir`
- Modify: `src/CMakeLists.txt` — add subdirectory and link
- Modify: `src/parser/netlist_parser.cpp` — NHFET/PHFET model type dispatch
- Modify: `src/parser/model_cards.hpp` / `src/parser/model_cards.cpp` — `to_hfet2_card()`
- Modify: `src/core/circuit.hpp` / `src/core/circuit.cpp` — model card storage
- Modify: `tests/CMakeLists.txt` — add `add_subdirectory(devices/hfet2)`

- [ ] **Step 1: Create descriptor YAML**

Create `tools/descriptors/hfet2.yaml`:
```yaml
model:
  ngspice_prefix: "HFET2"
  neospice_name: "hfet2"
  neospice_namespace: "hfet2"
  instance_struct: "HFET2instance"
  model_struct: "HFET2model"
  instance_tag: "sHFET2instance"
  model_tag: "sHFET2model"
  cpp_instance: "HFET2Instance"
  cpp_model: "HFET2Model"
  gen_instance: "GENinstance"
  gen_model: "GENmodel"
  terminals:
    - { name: "drain", field: "HFET2drainNode" }
    - { name: "gate", field: "HFET2gateNode" }
    - { name: "source", field: "HFET2sourceNode" }
  state_count: 13
  state_base_field: "HFET2state"
  next_instance_field: "HFET2nextInstance"
  instances_field: "HFET2instances"
  next_model_field: "HFET2nextModel"
  model_ptr_field: "HFET2modPtr"
  name_field: "HFET2name"
  matrix_ptr_suffix: "Ptr"
  source_files:
    setup: "hfet2setup.c"
    load: "hfet2load.c"
    temp: "hfet2temp.c"
    param: "hfet2param.c"
    mpar: "hfet2mpar.c"
    devsup: "hfet2.c"
  skip_files:
    - "hfet2acl.c"
    - "hfet2ask.c"
    - "hfet2del.c"
    - "hfet2dest.c"
    - "hfet2getic.c"
    - "hfet2init.c"
    - "hfet2mask.c"
    - "hfet2mdel.c"
    - "hfet2pzl.c"
    - "hfet2trunc.c"
  defines: []
  has_internal_nodes: true
  setup_function: "HFET2setup"
  temp_function: "HFET2temp"
  load_function: "HFET2load"
  geometry:
    - { name: "L", field: "HFET2length", given: "HFET2lengthGiven", default: "1e-6" }
    - { name: "W", field: "HFET2width", given: "HFET2widthGiven", default: "20e-6" }
    - { name: "M", field: "HFET2m", given: "HFET2mGiven", default: "1.0" }
  model_types:
    - { spice_name: "nhfet", flag_field: "HFET2type", flag_value: 1 }
    - { spice_name: "phfet", flag_field: "HFET2type", flag_value: -1 }
```

- [ ] **Step 2: Run auto-migration tool**

```bash
cd /home/subhagato/Codes/spice-cpp
python -m ngspice_migrate tools/descriptors/hfet2.yaml ~/Codes/ngspice/src/spicelib/devices/hfet2/
```

This generates `src/devices/hfet2/` with auto-translated files. Review the output for errors.

- [ ] **Step 3: Build fix pass**

Fix compilation errors in the auto-generated code:
- Add missing includes
- Fix macro expansions (CKT*, HERE, DEV*, etc.)
- Fix matrix stamp patterns (state ring buffer access)
- Add CMakeLists.txt to `src/devices/hfet2/`
- Add `add_subdirectory(devices/hfet2)` and `$<TARGET_OBJECTS:hfet2_obj>` to `src/CMakeLists.txt`

Build until clean: `cd build && cmake .. && cmake --build . -j$(nproc)`

- [ ] **Step 4: Implement Device adapter (hfet2_device.hpp/cpp)**

Create `src/devices/hfet2/hfet2_device.hpp` following `src/devices/mos1/mos1_device.hpp` as template.
Key differences from MOS1:
- 3 terminals (drain, gate, source) instead of 4
- Geometry: L, W, M (not W, L, AD, AS, PD, PS, NRD, NRS, M)
- 13 state variables instead of 17
- Model types: nhfet/phfet instead of nmos/pmos

Implement in `hfet2_device.cpp`:
- `make()` — create device, wire UCB instance fields
- `declare_internal_nodes()` — 2 internal nodes (drain', source')
- `stamp_pattern()` / `assign_offsets()` — from the UCB matrix pointer journal
- `evaluate()` — call UCB load via shim
- `set_state_ptrs()` — wire state ring buffer

- [ ] **Step 5: AC stamp (manual G/C split)**

Read `~/Codes/ngspice/src/spicelib/devices/hfet2/hfet2acl.c`. Extract conductance (G) and capacitance (C) entries. Implement `ac_stamp()` in `hfet2_device.cpp` following MOS1 pattern:
- G matrix: transconductance entries (gm, gds, Ggs, Ggd)
- C matrix: gate capacitance entries (Cgs, Cgd from charge derivatives)

- [ ] **Step 6: Truncation error**

Implement `compute_trunc()`. Identify charge state offsets from the `hfet2load.c` NIintegrate calls. Use Gear-2 LTE formula over those offsets. Follow `src/devices/mos1/mos1_device.cpp` compute_trunc pattern.

- [ ] **Step 7: Convergence and query**

Implement `device_converged()` — check UCB's noncon flag.
Implement `query_param()` — expose gm, gds, id, vgs, vds key operating-point values.

- [ ] **Step 8: Parser integration**

HFET2 uses NHFET/PHFET model type (not M-card MOSFET). It needs its own parse path:
- Add `to_hfet2_card()` in `src/parser/model_cards.cpp` — reads NHFET/PHFET model params
- Add model card storage to `src/core/circuit.hpp/cpp`
- In `netlist_parser.cpp`: detect NHFET/PHFET model type → create HFET2 devices
- HFET2 devices use 3-terminal syntax similar to J-cards (drain gate source)

Check how ngspice parses HFET2 — it may use M-card or have its own prefix. Reference `~/Codes/ngspice/src/spicelib/devices/hfet2/hfet2init.c` for the SPICEdev registration.

- [ ] **Step 9: Test circuits and ngspice comparison**

Create test circuits:
- `tests/circuits/hfet2_nfet_dc.cir` — NHFET DC operating point (Vgs sweep)
- `tests/circuits/hfet2_nfet_ac.cir` — NHFET AC small-signal response

Create `tests/devices/hfet2/test_hfet2_compare.cpp` with at least:
- DC operating point comparison vs ngspice
- AC gain comparison vs ngspice (if applicable)

Add `add_subdirectory(devices/hfet2)` to `tests/CMakeLists.txt`.

- [ ] **Step 10: Verify all tests pass**

```bash
cd build && cmake .. && cmake --build . -j$(nproc) && ctest --output-on-failure
```

All 817+ tests must pass. Zero regressions.

- [ ] **Step 11: Commit**

```bash
git add src/devices/hfet2/ tools/descriptors/hfet2.yaml tests/devices/hfet2/ tests/circuits/hfet2_* src/CMakeLists.txt src/parser/ src/core/circuit.hpp src/core/circuit.cpp tests/CMakeLists.txt
git commit -m "feat: migrate HFET2 device model from ngspice"
```

---

### Task 2: Migrate JFET2

**Device:** JFET2 — Parker-Skellern JFET model, 3-terminal, 18 states, 2,453 LOC.

**ngspice source:** `~/Codes/ngspice/src/spicelib/devices/jfet2/`

**Files:**
- Create: `tools/descriptors/jfet2.yaml`
- Create: `src/devices/jfet2/` (all files per template)
- Create: `tests/devices/jfet2/CMakeLists.txt`, `test_jfet2_compare.cpp`
- Create: `tests/circuits/jfet2_njf_dc.cir`, `tests/circuits/jfet2_njf_ac.cir`
- Modify: `src/CMakeLists.txt`, `src/parser/netlist_parser.cpp`, `src/parser/model_cards.*`, `src/core/circuit.*`, `tests/CMakeLists.txt`

- [ ] **Step 1: Create descriptor YAML**

Create `tools/descriptors/jfet2.yaml`:
```yaml
model:
  ngspice_prefix: "JFET2"
  neospice_name: "jfet2"
  neospice_namespace: "jfet2"
  instance_struct: "JFET2instance"
  model_struct: "JFET2model"
  instance_tag: "sJFET2instance"
  model_tag: "sJFET2model"
  cpp_instance: "JFET2Instance"
  cpp_model: "JFET2Model"
  gen_instance: "GENinstance"
  gen_model: "GENmodel"
  terminals:
    - { name: "drain", field: "JFET2drainNode" }
    - { name: "gate", field: "JFET2gateNode" }
    - { name: "source", field: "JFET2sourceNode" }
  state_count: 18
  state_base_field: "JFET2state"
  next_instance_field: "JFET2nextInstance"
  instances_field: "JFET2instances"
  next_model_field: "JFET2nextModel"
  model_ptr_field: "JFET2modPtr"
  name_field: "JFET2name"
  matrix_ptr_suffix: "Ptr"
  source_files:
    setup: "jfet2set.c"
    load: "jfet2load.c"
    temp: "jfet2temp.c"
    param: "jfet2par.c"
    mpar: "jfet2mpar.c"
    devsup: "jfet2.c"
  skip_files:
    - "jfet2acld.c"
    - "jfet2ask.c"
    - "jfet2del.c"
    - "jfet2dest.c"
    - "jfet2ic.c"
    - "jfet2init.c"
    - "jfet2mask.c"
    - "jfet2mdel.c"
    - "jfet2noi.c"
    - "jfet2trun.c"
    - "psmodel.c"
  defines: []
  has_internal_nodes: true
  setup_function: "JFET2setup"
  temp_function: "JFET2temp"
  load_function: "JFET2load"
  geometry:
    - { name: "area", field: "JFET2area", given: "JFET2areaGiven", default: "1.0" }
    - { name: "m", field: "JFET2m", given: "JFET2mGiven", default: "1.0" }
  model_types:
    - { spice_name: "njf", flag_field: "JFET2type", flag_value: 1 }
    - { spice_name: "pjf", flag_field: "JFET2type", flag_value: -1 }
```

- [ ] **Step 2: Run auto-migration tool**

```bash
python -m ngspice_migrate tools/descriptors/jfet2.yaml ~/Codes/ngspice/src/spicelib/devices/jfet2/
```

**Important:** `psmodel.c` is in skip_files because it's a helper that should be compiled alongside the load function. After auto-migration, manually copy and translate `psmodel.c` into the device directory as `jfet2_psmodel.cpp`, or include it in the load translation.

- [ ] **Step 3: Build fix pass**

Same pattern as HFET2. Additionally:
- Ensure `psmodel.c` functions are available to the load function (may need to include as a separate translation unit or inline)
- Add to CMakeLists.txt, link into neospice_lib

- [ ] **Step 4: Implement Device adapter**

Follow JFET1 pattern (`src/devices/jfet/jfet_device.hpp`). Key fields:
- 3 terminals, 2 internal nodes (drain', source')
- 18 state variables
- Geometry: area, m

- [ ] **Step 5: AC stamp (manual G/C split)**

Read `~/Codes/ngspice/src/spicelib/devices/jfet2/jfet2acld.c`. Extract G and C entries. JFET2 has gate junction caps (Cgs, Cgd) plus channel transconductance.

- [ ] **Step 6: Noise model**

Read `~/Codes/ngspice/src/spicelib/devices/jfet2/jfet2noi.c`. Implement `noise_sources()`:
- Thermal noise: drain resistance (Rd), source resistance (Rs), channel (2/3 * gm)
- Flicker noise: KF/AF model

- [ ] **Step 7: Truncation, convergence, query**

- `compute_trunc()`: Identify charge state offsets from NIintegrate calls in load
- `device_converged()`: Check UCB noncon flag
- `query_param()`: gm, gds, id, vgs, vds

- [ ] **Step 8: Parser integration**

JFET2 uses NJF/PJF model types with LEVEL=2. Extend the existing JFET dispatch in `netlist_parser.cpp` (lines 2679-2718):
- Add `detect_jfet_level()` (similar to `detect_mosfet_level()`)
- LEVEL=1 (default) → existing JFET; LEVEL=2 → JFET2
- Add `to_jfet2_card()` in model_cards
- Add model card storage in circuit.hpp/cpp

- [ ] **Step 9: Tests**

Create:
- `tests/circuits/jfet2_njf_dc.cir` — NJF DC operating point
- `tests/circuits/jfet2_njf_ac.cir` — NJF AC response
- `tests/devices/jfet2/test_jfet2_compare.cpp` — DC + AC ngspice comparison

- [ ] **Step 10: Verify all tests pass**

```bash
cd build && cmake .. && cmake --build . -j$(nproc) && ctest --output-on-failure
```

- [ ] **Step 11: Commit**

```bash
git add src/devices/jfet2/ tools/descriptors/jfet2.yaml tests/devices/jfet2/ tests/circuits/jfet2_* src/CMakeLists.txt src/parser/ src/core/circuit.* tests/CMakeLists.txt
git commit -m "feat: migrate JFET2 (Parker-Skellern) device model from ngspice"
```

---

### Task 3: Migrate MOS3

**Device:** MOS3 — SPICE Level 3 MOSFET, 4-terminal, 17 states, 8,502 LOC. Nearly identical structure to existing MOS1.

**ngspice source:** `~/Codes/ngspice/src/spicelib/devices/mos3/`

**Files:**
- Create: `tools/descriptors/mos3.yaml`
- Create: `src/devices/mos3/` (all files per template)
- Create: `tests/devices/mos3/`, test circuits
- Modify: `src/CMakeLists.txt`, `src/parser/netlist_parser.cpp` (LEVEL=3 dispatch), `src/parser/model_cards.*`, `src/core/circuit.*`, `tests/CMakeLists.txt`

- [ ] **Step 1: Create descriptor YAML**

Create `tools/descriptors/mos3.yaml`:
```yaml
model:
  ngspice_prefix: "MOS3"
  neospice_name: "mos3"
  neospice_namespace: "mos3"
  instance_struct: "MOS3instance"
  model_struct: "MOS3model"
  instance_tag: "sMOS3instance"
  model_tag: "sMOS3model"
  cpp_instance: "MOS3Instance"
  cpp_model: "MOS3Model"
  gen_instance: "GENinstance"
  gen_model: "GENmodel"
  terminals:
    - { name: "d", field: "MOS3dNode" }
    - { name: "g", field: "MOS3gNode" }
    - { name: "s", field: "MOS3sNode" }
    - { name: "b", field: "MOS3bNode" }
  state_count: 17
  state_base_field: "MOS3states"
  next_instance_field: "MOS3nextInstance"
  instances_field: "MOS3instances"
  next_model_field: "MOS3nextModel"
  model_ptr_field: "MOS3modPtr"
  name_field: "MOS3name"
  matrix_ptr_suffix: "Ptr"
  source_files:
    setup: "mos3set.c"
    load: "mos3load.c"
    temp: "mos3temp.c"
    param: "mos3par.c"
    mpar: "mos3mpar.c"
    devsup: "mos3.c"
  skip_files:
    - "mos3acld.c"
    - "mos3ask.c"
    - "mos3conv.c"
    - "mos3del.c"
    - "mos3dest.c"
    - "mos3dist.c"
    - "mos3dset.c"
    - "mos3ic.c"
    - "mos3init.c"
    - "mos3mask.c"
    - "mos3mdel.c"
    - "mos3noi.c"
    - "mos3pzld.c"
    - "mos3sacl.c"
    - "mos3sld.c"
    - "mos3sprt.c"
    - "mos3sset.c"
    - "mos3supd.c"
    - "mos3trun.c"
  defines: []
  has_internal_nodes: true
  setup_function: "MOS3setup"
  temp_function: "MOS3temp"
  load_function: "MOS3load"
  geometry:
    - { name: "W", field: "MOS3w", given: "MOS3wGiven", default: "1e-6" }
    - { name: "L", field: "MOS3l", given: "MOS3lGiven", default: "1e-4" }
    - { name: "AD", field: "MOS3drainArea", given: "MOS3drainAreaGiven", default: "0.0" }
    - { name: "AS", field: "MOS3sourceArea", given: "MOS3sourceAreaGiven", default: "0.0" }
    - { name: "PD", field: "MOS3drainPerimiter", given: "MOS3drainPerimiterGiven", default: "0.0" }
    - { name: "PS", field: "MOS3sourcePerimiter", given: "MOS3sourcePerimiterGiven", default: "0.0" }
    - { name: "NRD", field: "MOS3drainSquares", given: "MOS3drainSquaresGiven", default: "0.0" }
    - { name: "NRS", field: "MOS3sourceSquares", given: "MOS3sourceSquaresGiven", default: "0.0" }
    - { name: "M", field: "MOS3m", given: "MOS3mGiven", default: "1.0" }
  model_types:
    - { spice_name: "nmos", flag_field: "MOS3type", flag_value: 1 }
    - { spice_name: "pmos", flag_field: "MOS3type", flag_value: -1 }
  charge_states: [5, 8, 11, 13, 15]
```

- [ ] **Step 2: Run auto-migration**

```bash
python -m ngspice_migrate tools/descriptors/mos3.yaml ~/Codes/ngspice/src/spicelib/devices/mos3/
```

- [ ] **Step 3: Build fix pass**

Fix compilation. MOS3 is structurally identical to MOS1 — same terminal count, same state count, same Meyer capacitance model. Use MOS1 as direct reference for any ambiguity.

- [ ] **Step 4: Implement Device adapter**

Copy `src/devices/mos1/mos1_device.hpp` as starting point, rename MOS1→MOS3 throughout. The interface is identical: 4 terminals, 17 states, same Geom struct (W,L,AD,AS,PD,PS,NRD,NRS,M), same IC (vds,vgs,vbs).

- [ ] **Step 5: AC stamp**

Read `~/Codes/ngspice/src/spicelib/devices/mos3/mos3acld.c`. Meyer capacitance G/C split — same pattern as MOS1 (`mos1acld.c`). The matrix positions are identical (6x6 node matrix: d,g,s,b,d',s').

- [ ] **Step 6: Noise model**

Read `~/Codes/ngspice/src/spicelib/devices/mos3/mos3noi.c`. Standard MOS noise:
- Thermal: Rd, Rs, channel (8kT/3 * gm)
- Flicker: KF/AF model

Same pattern as MOS1 noise.

- [ ] **Step 7: Truncation, convergence, query**

- `compute_trunc()`: Same 5 charge states as MOS1: offsets [5, 8, 11, 13, 15]
- `device_converged()`: Check UCB noncon
- `query_param()`: gm, gds, id, vth, vdsat

- [ ] **Step 8: Parser integration**

Add LEVEL=3 to the MOSFET dispatch in `netlist_parser.cpp`:
- Add `} else if (level == 3) {` block after the LEVEL=1 block (line 2503)
- Add `to_mos3_card()` in model_cards
- Add `MOS3ModelCard` storage in circuit
- Wire up DeferredMOSFET → MOS3Device::make() with same Geom fields as MOS1

- [ ] **Step 9: Tests**

Create:
- `tests/circuits/mos3_nmos_dc_op.cir` — NMOS LEVEL=3 DC operating point
- `tests/circuits/mos3_nmos_iv_sweep.cir` — NMOS IV curve sweep
- `tests/circuits/mos3_nmos_ac.cir` — NMOS AC response
- `tests/devices/mos3/test_mos3_compare.cpp` — DC OP, IV sweep, AC comparison vs ngspice

- [ ] **Step 10: Verify**

```bash
cd build && cmake .. && cmake --build . -j$(nproc) && ctest --output-on-failure
```

- [ ] **Step 11: Commit**

```bash
git commit -m "feat: migrate MOS3 (Level 3) device model from ngspice"
```

---

### Task 4: Migrate MOS9

**Device:** MOS9 — Modified Level 3 MOSFET, 4-terminal, 17 states, 8,539 LOC. Nearly identical to MOS3.

**ngspice source:** `~/Codes/ngspice/src/spicelib/devices/mos9/`

- [ ] **Step 1: Create descriptor YAML**

Create `tools/descriptors/mos9.yaml` — identical structure to mos3.yaml with `MOS3` → `MOS9` prefix substitution. Same terminals, same state count, same geometry fields (note: MOS9 uses `MOS9drainPerimiter` with same typo as MOS3).

- [ ] **Step 2: Run auto-migration**

```bash
python -m ngspice_migrate tools/descriptors/mos9.yaml ~/Codes/ngspice/src/spicelib/devices/mos9/
```

- [ ] **Step 3: Build fix pass**

Same as MOS3. MOS9 is structurally identical.

- [ ] **Step 4: Device adapter**

Copy from MOS3 device adapter (once MOS3 is done) or from MOS1. Rename MOS3→MOS9.

- [ ] **Step 5: AC stamp**

Read `~/Codes/ngspice/src/spicelib/devices/mos9/mos9acld.c`. Same Meyer cap pattern.

- [ ] **Step 6: Noise model**

Read `~/Codes/ngspice/src/spicelib/devices/mos9/mos9noi.c`. Same pattern as MOS3.

- [ ] **Step 7: Truncation, convergence, query**

Same charge states [5, 8, 11, 13, 15]. Same interface.

- [ ] **Step 8: Parser integration**

Add LEVEL=9 to MOSFET dispatch: `} else if (level == 9) {`
Add `to_mos9_card()`, `MOS9ModelCard` storage.

- [ ] **Step 9: Tests**

Create:
- `tests/circuits/mos9_nmos_dc_op.cir`
- `tests/circuits/mos9_nmos_ac.cir`
- `tests/devices/mos9/test_mos9_compare.cpp` — DC + AC comparison vs ngspice

- [ ] **Step 10: Verify**

```bash
cd build && cmake .. && cmake --build . -j$(nproc) && ctest --output-on-failure
```

- [ ] **Step 11: Commit**

```bash
git commit -m "feat: migrate MOS9 (Modified Level 3) device model from ngspice"
```

---

## Tier 2: Medium Devices (2 parallel subagents)

---

### Task 5: Migrate HFET1

**Device:** HFET1 — heterojunction FET model A, 3-terminal, 24 states, 3,218 LOC. More internal nodes than HFET2 (5 vs 2).

**ngspice source:** `~/Codes/ngspice/src/spicelib/devices/hfet1/`

**Note:** ngspice uses prefix `HFETA` (not `HFET1`) in struct/field names.

- [ ] **Step 1: Create descriptor YAML**

Create `tools/descriptors/hfet1.yaml`:
```yaml
model:
  ngspice_prefix: "HFETA"
  neospice_name: "hfet1"
  neospice_namespace: "hfet1"
  instance_struct: "HFETAinstance"
  model_struct: "HFETAmodel"
  instance_tag: "sHFETAinstance"
  model_tag: "sHFETAmodel"
  cpp_instance: "HFETAInstance"
  cpp_model: "HFETAModel"
  gen_instance: "GENinstance"
  gen_model: "GENmodel"
  terminals:
    - { name: "drain", field: "HFETAdrainNode" }
    - { name: "gate", field: "HFETAgateNode" }
    - { name: "source", field: "HFETAsourceNode" }
  state_count: 24
  state_base_field: "HFETAstate"
  next_instance_field: "HFETAnextInstance"
  instances_field: "HFETAinstances"
  next_model_field: "HFETAnextModel"
  model_ptr_field: "HFETAmodPtr"
  name_field: "HFETAname"
  matrix_ptr_suffix: "Ptr"
  source_files:
    setup: "hfetsetup.c"
    load: "hfetload.c"
    temp: "hfettemp.c"
    param: "hfetparam.c"
    mpar: "hfetmpar.c"
    devsup: "hfet.c"
  skip_files:
    - "hfetacl.c"
    - "hfetask.c"
    - "hfetdel.c"
    - "hfetdest.c"
    - "hfetgetic.c"
    - "hfetinit.c"
    - "hfetmask.c"
    - "hfetmdel.c"
    - "hfetpzl.c"
    - "hfettrunc.c"
  defines: []
  has_internal_nodes: true
  setup_function: "HFETAsetup"
  temp_function: "HFETAtemp"
  load_function: "HFETAload"
  geometry:
    - { name: "L", field: "HFETAlength", given: "HFETAlengthGiven", default: "1e-6" }
    - { name: "W", field: "HFETAwidth", given: "HFETAwidthGiven", default: "20e-6" }
    - { name: "M", field: "HFETAm", given: "HFETAmGiven", default: "1.0" }
  model_types:
    - { spice_name: "nhfet", flag_field: "HFETAtype", flag_value: 1 }
    - { spice_name: "phfet", flag_field: "HFETAtype", flag_value: -1 }
```

- [ ] **Step 2-7: Auto-migrate, build fix, device adapter, AC, trunc, query**

Same workflow as HFET2 but with 5 internal nodes instead of 2:
- drain', gate', source', drainPrmPrm, sourcePrmPrm
- Larger matrix (8x8 instead of 5x5)
- More charge states (24 vs 13)
- No noise model (same as HFET2)

- [ ] **Step 8: Parser integration**

HFET1 shares NHFET/PHFET model types with HFET2. Need LEVEL dispatch:
- NHFET/PHFET LEVEL=5 → HFET1
- NHFET/PHFET LEVEL=6 → HFET2
- Default → HFET1 (or HFET2 — check ngspice convention)

If HFET2 was migrated in Tier 1, extend its parser dispatch to include LEVEL differentiation.

- [ ] **Step 9-11: Tests, verify, commit**

Same pattern. Test circuits with NHFET LEVEL=5 model.

---

### Task 6: Migrate BSIM3v32

**Device:** BSIM3v32 — BSIM3 version 3.24, 4-terminal, 17 states, 13,530 LOC. Similar to existing BSIM3 v3.3 but different version.

**ngspice source:** `~/Codes/ngspice/src/spicelib/devices/bsim3v32/`

- [ ] **Step 1: Create descriptor YAML**

Create `tools/descriptors/bsim3v32.yaml`:
```yaml
model:
  ngspice_prefix: "BSIM3v32"
  neospice_name: "bsim3v32"
  neospice_namespace: "bsim3v32"
  instance_struct: "BSIM3v32instance"
  model_struct: "BSIM3v32model"
  instance_tag: "sBSIM3v32instance"
  model_tag: "sBSIM3v32model"
  cpp_instance: "BSIM3v32Instance"
  cpp_model: "BSIM3v32Model"
  gen_instance: "GENinstance"
  gen_model: "GENmodel"
  terminals:
    - { name: "d", field: "BSIM3v32dNode" }
    - { name: "g", field: "BSIM3v32gNode" }
    - { name: "s", field: "BSIM3v32sNode" }
    - { name: "b", field: "BSIM3v32bNode" }
  state_count: 17
  state_base_field: "BSIM3v32states"
  next_instance_field: "BSIM3v32nextInstance"
  instances_field: "BSIM3v32instances"
  next_model_field: "BSIM3v32nextModel"
  model_ptr_field: "BSIM3v32modPtr"
  name_field: "BSIM3v32name"
  matrix_ptr_suffix: "Ptr"
  source_files:
    setup: "b3v32set.c"
    load: "b3v32ld.c"
    temp: "b3v32temp.c"
    check: "b3v32check.c"
    param: "b3v32par.c"
    mpar: "b3v32mpar.c"
    devsup: "b3v32.c"
  skip_files:
    - "b3v32acld.c"
    - "b3v32noi.c"
    - "b3v32pzld.c"
    - "b3v32cvtest.c"
    - "b3v32ask.c"
    - "b3v32mask.c"
    - "b3v32del.c"
    - "b3v32dest.c"
    - "b3v32getic.c"
    - "b3v32trunc.c"
    - "bsim3v32init.c"
  defines: []
  has_internal_nodes: true
  setup_function: "BSIM3v32setup"
  temp_function: "BSIM3v32temp"
  load_function: "BSIM3v32load"
  cleanup_linked_lists:
    - { field: "pSizeDependParamKnot", next_field: "pNext" }
  version_stamp:
    field: "BSIM3v32version"
    given_field: "BSIM3v32versionGiven"
    value: "3.24"
  geometry:
    - { name: "W", field: "BSIM3v32w", given: "BSIM3v32wGiven", default: "1e-6", always_given: true }
    - { name: "L", field: "BSIM3v32l", given: "BSIM3v32lGiven", default: "1e-7", always_given: true }
    - { name: "AD", field: "BSIM3v32drainArea", given: "BSIM3v32drainAreaGiven", default: "0.0" }
    - { name: "AS", field: "BSIM3v32sourceArea", given: "BSIM3v32sourceAreaGiven", default: "0.0" }
    - { name: "PD", field: "BSIM3v32drainPerimeter", given: "BSIM3v32drainPerimeterGiven", default: "0.0" }
    - { name: "PS", field: "BSIM3v32sourcePerimeter", given: "BSIM3v32sourcePerimeterGiven", default: "0.0" }
    - { name: "NRD", field: "BSIM3v32drainSquares", given: "BSIM3v32drainSquaresGiven", default: "0.0" }
    - { name: "NRS", field: "BSIM3v32sourceSquares", given: "BSIM3v32sourceSquaresGiven", default: "0.0" }
    - { name: "M", field: "BSIM3v32m", given: "BSIM3v32mGiven", default: "1.0" }
  model_types:
    - { spice_name: "nmos", flag_field: "BSIM3v32type", flag_value: 1 }
    - { spice_name: "pmos", flag_field: "BSIM3v32type", flag_value: -1 }
```

- [ ] **Step 2-7: Auto-migrate, build fix, device adapter, AC, noise, trunc**

Follow existing BSIM3 v3.3 as reference (`src/devices/bsim3/`). Key differences:
- NQS Q-node (3 internal nodes instead of 2)
- Size-dependent param linked list cleanup (same pattern as BSIM3)
- AC stamp has NQS entries (Q-node matrix positions)
- Noise model has multiple variants (noiMod 1-6)

- [ ] **Step 8: Parser integration**

BSIM3v32 uses LEVEL=8 or 49 — same as existing BSIM3 v3.3. Dispatch by version:
- Check `VERSION` model parameter: if 3.24 → BSIM3v32, if 3.3 → BSIM3 (existing)
- If no VERSION specified, default to BSIM3 v3.3 (existing behavior)
- Modify `detect_mosfet_level()` or add version-aware dispatch after level detection

- [ ] **Step 9-11: Tests, verify, commit**

Test with model card specifying `VERSION=3.24`. Compare against ngspice.

---

## Tier 3: Hard Devices (sequential)

---

### Task 7: Migrate HiSIM2

**Device:** HiSIM2 — Hiroshima-university STARC IGFET Model, 4-terminal, 18-21 states, 20,549 LOC.

**ngspice source:** `~/Codes/ngspice/src/spicelib/devices/hisim2/`

**ngspice prefix:** `HSM2`

- [ ] **Step 1: Create descriptor YAML**

Create `tools/descriptors/hisim2.yaml` with prefix `HSM2`. Terminals: D,G,S,B. Internal nodes: D',G',S',B',DB,SB. State count: 18 (or 21 with NQS). Geometry: `HSM2_l`, `HSM2_w`, `HSM2_m`, `HSM2_ad`, `HSM2_as`.

Source files: `hsm2set.c`, `hsm2ld.c`, `hsm2temp.c`, `hsm2par.c`, `hsm2mpar.c`, `hsm2.c`.

- [ ] **Step 2-7: Auto-migrate, build fix, adapter, AC, noise, trunc**

HiSIM2 has binning parameters (HSM2binningParam) — a complex parameter mapping layer. The auto-migration tool should handle the basic translation, but binning-aware parameter setup may need manual attention.

LEVEL=61 or 68 in the MOSFET dispatch.

- [ ] **Step 8: Parser integration**

Add LEVEL=61/68 to MOSFET dispatch.
HiSIM2 has a very large model parameter table — `to_hisim2_card()` will need to map many parameters.

- [ ] **Step 9-11: Tests, verify, commit**

```bash
git commit -m "feat: migrate HiSIM2 device model from ngspice"
```

---

### Task 8: Migrate HiSIM_HV

**Device:** HiSIM_HV — High-Voltage variant, 5-terminal, 31-36 states, 22,762 LOC.

**ngspice source:** `~/Codes/ngspice/src/spicelib/devices/hisimhv1/` (or `hisimhv2/`)

**ngspice prefix:** `HSMHV`

- [ ] **Step 1: Create descriptor YAML**

Create `tools/descriptors/hisimhv.yaml` with prefix `HSMHV`. **5 terminals:** D,G,S,B,Sub. Internal nodes: D',G',S',B',DB,SB,tempNode,qiNode,qbNode. State count: 31 (36 with NQS).

**Self-heating:** `tempNode` is an internal node for thermal modeling. This adds thermal stamps to the matrix.

- [ ] **Step 2-7: Auto-migrate, build fix, adapter, AC, noise, trunc**

Same HiSIM architecture as HiSIM2 plus:
- 5th terminal (substrate)
- Self-heating thermal node
- NQS charge nodes (qiNode, qbNode)
- LEVEL=62 or 73

Use HiSIM2 as reference (completed in Task 7).

- [ ] **Step 8: Parser integration**

Add LEVEL=62/73 to MOSFET dispatch. The 5th terminal (substrate) requires extending the deferred MOSFET struct to support optional 5th node.

- [ ] **Step 9-11: Tests, verify, commit**

```bash
git commit -m "feat: migrate HiSIM_HV device model from ngspice"
```

---

### Task 9: Migrate BSIMSOI

**Device:** BSIMSOI — SOI MOSFET v4.x, 6-terminal, 38 states, 32,103 LOC. Largest and most complex device.

**ngspice source:** `~/Codes/ngspice/src/spicelib/devices/bsimsoi/`

**ngspice prefix:** `B4SOI`

- [ ] **Step 1: Create descriptor YAML**

Create `tools/descriptors/bsimsoi.yaml` with prefix `B4SOI`. **6 terminals:** D, G_ext, S, E(substrate), P(body), B(bulk). Many internal nodes: D',S',G,GMid,DB,SB,tempNode + debug nodes. State count: 38.

- [ ] **Step 2-7: Auto-migrate, build fix, adapter, AC, noise, trunc**

BSIMSOI is structurally similar to BSIM4v7 (same UCB origin) but with:
- SOI-specific floating body dynamics
- Parasitic BJT in SOI structure
- Self-heating (tempNode)
- GIDL/GISL models
- 6 terminals requiring extended parser support
- LEVEL=10 or 58

Use BSIM4v7 as primary reference (`src/devices/bsim4v7/`).

- [ ] **Step 8: Parser integration**

BSIMSOI uses M-card MOSFET syntax but with LEVEL=10/58. The 6-terminal parsing requires extending the M-card parser to support optional 5th and 6th nodes. Check ngspice's parsing of BSIMSOI instance cards for the exact syntax.

- [ ] **Step 9-11: Tests, verify, commit**

```bash
git commit -m "feat: migrate BSIMSOI device model from ngspice"
```

---

## Post-Migration

### Task 10: Update documentation and push

- [ ] **Step 1: Update device-migration-status.md**

Move all 9 devices from "Priority 2 — Not Yet Migrated" to "Priority 2 — Migrated" with dates, test counts, and feature checklists.

- [ ] **Step 2: Update capabilities.md**

Add new MOSFET levels and device types to the capabilities table.

- [ ] **Step 3: Final test run**

```bash
cd build && cmake .. && cmake --build . -j$(nproc) && ctest --output-on-failure
```

Report total test count (should be 817 + new tests).

- [ ] **Step 4: Push**

```bash
git push
```
