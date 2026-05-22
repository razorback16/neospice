# Unified Multi-Mode Simulation Architecture for neospice

**Date:** 2026-05-22
**Status:** Design — not yet implemented
**Supersedes:** Section 11.1 (Verilog-A Behavioral Models) of `mixed-signal-architecture.md` — that section was a brief future-extension sketch; this document provides the full design.
**Builds on:** `mixed-signal-architecture.md` (multi-rate partitioning, digital models, boundary management)

## 1. Executive Summary

This document describes the architecture for extending neospice into a unified multi-mode circuit simulator supporting four simulation paradigms within a single transient run:

1. **SPICE mode** — the existing Newton-Raphson nonlinear solver (unchanged)
2. **PWL mode** — a new piecewise-linear topology-switching engine inspired by SIMPLIs, where nonlinear devices are modeled as explicit PWL segments with event-driven topology changes and direct linear solves (no Newton iteration)
3. **Verilog-A mode** — industry-standard behavioral device models compiled by OpenVAF into shared libraries, loaded at runtime via the OSDI (Open Source Device Interface)
4. **Digital mode** — PWL gates, compiled FSM evaluator, and real-number behavioral blocks (as designed in `mixed-signal-architecture.md`)

The key architectural decision is **multi-engine with shared MNA infrastructure** (Approach A). Each simulation mode has its own partition solver, but all solvers share the same `SparsityPattern`, `NumericMatrix`, and `NeoSolver` linear algebra stack. The multi-rate scheduler from the mixed-signal architecture orchestrates all partition types, managing per-partition timesteps and cross-mode boundary exchange.

Users control mode assignment via per-subcircuit annotations (`mode=spice|pwl|digital`). Devices within the same subcircuit are guaranteed to land in the same partition. Cross-mode connections become partition boundaries with PWL interpolation.

### What This Enables

- **Power supply designers** can simulate a buck converter's power stage with the PWL engine (10-100x faster than SPICE for switching circuits) while keeping the analog error amplifier in SPICE mode for accuracy
- **IC designers** can load BSIM-CMG or PSP compact models as pre-compiled Verilog-A shared libraries without recompiling neospice
- **Mixed-signal designers** can combine all four modes: a digital controller (FSM), driving a PWL power stage, feeding an analog filter (SPICE), with Verilog-A behavioral models for ADC/DAC interfaces

---

## 2. Architecture Overview

### 2.1 Multi-Engine Design

```
┌──────────────────────────────────────────────────────────────────────┐
│                     Multi-Rate Scheduler (orchestrator)               │
│                    (src/core/multirate_scheduler.hpp)                 │
│                                                                      │
│  Priority queue of partition events. Manages global sync,            │
│  boundary exchange, idle detection, adaptive sync interval.          │
│                                                                      │
│  ┌──────────────┐ ┌──────────────┐ ┌──────────────┐ ┌────────────┐ │
│  │SpicePartition │ │ PwlPartition │ │DigitalPartition│ │IdlePartition│
│  │              │ │              │ │              │ │            │ │
│  │ Newton-      │ │ Direct solve │ │ FSM table    │ │ Sleeping   │ │
│  │ Raphson loop │ │ + zero-cross │ │ lookup +     │ │ (skip)     │ │
│  │ (existing)   │ │ event detect │ │ PWL gates    │ │            │ │
│  │              │ │              │ │              │ │            │ │
│  │ Verilog-A    │ │ PWL device   │ │ (no matrix)  │ │            │ │
│  │ devices via  │ │ library      │ │              │ │            │ │
│  │ OSDI live    │ │              │ │              │ │            │ │
│  │ here too     │ │              │ │              │ │            │ │
│  │              │ │              │ │              │ │            │ │
│  │ NeoSolver    │ │ NeoSolver    │ │              │ │            │ │
│  │ NumericMatrix│ │ NumericMatrix│ │              │ │            │ │
│  └──────┬───────┘ └──────┬───────┘ └──────┬───────┘ └─────┬──────┘ │
│         └────────────────┴────────────────┴───────────────┘        │
│                     Boundary Value Exchange                         │
│               (PWL interpolation between syncs)                     │
└──────────────────────────────────────────────────────────────────────┘
```

### 2.2 Shared Infrastructure

All partition types share:

| Component | Usage |
|-----------|-------|
| `SparsityPattern` | One per partition — extracted from the monolithic pattern at finalize time |
| `NumericMatrix` | One per partition — stamped by the partition's devices |
| `NeoSolver` | One per partition — factorizes and solves the partition's sub-matrix |
| `BoundaryValue` | PWL interpolation segments at partition interfaces |
| `TimeStepController` | One per SPICE/PWL partition — LTE-based adaptive stepping |
| DC operating point | Always monolithic (all partitions merged) |
| Output interpolation | Unified output at user-requested `tstep` grid |

### 2.3 What Each Mode Is Good For

| Mode | Best For | Mechanism | Typical Speedup vs Monolithic SPICE |
|------|----------|-----------|--------------------------------------|
| SPICE | Analog precision (amplifiers, filters, bias networks) | Newton-Raphson iteration | 1x (baseline) |
| PWL | Switching circuits (power supplies, motor drives, Class-D) | Direct linear solve + topology switching | 10-100x |
| Digital | Controllers, state machines, digital logic | Compiled FSM lookup / PWL gate evaluation | 100-1000x |
| Idle | Stable bias networks, unused blocks | Skip entirely | ∞ |

---

## 3. PartitionSolver Interface

The polymorphic interface that all partition solver engines implement:

```cpp
struct StepResult {
    double t_reached;           // actual time advanced to
    double t_next_suggested;    // suggested next wake-up time
    bool topology_changed;      // PWL: topology switched; SPICE: switch device toggled
    bool converged;             // SPICE: Newton converged; PWL: always true
};

class PartitionSolver {
public:
    virtual ~PartitionSolver() = default;

    // Advance partition from current t_local toward t_target.
    // May stop early (zero-crossing in PWL, Newton failure in SPICE).
    // Returns actual time reached and next suggested step.
    virtual StepResult advance(double t_target) = 0;

    // Inject boundary values from other partitions before advance().
    virtual void update_boundaries(std::span<const BoundaryValue> values) = 0;

    // Export current solution at boundary nodes after advance().
    virtual void read_boundaries(std::span<BoundaryValue> out) const = 0;

    // Check if this partition should wake from IDLE state.
    virtual bool should_wake(std::span<const BoundaryValue> values) const = 0;

    // Initialize from DC operating point solution.
    virtual void initialize(const std::vector<double>& dc_solution) = 0;
};
```

### 3.1 SpicePartitionSolver

Wraps the existing `newton_solve()` + `TimeStepController`:

```cpp
class SpicePartitionSolver : public PartitionSolver {
    PartitionInfo& partition_;
    SimOptions opts_;

    StepResult advance(double t_target) override {
        // Existing adaptive loop: propose dt, stamp devices, Newton iterate,
        // check LTE, accept/reject, advance. Unchanged algorithm.
        // Boundary nodes injected as time-varying voltage sources in the RHS.
    }
};
```

OSDI-loaded Verilog-A devices participate in SPICE partitions as standard `Device` subclasses (see Section 6). They stamp conductances and currents via `evaluate()` exactly like built-in models.

### 3.2 PwlPartitionSolver

The new topology-switching engine (detailed in Section 4).

### 3.3 DigitalPartitionSolver

The compiled FSM + PWL gate evaluator from `mixed-signal-architecture.md`:

```cpp
class DigitalPartitionSolver : public PartitionSolver {
    StepResult advance(double t_target) override {
        // For FSM devices: one table lookup per clock edge
        // For PWL gate devices: evaluate gate network
        // No matrix solve — outputs computed directly from inputs
    }
};
```

---

## 4. PWL Topology-Switching Engine

### 4.1 Core Concept

In traditional SPICE, nonlinear devices stamp operating-point-dependent conductances and currents into the MNA matrix, and Newton-Raphson iterates until the stamps converge. In PWL mode, every device defines a finite set of **linear topologies** — each topology is a fixed set of MNA stamps (conductances + current sources) that are exact within a region of the device's operating space. The circuit is always linear within the current topology combination, so a single LU factorize + solve produces the exact solution. No iteration required.

Topology switches occur when a device's terminal voltage or current crosses a boundary between PWL segments. A zero-crossing detector locates these events precisely, the engine switches the affected device to its new topology, re-stamps the matrix, and continues.

### 4.2 PwlDevice Interface

`PwlDevice` extends the existing `Device` base class. This is critical: it means PWL devices participate in the standard `DeviceRegistry` dispatch, the monolithic `finalize()` loop, and the DC operating point solve — all without special-casing. The PWL-specific behavior is layered on top via additional virtual methods.

