# NEOSPICE Roadmap: Phases 5–10

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement each phase task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a modern, correct, GPU-acceleratable SPICE simulator with the device library, analysis modes, and netlist features needed for practical analog/mixed-signal design.

**What's done:** Milestones 1–9 delivered a working CPU simulator with R/C/L/V/I/Diode/BSIM4v7 (full: DC/transient/AC/trunc/convergence/IC/query), BJT (Gummel-Poon NPN/PNP), JFET (Shockley NJF/PJF), coupled inductors (K element), E/G/H/F controlled sources, DC/transient/AC/DC-sweep/noise analysis, adaptive time stepping, convergence aids, ngspice comparison framework, auto-migration tool, subcircuits (`.subckt`/`X` with hierarchical flattening), full `.param` expression evaluator, `.include`/`.lib` support, `.save` filtering, `.measure` post-processing, and `.print`/`.plot` output formatting. 470 C++ tests passing.

**Architecture principle:** Each phase is self-contained — it ships a testable increment, validated against ngspice, before the next phase begins. Phases are ordered by user impact (what unlocks the most real circuits).

**Reference codebase:** `~/Codes/ngspice/src/spicelib/` for device models, analysis algorithms, and parser patterns.

---

## Gap Analysis vs. Design Doc

| Design doc item | Status | Phase to address |
|-----------------|--------|------------------|
| DC operating point | Done | — |
| Transient (fixed + adaptive) | Done | — |
| AC small-signal | **Done** — all devices including BSIM4v7 | ~~Phase 5~~ Done |
| DC sweep (`.dc`) | **Done** — single + nested sweep, warm-start Newton | ~~Phase 6~~ Done |
| Phase 1 devices (R,C,L,V,I,D) | Done | — |
| Phase 2 device (BSIM4v7) | **Done** — DC, transient, AC, trunc, convergence, IC, query | ~~Phase 5~~ Done |
| `.subckt`/`.ends`/`X` | **Done** — hierarchical flattening, nested, param override | ~~Phase 7~~ Done |
| Controlled sources (E,G,F,H) | **Done** — all 4 linear controlled sources | ~~Phase 6~~ Done |
| `.param` expressions | **Done** — full evaluator: `**`, functions, `if()`, topo sort | ~~Phase 7~~ Done |
| `.save` filtering | **Done** — enforced in all analysis types | ~~Phase 6~~ Done |
| BJT (Gummel-Poon) | **Done** — NPN/PNP, DC/transient/AC/trunc/convergence/IC/query | ~~Phase 8~~ Done |
| JFET (Shockley) | **Done** — NJF/PJF, DC/transient/AC/trunc/convergence/IC/query | ~~Phase 8~~ Done |
| Coupled inductors (K element) | **Done** — mutual inductance, transformer support | ~~Phase 8~~ Done |
| Noise analysis | **Done** — adjoint method, resistor/diode/BSIM4v7 noise, ngspice-validated | ~~Phase 9~~ Done |
| `.measure` | **Done** — TRIG/TARG, FIND/WHEN, AVG/RMS/MIN/MAX/PP/INTEG, PARAM | ~~Phase 9~~ Done |
| `.print`/`.plot` | **Done** — tabular ASCII output, ASCII waveform plots | ~~Phase 9~~ Done |
| Flicker (1/f) noise | **Done** — Kf·I^Af/f in BJT, JFET, BSIM4v7, resistor | ~~Phase 9.5~~ Done |
| Full BSIM4v7 noise | **Done** — channel thermal, flicker, Rg/Rb/Rd/Rs thermal | ~~Phase 9.5~~ Done |
| BJT/JFET noise | **Done** — shot, thermal, flicker noise models | ~~Phase 9.5~~ Done |
| `.options` parsing | **Done** — reltol, abstol, vntol, chgtol, trtol, itl1, itl4, gmin, temp, tnom, method | ~~Phase 9.5~~ Done |
| `.four` (Fourier) | **Done** — DFT, harmonics, THD | ~~Phase 9.5~~ Done |
| Switches (SW/CSW) | **Done** — voltage/current controlled, hysteresis, smooth transition | ~~Phase 9.5~~ Done |
| Nonlinear controlled sources | **Done** — POLY, TABLE, B-element (ASRC) | ~~Phase 9.5~~ Done |
| Transmission lines (T/LTRA) | **Done** — lossless TL, lossy LTRA (LC/RLC/RC/RG) | ~~Phase 9.5~~ Done |
| `.step` parameter sweeping | **Done** — source, temp, param sweep over any analysis | ~~Phase 9.5~~ Done |
| `.pz` pole-zero analysis | **Done** — LAPACK dggev generalized eigenvalue | ~~Phase 9.5~~ Done |
| Random `.param` functions | **Done** — gauss/agauss/unif/aunif (ngspice numparam parity) | ~~Phase 9.5~~ Done |
| GPU acceleration (CUDA) | Not started (design doc M2/M3) | Phase 10 |

