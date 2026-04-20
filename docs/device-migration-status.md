# ngspice Device Migration Status

Last updated: 2026-04-20

## Overview

neospice migrates device models from ngspice's C codebase into a modern C++ framework.
Migration uses an 8-pass auto-translation tool (`tools/ngspice_migrate`) plus manual
implementation of AC stamps, noise models, truncation error, and parser integration.

ngspice source: `~/Codes/ngspice/src/spicelib/devices/`

---

## Already Migrated (Pre-existing)

These devices were implemented before the batch migration effort.

| Device | SPICE Prefix | Files | Notes |
|--------|-------------|-------|-------|
| Resistor | R | `src/devices/resistor.cpp` | Thermal + flicker noise |
| Capacitor | C | `src/devices/capacitor.cpp` | Trap/Gear-2 integration |
| Inductor | L | `src/devices/inductor.cpp` | Trap/Gear-2 integration |
| Coupled Inductor | K | `src/devices/coupled_inductor.cpp` | Mutual inductance |
| Voltage Source | V | `src/devices/vsource.cpp` | DC, PULSE, SIN, AC |
| Current Source | I | `src/devices/isource.cpp` | DC, PULSE, SIN, AC |
| VCVS (linear) | E | `src/devices/vcvs.cpp` | Linear voltage gain |
| VCVS (poly/table) | E | `src/devices/vcvs_nonlinear.cpp` | POLY and TABLE forms |
| VCCS (linear) | G | `src/devices/vccs.cpp` | Linear transconductance |
| VCCS (poly/table) | G | `src/devices/vccs_nonlinear.cpp` | POLY and TABLE forms |
| CCCS | F | `src/devices/cccs.cpp` | Linear current gain |
| CCVS | H | `src/devices/ccvs.cpp` | Linear transresistance |
| V-Switch | S | `src/devices/switch.cpp` | Cubic hermite, hysteresis |
| I-Switch | W | `src/devices/switch.cpp` | Cubic hermite, hysteresis |
| Lossless T-Line | T | `src/devices/tline.cpp` | Branin companion model |
| Diode | D | `src/devices/dio/` | Level 1, full feature set |
| BJT | Q | `src/devices/bjt/` | Gummel-Poon, 24 states |
| JFET | J | `src/devices/jfet/` | Standard model, 13 states |
| BSIM4v7 MOSFET | M | `src/devices/bsim4v7/` | Level 14/54, 29 states |

**Total pre-existing: 19 device types**

---

## Priority 1 -- Migrated (2026-04-19)

All 5 Priority 1 devices were migrated in a single session using parallel subagents.

### MOS1 (Level 1 Shichman-Hodges MOSFET)

| Attribute | Value |
|-----------|-------|
| SPICE prefix | M (LEVEL=1) |
| Location | `src/devices/mos1/` |
| Descriptor | `tools/descriptors/mos1.yaml` |
| Terminals | 4 (drain, gate, source, bulk) |
| Internal nodes | 2 (drain_prime, source_prime) |
| State variables | 17 |
| Complexity | Low |

**Features implemented:**
- [x] Auto-migrated setup, load, temp, param, mpar
- [x] AC stamp (G/C split) with Meyer gate capacitance
- [x] Noise: thermal (Rd, Rs, channel 2/3*gm) + flicker (KF/AF)
- [x] Truncation: Gear-2 LTE over 5 charge states
- [x] Initial conditions (ic=VDS,VGS)
- [x] Parameter query (gm, gds, id, vth, caps)
- [x] Parser: LEVEL=1 dispatch via `detect_mosfet_level()`

**Validation:** ngspice comparison tests pending.

---

### VBIC (Vertical Bipolar InterCompany Model)

| Attribute | Value |
|-----------|-------|
| SPICE prefix | Q (LEVEL=4/9/12/13) |
| Location | `src/devices/vbic/` |
| Descriptor | `tools/descriptors/vbic.yaml` |
| Terminals | 4 (collector, base, emitter, substrate) |
| Internal nodes | 7 |
| State variables | 66 |
| Complexity | Medium-High |

**Features implemented:**
- [x] Auto-migrated setup, load, temp, param, mpar
- [x] AC stamp: 15 conductance (G) + 6 capacitance (C) entries
- [x] Noise: 14 sources (7 thermal, 4 shot, 2 flicker, 1 total)
- [x] Truncation: Gear-2 LTE over 8 charge offsets
- [x] Initial conditions (ic=VBE,VCE)
- [x] Parameter query (gm, go, ic, ib, vbe, vbc, caps)
- [x] Parser: Q-card level dispatch (LEVEL=4/9/12/13 -> VBIC)

**Validation:** ngspice comparison tests pending.

---

### BSIM3 v3.3 (MOSFET)

