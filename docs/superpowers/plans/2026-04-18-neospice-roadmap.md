# NEOSPICE Roadmap: Phases 5–10

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement each phase task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a modern, correct, GPU-acceleratable SPICE simulator with the device library, analysis modes, and netlist features needed for practical analog/mixed-signal design.

**What's done:** Milestones 1–7 delivered a working CPU simulator with R/C/L/V/I/Diode/BSIM4v7 (full: DC/transient/AC/trunc/convergence/IC/query), E/G/H/F controlled sources, DC/transient/AC/DC-sweep analysis, adaptive time stepping, convergence aids, ngspice comparison framework, auto-migration tool, subcircuits (`.subckt`/`X` with hierarchical flattening), full `.param` expression evaluator, `.include`/`.lib` support, and `.save` filtering. 363 C++ tests passing.

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
| GPU acceleration (CUDA) | Not started (design doc M2/M3) | Phase 10 |
| Noise analysis | Not started | Phase 9 |
| `.measure` | Parsed, not executed | Phase 9 |

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

## Phase 8: BJT + Additional Device Models

**Goal:** Add the BJT (Gummel-Poon model) and optionally JFET. The BJT is essential for analog design — bipolar op-amps, bandgap references, current mirrors.

**Approach:** Use the auto-migration tool to port from ngspice. The BJT model (`~/Codes/ngspice/src/spicelib/devices/bjt/`) is simpler than BSIM4v7 (~3K LOC for the load function vs 5.6K).

### Task 8.1: BJT Descriptor + Auto-Migration

- [ ] Create `tools/descriptors/bjt.yaml` — map ngspice bjt struct names, terminals (C, B, E, S), source files
- [ ] Extend migration tool if needed (BJT uses similar patterns to BSIM4v7 but simpler)
- [ ] Run migration: generates `src/devices/bjt/` with def.hpp, shim, adapter, translated .cpp files
- [ ] Build, fix any compilation issues
- [ ] Verify migration tool handles BJT-specific patterns (e.g., `QXXXX` element prefix)

**Subagent:** `general-purpose`

### Task 8.2: BJT Device Adapter + Parser

- [ ] Wire `BJTDevice` adapter (same pattern as BSIM4v7Device)
- [ ] Parser: recognize `Q` element lines: `Qname C B E [S] model [area] [off]`
- [ ] Parser: `.model name NPN/PNP(params...)` routing to BJT mParam
- [ ] Test: NPN common-emitter DC characteristics vs ngspice

**Subagent:** `general-purpose`

### Task 8.3: BJT Validation Suite

- [ ] Test: NPN Ic-Vce family (DC sweep)
- [ ] Test: BJT current mirror (DC operating point)
- [ ] Test: Common-emitter amplifier transient (pulse input)
- [ ] Test: BJT amplifier AC gain/phase vs ngspice
- [ ] Test: PNP device (opposite polarity)

**Subagent:** `general-purpose`

### Task 8.4: JFET (Optional)

- [ ] Create descriptor, migrate via tool
- [ ] Parser: `J` element, `.model name NJF/PJF`
- [ ] Test: JFET common-source amplifier DC + AC

**Subagent:** `general-purpose`

### Task 8.5: Coupled Inductors (K element)

Mutual inductance between inductors — needed for transformers, coupled filters.

- [ ] Implement `K` element: `Kname L1 L2 coupling_coefficient`
- [ ] MNA: mutual inductance stamps cross-terms between L1 and L2 branch equations
- [ ] Parser: recognize `K` element lines
- [ ] Test: ideal transformer (K=1), loosely coupled coils (K=0.5), compare vs ngspice

**Subagent:** `general-purpose`

---

## Phase 9: Noise Analysis + Measurement

**Goal:** Add `.noise` frequency-domain noise analysis and `.measure` post-processing. Noise is critical for analog design (LNA, oscillator phase noise, sensor front-ends). `.measure` is critical for automation.

### Task 9.1: Noise Analysis Framework

- [ ] Parser: `.noise V(out) Vin dec 10 1 1e9` — output node, input source, sweep
- [ ] Implement `solve_noise()` in `src/core/noise.cpp`
- [ ] Framework: at each frequency point, sum noise contributions from all devices
- [ ] Noise result: input-referred and output-referred noise spectral density

**Subagent:** `general-purpose`

### Task 9.2: Device Noise Models