```cpp
class PwlDevice : public Device {
public:
    // ── PWL-specific types ──

    struct PwlTopology {
        std::string name;       // e.g., "OFF", "ON", "SATURATED"
        std::vector<std::pair<MatrixOffset, double>> stamps;  // {offset, conductance}
        std::vector<std::pair<int32_t, double>> rhs;          // {node, current}
    };

    struct SwitchCondition {
        int from_topology;
        int to_topology;
        // Zero-crossing function: triggers when zc(solution) crosses zero
        // direction: +1 = rising, -1 = falling, 0 = either
        std::function<double(const double* solution)> zc_func;
        int direction;
    };

    // ── PWL-specific interface ──

    virtual int num_topologies() const = 0;
    virtual int current_topology() const = 0;
    virtual const PwlTopology& get_topology(int idx) const = 0;
    virtual std::span<const SwitchCondition> switch_conditions() const = 0;

    // Called when a switch condition fires
    virtual void switch_to(int new_topology) = 0;

    // For reactive elements: companion model depends on timestep and integration method
    virtual void update_companion(double dt, int method) = 0;

    // ── Device interface implementation ──
    // PwlDevice provides a default evaluate() that stamps the current topology.
    // During monolithic DC solve, this makes the device behave as a standard
    // linear stamp. During PWL transient, the PwlPartitionSolver calls
    // stamp_current_topology() directly instead of evaluate().

    void evaluate(const std::vector<double>& voltages,
                  NumericMatrix& mat, std::vector<double>& rhs) override {
        stamp_current_topology(mat, rhs);
        // Check if solution moved across a topology boundary;
        // if so, switch and set matrix_structure_changed_ = true
        check_and_switch(voltages);
    }

    bool matrix_structure_changed() const override {
        return topology_changed_flag_;
    }

    // Stamp the current topology's fixed conductances and currents.
    // In PWL transient: called once per topology change or dt change
    // (not per Newton iteration). In monolithic DC: called via evaluate().
    void stamp_current_topology(NumericMatrix& mat, std::vector<double>& rhs);

    // For AC analysis: linearize around the DC operating point using the
    // current topology's conductance as the G matrix contribution.
    // Reactive elements (C, L in PWL companions) contribute to the C matrix.
    void ac_stamp(const std::vector<double>& voltages,
                  NumericMatrix& G, NumericMatrix& C) override;

protected:
    bool topology_changed_flag_ = false;
};
```

### 4.3 PWL Transient Algorithm

```
procedure pwl_advance(partition, t_target):
    while partition.t_local < t_target:
        dt = min(partition.dt_local, t_target - partition.t_local)

        // 1. Re-stamp matrix if needed
        if topology_changed or dt_changed_for_reactive:
            zero(matrix)
            zero(rhs)
            for each device in partition.pwl_devices:
                device.stamp(matrix, rhs)
            inject_boundary_sources(partition, partition.t_local + dt)
            solver.numeric(pattern, matrix)       // full LU factorization
            topology_changed = false
        else:
            // Only boundary values changed — update RHS, reuse existing factorization
            update_boundary_rhs(partition, partition.t_local + dt)
            // No refactorize needed: matrix is unchanged, just solve with new RHS

        // 2. Direct solve — ONE shot, no iteration
        rhs_copy = rhs
        solver.solve(rhs_copy)
        solution_new = rhs_copy

        // 3. Check zero-crossing conditions
        events = detect_zero_crossings(partition, solution_prev, solution_new)

        if events.empty():
            // No topology change — accept step
            solution_prev = solution_new
            partition.t_local += dt

            // LTE check for reactive elements
            dt_lte = evaluate_lte(partition)
            if dt_lte < 0.9 * dt:
                // Reject and retry with smaller step
                // (only possible when reactive elements cause curvature)
                partition.t_local -= dt
                dt = dt_lte
                continue
            partition.dt_local = min(dt * growth_factor, dt_max)

        else:
            // Find earliest zero-crossing
            (t_cross, event) = find_earliest_crossing(
                events, partition.t_local, dt, solution_prev, solution_new)

            // Accept partial step up to crossing point
            alpha = (t_cross - partition.t_local) / dt
            solution_at_cross = lerp(solution_prev, solution_new, alpha)
            solution_prev = solution_at_cross
            partition.t_local = t_cross

            // Switch topology
            event.device.switch_to(event.to_topology)
            topology_changed = true
            // dt stays the same — next iteration re-stamps and resolves

    return StepResult{
        .t_reached = partition.t_local,
        .t_next_suggested = partition.t_local + partition.dt_local,
        .topology_changed = any_topology_changed,
        .converged = true   // PWL always converges (direct solve)
    }
```

### 4.4 Zero-Crossing Detection

The zero-crossing detector locates the precise time when a switching condition function crosses zero, using Brent's method for robust root finding:

```
procedure find_earliest_crossing(events, t_local, dt, sol_prev, sol_new):
    t_earliest = t_local + dt
    earliest_event = null

    for each event in events:
        // Only check events relevant to current topology
        if event.from_topology != event.device.current_topology():
            continue

        zc_prev = event.zc_func(sol_prev)
        zc_new = event.zc_func(sol_new)

        // Check direction
        if event.direction == +1 and zc_prev >= 0: continue   // already past
        if event.direction == -1 and zc_prev <= 0: continue

        // Sign change confirms crossing occurred
        if sign(zc_prev) == sign(zc_new): continue

        // Brent's method to refine crossing time
        t_lo = t_local
        t_hi = t_local + dt
        f_lo = zc_prev
        f_hi = zc_new

        for i in 1..MAX_BISECTIONS:           // typically 10-12
            t_mid = brent_step(t_lo, t_hi, f_lo, f_hi)

            // For purely resistive PWL circuits, linear interpolation of
            // the solution is exact. For circuits with reactive elements
            // (C, L companions), the actual waveform is exponential between
            // timesteps. We use linear interpolation as an approximation,
            // which is accurate when dt is small relative to the RC/RL
            // time constant — guaranteed by the LTE-based adaptive
            // timestep control (Section 4.5).
            alpha = (t_mid - t_local) / dt
            sol_mid = lerp(sol_prev, sol_new, alpha)
            f_mid = event.zc_func(sol_mid)

            if |f_mid| < zc_abstol:
                break
            if sign(f_mid) == sign(f_lo):
                t_lo = t_mid
                f_lo = f_mid
            else:
                t_hi = t_mid
                f_hi = f_mid

        t_cross = (t_lo + t_hi) / 2
        if t_cross < t_earliest:
            t_earliest = t_cross
            earliest_event = event

    return (t_earliest, earliest_event)
```

**Accuracy of linear interpolation for reactive circuits:** Within a single topology, the solution between two timesteps follows the ODE `x'(t) = A*x(t) + b` where A is the topology's system matrix. The exact trajectory is exponential, not linear. Linear interpolation introduces an error proportional to `(dt/tau)^2` where `tau` is the smallest time constant in the partition. The LTE-based timestep controller (Section 4.5) already constrains `dt` so that the second-difference of the solution is within tolerance — this same constraint bounds the interpolation error for zero-crossing detection. For circuits where this is insufficient (very stiff reactive networks in PWL mode), the timestep will be small enough that the linear approximation is adequate. Additionally, after locating the approximate crossing time, the engine takes a step to that time and re-evaluates the full solution, so any residual interpolation error is corrected in the next step.

Only one event fires per step. After switching topology, the next step may immediately detect another crossing (cascading switches). The outer loop in `pwl_advance` handles this naturally. A configurable `max_cascading_switches` limit (default: 100) prevents infinite switching loops (Zeno behavior). When the limit is reached, the affected partition falls back to SPICE mode for the remainder of the current sync interval, then retries PWL mode at the next sync.

### 4.5 Reactive Elements in PWL Mode

Capacitors and inductors in PWL partitions use companion models with fixed coefficients per timestep:

**Trapezoidal rule:**
```
C: G_eq = 2C/dt,    I_eq = -G_eq * V_prev - I_C_prev
L: G_eq = dt/(2L),  I_eq =  I_L_prev + G_eq * V_L_prev
```

**Backward Euler:**
```
C: G_eq = C/dt,     I_eq = -G_eq * V_prev
L: G_eq = dt/L,     I_eq =  I_L_prev
```

Where `I_C_prev` / `I_L_prev` are the capacitor/inductor currents at the previous timestep (stored as state variables), and `V_prev` / `V_L_prev` are the terminal voltages at the previous timestep.

When `dt` changes, `update_companion(dt, method)` recomputes these values and sets `topology_changed = true` to force a full re-stamp. This is the one case where the PWL matrix changes without an actual device topology switch.

For LTE-based timestep control, the PWL engine uses the same second-difference check as the SPICE engine. Within a single topology, resistive circuits produce exactly linear solutions — curvature only appears from reactive elements, so the LTE check is simpler and cheaper than in SPICE.

### 4.6 PWL DC Operating Point

The DC operating point is always solved monolithically (Section 8.2). After DC convergence, each PWL device determines its initial topology by evaluating switching conditions against the DC solution:

```
procedure set_initial_topologies(pwl_partition, dc_solution):
    for each device in pwl_partition.pwl_devices:
        best_topo = 0          // fallback: topology 0
        best_margin = -inf     // distance from nearest boundary

        for topo_idx in 0..device.num_topologies():
            conditions = device.switch_conditions()
            // Find the topology with maximum margin from all switch boundaries
            min_margin = +inf
            for each cond in conditions:
                if cond.from_topology == topo_idx:
                    zc = cond.zc_func(dc_solution)
                    // margin = distance from zero-crossing (positive = inside region)
                    if cond.direction == +1:   margin = -zc   // rising: stable when zc < 0
                    elif cond.direction == -1: margin = zc    // falling: stable when zc > 0
                    else:                      margin = abs(zc)
                    min_margin = min(min_margin, margin)

            if min_margin > best_margin:
                best_margin = min_margin
                best_topo = topo_idx

        device.switch_to(best_topo)

        // Warn if operating point is very close to a boundary (margin < vntol)
        if best_margin < vntol:
            warn("PWL device %s: DC operating point near topology boundary "
                 "(margin=%.2e), initial topology may be ambiguous", 
                 device.name(), best_margin)
```