| Attribute | Value |
|-----------|-------|
| SPICE prefix | M (LEVEL=8/49) |
| Location | `src/devices/bsim3/` |
| Descriptor | `tools/descriptors/bsim3.yaml` |
| Terminals | 4 (drain, gate, source, bulk) |
| Internal nodes | yes (drain_prime, source_prime, etc.) |
| State variables | complex (size-dependent) |
| Complexity | High |

**Features implemented:**
- [x] Auto-migrated setup, load, temp, check, param, mpar
- [x] AC stamp: 22 standard + 9 NQS (Q-node) matrix entries
- [x] Noise: 6 model variants (noiMod 1-6), SPICE2/BSIM3 thermal + 1/f
- [x] Truncation: Gear-2 LTE on qb/qg/qd charges (offsets 4, 6, 8)
- [x] Initial conditions (ic=VDS,VGS,VBS)
- [x] Parameter query (gm, gds, gmbs, vth, vdsat, id, caps, charges)
- [x] Parser: LEVEL=8/49 dispatch, UCB BSIM3mParam table
- [x] Size-dependent parameter linked-list cleanup in destructor

**Validation:** ngspice comparison tests pending.

---

### LTRA (Lossy Transmission Line)

| Attribute | Value |
|-----------|-------|
| SPICE prefix | O |
| Location | `src/devices/ltra.cpp`, `src/devices/ltra.hpp` |
| Descriptor | `tools/descriptors/ltra.yaml` |
| Terminals | 4 (port1_pos, port1_neg, port2_pos, port2_neg) |
| Parameters | R, L, C, G (per-unit-length), LEN |
| Complexity | Medium |

**Features implemented:**
- [x] DC operating point for RC and RG line types
- [x] Model setup with Bessel function computation
- [x] Parser: 'O' element prefix, LTRA model type
- [x] Parameter query (R, L, C, G, LEN, Z0)
- [x] 20 unit tests (setup, Bessel, interpolation, parsing, DC)

**Known limitations (future work):**
- [ ] Transient convolution not operational (rcCoeffsSetup/rlcCoeffsSetup stubbed)
- [ ] AC stamp is simplified resistive approximation (not full Y-parameters)
- [ ] LTE calculation stubbed
- [ ] IC parsing not wired
- [ ] No LC/RLC transient support yet

**Validation:** ngspice DC comparison tests pending.

---

### ASRC (B Element -- Behavioral Source)

| Attribute | Value |
|-----------|-------|
| SPICE prefix | B |
| Location | `src/devices/asrc/` |
| Descriptor | `tools/descriptors/asrc.yaml` |
| Terminals | 2 (positive, negative) |
| Modes | Voltage (`V={expr}`) and Current (`I={expr}`) |
| Complexity | Medium (custom implementation, no auto-migration) |

**Features implemented:**
- [x] Recursive-descent expression parser
- [x] Forward-mode automatic differentiation (dual numbers) for exact Jacobian
- [x] Arithmetic: `+ - * / ^ **`
- [x] Functions: sin, cos, tan, asin, acos, atan, exp, log, log10, sqrt, abs, pow, min, max, sinh, cosh, tanh, atan2, if, limit
- [x] Variable references: V(node), V(n1,n2), I(Vname), TIME
- [x] SPICE number suffixes (1k, 2.5m, 100u, etc.)
- [x] MNA stamping for both voltage and current modes
- [x] Deferred I() resolution for VSource branch currents
- [x] 34 tests (24 expression + 10 circuit integration)

**Known limitations (future work):**
- [ ] No TEMP/DTEMP/tc1/tc2 support
- [ ] TIME derivative not tracked for transient (treated as parameter)
- [ ] No noise analysis for B elements
- [ ] No pole-zero analysis
- [ ] Singular derivatives at x=0 (sqrt, log) can cause issues without source stepping

**Validation:** ngspice comparison tests pending.

---

## MOSFET Level Dispatch

| LEVEL | Model | Status |
|-------|-------|--------|
| 1 | MOS1 (Shichman-Hodges) | Migrated |
| 2 | MOS2 (Grove-Frohman) | Not migrated |
| 3 | MOS3 | Not migrated |
| 4 | BSIM1 | Not migrated (obsolete) |
| 5 | BSIM2 | Not migrated (obsolete) |
| 6 | MOS6 | Not migrated |
| 8, 49 | BSIM3 v3.3 | Migrated |
| 9 | MOS9 (Modified Level 3) | Not migrated |
| 14, 54 | BSIM4v7 | Migrated (pre-existing) |

## BJT Level Dispatch

| LEVEL | Model | Status |
|-------|-------|--------|
| 1 (default) | BJT (Gummel-Poon) | Migrated (pre-existing) |
| 4, 9, 12, 13 | VBIC | Migrated |

---

