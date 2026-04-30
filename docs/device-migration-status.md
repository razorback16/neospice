# ngspice Device Migration Status

Last updated: 2026-04-23

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

**Validation:** 5 ngspice comparison tests (`tests/devices/mos1/test_mos1_compare.cpp`):
NMOS DC OP, NMOS IV sweep, PMOS DC OP, AC response, transient pulse.

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

**Validation:** 5 ngspice comparison tests (`tests/devices/vbic/test_vbic_compare.cpp`):
NPN DC OP, Gummel plot, PNP DC OP, AC small-signal, switching transient.

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

**Validation:** 7 ngspice comparison tests (`tests/devices/bsim3/test_bsim3_compare.cpp`):
NMOS DC OP, NMOS IV sweep, PMOS DC OP, CS amplifier AC, AC nonzero output,
CMOS inverter transient, CMOS inverter transitions.

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
- [x] Transient convolution with delayed-value interpolation (quadratic/linear)
- [x] LTE calculation for RLC/RC cases (second-derivative impulse response)
- [x] IC parameter parsing (IC=v1,i1,v2,i2 vector and individual V1=/I1=/V2=/I2=)
- [x] 20 unit tests (setup, Bessel, interpolation, parsing, DC)

**Known limitations (future work):**
- [ ] AC stamp is simplified resistive approximation (not full Y-parameters — requires framework support for per-frequency device evaluation)

**Validation:** 5 ngspice comparison tests (`tests/devices/ltra/test_ltra_compare.cpp`):
DC OP RC line, DC OP RG line, Transient RC, Transient RLC, Transient LC.

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
- [x] TEMP/DTEMP/tc1/tc2 temperature coefficient scaling
- [x] 34 tests (24 expression + 10 circuit integration)

**Known limitations (future work):**
- [ ] TIME derivative not tracked for transient (treated as parameter)
- [ ] No noise analysis for B elements (ngspice also has no B-element noise)
- [ ] No pole-zero analysis (ngspice also lacks this for B elements)
- [ ] Singular derivatives at x=0 (sqrt, log) can cause issues without source stepping

**Validation:** 13 ngspice comparison tests:
- `tests/devices/asrc/test_asrc_compare.cpp` (8): voltage doubler, VCCS, nonlinear,
  trig, multi-variable, AC gain, tc1/tc2 voltage mode, tc1/tc2 current mode
- `tests/unit/test_ngspice_compare.cpp` (5): TEMPER, PWL, HERTZ, DDT transient,
  IDT transient

---

## MOSFET Level Dispatch

| LEVEL | Model | Status |
|-------|-------|--------|
| 1 | MOS1 (Shichman-Hodges) | Migrated |
| 2 | MOS2 (Grove-Frohman) | Not migrated |
| 3 | MOS3 | Migrated |
| 4 | BSIM1 | Not migrated (obsolete) |
| 5 | BSIM2 | Not migrated (obsolete) |
| 6 | MOS6 | Not migrated |
| 8, 49 | BSIM3 v3.3 / BSIM3v32 | Migrated (both v3.3 and v3.24) |
| 9 | MOS9 (Modified Level 3) | Migrated |
| 10, 58 | BSIMSOI | Migrated (DC/transient; AC stub) |
| 14, 54 | BSIM4v7 | Migrated (pre-existing) |
| 61, 68 | HiSIM2 | Migrated |
| 62, 73 | HiSIM_HV | Migrated |

## BJT Level Dispatch

| LEVEL | Model | Status |
|-------|-------|--------|
| 1 (default) | BJT (Gummel-Poon) | Migrated (pre-existing) |
| 4, 9, 12, 13 | VBIC | Migrated |

## JFET Level Dispatch

| LEVEL | Model | Status |
|-------|-------|--------|
| 1 (default) | JFET | Migrated (pre-existing) |
| 2 | JFET2 (Parker-Skellern) | Migrated |

## HFET (Z-card) Level Dispatch

| LEVEL | Model | Status |
|-------|-------|--------|
| 5 (default) | HFET1 (Curtice cubic) | Migrated |
| 6 | HFET2 (Chalmers) | Migrated |

---

## Priority 2 -- Migrated (2026-04-22)

All 9 Priority 2 devices migrated via parallel subagent-driven development.

### MOS9 (Modified Level 3 MOSFET)

| Attribute | Value |
|-----------|-------|
| SPICE prefix | M (LEVEL=9) |
| Location | `src/devices/mos9/` |
| Descriptor | `tools/descriptors/mos9.yaml` |
| Terminals | 4 (drain, gate, source, bulk) |
| Internal nodes | 2 (drain_prime, source_prime) |
| State variables | 17 |
| Complexity | Medium |

**Features implemented:**
- [x] Auto-migrated setup, load, temp, param, mpar
- [x] AC stamp (G/C split) with Meyer gate capacitance
- [x] Noise: thermal (Rd, Rs, channel) + flicker (KF/AF)
- [x] Truncation: Gear-2 LTE over charge states
- [x] Parser: LEVEL=9 dispatch