This margin-based approach handles the edge case where the DC solution falls exactly on a topology boundary — it picks the topology with the most headroom. If no topology is stable (all margins are negative, which can happen with inconsistent switching conditions), topology 0 is used as fallback and a warning is issued.

During the monolithic DC solve, PWL devices participate as standard `Device` subclasses — they stamp their current topology's linear model and re-stamp if Newton iteration moves the solution across a topology boundary. This is effectively Newton-Raphson on the piecewise-linear model, which converges in 1-2 iterations because each linear segment is exact within its region.

---

## 5. PWL Model Format and Built-in Device Library

### 5.1 Netlist Syntax

A new `.pwl_model` directive defines piecewise-linear device models:

```spice
* Simple PWL Diode
.pwl_model D_FAST pwl_d
+ vf=0.7 ron=0.1 roff=10meg

* PWL MOSFET (switch-level)
.pwl_model SW_NMOS pwl_nmos
+ vth=2.0 ron=0.05 roff=10meg
+ ciss=1n coss=200p crss=50p
+ trise=10n tfall=8n

* PWL Comparator with hysteresis
.pwl_model CMP1 pwl_comp
+ vth_hi=2.51 vth_lo=2.49
+ voh=5.0 vol=0.0
+ rout=10 tpd=50n

* PWL Transformer (for flyback/forward topologies)
.pwl_model TX1 pwl_xfmr
+ n=10 lmag=500u rp=0.1 rs=0.05
+ lleak_p=1u lleak_s=100n

* Custom multi-segment PWL model
.pwl_model NL_RES pwl_custom
+ nodes=2
+ topology 0 name=LOW_R
+   g(1,2)=1.0
+   switch_to 1 when v(1,2) >= 5.0 rising
+ topology 1 name=HIGH_R
+   g(1,2)=0.001
+   switch_to 0 when v(1,2) <= 4.5 falling
```

### 5.2 Built-in PWL Model Types

| Type Keyword | Description | Topologies | Parameters |
|-------------|-------------|------------|------------|
| `pwl_d` | Diode (switch-level) | OFF, ON (optional: TRANSITION) | `vf`, `ron`, `roff`, `trr`, `cj` |
| `pwl_nmos` | NMOS switch | OFF, LINEAR (optional: SATURATION) | `vth`, `ron`, `roff`, `ciss`, `coss`, `crss`, `trise`, `tfall` |
| `pwl_pmos` | PMOS switch | Same as NMOS, inverted polarity | Same as `pwl_nmos` |
| `pwl_comp` | Comparator | LOW, HIGH (hysteresis) | `vth_hi`, `vth_lo`, `voh`, `vol`, `rout`, `tpd` |
| `pwl_srlatch` | SR Latch | SET, RESET | `vth`, `voh`, `vol`, `rout`, `tpd` |
| `pwl_xfmr` | Transformer *(deferred — use `pwl_custom`)* | — | — |
| `pwl_custom` | User-defined | User-specified stamps + switching conditions | User-defined |

### 5.3 Built-in PWL Device Topologies

**PwlDiode** (`pwl_d`) — 2 or 3 topologies:

```
Topology OFF:
  G(anode,cathode) = 1/Roff     (very small leakage conductance)
  I = 0

Topology ON:
  G(anode,cathode) = 1/Ron      (forward conductance)
  I_rhs = Vf / Ron              (offset for forward voltage drop)
  Optional: Cj companion model

Topology TRANSITION (optional, 3-segment mode):
  G(anode,cathode) = slope between OFF and ON breakpoints
  I_rhs = computed from PWL interpolation

Switching:
  OFF → ON:    V(anode,cathode) >= Vf  (rising)
  ON → OFF:    V(anode,cathode) <= 0   (falling)
  3-segment:   OFF → TRANS at Vf*0.8, TRANS → ON at Vf
```

**PwlMosfet** (`pwl_nmos` / `pwl_pmos`) — 2 or 3 topologies:

```
Topology OFF:
  G(drain,source) = 1/Roff
  Cgs, Cgd, Cds companion models (if specified)

Topology LINEAR:
  G(drain,source) = 1/Ron
  Same capacitance companions

Topology SATURATION (optional):
  Current source: Id = gm * (Vgs - Vth)
  Output conductance: G(drain,source) = 1/Rds_sat
  Where gm = 2*Id_sat / (Vgs_max - Vth), Rds_sat = Vds_max / Id_sat

Switching:
  OFF → LINEAR:     V(gate,source) >= Vth        (rising)
  LINEAR → OFF:     V(gate,source) <= Vth        (falling)
  LINEAR → SAT:     V(drain,source) >= Vgs - Vth (rising)
  SAT → LINEAR:     V(drain,source) <= Vgs - Vth (falling)
```

**PwlComparator** (`pwl_comp`) — 2 topologies with hysteresis:

```
Topology LOW:
  V(out) driven to Vol through Rout
  Cin on both inputs (loading)
  Propagation delay modeled as RC: Rout * Cpd where Cpd = tpd / (2.2 * Rout)

Topology HIGH:
  V(out) driven to Voh through Rout
  Same input loading and delay model

Switching:
  LOW → HIGH:   V(inp, inn) >= Vth_hi  (rising)
  HIGH → LOW:   V(inp, inn) <= Vth_lo  (falling)
```

**PwlTransformer** (`pwl_xfmr`) — **deferred to Phase 4b (future work)**

The PWL transformer is significantly more complex than the other built-in devices because its topology depends on multiple interacting state variables (magnetizing flux, primary/secondary currents) and the correct topology set varies by converter type (flyback vs forward vs push-pull). The initial device library ships without `pwl_xfmr`. Instead, users model magnetics using the `pwl_custom` format:

```spice
* Example: simplified flyback transformer via pwl_custom
.pwl_model TX_FLYBACK pwl_custom
+ nodes=4 state_vars=2
+ topology 0 name=CHARGING
+   l(1,2)=500u                ; magnetizing inductance, primary side
+   g(3,4)=1e-9                ; secondary open (leakage only)
+   switch_to 1 when i(1,2) <= 0 falling
+ topology 1 name=DISCHARGING
+   g(1,2)=1e-9                ; primary open
+   l(3,4)=5u                  ; secondary = Lmag/n^2 (n=10)
+   switch_to 0 when i(3,4) <= 0 falling
```

A full `pwl_xfmr` device with proper coupled inductor math, leakage, and automatic CCM/DCM detection will be specified in a separate design document once the core PWL engine is validated.

### 5.4 The `pwl_custom` Format

For advanced users who need full control over topology definitions:

```spice
.pwl_model SATURABLE_INDUCTOR pwl_custom
+ nodes=2 state_vars=1
+
+ topology 0 name=LINEAR
+   l(1,2)=100u
+   switch_to 1 when flux >= 0.5m rising
+
+ topology 1 name=SATURATED
+   l(1,2)=1u
+   switch_to 0 when flux <= 0.45m falling
```

**Stamp directives** in topology definitions:

| Directive | Meaning | MNA Effect |
|-----------|---------|------------|
| `g(n1,n2)=value` | Conductance between terminals | Stamp G into matrix (4 entries) |
| `r(n1,n2)=value` | Resistance between terminals | Stamp 1/R into matrix |
| `i(n1,n2)=value` | Current source from n1 to n2 | Stamp into RHS |
| `v(n1,n2)=value` | Voltage source (adds branch var) | Stamp into matrix + RHS |
| `c(n1,n2)=value` | Capacitor (companion model) | Stamp G_eq + I_eq from integration method |
| `l(n1,n2)=value` | Inductor (companion model) | Stamp G_eq + I_eq from integration method |
| `gm(n1,n2,nc1,nc2)=value` | Transconductance | Stamp controlled source |

**Switch condition expressions** support:

- `v(n1,n2)` — differential voltage between terminals
- `v(n1)` — terminal voltage (relative to partition ground)
- `i(branch)` — branch current through a voltage source or inductor
- `flux` / `charge` — state variables of reactive elements
- Arithmetic: `+`, `-`, `*`, `/`, constants
- Comparison operators define the zero-crossing function: `when expr >= threshold` is equivalent to `zc_func = expr - threshold, direction = rising`

### 5.5 Element Instantiation

PWL devices use the same element prefix as their SPICE counterparts. The parser distinguishes them by model type:

```spice
.pwl_model D_FAST pwl_d vf=0.7 ron=0.1 roff=10meg
.model D_PRECISE D level=1 IS=1e-14 N=1.05 BV=100

* Same prefix 'D', different model types:
D1 anode cathode D_FAST        ; → PwlDiode (PWL partition)
D2 anode2 cathode2 D_PRECISE   ; → standard Diode (SPICE partition)
```

Because `PwlDevice` extends `Device`, the standard `DeviceRegistry` dispatch works unchanged — the PWL device builder returns a `unique_ptr<Device>` pointing to a `PwlDevice` subclass. The registry dispatch logic:

1. Look up model card by name
2. Match device builder by prefix + priority (PWL builders at priority 50)
3. PWL builder checks if the model card type starts with `pwl_`; if so, returns a `PwlDevice` subclass as `unique_ptr<Device>`; otherwise returns `nullptr` (falls through to built-in builder at priority 0)

---

## 6. Verilog-A Integration via OpenVAF/OSDI

### 6.1 Overview