## Priority 2 -- Medium Impact (Not Yet Migrated)

| Device | ngspice Dir | Terminals | Why | Complexity | Notes |
|--------|------------|-----------|-----|------------|-------|
| BSIM3v32 | `bsim3v32/` | 4 | Some PDKs use v3.2.4 specifically | High | Similar to BSIM3 v3.3 |
| BSIMSOI | `bsimsoi/` | 4+ | SOI technology, advanced nodes | High | Level 10/58, v4.3.1 |
| MOS3 | `mos3/` | 4 | Legacy PDK support | Medium | |
| MOS9 | `mos9/` | 4 | Modified Level 3, some PDKs | Medium | |
| HiSIM2 | `hisim2/` | 4 | Japanese semiconductor PDKs | High | Level 61/68 |
| HiSIM_HV | `hisimhv1/` | 4 | High-voltage variant | High | Level 62/73 |
| HFET1 | `hfet1/` | 4 | RF/microwave GaAs/GaN | Medium | Level 5 |
| HFET2 | `hfet2/` | 4 | RF/microwave GaAs/GaN | Medium | Level 6 |
| JFET2 | `jfet2/` | 3 | Parker-Skellern, more accurate | Low-Medium | Level 2 |

---

## Priority 3 -- Low Impact (Not Yet Migrated)

| Device | ngspice Dir | Why Low Priority |
|--------|------------|-----------------|
| MOS2 | `mos2/` | Rarely used, superseded |
| MOS6 | `mos6/` | Rarely used |
| BSIM1 | `bsim1/` | Obsolete (Level 4) |
| BSIM2 | `bsim2/` | Obsolete (Level 5) |
| BSIM3v0 | `bsim3v0/` | Superseded by v3.3 |
| BSIM3v1 | `bsim3v1/` | Superseded by v3.3 |
| BSIM4 (v4.6.5) | `bsim4/` | Have v4.7 already |
| BSIM4v5 | `bsim4v5/` | Have v4.7 already |
| BSIM4v6 | `bsim4v6/` | Have v4.7 already |
| BSIM3SOI_FD | `bsim3soi_fd/` | Superseded by BSIMSOI |
| BSIM3SOI_DD | `bsim3soi_dd/` | Superseded by BSIMSOI |
| BSIM3SOI_PD | `bsim3soi_pd/` | Superseded by BSIMSOI |
| SOI3 | `soi3/` | Marked OBSOLETE in ngspice |
| MES | `mes/` | GaAs MESFET, very niche |
| MESA | `mesa/` | GaAs MESFET, multi-level |
| CPL | `cpl/` | Coupled multiconductor lines |
| TXL | `txl/` | Simple lossy T-line |
| URC | `urc/` | Uniform RC line |
| NBJT | `nbjt/` | Numerical BJT, research |
| NBJT2 | `nbjt2/` | Numerical BJT Level 2 |
| NUMD | `numd/` | Numerical diode |
| NUMD2 | `numd2/` | Numerical diode Level 2 |
| NUMOS | `numos/` | Numerical MOS |
| NDEV | `ndev/` | Numerical device base |

---

## Priority 4 -- ADMS/Verilog-A (Different Migration Path)

These use auto-generated C from Verilog-A via ADMS. Migration is possible with the
same tool but the source structure differs from hand-written ngspice devices.

| Device | ngspice Dir | Type | Notes |
|--------|------------|------|-------|
| EKV | `adms/ekv/` | MOS (Level 44) | Compact MOS model |
| PSP102 | `adms/psp102/` | MOS (Level 45) | Penn State Philips model |
| HICUM0 | `adms/hicum0/` | Bipolar (Level 7) | High-current model |
| HICUM2 | `adms/hicum2/` | Bipolar (Level 8) | High-current model v2 |
| Mextram | `adms/mextram/` | Bipolar (Level 6) | Most EXquisite TRAnsistor Model |

---

## Statistics

| Category | Count |
|----------|-------|
| Pre-existing devices | 19 |
| Priority 1 migrated | 5 |
| **Total migrated** | **24** |
| Priority 2 remaining | 9 |
| Priority 3 remaining | 24 |
| Priority 4 (Verilog-A) | 5 |
| **Total remaining** | **38** |
| **Total ngspice devices** | **~62** |

## Migration Tooling

| Tool | Location | Purpose |
|------|----------|---------|
| Auto-migration | `python -m ngspice_migrate` | 8-pass C-to-C++ translator |
| Descriptors | `tools/descriptors/*.yaml` | Per-device migration config |
| Migration guide | `/migrate-device` skill | Full workflow documentation |
| NgspiceRunner | `tests/framework/ngspice_runner.*` | Test comparison framework |
| Comparator | `tests/framework/comparator.*` | DC/AC/transient/noise comparison |