---

## Phase 5: BSIM4v7 Feature Completion ✅ COMPLETE

**Status:** All tasks completed and merged to `main` via `b561d9e`. 158 C++ tests passing.

**Goal:** Complete the BSIM4v7 migration — AC analysis, truncation error, and convergence test — so MOSFET circuits work correctly in all supported analyses.

**Why first:** BSIM4v7 is the workhorse model. Without AC support, no amplifier frequency response, no stability analysis. Without truncation error, transient timesteps are suboptimal for MOSFET switching circuits.

**Approach:** Used the auto-migration tool to translate the remaining ngspice files. AC stamp implemented directly in `bsim4v7_device.cpp` using G/C matrix split approach (not complex-number stamping). Truncation, convergence test, IC, and query all wired into the `BSIM4v7Device` adapter.

**Files delivered:**
- AC stamp: `bsim4v7_device.cpp::ac_stamp()` (G/C matrix split)
- Truncation: `bsim4v7_device.cpp::compute_trunc()` (LTE via divided differences)
- Convergence: `bsim4v7_device.cpp::device_converged()` (UCB noncon flag)
- Initial conditions: `bsim4v7_device.cpp::set_ic()` (VDS/VGS/VBS, parser `ic=` support)
- Query: `bsim4v7_device.cpp::query_param()` (gm, gds, vth, capacitances, geometry)

**Tasks:**

### Task 5.1: BSIM4v7 AC Small-Signal Load ✅

- [x] Analyze `b4v7acld.c` — identified complex-number patterns, CKT field accesses
- [x] Implemented G/C matrix split approach (real conductances → G, capacitance-derived → C)
- [x] Wired `BSIM4v7Device::ac_stamp()` with full QS-mode stamp
- [x] Fixed Csd capacitance formula and rgateMod absolute state offsets
- [x] Test: `test_bsim4v7_ac.cpp` — AC sweep gain/phase validation

### Task 5.2: BSIM4v7 Timestep Truncation Error ✅

- [x] Analyzed state variables contributing to LTE (charge/current pairs)
- [x] Extended `Device` interface with `virtual double compute_trunc()` override
- [x] Implemented divided-difference LTE estimation mirroring ngspice `CKTterr()`
- [x] Fixed state_base_ offset and code quality issues
- [x] Test: `test_bsim4v7_trunc.cpp` — truncation error validation

### Task 5.3: BSIM4v7 Convergence Test + Initial Conditions ✅

- [x] Implemented `device_converged()` — captures UCB `CKTnoncon` flag
- [x] Implemented `set_ic()` — VDS/VGS/VBS initial conditions
- [x] Parser: `ic=` on M-cards with empty-field handling
- [x] Test: `test_bsim4v7_cvtest.cpp` — convergence test validation

### Task 5.4: BSIM4v7 Parameter Query (ask/mask) ✅

- [x] Implemented `query_param()` — gm, gds, vth, von, capacitances, geometry, terminal voltages
- [x] Case-insensitive lookup, multi-device support
- [x] Test: `test_bsim4v7_query.cpp` — 8 test cases covering all parameter categories

---

## Phase 6: Controlled Sources + DC Sweep

**Status:** All tasks completed. 229 tests passing. Branch: `phase6-controlled-sources-dc-sweep`.

**Goal:** Add linear controlled sources (VCVS, VCCS, CCVS, CCCS) and DC sweep analysis. These are table-stakes features for any SPICE simulator — without them, op-amp models, feedback networks, and parametric characterization don't work.