OpenVAF compiles Verilog-A source (`.va` files) into shared libraries (`.so` on Linux, `.dylib` on macOS) that export the OSDI (Open Source Device Interface). neospice implements an OSDI host that loads these libraries at runtime and wraps each OSDI model as a neospice `Device` subclass.

This gives neospice access to any Verilog-A compact model — including BSIM-CMG (FinFET), PSP, MEXTRAM, EKV, HiSIM-SOTB, and user-defined behavioral models — without recompilation.

### 6.2 Loading Flow

```
User workflow:
  1. Write or obtain Verilog-A source (e.g., bsimcmg.va)
  2. Compile with OpenVAF:  openvaf bsimcmg.va → bsimcmg.osdi
  3. Reference in netlist:  .osdi /path/to/bsimcmg.osdi

neospice workflow:
  1. Parser encounters .osdi directive
  2. OsdiModelRegistry::load() calls dlopen() on the shared library
  3. Reads OSDI descriptor tables (parameters, nodes, Jacobian structure)
  4. Registers model factories + device builders in DeviceRegistry
  5. Subsequent .model cards can reference OSDI-provided model types
  6. Element lines (M, D, Q, etc.) create OsdiDevice instances
```

```
┌─────────────────────────────────────────────────────────┐
│                      Netlist Parser                      │
│                                                          │
│  .osdi "path/to/bsimcmg.osdi"      ← load directive    │
│  .model nch nmos level=72 ...       ← uses OSDI model   │
│  M1 d g s b nch W=1u L=30n         ← standard element   │
└──────────────────┬──────────────────────────────────────┘
                   │
                   ▼
┌─────────────────────────────────────────────────────────┐
│                OsdiModelRegistry                         │
│                                                          │
│  dlopen("bsimcmg.osdi")                                 │
│  Read OSDI descriptor tables                             │
│  Register model factories in DeviceRegistry              │
│  Map OSDI type+level → DeviceRegistry entries            │
└──────────────────┬──────────────────────────────────────┘
                   │
                   ▼
┌─────────────────────────────────────────────────────────┐
│               OsdiDevice : public Device                 │
│                                                          │
│  stamp_pattern()  → OSDI node/branch descriptors         │
│  assign_offsets() → map OSDI Jacobian → MatrixOffset     │
│  evaluate()       → call OSDI load() function            │
│  ac_stamp()       → call OSDI load_ac() function         │
│  noise_sources()  → call OSDI load_noise() function      │
│  limit_voltages() → OSDI $limit callback                 │
│  query_param()    → OSDI operating point info            │
└─────────────────────────────────────────────────────────┘
```

### 6.3 OsdiDevice Adapter

The adapter bridges OSDI's calling convention to neospice's `Device` interface:

```cpp
class OsdiDevice : public Device {
    const OsdiModelDescriptor* model_desc_;
    void* instance_data_;

    // Mapping tables (built at assign_offsets time)
    std::vector<int32_t> osdi_node_to_mna_;
    std::vector<MatrixOffset> osdi_jac_to_offset_;

    // Scratch buffers (OSDI expects flat arrays in its own layout)
    std::vector<double> jac_scratch_;
    std::vector<double> rhs_scratch_;
    std::vector<double> osdi_voltages_;

public:
    void stamp_pattern(SparsityBuilder& builder) override {
        // Declare non-zero entries for every Jacobian pair the OSDI model reports
        for (auto& [row, col] : model_desc_->jacobian_entries) {
            int mna_row = osdi_node_to_mna_[row];
            int mna_col = osdi_node_to_mna_[col];
            if (mna_row > 0 && mna_col > 0)
                builder.add(mna_row, mna_col);
        }
    }

    void assign_offsets(const SparsityPattern& pattern) override {
        for (int i = 0; i < model_desc_->num_jacobian_entries; i++) {
            auto [row, col] = model_desc_->jacobian_entries[i];
            int mna_row = osdi_node_to_mna_[row];
            int mna_col = osdi_node_to_mna_[col];
            osdi_jac_to_offset_[i] = pattern.offset(mna_row, mna_col);
        }
    }

    void evaluate(const std::vector<double>& voltages,
                  NumericMatrix& mat, std::vector<double>& rhs) override {
        // Gather node voltages into OSDI layout
        for (int i = 0; i < model_desc_->num_nodes; i++)
            osdi_voltages_[i] = voltages[osdi_node_to_mna_[i]];

        // Call OSDI load function
        model_desc_->load(instance_data_,
                          osdi_voltages_.data(),
                          jac_scratch_.data(),
                          rhs_scratch_.data(),
                          &integrator_adapter_);

        // Scatter Jacobian into neospice matrix
        for (int i = 0; i < model_desc_->num_jacobian_entries; i++)
            mat.add(osdi_jac_to_offset_[i], jac_scratch_[i]);

        // Scatter RHS
        for (int i = 0; i < model_desc_->num_rhs_entries; i++)
            add_rhs_if_valid(rhs, osdi_rhs_node_[i], rhs_scratch_[i]);
    }

    // AC analysis: linearize around DC operating point.
    // OSDI v0.4 provides Jacobian-with-offset functions that can separate
    // G (conductance) and C (capacitance) matrix contributions.
    // Note: exact OSDI function names below are illustrative — implementation
    // must match the OSDI v0.4 descriptor struct layout.
    void ac_stamp(const std::vector<double>& voltages,
                  NumericMatrix& G, NumericMatrix& C) override {
        // Call OSDI to fill separate G and C contributions
        model_desc_->load_dc(instance_data_, osdi_voltages_.data(),
                             g_scratch_.data(), c_scratch_.data());
        // Scatter into G and C matrices
    }

    // Noise: query OSDI noise contributions.
    std::vector<NoiseSource> noise_sources(double freq,
        const std::vector<double>& dc_sol) const override {
        // Call OSDI noise evaluation, convert to neospice NoiseSource format
    }
};
```

### 6.4 IntegratorCtx Bridging

OSDI models expect integrator context (mode flags, timestep, integration coefficients) matching ngspice's `CKTcircuit` conventions. neospice already uses ngspice-compatible mode flags and `ag[]` coefficients by design, so the bridge is thin:

```cpp
struct OsdiIntegratorAdapter {
    // Direct passthrough from neospice IntegratorCtx:
    int mode;               // ckt.integrator_ctx.mode (ngspice-compatible bits)
    double* ag;             // ckt.integrator_ctx.ag (8-element array)
    double delta;           // ckt.integrator_ctx.delta (current timestep)
    int order;              // ckt.integrator_ctx.order (1=BE, 2=Trap/Gear)

    // State buffer pointers (mapped to neospice's rotating buffers)
    double* state0;         // current state
    double* state1;         // previous step
    double* state2;         // two steps ago
};
```

State variable mapping: each OSDI model reports its state variable count via the descriptor. `OsdiDevice::state_vars()` returns this count, and `set_state_ptrs()` wires the OSDI instance's state pointers to neospice's contiguous state buffers.

### 6.5 `$limit` and Convergence Aid Callbacks

OSDI models that use `$limit` (junction voltage limiting) call back into the simulator. neospice provides a callback implementing the standard limiting functions:

```cpp
double osdi_limit_callback(void* sim_ctx, double vnew, double vold,
                           int limit_type) {
    switch (limit_type) {
        case OSDI_LIMIT_PNJLIM:
            return pnjlim(vnew, vold, vt, vcrit);
        case OSDI_LIMIT_FETLIM:
            return fetlim(vnew, vold, vto);
        default:
            return vnew;
    }
}
```

These are the same limiting functions already used by neospice's built-in diode and MOSFET models.

### 6.6 Registration and Conflict Resolution

When both a built-in neospice model and an OSDI model claim the same device type and level:

| Priority | Source | Example |
|----------|--------|---------|
| 100 | OSDI-loaded model | `.osdi bsim4.osdi` providing NMOS level=14 |
| 0 | Built-in model | neospice's native BSIM4v7 (NMOS level=14) |

OSDI models win by default. This lets users override built-in models with Verilog-A versions for testing or to get newer model revisions. Users can force built-in models with `.options prefer_builtin=1`.

### 6.7 Scope and Limitations

OSDI models participate in **SPICE partitions only**. They are Newton-Raphson devices that stamp operating-point-dependent conductances and currents. They cannot participate in PWL partitions (which require explicit topology definitions) or digital partitions.

neospice does not interpret Verilog-A source at runtime. Users must pre-compile with OpenVAF. The `.osdi` directive loads a pre-compiled shared library — there is no embedded Verilog-A parser or compiler.

Full Verilog-AMS (digital + analog co-simulation in Verilog syntax) is out of scope. The digital simulation capabilities described in `mixed-signal-architecture.md` cover that domain through the FSM evaluator and PWL gate library.

### 6.8 Small-Signal Analyses (AC, Noise, PZ, Sensitivity) with Mixed Partitions

AC, noise, pole-zero, sensitivity, and transfer function analyses are inherently **monolithic** — they linearize around the DC operating point and solve a single frequency-domain system. Partitioning does not apply to these analyses. However, mixed-mode circuits contain both standard `Device` and `PwlDevice` instances, and all must contribute to the linearized system.

**PWL devices in AC analysis:** Because `PwlDevice` extends `Device` and overrides `ac_stamp()`, PWL devices participate in AC analysis naturally. The AC contribution of a PWL device is determined by its DC operating point topology:

- Resistive stamps (conductances) from the current topology → contribute to the G matrix
- Reactive companion elements (C, L declared in PWL models) → contribute to the C matrix
- The result is a small-signal model that is exact within the current PWL segment

**PWL devices in noise analysis:** PWL devices produce thermal noise from their resistive elements. The `PwlDevice` base class provides a default `noise_sources()` implementation that returns thermal noise `4kT/R` for each conductance stamp in the current topology. Flicker noise and shot noise are not modeled (PWL devices are switch-level, not physics-based).

**OSDI devices in AC/noise:** OSDI models provide `load_ac` and `load_noise` functions via the OSDI descriptor, which are called by `OsdiDevice::ac_stamp()` and `OsdiDevice::noise_sources()` respectively. These work identically to built-in SPICE devices.

**Digital devices in AC/noise:** Digital partitions (FSM, PWL gates) do not contribute to small-signal analyses. Digital outputs appear as fixed DC values at their boundary nodes — effectively infinite impedance from the AC perspective.

**`.step` parameter sweeps:** When `.step` changes a parameter, `Circuit::reset()` is called on all devices (including `PwlDevice` instances, which reset to their default topology). Partitions are NOT rebuilt — the same partition structure is reused. If `.step` changes a model parameter that affects PWL segment boundaries, the DC operating point is re-solved and `set_initial_topologies()` re-runs to find the correct starting topology for the new parameter value.

---

## 7. Per-Subcircuit Mode Selection

### 7.1 Subcircuit Annotation Syntax

```spice
.subckt BUCK_POWER_STAGE vin sw gnd mode=pwl
  M1 vin gate sw sw SW_NMOS
  D1 gnd sw D_FAST
  L1 sw vout 10u
  C1 vout gnd 100u
.ends

.subckt ERROR_AMPLIFIER inp inn out vcc vee mode=spice
  * Full SPICE op-amp with BSIM4 transistors
  M1 ...
  M2 ...
.ends

.subckt PWM_CONTROLLER clk fb pwm_out mode=digital
  .fsm CTRL clk=clk rst=0
  + in=[fb] out=[pwm_out]
  + vdd=3.3 vih=1.65 vil=1.65
  .state IDLE
  + out=[0]
  + fb=1 -> ON
  + *    -> IDLE
  .state ON
  + out=[1]
  + fb=0 -> IDLE
  + *    -> ON
  .ends_fsm
.ends

* Top-level: no mode → defaults to spice
X_POWER vin sw gnd BUCK_POWER_STAGE
X_EA inp inn comp_out vcc vee ERROR_AMPLIFIER
X_CTRL clk comp_out pwm_out PWM_CONTROLLER
```

### 7.2 Mode Resolution Rules

Applied during `Circuit::finalize()`, in priority order:

1. **Explicit annotation**: `mode=pwl|spice|digital` on `.subckt` — highest priority
2. **Model-type inference**: if all devices in a subcircuit use `pwl_*` model cards → `pwl`
3. **Digital inference**: if subcircuit contains only FSM devices and/or `A`-prefix digital gates → `digital`
4. **Global override**: `.options default_mode=pwl` forces all untagged subcircuits
5. **Default**: `spice`

### 7.3 Constraints

- All devices within a mode-annotated subcircuit are placed in the same partition
- Nested subcircuits inherit the parent's mode unless they have their own annotation
- A `mode=pwl` subcircuit may only contain `PwlDevice` instances and passive elements (R, C, L with PWL companion models). If a standard SPICE device (e.g., a BSIM4 MOSFET) is found inside a `mode=pwl` subcircuit, the parser raises an error
- A `mode=spice` subcircuit may contain both standard SPICE devices and OSDI-loaded Verilog-A devices
- Cross-mode connections automatically become partition boundary nodes

### 7.4 Mixed Mode at Top Level

Devices at the top level (not inside any subcircuit) are assigned modes based on their model type:

```spice
* Top-level devices without subcircuit wrapping:
R1 a b 1k                     ; → spice (default for passives)
D1 c d D_FAST                 ; → pwl (model is pwl_d)
M1 e f g g BSIM4_NMOS         ; → spice (standard MOSFET model)
```

**Partition assignment for top-level devices:**

1. Each device gets a mode tag: `pwl` if its model is a `pwl_*` type, `spice` otherwise. Passive elements (R, C, L) without a model card default to `spice`.
2. The partitioner performs a connected-component traversal: starting from each PWL device, it floods through shared nodes, collecting all directly-connected devices. If a shared node connects a PWL device and a SPICE device (e.g., `D1` and `R1` sharing node `b`), the **shared node becomes a boundary node**. The SPICE device stays in the SPICE partition; the PWL device goes in the PWL partition.
3. Passive elements connected *only* to PWL devices (no SPICE neighbors) are absorbed into the PWL partition and use companion models.
4. Single-device PWL partitions are permitted but generate a diagnostic note suggesting the user wrap them in a subcircuit for clarity.

```spice
* Example: D1 and R1 share node b
D1 a b D_FAST                 ; → PWL partition 0
R1 b c 1k                     ; → SPICE partition 1
* Node b is a boundary node (owned by PWL partition 0)
```

---

## 8. Multi-Rate Scheduler and Cross-Mode Boundaries

### 8.1 Scheduler Data Structures

```cpp
struct PartitionInfo {
    PartitionId id;
    PartitionMode mode;                 // SPICE, PWL, DIGITAL, IDLE
    PartitionMode previous_mode;        // saved when entering IDLE

    // The partition solver engine owns ALL solver state:
    // SparsityPattern, NeoSolver, NumericMatrix, RHS, solution vectors.
    // PartitionInfo holds only the engine and partition metadata.
    std::unique_ptr<PartitionSolver> engine;

    // Devices (all are Device* since PwlDevice extends Device)
    std::vector<Device*> devices;
    // Convenience: pre-cast PwlDevice pointers for PWL partitions
    std::vector<PwlDevice*> pwl_devices;

    // Nodes
    std::vector<int32_t> internal_nodes;
    std::vector<int32_t> boundary_nodes;

    // Time state
    double t_local;
    double dt_local;

    // Current solution (owned here, shared with engine via pointer)
    std::vector<double> solution;
    std::vector<double> solution_prev;

    // History buffer for output interpolation
    struct Snapshot { double t; std::vector<double> sol; };
    std::deque<Snapshot> history;
};

class MultiRateScheduler {
    std::vector<PartitionInfo> partitions_;

    struct Event {
        double time;
        PartitionId partition;
        enum Type { TIMESTEP, SYNC, WAKE } type;
        bool operator>(const Event& o) const { return time > o.time; }
    };
    std::priority_queue<Event, std::vector<Event>, std::greater<>> queue_;

    double dt_sync_;
    double t_global_;

    struct BoundaryEdge {
        PartitionId owner;
        PartitionId consumer;
        int32_t node;
        BoundaryValue cache;
        double retro_error;
    };
    std::vector<BoundaryEdge> boundaries_;
};
```

### 8.2 Main Simulation Loop

```
procedure run_multirate_transient(circuit, tstep, tstop, uic):
    // ── Phase 0: DC operating point (always monolithic) ──
    dc_solution = compute_dc_operating_point(circuit)

    // ── Phase 1: Build partitions from mode annotations ──
    partitions = build_partitions(circuit)
    boundaries = identify_boundary_edges(partitions, circuit)

    // ── Phase 2: Initialize each partition ──
    for each partition p:
        p.solution = extract_local(dc_solution, p.internal_nodes)
        p.solution_prev = p.solution
        p.t_local = 0

        // Apply UIC (.ic) overrides if uic=true
        if uic:
            for each node in p.internal_nodes:
                if circuit.ic.contains(node):
                    p.solution[local_idx(node)] = circuit.ic[node]
            // Boundary nodes with .ic: owner's value takes precedence
            for each bnode in p.boundary_nodes:
                if circuit.ic.contains(bnode) and partition_owns(p, bnode):
                    p.solution[local_idx(bnode)] = circuit.ic[bnode]

        // Each engine constructor builds its own SparsityPattern, NeoSolver,
        // NumericMatrix, and RHS from the partition's device list and node set.
        switch p.mode:
            case SPICE:
                p.engine = make_unique<SpicePartitionSolver>(p, circuit)
            case PWL:
                p.engine = make_unique<PwlPartitionSolver>(p, circuit)
                set_initial_topologies(p, dc_solution)
            case DIGITAL:
                p.engine = make_unique<DigitalPartitionSolver>(p)

        p.engine.initialize(p.solution)
        schedule(Event{t=0, partition=p.id, type=TIMESTEP})

    // Initialize boundary cache from DC
    for each edge in boundaries:
        v = dc_solution[edge.node]
        edge.cache = BoundaryValue{t0=0, v0=v, t1=0, v1=v, slope=0}

    // Schedule first global sync
    dt_sync = initial_sync_interval(partitions)
    schedule(Event{t=dt_sync, partition=ALL, type=SYNC})

    // ── Phase 3: Main event loop ──
    while queue.top().time <= tstop:
        event = queue.pop()

        if event.type == SYNC:
            global_sync(partitions, boundaries)
            t_global = event.time
            schedule(Event{t=t_global + dt_sync, type=SYNC})
            continue

        p = partitions[event.partition]

        // Idle check
        if p.mode == IDLE:
            if not p.engine.should_wake(get_boundary_values(p)):
                continue
            p.mode = p.previous_mode

        // Inject latest boundary values
        p.engine.update_boundaries(get_boundary_values(p))

        // Advance partition
        result = p.engine.advance(event.time)

        // Save snapshot for output interpolation
        p.history.push_back(Snapshot{result.t_reached, p.solution})
        trim_history(p)

        // Export boundary values we own
        p.engine.read_boundaries(update_edges_owned_by(p))

        // Check for cross-mode wake-ups
        check_cross_mode_wakes(p, partitions, boundaries)

        // Schedule next step
        if result.t_reached < tstop:
            schedule(Event{t=result.t_next_suggested, partition=p.id, type=TIMESTEP})

    // ── Phase 4: Output interpolation ──
    return interpolate_all(partitions, tstep, tstop)
```

