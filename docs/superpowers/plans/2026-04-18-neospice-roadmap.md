# NEOSPICE Roadmap: Phases 5–10

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement each phase task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a modern, correct, GPU-acceleratable SPICE simulator with the device library, analysis modes, and netlist features needed for practical analog/mixed-signal design.

**What's done:** Milestones 1–4 delivered a working CPU simulator with R/C/L/V/I/Diode/BSIM4v7, DC/transient/AC analysis, adaptive time stepping, convergence aids, ngspice comparison framework, and an auto-migration tool for porting ngspice device models. 135 C++ tests, 135 Python tests, 15 golden circuits all passing.

**Architecture principle:** Each phase is self-contained — it ships a testable increment, validated against ngspice, before the next phase begins. Phases are ordered by user impact (what unlocks the most real circuits).

**Reference codebase:** `~/Codes/ngspice/src/spicelib/` for device models, analysis algorithms, and parser patterns.

---

## Gap Analysis vs. Design Doc

| Design doc item | Status | Phase to address |
|-----------------|--------|------------------|
| DC operating point | Done | — |
| Transient (fixed + adaptive) | Done | — |
| AC small-signal | Done for R/C/L/V/I/Diode; **BSIM4v7 AC not migrated** | Phase 5 |
| DC sweep (`.dc`) | Enum exists, driver not wired | Phase 6 |
| Phase 1 devices (R,C,L,V,I,D) | Done | — |
| Phase 2 device (BSIM4v7) | Done (DC + transient); AC/noise/trunc missing | Phase 5 |
| `.subckt`/`.ends`/`X` | Not started | Phase 7 |
| Controlled sources (E,G,F,H) | Not started | Phase 6 |
| `.param` expressions | Scalar only | Phase 7 |
| `.save` filtering | Parsed, not enforced | Phase 6 |
| GPU acceleration (CUDA) | Not started (design doc M2/M3) | Phase 10 |
| Noise analysis | Not started | Phase 9 |
| `.measure` | Parsed, not executed | Phase 9 |

---

## Phase 5: BSIM4v7 Feature Completion

**Goal:** Complete the BSIM4v7 migration — AC analysis, truncation error, and convergence test — so MOSFET circuits work correctly in all supported analyses.

**Why first:** BSIM4v7 is the workhorse model. Without AC support, no amplifier frequency response, no stability analysis. Without truncation error, transient timesteps are suboptimal for MOSFET switching circuits.

**Approach:** Use the auto-migration tool to translate the remaining ngspice files. Extend the migration tool if needed (AC load uses complex arithmetic patterns the transformer may not handle yet).

**Files to migrate:**
- `b4v7acld.c` → `bsim4v7_acld.cpp` (AC small-signal load)
- `b4v7trunc.c` → `bsim4v7_trunc.cpp` (timestep truncation error)
- `b4v7cvtest.c` → `bsim4v7_cvtest.cpp` (convergence test)
- `b4v7getic.c` → `bsim4v7_getic.cpp` (initial conditions)

**Tasks:**

### Task 5.1: BSIM4v7 AC Small-Signal Load

Migrate `b4v7acld.c` — this stamps the linearized small-signal conductance (G) and capacitance (C) matrices for the BSIM4v7 at the DC operating point. The existing `BSIM4v7Device::ac_stamp()` is currently a no-op inherited from `Device`.

- [ ] Analyze `~/Codes/ngspice/src/spicelib/devices/bsim4v7/b4v7acld.c` — identify complex-number patterns, CKT field accesses
- [ ] Extend migration tool transformer if needed (complex stamp patterns `*(here->XPtr) += value` into complex matrix)
- [ ] Add `b4v7acld.c` to descriptor `source_files` (remove from `skip_files`)
- [ ] Run migration, build, fix any compilation errors
- [ ] Wire `BSIM4v7Device::ac_stamp()` to call the translated UCB AC load function
- [ ] Add test: RC+NMOS common-source amplifier AC sweep, compare gain/phase vs ngspice
- [ ] Add test: CMOS inverter AC small-signal gain

**Subagent:** `general-purpose` (multi-file integration, Device interface changes)

### Task 5.2: BSIM4v7 Timestep Truncation Error

