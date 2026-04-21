# Migration Tool Improvements & BSIM4v7 Fix

**Date:** 2026-04-21  
**Status:** Approved  
**Goal:** Make the ngspice device migration tool robust enough that new device migrations require near-zero manual work (except `ac_stamp()` and noise), while fixing BSIM4v7 transient accuracy.

## Context

The 8-pass migration tool (`tools/ngspice_migrate/`) automates ~70% of each device migration (C-to-C++ translation, shim, adapter skeleton). The remaining ~30% is manual boilerplate that follows identical patterns across all 7 migrated devices:

- Model card conversion (`to_*_card()`) — ~570 lines total, 100% boilerplate
- Parser integration — ~180 lines total, 100% boilerplate
- `compute_trunc()` — identical LTE algorithm, only charge offset lists differ
- `query_param()` — pure name→field mapping
- `assign_offsets()` RESOLVE section — extractable from def header
- Sensitivity code stripping — currently manual, pattern-based

BSIM4v7 has wide test tolerances (DC: 500%, transient: 25-50%) that need tightening. The raw 8-pass translation is correct — discrepancies are in the adapter/shim/framework layer. Debugging BSIM4v7 will reveal tool-level bugs to fix.

## Approach: Learn-Then-Automate

Bug-driven tool improvement: debug BSIM4v7 first to learn what the tool gets wrong, feed those learnings into tool improvements, validate by re-migrating BSIM4v7, then migrate new devices.

## Phase 1: BSIM4v7 Debugging

### Current test state

| Test | Tolerance | Target | Issue |
|---|---|---|---|
| `NMOS_DC_IV` | 5.0 (500%) | <0.05 (5%) | DC currents off |
| `CMOSInverterTransient` | 0.25 (25%) | <0.05 (5%) | Transient waveform drift |
| `CMOSInverterTransientWithResistance` | 0.50 (50%) | <0.05 (5%) | Worse with parasitics |
| `RingOscillator5Stage` | 1% period, 2% amp | Same or tighter | Already decent |

### Debugging steps (systematic elimination)

**Step 1: DC operating point audit.** Run a single NMOS at a known bias (Vgs=1.0, Vds=1.8) in both simulators. Compare Ids, gm, gds, Vth. Isolates physics vs framework issues.

**Step 2: Model parameter audit.** Dump all model parameters after `to_bsim4_card()` and compare against ngspice internal params (via `show` command). Catches missing or wrongly-mapped parameters.

**Step 3: Evaluate() orchestration audit.** Trace a single Newton iteration: compare ghost voltage array, RHS contributions, and matrix stamps against ngspice. Catches adapter-layer bugs (array sizing, node mapping off-by-one, mode flag issues).

**Step 4: Transient integration audit.** If DC is fixed but transient drifts, compare Gear-2 ag[] coefficients, timestep selection, and charge state evolution.

### Bug classification

Each bug found gets tagged:
- **tool-fixable**: generator/transformer issue → fix in Phase 2
- **framework**: solver/integrator issue → fix directly

## Phase 2: Tool Improvements

### 2.1 Model Card Conversion Generator (`gen_model_card.py`)

Generates `<ns>_model_card.hpp` and `<ns>_model_card.cpp` containing:
- ModelCard struct with UCB model + type enum
- `to_<prefix>_card()` function using auto-translated mPTable/mParam

Descriptor addition — `model_types` field:
```yaml
model_types:
  - { spice_name: "nmos", flag_field: "BSIM4v7type", flag_value: 1 }
  - { spice_name: "pmos", flag_field: "BSIM4v7type", flag_value: -1 }
```

### 2.2 Parser Integration Generator (`gen_parser.py`)

Generates `<ns>_parser.hpp` with:
- `create_<ns>_device()` — terminal extraction, geometry fill, `Device::make()` call, `ic=` handling
- `create_<ns>_model_card()` — model lookup and card creation
- Registration data for the main parser dispatch

Uses `terminals`, `geometry`, and `model_types` from descriptor.

### 2.3 `compute_trunc()` Generator

New logic in `gen_adapter.py`:
- Scan translated load file for `NIintegrate(ckt, ..., state_offset)` calls
- Extract charge state offsets automatically
- Generate standard LTE loop (identical algorithm across all devices)

Descriptor fallback for manual override:
```yaml
charge_states: [3]  # e.g., DIOcapCharge
```

### 2.4 `query_param()` Skeleton Generator

New logic in `gen_adapter.py`:
- Parse translated devsup parameter table (mPTable / instance pTable)
- Extract parameter names, associated instance fields, IOP vs OP classification
- Generate name→field mapping with TODO markers for multiplier scaling

### 2.5 RESOLVE Extraction from def.hpp