**Delivered:**
- 4 controlled sources: E (VCVS), G (VCCS), H (CCVS), F (CCCS) — all with DC, AC, parser, tests
- DC sweep with nested sweep support, warm-start Newton, RAW writer
- `.save` signal filtering across all analysis types
- Refactored `Circuit::finalize()` to use virtual `assign_branch_index()`

### Task 6.1: VCVS (E element) ✅

- [x] `src/devices/vcvs.hpp/cpp` — branch variable, 6-position MNA stamp, gain relationship
- [x] Parser, `Circuit::finalize()` integration, AC stamp
- [x] 11 tests: unit + DC integration + AC flat response

### Task 6.2: VCCS (G element) ✅

- [x] `src/devices/vccs.hpp/cpp` — 4-position transconductance stamp, no branch variable
- [x] Parser, AC stamp
- [x] 10 tests: unit + DC integration + AC flat response

### Task 6.3: CCVS (H element) ✅

- [x] `src/devices/ccvs.hpp/cpp` — branch variable + `const VSource*` sense reference
- [x] Parser with deferred VSource resolution, AC stamp
- [x] 13 tests: unit + DC transimpedance + AC + error cases

### Task 6.4: CCCS (F element) ✅

- [x] `src/devices/cccs.hpp/cpp` — 2-position stamp against sense branch, no branch variable
- [x] Parser with deferred VSource resolution, AC stamp
- [x] 13 tests: unit + DC current mirror + AC + error cases

### Task 6.5: DC Sweep Analysis ✅

- [x] `solve_dc_sweep()` with warm-start Newton (INITJCT first point, INITFIX subsequent)
- [x] Nested sweep (outer + inner loop, flattened results)
- [x] Parser for `.dc`, `DCSweepResult`, RAW writer, Simulator API
- [x] 10 tests: resistor divider, diode IV, nested, parser, RAW writer, errors

### Task 6.6: `.save` Filtering ✅

- [x] Parser populates `ckt.save_signals` from `.save` lines
- [x] API layer filters DC, transient, AC, DC sweep results
- [x] Default: all signals when no `.save` present
- [x] 13 tests: all analysis types with and without filtering

---

## Phase 7: Subcircuits + Parameter Expressions ✅ COMPLETE

**Status:** All tasks completed and merged to `main` via `5b827b6`. 363 C++ tests passing.

**Goal:** Add hierarchical netlists (`.subckt`/`.ends`/`X`) and full `.param` expression evaluation. This is the gateway to real-world netlists — virtually every PDK and design uses subcircuits.

**Why now:** Without subcircuits, users can't use library models, PDK cells, or any hierarchical design. This is the single biggest usability gap.