- [ ] Resistor: thermal noise `4kTR`
- [ ] Diode: shot noise `2qI`, flicker noise
- [ ] BSIM4v7: migrate `b4v7noi.c` (channel thermal, flicker, gate-induced noise)
- [ ] Test: resistor divider noise floor
- [ ] Test: common-source amplifier noise figure vs ngspice

**Subagent:** `general-purpose`

### Task 9.3: `.measure` Implementation

- [ ] Parser: `.meas tran|ac|dc result_name TRIG ... TARG ...` and simpler forms
- [ ] Supported measures: `TRIG/TARG` (delay), `FIND/WHEN` (threshold crossing), `AVG`, `RMS`, `MIN`, `MAX`, `PP`, `INTEG`
- [ ] Execute after simulation completes, operate on result vectors
- [ ] Output: print measured values to stdout and include in result struct
- [ ] Test: measure rise time, delay, overshoot on pulse response
- [ ] Test: measure -3dB bandwidth on AC sweep

**Subagent:** `general-purpose`

### Task 9.4: `.print` / `.plot` Output Formatting

- [ ] `.print tran V(out) I(V1)` — tabular ASCII output
- [ ] `.plot tran V(out)` — ASCII waveform plot (like ngspice)
- [ ] Wire into simulation run loop
- [ ] Test: verify output format matches ngspice

**Subagent:** `general-purpose` (low complexity)

---

## Phase 10: GPU Acceleration

**Goal:** Port the hot path (BSIM4v7 device evaluation) to CUDA. This is the original design doc vision (M2 GPU Phase 1). Only pursue after profiling confirms device eval is the bottleneck on representative circuits.

**Prerequisites:** Phases 5–8 complete. A diverse circuit test suite exists. Profiling data shows device evaluation dominates wall time for circuits with 500+ MOSFETs.

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
Phase 10 (GPU)  ← requires diverse circuit suite from 5-9
```

Phases 8 and 9 can run **in parallel** after Phase 7 is complete. Phase 10 is independent but benefits from the full device/analysis suite for profiling.

---

## Subagent Strategy

| Phase | Recommended model | Parallelism | Rationale |
|-------|------------------|-------------|-----------|
| 5 (BSIM4v7 AC/trunc) | opus | Sequential tasks (each builds on prior) | Migration tool integration, Device interface changes |
| 6.1–6.4 (E/G/H/F) | sonnet | **Parallel** — 4 independent device implementations | Each controlled source is self-contained |
| 6.5 (DC sweep) | sonnet | Sequential (after 6.1–6.4 for testing) | Analysis driver, straightforward |
| 7.1–7.3 (subcircuit core) | opus | Sequential — parser changes are coupled | Complex parser refactoring |
| 7.4–7.6 (lib/include/test) | sonnet | Can parallelize some | File handling, less coupled |
| 8.1–8.3 (BJT) | sonnet | Sequential | Migration tool + validation |
| 9.1–9.3 (noise + measure) | opus for noise framework, sonnet for measure | Noise sequential, measure independent | Noise requires new analysis type |
| 10.x (GPU) | opus | Sequential | CUDA integration, architecture decisions |

**Key principle:** Use `isolation: "worktree"` for each subagent to prevent conflicts. Merge each task's worktree after review passes.

---

## Estimated Effort

| Phase | Tasks | Est. sessions | Cumulative tests |
|-------|-------|---------------|------------------|
| ~~5~~ | ~~4~~ | ~~Done~~ | 158 |
| ~~6~~ | ~~6~~ | ~~Done~~ | 229 |
| 7 | 6 | 4–6 | ~200 |
| 8 | 5 | 3–4 | ~225 |
| 9 | 4 | 3–4 | ~245 |
| 10 | 4 | 4–6 | ~255 |

Total: ~20–27 sessions across all phases.

---

## Success Criteria

At the end of Phase 9, NEOSPICE should be able to:

1. **Simulate any flat netlist** using R, C, L, V, I, D, M (BSIM4v7), Q (BJT), E, G, F, H, K
2. **Simulate hierarchical netlists** with `.subckt`/`X` and parameter expressions
3. **Run all analysis types:** `.op`, `.dc`, `.tran`, `.ac`, `.noise`
4. **Post-process:** `.measure` for automated characterization
5. **Match ngspice** within documented tolerances for all supported features
6. **Handle PDK-style netlists** with `.lib` section selection and `.include` paths

Phase 10 adds GPU acceleration for large circuits as a performance multiplier.