Replace fragile setup-source regex with reliable approach:
- Scan `_def.hpp` for all fields typed `neospice::MatrixOffset`
- Generate RESOLVE list directly (100% reliable since `gen_def.py` produces these)

### 2.6 Sensitivity Stripping Pass

New Pass 1b in the transformer pipeline:
- Remove `SenCond` variable declarations and `if (SenCond)` blocks
- Remove `if (ckt->CKTsenInfo)` guard blocks
- Remove `goto next1`/`next2` labels used only by sensitivity flow
- Remove sensitivity state allocation in setup functions

### 2.7 Test Scaffolding Generator (`gen_test.py`)

Generates per-device test directory with:
- DC/transient/AC test skeletons using NgspiceRunner
- Standard circuit templates appropriate for device type
- CMakeLists.txt linking to device object library

### 2.8 Adapter/Shim Hardening

Fix any bugs found during Phase 1 BSIM4v7 debugging in:
- `gen_adapter.py` — evaluate() orchestration, ghost array sizing, mode flags
- `gen_shim.py` — NIintegrate, state management, integration coefficients

### Validation

Re-migrate BSIM4v7 with improved tool. Verify:
- Test tolerances match or improve on Phase 1 manual fixes
- No regressions in other device tests

## Phase 3: New Device Migrations

### Target devices (prioritized by real-netlist usage)

| Priority | Device | Dir | Complexity | Rationale |
|---|---|---|---|---|
| 1 | MOS2 | `mos2/` | Medium | Level 2 MOSFET — legacy analog/educational |
| 2 | MOS3 | `mos3/` | Medium | Level 3 — semi-empirical, older PDKs |
| 3 | JFET2 | `jfet2/` | Low | Parker-Skellern — RF/analog |
| 4 | BSIMSOI | `bsimsoi/` | High | SOI MOSFET — modern SOI processes |
| 5 | HISIM2 | `hisim2/` | High | HiSIM — Japanese foundry PDKs |

### Per-device workflow (with improved tool)

1. Create descriptor YAML
2. Run `python -m ngspice_migrate` — produces all translated + generated files
3. Build, fix any remaining compilation issues
4. Implement `ac_stamp()` manually (read ngspice `*acld.c`, split G/C)
5. Review auto-generated `query_param()` for multiplier scaling correctness
6. Run auto-generated test suite, tighten tolerances

### Expected manual work per device

| Device complexity | Before | After | Reduction |
|---|---|---|---|
| Simple (JFET2) | ~400 lines | ~50 lines | ~88% |
| Medium (MOS2, MOS3) | ~550 lines | ~100 lines | ~82% |
| Complex (BSIMSOI, HISIM2) | ~900 lines | ~250 lines | ~72% |

### Parallel-capability

Each device migration is independent — can be done by separate agents in worktrees. No shared files modified except the main parser dispatch table (which gets a single-line addition per device).

## Test Organization

### Directory structure

```
tests/
  devices/
    dio/
      test_dio_dc.cpp
      test_dio_transient.cpp
      test_dio_ac.cpp
      circuits/
        diode_iv.cir
        diode_transient.cir
    bsim4v7/
      test_bsim4v7_dc.cpp
      test_bsim4v7_transient.cpp
      test_bsim4v7_ac.cpp
      circuits/
        nmos_iv.cir
        cmos_inverter.cir
        ring_osc_5stage.cir
    mos2/
      test_mos2_dc.cpp
      ...
      circuits/
        ...
```

Each device directory is self-contained with test sources, circuit files, and CMakeLists.txt. The existing `test_ngspice_compare.cpp` monolith gets split into per-device files as part of Phase 2.

### Rationale

- Agents working on different devices in parallel never touch the same files
- Each device's tests can be built/run independently
- Circuit files co-located with their tests

## Phase 4: Update migrate-device Skill

Update `/migrate-device` skill doc to reflect:
- New auto-generated outputs (model card, parser, compute_trunc, query_param, tests)
- Reduced manual steps
- New descriptor fields (`model_types`, `charge_states`)
- Troubleshooting for common remaining issues

## What Remains Manual After All Improvements

| Feature | Status |
|---|---|
| `ac_stamp()` G/C split | Manual — requires device physics knowledge |
| `noise_sources()` | Out of scope — physics-specific |
| Device-specific quirks (NQS modes, geometry preprocessing) | Manual — too varied to template |
| Multiplier scaling review in `query_param()` | Manual review of auto-generated code |

## Success Criteria

- BSIM4v7 test tolerances tightened to <5% relative for DC, <10% for transient
- Migration tool generates compilable code for a new device with zero manual edits (except ac_stamp)
- New device migration end-to-end time reduced from ~2 hours to ~30 minutes
- Per-device test directories enable conflict-free parallel agent work