Migrate `b4v7trunc.c` — this computes the local truncation error (LTE) for charge-storing elements in the MOSFET, enabling the adaptive time stepper to choose optimal step sizes.

- [ ] Analyze `b4v7trunc.c` — identify which state variables contribute to LTE
- [ ] Extend `Device` interface with `virtual double trunc_error(double dt)` or integrate with existing `TimeStepController`
- [ ] Migrate `b4v7trunc.c` via the auto-migration tool
- [ ] Wire into `BSIM4v7Device` — call during transient step acceptance
- [ ] Add test: ring oscillator transient with truncation-aware stepping, compare waveform quality vs ngspice
- [ ] Verify step count is within 2× of ngspice's adaptive step count

**Subagent:** `general-purpose`

### Task 5.3: BSIM4v7 Convergence Test + Initial Conditions

- [ ] Migrate `b4v7cvtest.c` — convergence aid for Newton iteration
- [ ] Migrate `b4v7getic.c` — apply `.ic` to MOSFET terminals
- [ ] Wire both into `BSIM4v7Device` lifecycle
- [ ] Test: MOSFET circuit with `.ic` on drain/gate, verify correct startup

**Subagent:** `general-purpose`

### Task 5.4: BSIM4v7 Parameter Query (ask/mask)

Lower priority — enables post-simulation parameter inspection.

- [ ] Migrate `b4v7ask.c` and `b4v7mask.c`
- [ ] Wire into a `query_param()` interface on the device
- [ ] Test: query Vth0, gm, gds after DC solve, compare to ngspice `.op` output

**Subagent:** `general-purpose`

---

## Phase 6: Controlled Sources + DC Sweep

**Goal:** Add linear controlled sources (VCVS, VCCS, CCVS, CCCS) and DC sweep analysis. These are table-stakes features for any SPICE simulator — without them, op-amp models, feedback networks, and parametric characterization don't work.

**Why now:** Controlled sources are the second-most-used element type after passives. DC sweep is the most common characterization analysis. Together they unlock IV curves, transfer functions, and subcircuit building blocks.

### Task 6.1: VCVS (E element — Voltage-Controlled Voltage Source)

Linear `E` element: `Eout np nn nc+ nc- gain`

- [ ] Create `src/devices/vcvs.hpp/cpp`
- [ ] MNA formulation: adds branch variable (like VSource), stamps gain into matrix
- [ ] Stamp pattern: 4 control + 2 output terminal entries
- [ ] DC evaluate: `V_out = gain * (V_nc+ - V_nc-)`
- [ ] AC stamp: same as DC (linear device)
- [ ] Parser: recognize `E` element lines
- [ ] Test: unity-gain buffer, voltage follower, compare vs ngspice

**Subagent:** `general-purpose` (new device + parser, moderate complexity)

### Task 6.2: VCCS (G element — Voltage-Controlled Current Source)

Linear `G` element: `Gout np nn nc+ nc- transconductance`

- [ ] Create `src/devices/vccs.hpp/cpp`
- [ ] MNA formulation: stamps transconductance directly (no branch variable needed)
- [ ] DC evaluate: `I_out = gm * (V_nc+ - V_nc-)`
- [ ] AC stamp: same as DC
- [ ] Parser: recognize `G` element lines
- [ ] Test: transconductance amplifier, compare vs ngspice

**Subagent:** `general-purpose`

### Task 6.3: CCVS (H element — Current-Controlled Voltage Source)

Linear `H` element: `Hout np nn Vsense transresistance`

- [ ] Create `src/devices/ccvs.hpp/cpp`
- [ ] MNA formulation: requires branch variable for output + references Vsense branch current
- [ ] Must resolve `Vsense` device reference during circuit finalization
- [ ] Parser: recognize `H` element lines, resolve voltage source reference
- [ ] Test: transimpedance circuit, compare vs ngspice

**Subagent:** `general-purpose`

### Task 6.4: CCCS (F element — Current-Controlled Current Source)

Linear `F` element: `Fout np nn Vsense gain`

- [ ] Create `src/devices/cccs.hpp/cpp`
- [ ] MNA formulation: stamps gain * branch current of Vsense into output terminals
- [ ] Must resolve `Vsense` device reference
- [ ] Parser: recognize `F` element lines
- [ ] Test: current mirror model, compare vs ngspice