**Validation:** ngspice comparison tests (`tests/devices/mos9/test_mos9_compare.cpp`)

---

### MOS3 (Level 3 MOSFET)

| Attribute | Value |
|-----------|-------|
| SPICE prefix | M (LEVEL=3) |
| Location | `src/devices/mos3/` |
| Descriptor | `tools/descriptors/mos3.yaml` |
| Terminals | 4 (drain, gate, source, bulk) |
| Internal nodes | 2 (drain_prime, source_prime) |
| State variables | 17 |
| Complexity | Medium |

**Features implemented:**
- [x] Auto-migrated setup, load, temp, param, mpar
- [x] AC stamp (G/C split) with Meyer gate capacitance
- [x] Noise: thermal (Rd, Rs, channel) + flicker (KF/AF)
- [x] Truncation: Gear-2 LTE over charge states
- [x] Parser: LEVEL=3 dispatch

**Validation:** ngspice comparison tests (`tests/devices/mos3/test_mos3_compare.cpp`)

---

### HFET2 (Chalmers GaAs/GaN HFET)

| Attribute | Value |
|-----------|-------|
| SPICE prefix | Z (LEVEL=6) |
| Location | `src/devices/hfet2/` |
| Descriptor | `tools/descriptors/hfet2.yaml` |
| Terminals | 3 (drain, gate, source) |
| Internal nodes | 2 (drain_prime, source_prime) |
| State variables | 16 |
| Complexity | Medium |

**Features implemented:**
- [x] Auto-migrated setup, load, temp, param, mpar
- [x] AC stamp (G/C split)
- [x] Noise: thermal + flicker
- [x] Truncation: Gear-2 LTE
- [x] Parser: Z-card LEVEL=6 dispatch

**Validation:** ngspice comparison tests (`tests/devices/hfet2/test_hfet2_compare.cpp`)

---

### JFET2 (Parker-Skellern)

| Attribute | Value |
|-----------|-------|
| SPICE prefix | J (LEVEL=2) |
| Location | `src/devices/jfet2/` |
| Descriptor | `tools/descriptors/jfet2.yaml` |
| Terminals | 3 (drain, gate, source) |
| Internal nodes | 2 (drain_prime, source_prime) |
| State variables | 22 |
| Complexity | Low-Medium |

**Features implemented:**
- [x] Auto-migrated setup, load, temp, param, mpar
- [x] AC stamp (G/C split)
- [x] Noise: thermal + flicker + shot
- [x] Truncation: Gear-2 LTE
- [x] Parser: J-card LEVEL=2 dispatch

**Validation:** ngspice comparison tests (`tests/devices/jfet2/test_jfet2_compare.cpp`)

---

### HFET1 (Curtice Cubic GaAs/GaN HFET)

| Attribute | Value |
|-----------|-------|
| SPICE prefix | Z (LEVEL=5, default) |
| Location | `src/devices/hfet1/` |
| Descriptor | `tools/descriptors/hfet1.yaml` |
| Terminals | 3 (drain, gate, source) |
| Internal nodes | 5 (drain_prime, source_prime, gate_prime, drain_prime_prime, source_prime_prime) |
| State variables | 24 |
| Complexity | Medium |

**Features implemented:**
- [x] Auto-migrated setup, load, temp, param, mpar
- [x] AC stamp (G/C split)
- [x] Truncation: Gear-2 LTE
- [x] Parser: Z-card LEVEL=5 dispatch (default for NHFET/PHFET)

**Validation:** ngspice comparison tests (`tests/devices/hfet1/test_hfet1_compare.cpp`)

---

### BSIM3v32 (BSIM3 v3.24)

| Attribute | Value |
|-----------|-------|
| SPICE prefix | M (LEVEL=49, VERSION<3.3) |
| Location | `src/devices/bsim3v32/` |
| Descriptor | `tools/descriptors/bsim3v32.yaml` |
| Terminals | 4 (drain, gate, source, bulk) |
| Internal nodes | yes (drain_prime, source_prime, Q-node for NQS) |
| State variables | 17 |
| Complexity | High |

**Features implemented:**
- [x] Auto-migrated setup, load, temp, check, param, mpar
- [x] AC stamp: 22 standard + 9 NQS (Q-node) matrix entries
- [x] Noise: thermal (SPICE2/BSIM3v32) + flicker (SPICE2/holistic)
- [x] Truncation: Gear-2 LTE on qb/qg/qd charges
- [x] SIZE-dependent parameter linked list cleanup in destructor
- [x] ACM stub functions for source/drain resistance
- [x] Parser: LEVEL=49 with VERSION<3.3 routes to BSIM3v32

**Validation:** 3 ngspice comparison tests (`tests/devices/bsim3v32/test_bsim3v32_compare.cpp`):
DC operating point, CS amplifier AC gain, AC sanity check.