### 8.3 Global Sync Procedure

```
procedure global_sync(partitions, boundaries):
    // 1. Bring all partitions up to sync time
    for each partition p where p.t_local < t_sync and p.mode != IDLE:
        result = p.engine.advance(t_sync)
        // Record snapshot and update boundary values even during forced sync
        p.history.push_back(Snapshot{result.t_reached, p.solution})
        p.engine.read_boundaries(update_edges_owned_by(p))
        // If the advance stopped early (e.g., zero-crossing in PWL), re-schedule
        if result.t_reached < t_sync - dt_min:
            schedule(Event{t=result.t_next_suggested, partition=p.id, type=TIMESTEP})

    // 2. Exchange boundary values
    for each edge in boundaries:
        v_new = partitions[edge.owner].solution[local_idx(edge.node, edge.owner)]
        v_old = edge.cache.v1
        t_old = edge.cache.t1

        // Retrospective error: how accurate was our PWL prediction?
        v_predicted = edge.cache.v0 + edge.cache.slope * (t_sync - edge.cache.t0)
        edge.retro_error = abs(v_new - v_predicted) / (abs(v_new) + vntol)

        // Update PWL segment
        edge.cache = BoundaryValue{
            t0=t_old, v0=v_old,
            t1=t_sync, v1=v_new,
            slope=(v_new - v_old) / (t_sync - t_old + 1e-30)
        }

    // 3. Adapt sync interval
    // Note: unlike mixed-signal-architecture.md §6.4 which proposed rollback on
    // boundary error, this design tightens the sync interval going forward
    // without rollback. Rollback is expensive (requires re-solving from the
    // last sync point) and unnecessary when the adaptive sync converges within
    // a few intervals. The monolithic fallback (step 5) handles pathological cases.
    max_error = max(edge.retro_error for all edges)
    if max_error > boundary_reltol:
        dt_sync = dt_sync / 2
    elif max_error < boundary_reltol / 10:
        dt_sync = min(dt_sync * 1.5, dt_sync_max)

    // 4. Idle detection
    for each partition p where p.mode != IDLE:
        if partition_is_quiescent(p, idle_threshold, idle_window):
            p.previous_mode = p.mode
            p.mode = IDLE

    // 5. Fallback: if sync too small, merge partitions into monolithic
    if dt_sync < dt_min:
        merge_struggling_partitions(partitions)
```

### 8.4 Cross-Mode Boundary Handling

#### SPICE ↔ PWL Boundaries

Both directions use the same mechanism. The owning partition solves the boundary node normally. The consuming partition sees it as an **ideal voltage source** (MNA branch variable) driven by the PWL interpolation of the boundary cache:

```
// In consuming partition's matrix:
// Boundary node stamped as an ideal voltage source V_bnd = V_pwl(t).
// This adds a branch variable i_bnd to the MNA system:
//   Row for node:   ... + i_bnd = 0      (KCL: branch current enters node)
//   Row for branch: V_node - V_pwl(t) = 0  (branch equation)
//
// Matrix stamps (standard MNA voltage source pattern):
mat.add(node_row, branch_col, +1);
mat.add(branch_row, node_col, +1);
rhs[branch_row] = boundary_voltage_pwl(t);
```

Using an ideal voltage source branch variable avoids the ill-conditioning that would result from a large shunt conductance (Norton equivalent). The branch variable adds one row/column to the partition's MNA system per boundary node — a negligible cost since boundary nodes are few. The boundary voltage is linearly interpolated from the cache:

```
function boundary_voltage_pwl(t):
    bv = boundary_cache[node]
    if t <= bv.t0: return bv.v0
    if t >= bv.t1: return bv.v1
    return bv.v0 + bv.slope * (t - bv.t0)
```

#### Analog → Digital Boundaries

When an analog (SPICE or PWL) partition's boundary node crosses a digital threshold:

```
procedure check_cross_mode_wakes(source_partition, partitions, boundaries):
    for each edge where edge.owner == source_partition.id:
        consumer = partitions[edge.consumer]

        if consumer.mode == DIGITAL or consumer.mode == IDLE:
            v_old = edge.cache.v0
            v_new = edge.cache.v1
            vih = consumer.engine.input_threshold_high()
            vil = consumer.engine.input_threshold_low()

            if crosses(v_old, v_new, vih) or crosses(v_old, v_new, vil):
                t_cross = interpolate_crossing(edge.cache, threshold)
                schedule(Event{t=t_cross, partition=consumer.id, type=WAKE})
```

#### Digital → Analog Boundaries

Digital output changes are injected as PWL ramps with configurable rise/fall times:

```
// When digital partition produces a new output:
v_old = previous_output
v_new = current_output
t_rf = (v_new > v_old) ? tpd_rise : tpd_fall

analog_boundary_cache[node] = BoundaryValue{
    t0 = t_now,        v0 = v_old,
    t1 = t_now + t_rf, v1 = v_new,
    slope = (v_new - v_old) / t_rf
}
// Insert breakpoint in consuming analog partition
analog_partition.add_breakpoint(t_now + t_rf)
```

### 8.5 Output Collection and Interpolation

Each partition produces solution values at its own irregular time points. The output collector assembles a unified result at the user's `tstep` grid:

```
procedure interpolate_all(partitions, tstep, tstop):
    result = TransientResult{}

    for t in 0, tstep, 2*tstep, ..., tstop:
        for each node in circuit.all_nodes:
            owner = partition_owning(node)

            // Find bracketing snapshots in owner's history
            (s0, s1) = owner.history.bracket(t)

            if s0.t == s1.t:
                v = s0.sol[local_idx(node)]
            else:
                // Linear interpolation (or quadratic with 3 points if available)
                alpha = (t - s0.t) / (s1.t - s0.t)
                v = s0.sol[local_idx(node)] * (1 - alpha)
                  + s1.sol[local_idx(node)] * alpha

            result.set(node, t, v)

    return result
```