**Subagent:** `general-purpose`

### Task 6.5: DC Sweep Analysis

`.dc Vsrc start stop step` — sweep a source value, solve DC at each point.

- [ ] Implement `solve_dc_sweep()` in `src/core/dc.cpp`
- [ ] Loop: set source value → solve DC OP → collect results
- [ ] Support nested sweep (`.dc V1 0 5 0.1 V2 0 3 0.5`) — outer + inner loop
- [ ] Wire into run loop: handle `AnalysisCommand::DC_SWEEP`
- [ ] Parser: parse `.dc` line with source name + start/stop/step
- [ ] RAW writer: output DC sweep results (multiple operating points)
- [ ] Test: NMOS Id-Vds family of curves, compare vs ngspice
- [ ] Test: diode IV sweep

**Subagent:** `general-purpose`

### Task 6.6: `.save` Filtering

- [ ] Enforce `.save` variable selection in result collection
- [ ] Only write requested signals to RAW output
- [ ] Default (no `.save`): all node voltages + voltage source currents (ngspice behavior)
- [ ] Test: `.save V(out) I(V1)` produces only those signals

**Subagent:** `general-purpose` (small, isolated)

---

## Phase 7: Subcircuits + Parameter Expressions

**Goal:** Add hierarchical netlists (`.subckt`/`.ends`/`X`) and full `.param` expression evaluation. This is the gateway to real-world netlists — virtually every PDK and design uses subcircuits.

**Why now:** Without subcircuits, users can't use library models, PDK cells, or any hierarchical design. This is the single biggest usability gap.

### Task 7.1: `.subckt` / `.ends` Definition Parsing

- [ ] Parse subcircuit blocks: `.subckt name port1 port2 ... [params]` through `.ends`
- [ ] Store as `SubcircuitDef` — template with port list, internal netlist, parameters
- [ ] Support nested subcircuit definitions
- [ ] Support `.param` defaults in subcircuit header

**Subagent:** `general-purpose`

### Task 7.2: `X` Instance Expansion (Flattening)

- [ ] Parse `X` element lines: `Xname n1 n2 ... subckt_name [param=val ...]`
- [ ] Flatten: create unique internal node names (e.g., `Xname.internal_node`)
- [ ] Map subcircuit ports to instance connections
- [ ] Recursive expansion for nested `X` instances
- [ ] Parameter override: instance params override subcircuit defaults
- [ ] Test: simple resistor-divider subcircuit instantiated 3 times

**Subagent:** `general-purpose` (complex parser + circuit construction changes)

### Task 7.3: `.param` Expression Evaluator

- [ ] Implement arithmetic expression parser: `+`, `-`, `*`, `/`, `**`, unary `-`
- [ ] Built-in functions: `sqrt()`, `abs()`, `log()`, `log10()`, `exp()`, `sin()`, `cos()`, `min()`, `max()`, `pow()`
- [ ] Conditional: `if(cond, true_val, false_val)` or ternary
- [ ] Parameter cross-reference: `.param x=1e-6` then `.param y={2*x}`
- [ ] Resolve order: topological sort of parameter dependencies
- [ ] Test: PDK-style parameter chains, compare resolved values

**Subagent:** `general-purpose`

### Task 7.4: `.lib` Section Selection

- [ ] Parse `.lib filename section` — include only the named section
- [ ] `.lib` / `.endl` delimiters within library files
- [ ] Path resolution relative to including file
- [ ] Test: multi-section library file, select specific corner

**Subagent:** `general-purpose`

### Task 7.5: `.include` with Path Resolution

- [ ] Handle relative paths (relative to including file, not CWD)
- [ ] Handle absolute paths
- [ ] Detect and error on circular includes
- [ ] Test: multi-file netlist hierarchy

**Subagent:** `general-purpose` (straightforward)

### Task 7.6: Integration Test — PDK-Style Netlists

- [ ] Create test netlist using subcircuit MOSFET wrapper (like PDK cell)
- [ ] Subcircuit with internal parasitics (series R on gate/drain)
- [ ] Three-stage amplifier using `X` instances
- [ ] DC, transient, AC — all compared vs ngspice

**Subagent:** `general-purpose`

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
| 5 | 4 | 2–3 | ~150 |
| 6 | 6 | 3–4 | ~175 |
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