---

### HiSIM2 (Hiroshima-University STARC IGFET)

| Attribute | Value |
|-----------|-------|
| SPICE prefix | M (LEVEL=61/68) |
| Location | `src/devices/hisim2/` |
| Descriptor | `tools/descriptors/hisim2.yaml` |
| Terminals | 4 (drain, gate, source, bulk) |
| Internal nodes | variable (D', G', S', B', DB, SB, etc.) |
| State variables | 21 |
| Complexity | High |

**Features implemented:**
- [x] Auto-migrated setup, load, temp, eval, param, mpar, devsup
- [x] AC stamp with NQS/corbnet/corg modes
- [x] Noise: thermal + flicker + shot
- [x] Truncation: Gear-2 LTE on charge states
- [x] Parser: LEVEL=61/68 dispatch

**Validation:** 4 ngspice comparison tests (`tests/devices/hisim2/test_hisim2_compare.cpp`):
NMOS DC, PMOS DC, IV sweep, AC response.

---

### HiSIM_HV (High-Voltage variant)

| Attribute | Value |
|-----------|-------|
| SPICE prefix | M (LEVEL=73) |
| Location | `src/devices/hisimhv/` |
| Descriptor | `tools/descriptors/hisimhv.yaml` |
| Terminals | 5 (drain, gate, source, bulk, substrate) |
| Internal nodes | variable (D', G', S', B', DB, SB, tempNode, qiNode, qbNode) |
| State variables | 31 (36 with NQS) |
| Complexity | High |

**Features implemented:**
- [x] Auto-migrated setup, load, temp, eval, param, mpar, devsup
- [x] AC stamp with self-heating + NQS support
- [x] Noise: thermal (drain/source/channel), flicker 1/f, induced gate, gate tunneling shot
- [x] 5th terminal (substrate) parser support for M-card
- [x] Truncation: Gear-2 LTE on charge states
- [x] Parser: LEVEL=73 dispatch with 5-terminal M-card detection

**Validation:** 4 ngspice comparison tests (`tests/devices/hisimhv/test_hisimhv_compare.cpp`):
NMOS DC, PMOS DC, AC response, NMOS noise.

---

### BSIMSOI (Berkeley SOI MOSFET v4.x)

| Attribute | Value |
|-----------|-------|
| SPICE prefix | M (LEVEL=10/58) |
| Location | `src/devices/bsimsoi/` |
| Descriptor | `tools/descriptors/bsimsoi.yaml` |
| Terminals | 6 (drain, gate_ext, source, substrate, body, bulk) |
| Internal nodes | variable (D', S', G, GMid, DB, SB, tempNode, etc.) |
| State variables | 38 |
| Complexity | High (largest P2 device, 32K LOC) |

**Features implemented:**
- [x] Auto-migrated setup, load, temp, check, param, mpar, devsup
- [x] Size-dependent parameter linked list cleanup in destructor
- [x] Version stamp (v4.6.1) auto-set
- [x] 6-terminal M-card parser support
- [x] Truncation: Gear-2 LTE on 8 charge states
- [x] Parser: LEVEL=10/58 dispatch with 6-terminal detection
- [x] AC stamp: 67 G-matrix + 35 C-matrix entries with rgateMod/soiMod/rbodyMod/rdsMod/selfheat support
- [x] Noise: 13 sources (thermal, shot, flicker, gate-induced, body resistance)

**Validation:** 3 ngspice comparison tests (`tests/devices/bsimsoi/test_bsimsoi_compare.cpp`):
NMOS DC OP, PMOS DC OP, NMOS AC response.

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
| Priority 2 migrated | 9 |
| **Total migrated** | **33** |
| Priority 2 remaining | 0 |
| Priority 3 remaining | 24 |
| Priority 4 (Verilog-A) | 5 |
| **Total remaining** | **30** |
| **Total ngspice devices** | **~62** |

## Known Technical Debt

Several auto-translated device `_setup.cpp` files contain `TODO(translator): TSTALLOC macro kept as-is; needs manual rewrite` comments. These mark matrix allocation code that was mechanically translated from ngspice's C macros. The code works correctly — the TODOs indicate it could be further cleaned up to use native C++ patterns. Affected devices: MOS1, MOS3, MOS9, JFET2, VBIC, HFET1, HFET2, DIO, BSIM3, BSIM3v32, BSIM4v7, BSIMSOI, HiSIM2, HiSIM_HV, MES.

---

## Migration Tooling

| Tool | Location | Purpose |
|------|----------|---------|
| Auto-migration | `python -m ngspice_migrate` | 8-pass C-to-C++ translator |
| Descriptors | `tools/descriptors/*.yaml` | Per-device migration config |
| Migration guide | `/migrate-device` skill | Full workflow documentation |
| NgspiceRunner | `tests/framework/ngspice_runner.*` | Test comparison framework |
| Comparator | `tests/framework/comparator.*` | DC/AC/transient/noise comparison |