Each partition maintains a bounded rolling history (snapshots are discarded once they're older than the interpolation window needs).

---

## 9. Unified `Circuit::finalize()` Flow

The finalize flow is extended with partitioning and mode assignment:

```
procedure Circuit::finalize():
    // ── Existing steps (unchanged) ──
    // Step 0: Declare internal nodes
    for each device: device.declare_internal_nodes(*this)

    // Step 1: Assign branch indices
    for each device: device.assign_branch_index(next_branch)
    num_vars_ = next_branch

    // Step 2: Build monolithic sparsity pattern
    // PwlDevice extends Device, so the single device loop covers both types
    SparsityBuilder builder(num_vars_)
    for each device: device.stamp_pattern(builder)
    pattern_ = builder.build()

    // Step 3: Assign monolithic offsets
    for each device: device.assign_offsets(*pattern_)

    // Step 4: Temperature processing
    for each device: device.process_temperature(options.temp, options.tnom)

    // Step 5: State buffer allocation
    total_states = sum(device.state_vars() for all devices)
    allocate state0_, state1_, state2_ [total_states]
    for each device: device.set_state_ptrs(...)

    // ── New steps ──
    // Step 6: Mode assignment
    assign_partition_modes()     // apply rules from Section 7.2

    // Step 7: Build partitions
    partitions_ = build_partitions()
    //   - Group devices by mode annotation
    //   - Identify boundary nodes
    //   - Extract sub-pattern per partition
    //   - Build per-partition NeoSolver instance

    // Step 8: Validate
    validate_partitions()
    //   - Error if SPICE device in PWL partition
    //   - Error if PWL device in digital partition
    //   - Warn if single-device partitions (merge into neighbor)
```

Steps 0-5 are unchanged. Steps 6-8 are new. The monolithic pattern and offsets are still built (needed for DC operating point), and partition sub-patterns are extracted from the monolithic pattern.

---

## 10. Netlist Parser Extensions

### 10.1 New Directives

| Directive | Pass | Purpose |
|-----------|------|---------|
| `.pwl_model name type params...` | pass1 | Define a PWL device model card |
| `.osdi "path/to/file.osdi"` | pass0 (before model collection) | Load an OSDI shared library |
| `mode=pwl\|spice\|digital` | pass0 (subcircuit extraction) | Subcircuit mode annotation |
| `.options default_mode=...` | pass1 | Global mode override |
| `.options multirate=1` | pass1 | Enable multi-rate partitioning |
| `.options boundary_reltol=...` | pass1 | Boundary PWL error tolerance |
| `.options sync_ratio=...` | pass1 | Sync interval ratio |
| `.options idle_threshold=...` | pass1 | Idle detection threshold |
| `.options wake_threshold=...` | pass1 | Wake-up threshold |

### 10.2 DeviceRegistry Extensions

New registration entries:

```cpp
void DeviceRegistry::register_pwl_devices() {
    // PWL Diode
    add_model_factory({
        .type_group = "pwl_d",
        .create = [](const ModelCard& mc) { return make_pwl_diode_card(mc); }
    });
    add_device_builder({
        .prefix = 'D',
        .priority = 50,   // between built-in (0) and OSDI (100)
        .build = [](name, nodes, params, card) -> unique_ptr<Device> {
            if (card.type_group.starts_with("pwl_"))
                return make_pwl_diode(name, nodes, params, card);  // PwlDiode : PwlDevice : Device
            return nullptr;  // not a PWL model, fall through to built-in
        }
    });

    // PWL MOSFET
    add_model_factory({.type_group = "pwl_nmos", ...});
    add_model_factory({.type_group = "pwl_pmos", ...});
    add_device_builder({.prefix = 'M', .priority = 50, ...});

    // PWL Comparator, Latch
    add_model_factory({.type_group = "pwl_comp", ...});
    add_model_factory({.type_group = "pwl_srlatch", ...});
    // pwl_xfmr deferred — users use pwl_custom for magnetics

    // PWL Custom
    add_model_factory({.type_group = "pwl_custom", ...});
}

void DeviceRegistry::register_osdi_models(const OsdiLibrary& lib) {
    for (auto& model : lib.models()) {
        add_model_factory({
            .type_group = model.type_group,
            .level = model.level,
            .priority = 100,   // OSDI wins over built-in
            .create = [&model](const ModelCard& mc) {
                return make_osdi_model_card(model, mc);
            }
        });
        add_device_builder({
            .prefix = model.element_prefix,
            .priority = 100,
            .build = [&model](...) { return make_osdi_device(model, ...); }
        });
    }
}
```

Priority ordering for device builder dispatch:

| Priority | Source | Description |
|----------|--------|-------------|
| 100 | OSDI | Verilog-A compiled models (highest priority) |
| 50 | PWL | Built-in PWL device models |
| 0 | Built-in | Native neospice SPICE models (lowest priority) |

---

## 11. Python API Extensions

### 11.1 PWL Model Definition

```python
import neospice as ns

ckt = ns.Circuit()

# Define PWL models
ckt.pwl_model("D_FAST", "pwl_d", vf=0.7, ron=0.1, roff=10e6)
ckt.pwl_model("SW_N", "pwl_nmos", vth=2.0, ron=0.05, roff=10e6,
              ciss=1e-9, coss=200e-12)

# Use PWL devices (same prefix as SPICE equivalents)
ckt.D("D1", "anode", "cathode", model="D_FAST")
ckt.M("M1", "drain", "gate", "source", "source", model="SW_N")
```

### 11.2 Subcircuit Mode Annotation

```python
# Create a subcircuit with mode annotation
buck = ns.Subcircuit("BUCK_STAGE", ["vin", "sw", "gnd"], mode="pwl")
buck.pwl_model("SW", "pwl_nmos", vth=2.0, ron=0.05, roff=10e6)
buck.M("M1", "vin", "gate", "sw", "sw", model="SW")
buck.D("D1", "gnd", "sw", model="D_FAST")
buck.L("L1", "sw", "vout", 10e-6)
buck.C("C1", "vout", "gnd", 100e-6)

# Instantiate
ckt.X("X_BUCK", buck, "vin", "sw", "gnd")
```

### 11.3 OSDI Model Loading

```python
# Load Verilog-A compiled models
ckt.load_osdi("/path/to/bsimcmg.osdi")
ckt.model("nch", "nmos", level=72, LLONG=30e-9, WWIDE=1e-6)
ckt.M("M1", "d", "g", "s", "b", model="nch")
```

### 11.4 Partition Inspection

```python
result = ns.transient(ckt, tstep=1e-9, tstop=1e-3)

# Inspect partition assignment
for p in result.partitions:
    print(f"Partition {p.id}: mode={p.mode}, nodes={len(p.nodes)}, "
          f"devices={len(p.devices)}, avg_dt={p.avg_timestep:.2e}")
```

### 11.5 Multi-Rate Simulation Options

```python
result = ns.transient("buck_converter.cir",
                      tstep=1e-9, tstop=1e-3,
                      multirate=True,
                      boundary_reltol=1e-3,
                      sync_ratio=10)
```

---

## 12. Complete Example: Buck Converter

A realistic example showing all four modes in a single simulation:

```spice
* Buck Converter with Digital Controller
* Demonstrates SPICE + PWL + Digital modes in one simulation

* ── OSDI model for precision analog ──
.osdi /models/bsim4v7.osdi

* ── PWL models for power stage ──
.pwl_model SW_HI pwl_nmos vth=2.5 ron=0.025 roff=10meg ciss=2n coss=500p
.pwl_model SW_LO pwl_nmos vth=2.0 ron=0.015 roff=10meg ciss=3n coss=800p
.pwl_model D_BODY pwl_d vf=0.5 ron=0.05 roff=10meg trr=30n

* ── Standard SPICE models ──
.model NPN_SMALL NPN IS=1e-15 BF=200 VAF=100

* ── Power Stage (PWL mode — fast switching simulation) ──
.subckt POWER_STAGE vin vout gnd pwm_hi pwm_lo mode=pwl
  M_HI vin pwm_hi sw sw SW_HI
  M_LO sw pwm_lo gnd gnd SW_LO
  D_HI sw vin D_BODY
  D_LO gnd sw D_BODY
  L1 sw vout 4.7u
  C_OUT vout gnd 100u
  R_ESR vout vout_sense 10m
.ends

* ── Error Amplifier (SPICE mode — analog precision) ──
.subckt ERROR_AMP vfb vref comp vcc gnd mode=spice
  * Type-III compensator with precise op-amp model
  R1 vfb comp 10k
  R2 comp mid 100k
  C1 comp mid 100p
  C2 mid gnd 1n
  R3 vref inv 10k
  * Op-amp using OSDI-loaded or built-in transistors
  Q1 vcc inv mid NPN_SMALL
  Q2 vcc vfb out NPN_SMALL
  R_LOAD out gnd 10k
.ends

* ── Digital PWM Controller (Digital mode — compiled FSM) ──
.subckt PWM_CTRL clk comp pwm_hi pwm_lo mode=digital
  .fsm CTRL clk=clk rst=0
  + in=[comp]
  + out=[pwm_hi, pwm_lo]
  + vdd=5.0 vih=2.5 vil=2.5
  + tpd_rise=5n tpd_fall=5n rout=10
  .state OFF
  + out=[0, 0]
  + comp=1 -> ON
  + *      -> OFF
  .state ON
  + out=[1, 0]
  + comp=0 -> DEADTIME
  + *      -> ON
  .state DEADTIME
  + out=[0, 0]
  + *      -> OFF
  .ends_fsm
.ends

* ── Top-level circuit ──
VIN vin 0 12
VREF vref 0 1.25
VCLK clk 0 PULSE(0 5 0 1n 1n 4.9u 10u)

X_POWER vin vout 0 pwm_hi pwm_lo POWER_STAGE
X_EA vout_sense vref comp vcc 0 ERROR_AMP
X_CTRL clk comp pwm_hi pwm_lo PWM_CTRL

.tran 1n 1m
.options multirate=1
```

In this circuit:
- The power stage runs with the **PWL engine** — topology switches at M_HI/M_LO/D turn-on/off events, direct linear solves, no Newton iteration
- The error amplifier runs with the **SPICE engine** — Newton-Raphson for accurate small-signal behavior of the compensation network
- The PWM controller runs with the **digital engine** — compiled FSM lookup table, evaluated only on clock edges
- The output filter (L1, C_OUT) is inside the PWL partition and uses trapezoidal companion models

---

## 13. Performance Analysis

### 13.1 Expected Speedup: Buck Converter Example

Consider the circuit from Section 12 with a 100kHz switching frequency, simulated for 1ms:

| Partition | Mode | Matrix Size | Steps | Cost per Step | Total Cost |
|-----------|------|-------------|-------|---------------|------------|
| Power stage | PWL | ~10 nodes | ~100k (switching events) | 1 LU solve | 100k × O(10³) |
| Error amp | SPICE | ~20 nodes | ~10k (slow dynamics) | 5 Newton × LU | 50k × O(20³) |
| PWM controller | Digital | 0 (no matrix) | ~100 (clock edges) | 1 table lookup | 100 × O(1) |

**Monolithic SPICE** (current): 100k steps × 5 Newton iterations × full ~30-node LU = baseline

**Multi-rate with PWL**: Power stage does 1 solve per step (not 5 Newton), error amp takes 10x fewer steps, digital is essentially free. **Estimated speedup: 10-30x**.

### 13.2 Where Each Mode Wins

| Circuit Type | Best Mode | Why |
|-------------|-----------|-----|
| Buck/boost/flyback converters | PWL | Switching events dominate; PWL avoids Newton convergence at transitions |
| Class-D audio amplifiers | PWL | High switching frequency, simple device behavior |
| LDO regulators | SPICE | Continuous analog behavior, no switching |
| PLL/DLL | SPICE + Digital | VCO/charge pump need SPICE accuracy; PFD is digital |
| Sigma-delta ADC | PWL + SPICE | Comparator and switches in PWL; integrator in SPICE |
| Motor drive inverters | PWL | 6-switch inverter with simple gate drive logic |

### 13.3 Comparison with Existing Tools

| Feature | ngspice | SIMPLIs | neospice (this design) |
|---------|---------|---------|----------------------|
| SPICE NR solver | Yes | No | Yes |
| PWL topology switching | No | Yes | Yes |
| Verilog-A models | Yes (OSDI) | No | Yes (OSDI) |
| Digital simulation | XSPICE (event-driven) | Basic gates | Compiled FSM + PWL gates |
| Mixed-mode in one run | Co-simulation (slow) | Single mode only | Per-subcircuit mode selection |
| Per-subcircuit solver choice | No | No | Yes |
| Multi-rate timestepping | No | No | Yes (partition-level) |

---

## 14. Implementation Plan

### Phase 1: Partitioning Infrastructure

**Goal**: Circuit graph partitioning from mode annotations. Run all partitions sequentially at the same timestep (validates partitioning without multi-rate complexity).

**Files to create:**
- `src/core/partition.hpp/.cpp` — `PartitionInfo`, `PartitionSolver` interface, partition builder
- `src/core/partition_graph.hpp/.cpp` — weighted graph from circuit topology, boundary identification

**Files to modify:**
- `src/core/circuit.hpp/.cpp` — add Steps 6-8 to `finalize()`, store partition metadata
- `src/core/transient.hpp/.cpp` — add partition-aware path (iterate partitions sequentially at same dt)
- `src/parser/netlist_parser.cpp` — parse `mode=` annotation on `.subckt`

**Validation**: All existing tests pass. Partitioned simulation produces identical results to monolithic.

### Phase 2: Multi-Rate Scheduler

**Goal**: Priority-queue event scheduler, per-partition timestep, boundary exchange, adaptive sync.

**Files to create:**
- `src/core/multirate_scheduler.hpp/.cpp` — `MultiRateScheduler`, event queue, main loop
- `src/core/boundary.hpp/.cpp` — `BoundaryValue`, `BoundaryEdge`, PWL interpolation, error monitoring

**Files to modify:**
- `src/core/transient.cpp` — new entry point `solve_transient_multirate()` dispatching through scheduler
- `src/core/timestep.hpp/.cpp` — per-partition `TimeStepController` instances

**Validation**: Benchmark on circuits with disparate time constants. Verify speedup proportional to time-constant ratio.

### Phase 3: PWL Engine

**Goal**: `PwlDevice` interface, PWL transient algorithm, zero-crossing detector.

**Files to create:**
- `src/core/pwl_solver.hpp/.cpp` — `PwlPartitionSolver`, zero-crossing detection, topology switching
- `src/devices/pwl/pwl_device.hpp` — `PwlDevice` base class

**Files to modify:**
- `src/core/partition.hpp` — integrate `PwlPartitionSolver` as a `PartitionSolver` variant

**Validation**: Simple PWL circuits (ideal buck converter with PWL switch + diode) produce correct waveforms. Compare against SPICE simulation of the same circuit.

### Phase 4: PWL Device Library

**Goal**: Built-in PWL device models and `.pwl_model` netlist syntax.

**Files to create:**
- `src/devices/pwl/pwl_diode.hpp/.cpp`
- `src/devices/pwl/pwl_mosfet.hpp/.cpp`
- `src/devices/pwl/pwl_comparator.hpp/.cpp`
- `src/devices/pwl/pwl_latch.hpp/.cpp`
- `src/devices/pwl/pwl_transformer.hpp/.cpp`
- `src/devices/pwl/pwl_custom.hpp/.cpp` — custom topology parser
- `src/devices/pwl/pwl_factory.cpp` — `DeviceRegistry` registration

**Files to modify:**
- `src/parser/netlist_parser.cpp` — parse `.pwl_model` directive
- `src/devices/device_registry.cpp` — call `register_pwl_devices()`

**Validation**: Each PWL device tested against its SPICE equivalent. Buck/boost/flyback converter test circuits matching ngspice results.

### Phase 5: OSDI Host

**Goal**: Load OpenVAF-compiled Verilog-A models at runtime.

**Files to create:**
- `src/devices/osdi/osdi_loader.hpp/.cpp` — `OsdiModelRegistry`, `dlopen`/`dlsym` wrapper
- `src/devices/osdi/osdi_device.hpp/.cpp` — `OsdiDevice : public Device` adapter
- `src/devices/osdi/osdi_types.hpp` — OSDI descriptor struct definitions (from OSDI spec)

**Files to modify:**
- `src/parser/netlist_parser.cpp` — parse `.osdi` directive
- `src/devices/device_registry.cpp` — call `register_osdi_models()` after `.osdi` loading

**Validation**: Load BSIM-CMG compiled by OpenVAF. Run DC/AC/transient against ngspice with the same OSDI model. Verify matching results within tolerance.

### Phase 6: Digital Integration

**Goal**: Integrate the digital simulation components from `mixed-signal-architecture.md` into the unified scheduler.

**Files to create:**
- `src/core/digital_solver.hpp/.cpp` — `DigitalPartitionSolver`
- Files from `mixed-signal-architecture.md` Phase 3 (PWL gates) and Phase 4 (FSM evaluator)

**Files to modify:**
- `src/core/partition.hpp` — integrate `DigitalPartitionSolver`
- `src/core/multirate_scheduler.cpp` — analog↔digital boundary wake-up logic

**Validation**: Mixed-signal test circuits with FSM controllers driving PWL power stages.

### Phase 7: Idle Detection and Optimization

**Goal**: Idle partition detection, adaptive sync interval, partition merging fallback.

**Files to modify:**
- `src/core/multirate_scheduler.cpp` — idle detection heuristics, wake-up conditions
- `src/core/boundary.cpp` — retrospective error estimation, adaptive sync

**Validation**: Large mixed-signal circuits. Verify idle partitions have zero cost. Verify fallback to monolithic under tight coupling.

### Phase 8: Python API and Documentation

**Goal**: Expose all new features to Python.

**Files to modify:**
- `python/bindings.cpp` — bind PWL model types, OSDI loading, partition info, mode annotation
- `python/neospice/__init__.py` — convenience functions, `load_osdi()`, `pwl_model()`

**New tests:**
- Python tests for PWL model definition, OSDI loading, subcircuit mode annotation, multi-rate options

---

## 15. Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| PWL models too coarse for some circuits | Inaccurate switching waveforms | Multi-segment `pwl_custom` allows arbitrary segmentation; users can fall back to SPICE mode |
| OSDI spec version mismatch with OpenVAF | Models fail to load | Target OSDI v0.4 (current version); check `OSDI_DESCRIPTOR_SIZE` for forward-compatible traversal; version check at load time with clear error message |
| Cross-mode boundary error accumulates | Solution drift between partitions | Adaptive sync with retrospective error monitoring; automatic fallback to monolithic |
| PWL zero-crossing detection misses events | Topology stuck in wrong state | Configurable `max_step` within PWL partitions to limit step size; post-step validation that current topology is consistent with solution |
| Cascading topology switches (Zeno behavior) | Simulation hangs | `max_cascading_switches` limit (default 100); fallback to SPICE mode for the affected partition |
| `dlopen` portability (OSDI loading) | Doesn't work on all platforms | Abstract behind platform layer; WASM build disables OSDI (compile-time flag) |
| PWL reactive companion accuracy | LTE errors at topology boundaries | Reduce dt at topology switches (similar to SPICE breakpoint handling); backward Euler for first step after switch |
| DC operating point with mixed PWL/SPICE devices | Convergence issues during monolithic DC | PWL devices participate in DC as linear stamps (their current topology); re-stamp on topology change during Newton iteration |

---

## 16. Relationship to Other Documents

| Document | Relationship |
|----------|-------------|
| `mixed-signal-architecture.md` | This document **builds on** its multi-rate scheduler, boundary management, PWL digital gates, and FSM evaluator. Those designs are incorporated by reference and extended with SPICE/PWL/OSDI partition types. |
| `ROADMAP.md` Phase 7 | "Verilog-A device model compilation" is realized here as OSDI host integration (Section 6). "BSIM-CMG (FinFET) model" becomes available through OSDI loading without native implementation. |
| `neospice-design.md` | The `Device` interface, `DeviceRegistry`, `NeoSolver`, `SparsityPattern`, `NumericMatrix`, `IntegratorCtx`, and Newton-Raphson solver described there are reused unchanged. |

---

## 17. Future Extensions

### 17.1 Automatic PWL Model Generation (TPWL)

Generate PWL approximations automatically from SPICE device models using Trajectory Piecewise-Linear (TPWL) methods: run a training simulation, linearize at trajectory points, and assemble a multi-topology PWL model. This would allow any SPICE device to be used in PWL mode without manual model creation.

### 17.2 Periodic Operating Point (POP) Analysis

Leverage the PWL engine's deterministic topology switching to implement POP analysis for switching power supplies: detect steady-state periodicity, skip startup transient, and directly compute the periodic waveform. This is SIMPLIs' signature analysis mode.

### 17.3 Custom Verilog-A Compiler

A native neospice Verilog-A compiler (long-term) could generate both `Device` subclasses for SPICE mode and `PwlDevice` subclasses for PWL mode from the same Verilog-A source, with `@(cross(...))` events mapping to topology switch conditions.

### 17.4 GPU-Accelerated Multi-Rate

Independent partitions can be evaluated on different GPU streams. SPICE partitions with large matrices benefit most from GPU-accelerated LU factorization. PWL and digital partitions stay on CPU (small, fast, event-driven).

### 17.5 Partition Visualization

A diagnostic tool that renders the partition graph showing which subcircuits map to which partitions, boundary nodes, and real-time activity (timestep, idle status) during simulation. Useful for debugging performance and identifying opportunities for mode reassignment.