**Approach:** Expansion happens in two stages: `.include`/`.lib` at the text level (pre-tokenization), then subcircuit X-instance flattening as a token-level "Pass 0.5" between subcircuit extraction and element parsing. Hierarchical naming uses dot separators (`x1.r1`, `x1.mid`). Parameters resolved via topological sort (Kahn's algorithm).

**Files delivered:**
- Expression evaluator: `expression.cpp` (enhanced with `**`, 10 functions, `if()`, topo sort)
- Subcircuit parsing: `subcircuit.hpp`, `subcircuit_expand.hpp/cpp`
- Parser: `netlist_parser.cpp` (Pass 0 subcircuit extraction, Pass 0.25 param pre-resolve, Pass 0.5 X expansion, `.include`/`.lib` resolution)

### Task 7.1: `.subckt` / `.ends` Definition Parsing ✅

- [x] Parse subcircuit blocks: `.subckt name port1 port2 ... [params]` through `.ends`
- [x] Store as `SubcircuitDef` — template with port list, internal netlist, parameters
- [x] Support nested subcircuit definitions
- [x] Support `.param` defaults in subcircuit header
- [x] 14 tests

### Task 7.2: `X` Instance Expansion (Flattening) ✅

- [x] Parse `X` element lines: `Xname n1 n2 ... subckt_name [param=val ...]`
- [x] Flatten: create unique internal node names (e.g., `x1.internal_node`)
- [x] Map subcircuit ports to instance connections
- [x] Recursive expansion for nested `X` instances with depth limit (100)
- [x] Parameter override: instance params override subcircuit defaults
- [x] CCVS/CCCS Vsense hierarchical prefixing
- [x] Top-level `.param` visible during expansion
- [x] 22 tests

### Task 7.3: `.param` Expression Evaluator ✅

- [x] Arithmetic: `+`, `-`, `*`, `/`, `**` (right-associative), unary `-`
- [x] Built-in functions: `sqrt()`, `abs()`, `log()`, `log10()`, `exp()`, `sin()`, `cos()`, `min()`, `max()`, `pow()`
- [x] Conditional: `if(cond, true_val, false_val)` (cond > 0 = true)
- [x] Parameter cross-reference with `{expr}` brace syntax
- [x] Topological sort via Kahn's algorithm, circular dependency detection
- [x] Case-insensitive function and parameter names
- [x] 46 tests including PDK-style parameter chains

### Task 7.4: `.lib` Section Selection ✅

- [x] Parse `.lib filename section` — include only the named section
- [x] `.lib` / `.endl` delimiters within library files
- [x] Path resolution relative to including file
- [x] Nested `.include` within lib sections
- [x] 13 tests: multi-section library, corner selection, case-insensitive

### Task 7.5: `.include` with Path Resolution ✅

- [x] Handle relative paths (relative to including file, not CWD)
- [x] Handle absolute paths
- [x] Detect and error on circular includes (canonical path tracking)
- [x] Diamond includes allowed (non-circular re-include)
- [x] Quoted and unquoted filenames
- [x] 14 tests: multi-file netlist hierarchy

### Task 7.6: Integration Test — PDK-Style Netlists ✅

- [x] MOSFET wrapper subcircuit with internal parasitics (series gate R)
- [x] Three-stage inverter chain using `X` instances
- [x] DC, transient, AC analysis on subcircuit-based netlists
- [x] `.lib` corner selection integration (TT vs FF)
- [x] `.include` + subcircuit integration
- [x] Nested subcircuits (3 levels deep)
- [x] Parameter expression chains, `.param` inside body
- [x] 24 PDK-style integration tests

---

## Phase 8: BJT + Additional Device Models ✅ COMPLETE

**Status:** All tasks completed and merged to `main` via `81b97de`. 416 C++ tests passing.

**Goal:** Add BJT (Gummel-Poon model), JFET (Shockley model), and coupled inductors (K element). The BJT is essential for analog design — bipolar op-amps, bandgap references, current mirrors. The JFET covers analog switches and low-noise front-ends. Coupled inductors enable transformers and coupled filters.

**Approach:** Used the auto-migration tool to port BJT and JFET from ngspice. Coupled inductors implemented natively (no ngspice counterpart needed). All devices validated against ngspice with dedicated comparison tests.

**Files delivered:**
- BJT: `src/devices/bjt/` — descriptor, auto-migrated + adapted (def.hpp, shim, load, temp, setup, device adapter)
- JFET: `src/devices/jfet/` — descriptor, auto-migrated + adapted (same structure as BJT)
- Coupled inductor: `src/devices/coupled_inductor.hpp/cpp` — native K element implementation
- Parser: `netlist_parser.cpp` — Q/J/K element parsing, deferred resolution, `.model NPN/PNP/NJF/PJF`
- Model cards: `model_cards.hpp/cpp` — `to_bjt_card()`, `to_jfet_card()` dispatching
- Tests: `test_bjt.cpp`, `test_jfet.cpp`, `test_coupled_inductor.cpp` — 53 new tests

**Known limitations (to be addressed in Phase 9):**
- K elements inside subcircuits don't get inductor name prefixing during expansion
- `temp_done_` flag prevents multi-temperature reuse (matches existing BSIM4v7 pattern)

### Task 8.1: BJT Descriptor + Auto-Migration ✅

- [x] Created `tools/descriptors/bjt.yaml` — 4 terminals (C, B, E, S), 24 states, 7 source files
- [x] Ran migration: generated `src/devices/bjt/` with def.hpp, shim, adapter, translated .cpp files
- [x] Fixed compilation issues: missing constants (CONSTKoverQ, REFTEMP), macros (IOPAU/IOPR/IOPA), type casts
- [x] Build passing with all translated files

### Task 8.2: BJT Device Adapter + Parser ✅

- [x] Wired `BJTDevice` adapter — ghost voltages, journal-based sparsity, instance splicing
- [x] Parser: `Q` element lines with 3 or 4 terminals, deferred model resolution
- [x] Parser: `.model name NPN/PNP(params...)` routing through BJT mParam tables
- [x] AC stamp (G/C split), truncation error, convergence test, IC support, query_param

### Task 8.3: BJT Validation Suite ✅

- [x] Test: NPN Ic-Vce DC sweep characteristics vs ngspice
- [x] Test: BJT current mirror DC operating point vs ngspice
- [x] Test: Common-emitter amplifier transient (pulse input) vs ngspice
- [x] Test: BJT amplifier AC gain/phase vs ngspice
- [x] Test: PNP device (opposite polarity) vs ngspice

### Task 8.4: JFET ✅

- [x] Created `tools/descriptors/jfet.yaml` — 3 terminals, 13 states, 6 source files
- [x] Ran migration, fixed compilation (same pattern as BJT)
- [x] Parser: `J` element with area/m/ic= support, `.model name NJF/PJF`
- [x] AC stamp (15 G-entries, 7 C-entries), truncation, convergence, query_param
- [x] 14 tests: parser, model card, device, ngspice comparison (DC + AC)

### Task 8.5: Coupled Inductors (K element) ✅

- [x] Implemented `CoupledInductor` — M = k*sqrt(L1*L2), cross-coupling MNA stamps
- [x] Trapezoidal and Gear-2 companion models (R_eq_m = 2M/dt and 1.5M/dt)
- [x] Parser: `K` element with deferred inductor resolution
- [x] Constructor validation: null checks, coupling range, self-coupling rejection
- [x] 20 tests: unit, DC, transient, AC transformer, ngspice comparison

---

## Phase 9: Noise Analysis + Measurement ✅ COMPLETE

**Status:** All tasks completed and merged to `main`. 470 C++ tests passing (54 new).

**Goal:** Add `.noise` frequency-domain noise analysis and `.measure` post-processing. Noise is critical for analog design (LNA, oscillator phase noise, sensor front-ends). `.measure` is critical for automation.

**Approach:** Noise uses the adjoint method (solve Y^T * adj = e_out, then accumulate S * |adj[i]-adj[j]|^2). Device noise models: resistor thermal (4kT/R), diode shot (2qI), BSIM4v7 simplified channel thermal (4kT*2/3*gm). Measure supports all standard SPICE forms. All noise results validated against ngspice.

**Files delivered:**
- Noise: `src/core/noise.hpp/cpp` — adjoint noise solver with 2n×2n real encoding
- Device noise: `resistor.cpp`, `diode.cpp`, `bsim4v7_device.cpp` — `noise_sources()` overrides
- Measure: `src/core/measure.hpp/cpp` — TRIG/TARG, FIND/WHEN, statistics, PARAM
- Output: `src/output/output.hpp/cpp` — `.print`/`.plot` formatting with AC variants
- Parser: `.noise`, `.meas`/`.measure`, `.print`/`.plot` in `netlist_parser.cpp`
- Tests: `test_noise.cpp` (17), `test_measure.cpp` (21), `test_output.cpp` (9), ngspice noise comparison (3)
- Fixes: K-in-subcircuit inductor prefixing, reset_temp() for multi-temperature reuse

**Known limitations (to be addressed in Phase 9.5):**
- No flicker (1/f) noise for any device
- BSIM4v7 noise simplified (channel thermal only, not full b4v7noi.c)
- BJT and JFET have no noise models
- Resistor/BSIM4v7 noise uses hardcoded T_NOMINAL, not simulation temperature
- Duplicated `generate_frequencies()` between ac.cpp and noise.cpp

### Task 9.1: Noise Analysis Framework ✅

- [x] Parser: `.noise V(out) Vin dec 10 1 1e9` — output node, input source, sweep
- [x] Implement `solve_noise()` in `src/core/noise.cpp` (adjoint method)
- [x] Framework: at each frequency point, sum noise contributions from all devices
- [x] Noise result: input-referred and output-referred noise spectral density
- [x] 2n×2n real encoding with separate Y and Y^T matrices
- [x] 10 tests

### Task 9.2: Device Noise Models ✅

- [x] Resistor: thermal noise `4kT/R` (white, frequency-independent)
- [x] Diode: shot noise `2qI` (series resistance thermal omitted — no Rs in model)
- [x] BSIM4v7: simplified channel thermal `4kT*(2/3)*gm*m`
- [x] Test: resistor divider noise floor, RC rolloff, diode noise
- [x] Test: MOSFET channel noise order-of-magnitude check
- [x] ngspice comparison: resistor divider, RC lowpass, diode noise (3 tests)
- [x] 7 tests

### Task 9.3: `.measure` Implementation ✅

- [x] Parser: `.meas tran|ac|dc result_name TRIG ... TARG ...` and simpler forms
- [x] Supported measures: `TRIG/TARG` (delay), `FIND/WHEN` (threshold crossing), `AVG`, `RMS`, `MIN`, `MAX`, `PP`, `INTEG`, `PARAM`
- [x] Execute after simulation completes, operate on result vectors
- [x] Output: print measured values to stdout and include in result struct
- [x] 21 tests

### Task 9.4: `.print` / `.plot` Output Formatting ✅

- [x] `.print tran V(out) I(V1)` — tabular ASCII output (tab-separated, %.6e)
- [x] `.plot tran V(out)` — ASCII waveform plot (50-char width, min/max scale)
- [x] AC signal variants: VM/VP/VDB/VR/VI
- [x] Wire into simulation run loop
- [x] 9 tests

---

## Phase 9.5: ngspice Feature Parity ✅ COMPLETE

**Goal:** Close remaining gaps between neospice and ngspice for the feature set that real-world analog designers expect. After this phase, neospice should handle any netlist that uses only the devices and analyses ngspice supports in its "standard" (non-XSPICE) mode.

**Why before GPU:** GPU acceleration is a performance multiplier on an already-correct simulator. Shipping it before feature parity means users hit missing-feature walls that no amount of speed can fix. This phase ensures the simulator is functionally complete for practical use.

**Prerequisites:** Phases 5–9 complete.

### Task 9.5.1: `.options` Parsing ✅

- [x] Parser: `.options reltol=1e-4 abstol=1e-12 vntol=1e-6 trtol=7 ...`
- [x] Wire parsed values into `SimOptions` struct
- [x] Support key options: `reltol`, `abstol`, `vntol`, `chgtol`, `trtol`, `itl1`, `itl4`, `gmin`, `temp`, `tnom`, `method` (trap/gear)
- [x] Test: verify option values affect simulation behavior
- [x] Test: `.options temp=85` changes device operating points vs default 27C

### Task 9.5.2: Temperature-Aware Noise ✅

- [x] Use simulation temperature (from `.options temp` or default 27C) in noise calculations
- [x] Noise solver propagates sim temp to devices via `set_sim_temp()`
- [x] Test: noise at T=100C > noise at T=27C for resistor (4kT scales with T)
- [x] Test: ngspice comparison at non-default temperature

### Task 9.5.3: Full BSIM4v7 Noise Model ✅

- [x] Channel thermal noise (tnoiMod 0, 1, 2), flicker noise (fnoiMod 0, 1)
- [x] Gate resistance thermal noise (rgateMod 1, 2, 3)
- [x] Body resistance and drain/source series resistance thermal noise
- [x] Test: ngspice comparison for NMOS common-source amplifier noise

### Task 9.5.4: BJT + JFET Noise Models ✅

- [x] BJT: collector/base shot noise, Rb/Rc/Re thermal, base flicker noise
- [x] JFET: channel thermal/flicker noise, gate shot noise
- [x] Test: ngspice comparison for BJT and JFET noise

### Task 9.5.5: Flicker (1/f) Noise Framework ✅

- [x] Frequency-dependent noise: `S(f) = Kf * I^Af / f` in BJT, JFET, BSIM4v7, resistor
- [x] Spectrum shows 1/f slope at low frequencies, flattens at high frequencies
- [x] Test: flicker corner frequency identification

### Task 9.5.6: Voltage-Controlled / Current-Controlled Switches (SW/CSW) ✅

- [x] `src/devices/switch.hpp/cpp` — SW (voltage-controlled) and CSW (current-controlled)
- [x] Smooth transition model with hysteresis (4-state model)
- [x] `.model name SW/CSW` with Vt/Vh/It/Ih/Ron/Roff parameters
- [x] Parser: `S` element (SW) and `W` element (CSW)
- [x] Test: ngspice comparison

### Task 9.5.7: Nonlinear Controlled Sources ✅

- [x] Polynomial form: E/G/H/F POLY(N) with multi-dimensional coefficients
- [x] Table form: E/G TABLE with piecewise-linear interpolation
- [x] Test: ngspice comparison for POLY and TABLE forms

### Task 9.5.8: `.four` Fourier Analysis ✅

- [x] Parser: `.four freq V(out) [V(out2) ...]` and `.fourier` alias
- [x] DFT on last period of transient data
- [x] Output: DC, fundamental, harmonics (up to 9th), THD
- [x] Implementation: `src/core/fourier.hpp/cpp`

### Task 9.5.9: Transmission Lines ✅

- [x] Lossless TL (T element): delay-based companion model
- [x] Lossy TL (O/LTRA): LC, RLC, RC, RG modes with convolution
- [x] Breakpoints at t = k*TD for timestep control
- [x] Test: ngspice comparison

### Task 9.5.10: Code Quality + Deduplication — PARTIAL

- [ ] Extract shared `generate_frequencies()` from `ac.cpp` and `noise.cpp`
- [x] All `Device::noise_sources()` use simulation temperature
- [ ] Audit all TODO/FIXME comments in codebase
- [x] Test: full regression suite passes (785 tests)

### Remaining from 9.5 (COMPLETE):

- ~~`.step` / `.temp` parameter sweeping~~ ✅ `.step` parsing and execution (source, temp, param)
- ~~Random ASRC functions (gauss, unif, limit)~~ ✅ `gauss/agauss/unif/aunif` in `.param` expressions
- ~~LTRA RL mode~~ Not a gap — ngspice also rejects RL with "not supported yet"
- ✅ `.pz` pole-zero analysis (LAPACK dggev generalized eigenvalue)
- 785 tests passing across 123 test suites

See `docs/superpowers/plans/2026-04-20-remaining-feature-gaps.md` (COMPLETE).

---

## Phase 10: GPU Acceleration

**Goal:** Port the hot path (BSIM4v7 device evaluation) to CUDA. This is the original design doc vision (M2 GPU Phase 1). Only pursue after profiling confirms device eval is the bottleneck on representative circuits.

**Prerequisites:** Phases 5–9.5 complete. A diverse circuit test suite exists. Profiling data shows device evaluation dominates wall time for circuits with 500+ MOSFETs.

### Task 10.1: Profiling + Bottleneck Identification

- [ ] Profile representative circuits: ring oscillator, amplifier chain, SRAM cell array
- [ ] Breakdown: % time in device eval vs. matrix assembly vs. KLU solve vs. other
- [ ] Identify minimum circuit size where GPU would break even
- [ ] Document findings, get user go/no-go

**Subagent:** `general-purpose`

### Task 10.2: CUDA BSIM4v7 Kernel

- [ ] SoA (Struct of Arrays) layout for MOSFET instance parameters
- [ ] Host→device transfer of node voltages, state vectors
- [ ] CUDA kernel: batched BSIM4v7 evaluate (one thread per instance)
- [ ] Device→host: conductances, currents, charges (or keep on device if solver is GPU too)
- [ ] Test: numerical agreement with CPU path to float64 precision

**Subagent:** `general-purpose` (requires CUDA expertise)

### Task 10.3: GPU Sparse Solve (cusolverRF)

Only if profiling from Task 10.1 shows the solver is significant:

- [ ] cusolverRF wrapper: reuse KLU symbolic factorization, numeric on GPU
- [ ] Keep matrix on device (device eval writes directly into GPU matrix)
- [ ] Solution vector: device→host only for convergence check
- [ ] Test: same results as KLU path

**Subagent:** `general-purpose`

### Task 10.4: GPU/CPU Auto-Selection

- [ ] Threshold-based dispatch: GPU if node_count > threshold AND CUDA available
- [ ] Configurable threshold via `.options gpu_threshold=N` or API
- [ ] Fallback: CPU path identical to current behavior
- [ ] Test: same circuit produces same results on both paths

**Subagent:** `general-purpose`

---

## Phase Dependency Graph

```
Phase 5 (BSIM4v7 completion)
    |
    v
Phase 6 (controlled sources + DC sweep)
    |
    v
Phase 7 (subcircuits + expressions)  ← biggest unlock for real netlists
    |
    +-----> Phase 8 (BJT + devices)
    |
    +-----> Phase 9 (noise + measure)
    |
    v
Phase 9.5 (ngspice parity)  ← complete feature set before GPU
    |
    v
Phase 10 (GPU)  ← requires diverse circuit suite from 5-9.5
```

Phases 8 and 9 can run **in parallel** after Phase 7 is complete. Phase 9.5 requires both 8 and 9. Phase 10 benefits from the full device/analysis suite for profiling.

---

## Subagent Strategy

| Phase | Recommended model | Parallelism | Rationale |
|-------|------------------|-------------|-----------|
| ~~5 (BSIM4v7 AC/trunc)~~ | ~~opus~~ | ~~Done~~ | — |
| ~~6.1–6.4 (E/G/H/F)~~ | ~~sonnet~~ | ~~Done~~ | — |
| ~~6.5 (DC sweep)~~ | ~~sonnet~~ | ~~Done~~ | — |
| ~~7.1–7.3 (subcircuit core)~~ | ~~opus~~ | ~~Done~~ | — |
| ~~7.4–7.6 (lib/include/test)~~ | ~~sonnet~~ | ~~Done~~ | — |
| ~~8.1–8.5 (BJT/JFET/K)~~ | ~~sonnet~~ | ~~Done~~ | — |
| ~~9.1–9.4 (noise/measure/output)~~ | ~~opus + sonnet~~ | ~~Done~~ | — |
| 9.5.1 (.options) | sonnet | Independent | Parser + wiring |
| 9.5.2 (temp noise) | sonnet | After 9.5.1 (needs .options temp) | Small fix |
| 9.5.3 (BSIM4v7 noise) | opus | After 9.5.5 (needs 1/f framework) | Complex migration |
| 9.5.4 (BJT/JFET noise) | sonnet | After 9.5.5 | Pattern-follow |
| 9.5.5 (1/f framework) | sonnet | Independent | Framework extension |
| 9.5.6 (switches) | sonnet | Independent | Self-contained |
| 9.5.7 (nonlinear sources) | opus | Independent | Parser complexity |
| 9.5.8 (.four) | sonnet | Independent | Standard algorithm |
| 9.5.9 (transmission lines) | opus | Independent | State management |
| 9.5.10 (cleanup) | sonnet | After all 9.5.x | Sweep cleanup |
| 10.x (GPU) | opus | Sequential | CUDA integration |

**Parallelism within 9.5:** Tasks 9.5.1, 9.5.5, 9.5.6, 9.5.7, 9.5.8, 9.5.9 are independent and can run in parallel. Tasks 9.5.2–9.5.4 depend on 9.5.1 and 9.5.5. Task 9.5.10 runs last.

**Key principle:** Use `isolation: "worktree"` for each subagent to prevent conflicts. Merge each task's worktree after review passes.

---

## Estimated Effort

| Phase | Tasks | Est. sessions | Cumulative tests |
|-------|-------|---------------|------------------|
| ~~5~~ | ~~4~~ | ~~Done~~ | 158 |
| ~~6~~ | ~~6~~ | ~~Done~~ | 229 |
| ~~7~~ | ~~6~~ | ~~Done~~ | 363 |
| ~~8~~ | ~~5~~ | ~~Done~~ | 416 |
| ~~9~~ | ~~4~~ | ~~Done~~ | 470 |
| 9.5 | 10 | 5–8 | ~540 |
| 10 | 4 | 4–6 | ~560 |

Total: ~25–35 sessions across all phases.

---

## Success Criteria

At the end of Phase 9.5, NEOSPICE should be able to:

1. **Simulate any flat netlist** using R, C, L, V, I, D, M (BSIM4v7), Q (BJT), J (JFET), E, G, F, H, K, S (SW), W (CSW), T (transmission line)
2. **Simulate hierarchical netlists** with `.subckt`/`X` and parameter expressions
3. **Run all analysis types:** `.op`, `.dc`, `.tran`, `.ac`, `.noise`, `.four`
4. **Post-process:** `.measure` for automated characterization, `.print`/`.plot` output
5. **Match ngspice** within 1% for all supported features including noise (with flicker, full device models)
6. **Handle PDK-style netlists** with `.lib` section selection, `.include` paths, `.options`
7. **Support nonlinear controlled sources** (POLY, TABLE forms)
8. **Configurable simulation parameters** via `.options`

Phase 10 adds GPU acceleration for large circuits as a performance multiplier.
