# Mixed-Signal Simulation Architecture for neospice

**Date:** 2026-05-03
**Status:** Design — not yet implemented

## 1. Executive Summary

This document describes the architecture for adding mixed-signal and digital behavioral simulation to neospice. The design avoids building a separate event-driven digital engine (the ngspice/XSPICE approach) and instead extends the existing analog transient solver with three complementary techniques:

1. **Latency-based multi-rate partitioning** — partition the circuit into weakly-coupled clusters, each advancing at its own optimal timestep
2. **PWL analog digital models** — small digital/mixed-signal blocks modeled as piecewise-linear voltage-mode elements in the KCL matrix
3. **Compiled FSM evaluator** — large digital blocks (state machines, controllers) compiled to native lookup functions, triggered only on clock edges

The result is a unified solver architecture — no separate event engine, no bridge models, no rollback bookkeeping — that is faster and simpler than the XSPICE approach while matching or exceeding its accuracy for behavioral-level mixed-signal simulation.

---

## 2. Motivation

### 2.1 Why Mixed-Signal Matters

Modern analog circuits rarely exist in isolation. A switched-mode power supply has a digital controller. A PLL has a phase-frequency detector. A sensor AFE has an ADC and a digital filter. Simulating these circuits today requires either:

- Replacing digital blocks with ideal sources (loses timing, loading, and feedback effects)
- Using ngspice XSPICE (complex, slow co-simulation overhead)
- Using commercial tools like Spectre AMS ($$$)

neospice can occupy a unique position: a fast, embeddable, open simulator that handles mixed-signal natively.

### 2.2 Why Not Clone XSPICE

ngspice's XSPICE approach has fundamental limitations:

| Problem | Consequence |
|---------|-------------|
| Separate event engine takes turns with analog solver | Serialized co-simulation; synchronization overhead at every boundary |
| 3-state digital model (0/1/X) with strength resolution | Loses analog information (ringing, supply droop, crosstalk) inside digital blocks |
| ADC/DAC bridge models force analog breakpoints | Shatters timestep efficiency in circuits with many crossings (PLLs, sigma-deltas) |
| Time-stamped linked lists for rollback | Memory allocation and bookkeeping overhead on every event |
| Per-gate event propagation | O(gates) work per clock edge; no batching |

### 2.3 Design Principles

1. **No separate engine** — everything flows through the existing transient solver
2. **Multi-rate by default** — idle subcircuits don't waste computation
3. **Analog accuracy where it matters** — small mixed-signal blocks retain full voltage-level simulation
4. **Compilation over interpretation** — large digital blocks are evaluated as native functions, not event queues
5. **Incremental adoption** — each component (partitioning, PWL models, FSM evaluator) is independently useful

---

## 3. Architecture Overview

```
┌──────────────────────────────────────────────────────────────────┐
│                    Transient Analysis Driver                      │
│                   (src/core/transient.cpp)                        │
│                                                                  │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │              Multi-Rate Scheduler (new)                     │  │
│  │                                                            │  │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐  │  │
│  │  │Partition 0│  │Partition 1│  │Partition 2│  │Partition 3│  │  │
│  │  │ Analog   │  │ Analog   │  │ Digital  │  │ Idle     │  │  │
│  │  │ dt=1ps   │  │ dt=100ps │  │ clk-edge │  │ sleeping │  │  │
│  │  │          │  │          │  │  only    │  │          │  │  │
│  │  │ Newton   │  │ Newton   │  │ Compiled │  │ (skip)   │  │  │
│  │  │ solve    │  │ solve    │  │ FSM eval │  │          │  │  │
│  │  └────┬─────┘  └────┬─────┘  └────┬─────┘  └──────────┘  │  │
│  │       │              │              │                       │  │
│  │       └──────────────┴──────────────┘                       │  │
│  │              Boundary value exchange                         │  │
│  │          (PWL interpolation between syncs)                  │  │
│  └────────────────────────────────────────────────────────────┘  │
│                                                                  │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────────────┐  │
│  │ PWL Digital  │  │ Compiled FSM │  │ Existing Device      │  │
│  │ Device Models│  │ Evaluator    │  │ Models (R,C,M,...)   │  │
│  │ (new)        │  │ (new)        │  │ (unchanged)          │  │
│  └──────────────┘  └──────────────┘  └──────────────────────┘  │
└──────────────────────────────────────────────────────────────────┘
```

The architecture has three layers:

- **Layer 1: Multi-Rate Scheduler** — partitions the circuit graph and manages per-partition timesteps
- **Layer 2: Digital Device Models** — PWL analog models for small blocks, compiled evaluators for large blocks
- **Layer 3: Boundary Management** — PWL interpolation and threshold-based wake-up at partition interfaces

---

## 4. Layer 1: Latency-Based Multi-Rate Partitioning

### 4.1 Core Idea

Instead of solving the entire circuit as one monolithic Newton system at every timestep, we:

1. Partition the circuit into weakly-coupled clusters at `Circuit::finalize()` time
2. Give each partition its own timestep, matrix, and solver instance
3. Only re-evaluate a partition when its boundary inputs change beyond a threshold
4. Between sync points, approximate boundary signals as piecewise-linear

### 4.2 Circuit Graph Partitioning

#### 4.2.1 Graph Construction

At finalize time, build an undirected weighted graph:

- **Nodes** = circuit nodes (voltage unknowns) + branch variables
- **Edges** = connections through devices, weighted by coupling strength

Coupling strength heuristics:

| Connection Type | Weight | Rationale |
|----------------|--------|-----------|
| Resistor < 1kΩ | 1.0 (strong) | Low impedance = tight coupling |
| Resistor > 1MΩ | 0.01 (weak) | High impedance = natural partition point |
| Capacitor | `C × ω_max` | Frequency-dependent coupling |
| MOSFET drain-source | 1.0 (strong) | Active device = tight coupling |
| MOSFET gate-drain (Cgd) | 0.1–0.5 | Parasitic coupling |
| Voltage source | ∞ (forced same partition) | Nodes are algebraically coupled |
| B-source input → output | 0.5 | Behavioral coupling |
| Digital block I/O | 0.01 (weak) | Natural partition boundary |

#### 4.2.2 Partitioning Algorithm

Use multilevel graph partitioning (Metis-style, but simplified for circuit graphs):

1. **Coarsen** — merge strongly-coupled node pairs into supernodes
2. **Partition** — bisect the coarsened graph at minimum-weight cuts
3. **Refine** — un-coarsen and refine boundaries using Kernighan-Lin swaps
4. **Classify** — label each partition as `ANALOG_FAST`, `ANALOG_SLOW`, `DIGITAL`, or `IDLE`

**Partition classification rules:**

| Classification | Criteria | Timestep Strategy |
|---------------|----------|-------------------|
| `ANALOG_FAST` | Contains switching devices, oscillators, or high-frequency signals | Fine adaptive timestep (existing LTE control) |
| `ANALOG_SLOW` | Contains only bias networks, slow filters, or DC paths | Coarse timestep (10-100× larger) |
| `DIGITAL` | Contains only PWL digital models or compiled FSM blocks | Event-triggered (clock edges only) |
| `IDLE` | No input changes above threshold since last evaluation | Sleeping — skip entirely |

**Constraints on partitioning:**
- All nodes of a single device must be in the same partition (no device splitting)
- Voltage sources force their two nodes into the same partition
- User can override with `.partition` directives (explicit assignment)
- Minimum partition size: 2 nodes (below this, merge into neighbor)
- Maximum partition count: `sqrt(N)` where N = total nodes (diminishing returns beyond this)

#### 4.2.3 Boundary Nodes

Nodes at partition boundaries are **shared nodes**. Each shared node belongs to exactly one partition (the "owner") and appears as a fixed voltage source in all other partitions that reference it.

```
Partition A (owner of node X)          Partition B (uses node X)
┌─────────────────────┐                ┌─────────────────────┐
│  ... ──── node X ───┼── boundary ──→ │  V_boundary = PWL(t)│
│     (solved)        │                │      (fixed source) │
└─────────────────────┘                └─────────────────────┘
```

Ownership assignment: the partition containing the strongest driver of the node owns it. For resistive connections, the partition with the lower Thevenin impedance at that node owns it.

### 4.3 Per-Partition Solver Infrastructure

Each partition gets:

```cpp
struct Partition {
    PartitionId id;
    PartitionType type;  // ANALOG_FAST, ANALOG_SLOW, DIGITAL, IDLE

    // Devices and nodes in this partition
    std::vector<Device*> devices;
    std::vector<int32_t> internal_nodes;   // nodes owned by this partition
    std::vector<int32_t> boundary_nodes;   // shared nodes (input from other partitions)

    // Independent solver state
    std::unique_ptr<SparsityPattern> pattern;
    std::unique_ptr<NeoSolver> solver;
    NumericMatrix matrix;
    std::vector<double> rhs;
    std::vector<double> solution;

    // Independent timestep state
    double t_local;           // current local time
    double dt_local;          // current local timestep
    double t_next_sync;       // next mandatory sync point

    // Boundary value cache
    struct BoundaryValue {
        int32_t node;
        double t0, v0;       // start of PWL segment
        double t1, v1;       // end of PWL segment
        double slope;         // (v1 - v0) / (t1 - t0)
    };
    std::vector<BoundaryValue> boundary_cache;

    // Activity tracking
    double max_boundary_delta;  // largest boundary change since last eval
    bool awake;
};
```

Each partition's `SparsityPattern` and `NeoSolver` are built during finalization by extracting the sub-matrix corresponding to that partition's nodes. This means each partition solves a smaller linear system — the key performance advantage.

### 4.4 Multi-Rate Time-Stepping Algorithm

#### 4.4.1 Global Scheduler

The scheduler maintains a priority queue of partition wake-up times:

```cpp
struct SchedulerEvent {
    double time;
    PartitionId partition;
    EventType type;  // TIMESTEP, SYNC, WAKE_FROM_IDLE
};

// Priority queue ordered by time (earliest first)
std::priority_queue<SchedulerEvent, ..., greater<>> event_queue;
```

#### 4.4.2 Main Loop

```
procedure multi_rate_transient(circuit, t_stop):
    partitions = partition_circuit(circuit)
    for each partition p:
        build_sub_matrix(p)
        symbolic_factor(p.solver, p.pattern)
        p.t_local = 0
        schedule(p, t=0, TIMESTEP)

    // Global sync interval (configurable, default = output timestep)
    dt_sync = output_timestep

    while event_queue.top().time <= t_stop:
        event = event_queue.pop()
        p = partitions[event.partition]

        if event.type == SYNC:
            exchange_boundary_values(p, partitions)
            check_wake_conditions(partitions)
            schedule_next_sync(dt_sync)
            continue

        if p.type == IDLE:
            if p.max_boundary_delta < wake_threshold:
                continue  // stay asleep
            else:
                p.type = previous_type
                p.awake = true

        // Advance partition to event.time
        t_target = event.time

        if p.type == DIGITAL:
            evaluate_digital(p, t_target)
        else:
            // Standard Newton-Raphson with local adaptive timestep
            while p.t_local < t_target:
                dt = min(p.dt_local, t_target - p.t_local)
                inject_boundary_values(p, p.t_local + dt)

                converged = newton_solve(p)
                if not converged:
                    dt /= 8
                    retry
                
                dt_lte = evaluate_lte(p)
                if dt_lte < 0.9 * dt:
                    reject, retry with dt_lte
                
                accept_step(p, dt)
                p.t_local += dt
                p.dt_local = propose_next_dt(p, dt, dt_lte)

        // Schedule next event for this partition
        schedule(p, p.t_local + p.dt_local, TIMESTEP)

    // Final sync and output interpolation
    synchronize_all(partitions)
    interpolate_to_output_times(partitions)
```

#### 4.4.3 Boundary Value Exchange

At sync points, partitions exchange boundary node values:

```
procedure exchange_boundary_values(partitions):
    for each shared node N:
        owner = partition_owning(N)
        v_new = owner.solution[N]
        t_new = owner.t_local

        for each partition P that uses N as boundary:
            v_old = P.boundary_cache[N].v1
            t_old = P.boundary_cache[N].t1

            // Update PWL segment
            P.boundary_cache[N] = {
                t0 = t_old, v0 = v_old,
                t1 = t_new, v1 = v_new,
                slope = (v_new - v_old) / (t_new - t_old)
            }

            // Track activity
            delta = abs(v_new - v_old)
            P.max_boundary_delta = max(P.max_boundary_delta, delta)
```

Between sync points, boundary values are interpolated using the PWL segment:

```
function boundary_voltage(partition, node, t):
    bv = partition.boundary_cache[node]
    if t <= bv.t0: return bv.v0
    if t >= bv.t1: return bv.v1
    return bv.v0 + bv.slope * (t - bv.t0)
```

#### 4.4.4 Idle Detection and Wake-Up

A partition transitions to `IDLE` when:
- All its internal nodes have changed less than `idle_threshold` (default: `vntol * 10`) over the last `idle_window` timesteps (default: 5)
- All its boundary inputs have been stable for the same window

A partition wakes from `IDLE` when:
- Any boundary input changes by more than `wake_threshold` (default: `vntol * 100`)
- A scheduled breakpoint (source event) occurs within the partition

### 4.5 Convergence and Stability

#### 4.5.1 Waveform Relaxation Convergence

Multi-rate partitioned simulation is a form of **waveform relaxation** (Gauss-Seidel iteration on waveform segments). Convergence is guaranteed when:

1. The coupling between partitions is weak relative to the coupling within partitions
2. The sync interval is small enough relative to the time constants of inter-partition coupling
3. Boundary value extrapolation doesn't diverge

The partitioning algorithm ensures condition (1) by cutting at weak connections. Condition (2) is enforced by the sync interval constraint. Condition (3) is handled by using PWL interpolation (bounded, no divergence) rather than polynomial extrapolation.

#### 4.5.2 Sync Interval Selection

The global sync interval `dt_sync` is set to:

```
dt_sync = min(
    output_timestep,                          // land on output points
    1 / (10 * f_max_boundary),               // 10× Nyquist of fastest boundary signal
    min_partition_timestep * sync_ratio       // sync_ratio = 10 (configurable)
)
```

Where `f_max_boundary` is estimated from the boundary signal slopes observed during simulation (adaptive).

#### 4.5.3 Error Control

Each partition has its own LTE-based timestep control (reusing existing `TimeStepController`). The global error is bounded by:

- **Intra-partition error**: controlled by per-partition LTE (existing mechanism)
- **Inter-partition error**: controlled by sync interval and boundary PWL approximation error

The boundary PWL approximation error at a sync point is:

```
e_boundary = |v_actual(t) - v_pwl(t)|_max over the sync interval
```

This is estimated retrospectively: at each sync, compare the new boundary value against what the PWL predicted. If the error exceeds `boundary_tol` (default: `reltol * |v| + vntol`), halve the sync interval for the next segment.

#### 4.5.4 Fallback to Monolithic

If partitioned simulation fails to converge (boundary errors remain large after sync interval reduction), the scheduler can fall back to monolithic (single-partition) simulation. This ensures robustness:

```
if sync_interval < dt_min:
    merge_all_partitions()
    // Fall back to existing monolithic transient solver
```

---

## 5. Layer 2: Digital Device Models

### 5.1 PWL Analog Digital Models

For small digital blocks (< ~50 gates) or mixed-signal-critical elements (comparators, level shifters, sense amplifiers), we model digital behavior using analog voltage-mode elements with PWL transfer characteristics.

#### 5.1.1 PWL Inverter Model

The fundamental building block. Modeled as a voltage-controlled voltage source with a piecewise-linear transfer curve:

```
         Vout
         VDD ┤─────┐
              │      \
              │       \  slope = -gain
              │        \
           0  ┤         └─────
              └──┬──┬──┬──────── Vin
                 0  VIL VIH  VDD
```

Parameters:
- `VDD` — supply voltage
- `VIL` — input low threshold
- `VIH` — input high threshold
- `gain` — slope in transition region (= `VDD / (VIH - VIL)`)
- `Rout` — output resistance
- `Cin` — input capacitance (loads the driving stage)
- `tpd` — propagation delay (modeled as an RC at the output)

Device implementation:

```cpp
class PWLInverter : public Device {
    // Stamps into KCL matrix as:
    //   G-source: I_out = f(V_in) where f is the PWL curve
    //   R_out: output resistance
    //   C_in: input capacitance
    //
    // f(Vin) is continuous and piecewise-linear:
    //   Vin < VIL:           Vout = VDD
    //   VIL <= Vin <= VIH:   Vout = VDD - gain*(Vin - VIL)
    //   Vin > VIH:           Vout = 0
    //
    // The Jacobian df/dVin is exact and piecewise-constant:
    //   dVout/dVin = 0        (Vin < VIL or Vin > VIH)
    //   dVout/dVin = -gain    (VIL <= Vin <= VIH)
};
```

This is numerically well-behaved for SPICE: the transfer function is continuous, the Jacobian is bounded, and Newton converges in 2-3 iterations for typical digital transitions.

#### 5.1.2 PWL Gate Library

All gates derived from the inverter model using De Morgan equivalents and series/parallel output networks:

| Gate | Implementation |
|------|---------------|
| Inverter | Single PWL stage |
| Buffer | Two cascaded inverters |
| NAND | Parallel pull-up + series pull-down (2-input PWL) |
| NOR | Series pull-up + parallel pull-down (2-input PWL) |
| AND | NAND + inverter |
| OR | NOR + inverter |
| XOR | NAND/NOR combination |
| Tristate | PWL with high-impedance output state |

Each gate has parameterizable: `VDD`, `VIL`, `VIH`, `Rout`, `Cin`, `tpd_rise`, `tpd_fall`.

#### 5.1.3 PWL Flip-Flop

A D flip-flop as interconnected PWL gates:

```
        ┌─────────────────────────────┐
  D ────┤  Master latch (transparent  │
        │  when CLK=0)                │
  CLK ──┤                             ├──── Q
        │  Slave latch (transparent   │
  RST ──┤  when CLK=1)               ├──── Q_bar
        └─────────────────────────────┘
```

Internally: 4 NAND gates + 2 inverters = 6 PWL elements. The master-slave topology naturally provides edge-triggered behavior through the analog dynamics of the PWL gates and their RC delays.

**Advantage over event-driven**: the flip-flop's setup/hold timing emerges naturally from the analog simulation — no need to specify timing constraints as parameters. Metastability is automatically captured when the setup time is violated.

#### 5.1.4 PWL DAC/ADC Models

**DAC**: A voltage source whose output is computed from digital input bits:

```cpp
class PWLDac : public Device {
    // V_out = V_ref * (sum of weighted input bits) / 2^N
    // Each input bit is a voltage node; threshold at VDD/2
    // Output has Rout and slew-rate limiting via RC
};
```

**ADC**: A bank of comparators (PWL inverters with offset thresholds) driving a priority encoder:

```cpp
class PWLAdc : public Device {
    // Flash-style: N comparators at evenly spaced thresholds
    // Outputs are analog voltages that represent digital bits
    // Sampling controlled by a clock input (PWL switch)
};
```

### 5.2 Compiled FSM Evaluator

For large digital blocks (state machines, protocol controllers, counters, shift registers), gate-level PWL simulation is unnecessarily expensive. Instead, we compile the FSM description into a native evaluation function.

#### 5.2.1 FSM Description Format

Users describe state machines in a structured format (embedded in netlist or separate file):

```spice
* neospice FSM definition
.fsm CONTROLLER clk=CLK rst=RST
+ in=[SDA, SCL, BUSY]
+ out=[ACK, DATA_VALID, ERROR]
+ vdd=3.3 vih=1.65 vil=1.65
+ tpd_rise=1n tpd_fall=1n
+ rout=100

.state IDLE
+ out=[0, 0, 0]
+ SDA=1 SCL=1       -> WAIT_START
+ SDA=0 SCL=1       -> START_DET
+ *                  -> IDLE

.state START_DET
+ out=[0, 0, 0]
+ BUSY=0             -> RECEIVE
+ BUSY=1             -> ERROR_STATE
+ *                  -> IDLE

.state RECEIVE
+ out=[0, 0, 0]
+ SCL=1 SDA=*        -> CHECK_DATA
+ *                  -> RECEIVE

.state CHECK_DATA
+ out=[1, 1, 0]
+ *                  -> IDLE

.state ERROR_STATE
+ out=[0, 0, 1]
+ RST=1              -> IDLE
+ *                  -> ERROR_STATE

.ends_fsm
```

#### 5.2.2 Compilation to Lookup Table

At `Circuit::finalize()` time, the FSM description is compiled into a flat lookup table:

```cpp
struct CompiledFSM {
    int n_states;
    int n_inputs;
    int n_outputs;

    // Transition table: state × input_combination → next_state
    // Flattened: transition[state * n_input_combos + input_code] = next_state
    std::vector<int> transition_table;

    // Output table: state → output_values
    // Flattened: output[state * n_outputs + output_idx] = value (0.0 or VDD)
    std::vector<double> output_table;

    // Don't-care mask for input matching
    std::vector<uint64_t> dont_care_masks;
};
```

The lookup is O(1) per clock edge: read input voltages → threshold to bits → table lookup → write output voltages. No event queue, no iteration, no linked lists.

#### 5.2.3 FSM Device Integration

```cpp
class FSMDevice : public Device {
    CompiledFSM fsm;
    int current_state;

    // Clock edge detection
    double v_clk_prev;
    bool rising_edge(double v_clk_new) {
        return v_clk_prev < vih && v_clk_new >= vih;
    }

    void evaluate(const vector<double>& voltages,
                  NumericMatrix& mat, vector<double>& rhs) override {
        double v_clk = voltages[clk_node];

        if (rising_edge(v_clk)) {
            // Read inputs: threshold analog voltages to digital
            int input_code = 0;
            for (int i = 0; i < fsm.n_inputs; i++) {
                if (voltages[input_nodes[i]] >= vih)
                    input_code |= (1 << i);
            }

            // Table lookup: O(1)
            int next_state = fsm.transition_table[
                current_state * n_input_combos + input_code];
            current_state = next_state;
        }

        // Stamp outputs as voltage sources with Rout
        for (int i = 0; i < fsm.n_outputs; i++) {
            double v_target = fsm.output_table[
                current_state * fsm.n_outputs + i];
            // Stamp as: I = (v_target - V_node) / Rout
            double g = 1.0 / rout;
            mat.add(out_diag[i], g);
            rhs[out_nodes[i]] += g * v_target;
        }

        v_clk_prev = v_clk;
    }
};
```

#### 5.2.4 FSM in a Digital Partition

When the FSM device is placed in a `DIGITAL` partition, the multi-rate scheduler only evaluates it on clock edges:

1. Scheduler detects clock edge (boundary input from analog partition crosses threshold)
2. Wakes the digital partition
3. FSM evaluates: one table lookup per FSM instance
4. Output boundary values updated
5. Partition goes back to sleep until next clock edge

Total cost per clock cycle: one table lookup + boundary value exchange. Compare to XSPICE: dequeue events + iterate to quiescence + load each code model + queue output events + resolve wired logic. Orders of magnitude faster for complex FSMs.

### 5.3 Real-Number Behavioral Blocks

For arbitrary analog behavioral functions in the digital domain (inspired by Spectre AMS's Real Number Modeling):

```cpp
class RealNumberBlock : public Device {
    // User-defined function: inputs → outputs
    // Evaluated as a regular B-source expression, but placed in a
    // DIGITAL partition so it's only evaluated on input changes
    //
    // Example: a behavioral DAC
    //   V(out) = VREF * (V(b0) + 2*V(b1) + 4*V(b2)) / 8
    //
    // This is just an ASRCDevice in a digital partition.
    // The multi-rate scheduler handles the "only evaluate on change" part.
};
```

No new device type needed — an existing B-source in a digital partition automatically becomes a real-number behavioral block through the multi-rate scheduler.

---

## 6. Layer 3: Boundary Management

### 6.1 Boundary Types

| Boundary | Direction | Method |
|----------|-----------|--------|
| Analog → Analog | Bidirectional | PWL interpolation of shared node voltage |
| Analog → Digital | Unidirectional | Threshold detection on boundary voltage |
| Digital → Analog | Unidirectional | PWL voltage ramp with slew rate limiting |
| Digital → Digital | Unidirectional | Immediate value propagation at sync points |

### 6.2 Analog → Digital Threshold Detection

When an analog partition's boundary node crosses a digital partition's input threshold:

```
procedure check_analog_to_digital_crossing(analog_partition, t_old, t_new):
    for each boundary node N shared with a digital partition:
        v_old = analog_partition.solution_prev[N]
        v_new = analog_partition.solution[N]

        if crosses_threshold(v_old, v_new, VIH) or
           crosses_threshold(v_old, v_new, VIL):
            // Interpolate exact crossing time
            t_cross = t_old + (t_new - t_old) *
                      (VTH - v_old) / (v_new - v_old)
            
            // Schedule digital partition wake-up at t_cross
            schedule(digital_partition, t_cross, WAKE_FROM_IDLE)
```

This replaces XSPICE's `adc_bridge` model. No separate device — the scheduler handles it.

### 6.3 Digital → Analog Injection

When a digital partition produces a new output value:

```
procedure inject_digital_to_analog(digital_partition, analog_partition):
    for each output node N of digital_partition used by analog_partition:
        v_old = previous_output[N]
        v_new = current_output[N]

        if v_new != v_old:
            // Create PWL ramp: v_old → v_new over rise/fall time
            t_rise_or_fall = (v_new > v_old) ? tpd_rise : tpd_fall
            
            analog_partition.boundary_cache[N] = {
                t0 = t_now,        v0 = v_old,
                t1 = t_now + t_rf, v1 = v_new,
                slope = (v_new - v_old) / t_rf
            }

            // Insert breakpoint in analog partition at ramp completion
            analog_partition.add_breakpoint(t_now + t_rf)
```

This replaces XSPICE's `dac_bridge`. The PWL ramp naturally models the slew rate, and the analog solver's timestep control adapts to track it.

### 6.4 Boundary Error Monitoring

At each sync point, compute the retrospective boundary error:

```
for each boundary value BV:
    // What the PWL predicted at this time
    v_predicted = BV.v0 + BV.slope * (t_sync - BV.t0)
    // What the owning partition actually computed
    v_actual = owner.solution[BV.node]
    
    error = abs(v_actual - v_predicted)
    relative_error = error / (abs(v_actual) + vntol)

    if relative_error > boundary_reltol:
        // Tighten sync interval
        dt_sync = dt_sync / 2
        // Re-evaluate affected partitions from last sync
        rollback_to_last_sync(affected_partitions)
```

---

## 7. Implementation Plan

### 7.1 Phase 1: Partitioning Infrastructure (no multi-rate yet)

**Goal**: Build the circuit graph, partitioning algorithm, and per-partition solver infrastructure. Run all partitions at the same timestep (functionally identical to monolithic, but validates the partitioning).

**Files to create:**
- `src/core/partition.hpp/.cpp` — `Partition` struct, `CircuitPartitioner` class
- `src/core/partition_graph.hpp/.cpp` — weighted graph construction from circuit topology

**Files to modify:**
- `src/core/circuit.hpp/.cpp` — add `partition()` call in `finalize()`, store partition metadata
- `src/core/transient.hpp/.cpp` — add partition-aware path (initially just iterating partitions sequentially at the same dt)

**Validation**: All existing 1,109 tests pass with partitioning enabled. Partition boundaries produce identical results to monolithic solve.

### 7.2 Phase 2: Multi-Rate Scheduler

**Goal**: Per-partition timestep control and the priority-queue scheduler.

**Files to create:**
- `src/core/multirate_scheduler.hpp/.cpp` — `SchedulerEvent`, priority queue, main loop
- `src/core/boundary.hpp/.cpp` — `BoundaryValue` management, PWL interpolation, error monitoring

**Files to modify:**
- `src/core/transient.cpp` — replace monolithic loop with scheduler dispatch
- `src/core/timestep.hpp/.cpp` — per-partition `TimeStepController` instances

**Validation**: Benchmark on circuits with widely disparate time constants (e.g., slow bias + fast oscillator). Verify speedup proportional to time-constant ratio. Verify accuracy within tolerances.

### 7.3 Phase 3: PWL Digital Device Models

**Goal**: Implement the PWL gate library as standard neospice devices.

**Files to create:**
- `src/devices/digital/pwl_inverter.hpp/.cpp`
- `src/devices/digital/pwl_gate.hpp/.cpp` — parameterized NAND/NOR/XOR
- `src/devices/digital/pwl_flipflop.hpp/.cpp` — D, JK, SR, T
- `src/devices/digital/pwl_latch.hpp/.cpp`
- `src/devices/digital/digital_factory.cpp` — DeviceRegistry integration

**Parser additions:**
- New element prefix `A` for digital devices (matches ngspice convention)
- `.model` support for digital device parameters

**Validation**: Compare PWL gate behavior against ngspice XSPICE d_* models. Verify timing, thresholds, and loading effects.

### 7.4 Phase 4: Compiled FSM Evaluator

**Goal**: FSM description parser, compiler, and device integration.

**Files to create:**
- `src/devices/digital/fsm_parser.hpp/.cpp` — `.fsm` / `.state` / `.ends_fsm` parsing
- `src/devices/digital/fsm_compiler.hpp/.cpp` — state table → lookup table compilation
- `src/devices/digital/fsm_device.hpp/.cpp` — `FSMDevice` implementation

**Parser additions:**
- `.fsm` / `.state` / `.ends_fsm` directives in netlist parser

**Validation**: Implement standard FSM test cases (traffic light controller, SPI master, UART transmitter). Compare I/O behavior against ngspice `d_state` model.

### 7.5 Phase 5: Idle Detection and Adaptive Sync

**Goal**: Full latency optimization — sleeping idle partitions, adaptive sync interval.

**Files to modify:**
- `src/core/multirate_scheduler.cpp` — idle detection, wake-up logic, adaptive sync interval
- `src/core/boundary.cpp` — retrospective error estimation, sync interval adaptation

**Validation**: Benchmark on large mixed-signal circuits. Verify that idle partitions produce zero computational cost. Verify that adaptive sync interval converges to appropriate values.

### 7.6 Phase 6: Python API and Documentation

**Goal**: Expose mixed-signal features to Python users.

**Files to modify:**
- `python/bindings.cpp` — bind digital device types, partition info, FSM definition
- `python/neospice/__init__.py` — convenience functions for mixed-signal simulation

**New Python API:**

```python
import neospice as ns

ckt = ns.Circuit()

# PWL digital gates
ckt.A("inv1", "in", "out", model="d_inv", vdd=3.3)
ckt.A("nand1", ["a", "b"], "out", model="d_nand", vdd=3.3)
ckt.A("dff1", d="data", clk="clock", q="out", model="d_dff", vdd=3.3)

# Compiled FSM
ckt.fsm("ctrl", clk="CLK", rst="RST",
        inputs=["SDA", "SCL"],
        outputs=["ACK", "VALID"],
        states={
            "IDLE":  {"out": [0, 0], "SDA=1 SCL=1": "START"},
            "START": {"out": [1, 0], "*": "IDLE"},
        },
        vdd=3.3, tpd=1e-9)

# Partition hints (optional — auto-partition by default)
ckt.set_partition_hint("inv1", "digital")
ckt.set_partition_hint("R1", "analog_slow")

result = ns.transient(ckt, tstep=1e-9, tstop=1e-3)
```

---

## 8. Performance Analysis

### 8.1 Expected Speedup from Multi-Rate

Consider a typical mixed-signal circuit: a 10MHz digital controller driving a 100kHz switching power supply with a slow (1kHz bandwidth) output filter.

| Partition | Activity | Timestep | Steps for 1ms sim |
|-----------|----------|----------|-------------------|
| Digital controller | 10MHz clock edges | 100ns (event-triggered) | 10,000 |
| Power stage | 100kHz switching | 1ns adaptive | 1,000,000 |
| Output filter | 1kHz bandwidth | 100ns | 10,000 |

**Monolithic** (current): 1,000,000 steps × full matrix size = baseline

**Multi-rate**: 
- Digital: 10,000 evals × O(1) table lookup ≈ negligible
- Power stage: 1,000,000 steps × small sub-matrix (just the switching stage)
- Output filter: 10,000 steps × small sub-matrix

**Estimated speedup**: 5-20× depending on circuit proportions, because:
1. Sub-matrices are smaller → LU factorization is O(n³) cheaper
2. Slow partitions take far fewer steps
3. Digital partitions are O(1) per clock edge instead of O(n) Newton iterations
4. Idle partitions have zero cost

### 8.2 Comparison with XSPICE

| Metric | XSPICE (ngspice) | neospice Multi-Rate |
|--------|-------------------|---------------------|
| Digital evaluation | Per-gate event propagation | Compiled table lookup |
| A/D boundary | Bridge model + breakpoint | Threshold detection in scheduler |
| D/A boundary | Bridge model + ramp source | PWL injection + breakpoint |
| Rollback | Time-stamped linked list splice | Per-partition solution restore |
| Memory overhead | Event queues + state lists per node | Boundary cache per shared node |
| Analog accuracy in digital domain | None (3-state model) | Full (PWL models in KCL matrix) |
| Large FSM (1000 states) | 1000 event propagations/cycle | 1 table lookup/cycle |

### 8.3 Memory Overhead

Per partition:
- Sub-matrix values: `O(nnz_partition)` doubles
- Solver workspace: `O(n_partition²)` in worst case, typically `O(n_partition * fill_in)`
- Boundary cache: `O(n_boundary)` × 5 doubles = negligible

Total overhead: roughly 2× the monolithic matrix storage (original matrix + partition sub-matrices). The original monolithic matrix can be freed after partitioning, making the overhead closer to 1×.

---

## 9. Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Partitioning produces poor cuts | No speedup; overhead of partition management | Fallback to monolithic; tunable partition parameters; user override via `.partition` |
| Boundary PWL approximation error too large | Inaccurate results at partition boundaries | Adaptive sync interval; retrospective error monitoring; automatic fallback to tighter sync |
| Newton convergence affected by partition boundary injection | More rejected timesteps, slower simulation | Boundary values injected as smooth PWL ramps (not discontinuous jumps); Norton equivalent at boundaries |
| FSM compilation blowup for large input spaces | Memory explosion for `2^N` input combinations | Don't-care compression; sparse transition table; fall back to interpreted evaluation for >20 inputs |
| PWL digital models don't converge in feedback loops | Ring oscillator diverges or oscillates | PWL gain tuned to < 1 at DC; output resistance provides natural damping; existing ringing detection handles edge cases |

---

## 10. Integration with Existing neospice Architecture

### 10.1 What Changes

| Component | Change |
|-----------|--------|
| `Circuit::finalize()` | New partitioning phase after existing sparsity/offset assignment |
| `transient.cpp` | New multi-rate path alongside existing monolithic path (selected by option) |
| `newton.cpp` | No change — reused per-partition |
| `NeoSolver` | No change — one instance per partition |
| `TimeStepController` | No change — one instance per partition |
| `Device` interface | No change — existing interface sufficient for PWL and FSM devices |
| `DeviceRegistry` | Add registrations for new digital device types |
| Netlist parser | Add `.fsm` / `.state` / `.ends_fsm` / `A` element prefix |

### 10.2 What Doesn't Change

- All existing device models (R, C, L, M, D, Q, B, ...) — unchanged
- DC operating point — still monolithic (partitioning only benefits transient)
- AC, noise, sensitivity, PZ — unchanged (small-signal analyses are inherently monolithic)
- Newton-Raphson algorithm — unchanged
- NeoSolver sparse LU — unchanged
- Test framework — unchanged (add new tests, existing tests still pass)
- Python API — backward compatible (new features are additive)

### 10.3 Activation

Multi-rate partitioning is **opt-in** via simulation option:

```spice
.options multirate=1          ; enable multi-rate partitioning
.options partition_method=auto ; auto, manual, or off
.options sync_ratio=10        ; sync interval = sync_ratio × fastest partition dt
.options idle_threshold=1e-5  ; voltage change threshold for idle detection
.options wake_threshold=1e-4  ; voltage change threshold for wake-up
.options boundary_reltol=1e-3 ; boundary PWL approximation tolerance
```

Default is `multirate=0` (monolithic, matching current behavior).

---

## 11. Future Extensions

### 11.1 Verilog-A Behavioral Models

The compiled FSM evaluator framework can be extended to support Verilog-A:
- Parse Verilog-A `analog begin ... end` blocks
- Compile to native evaluation functions (like FSM tables but with continuous math)
- Place in appropriate partition based on activity analysis

### 11.2 SystemVerilog Real Number Modeling

The real-number behavioral block concept (Section 5.3) is a subset of SystemVerilog RNM. Full RNM support would add:
- `real` variable types propagated through digital partitions
- Module hierarchy with `real` ports
- Continuous assignment of `real` expressions

### 11.3 GPU-Accelerated Multi-Rate

Multi-rate partitioning is naturally parallel — independent partitions can be evaluated on different GPU streams:
- `ANALOG_FAST` partitions on GPU (largest matrices, most compute)
- `ANALOG_SLOW` and `DIGITAL` partitions on CPU (small, infrequent)
- Boundary exchange via pinned memory

### 11.4 Automatic Abstraction Level Selection

Given both PWL and compiled FSM representations of the same block, automatically select the appropriate level:
- Close to a mixed-signal boundary → PWL (full analog accuracy)
- Deep inside digital logic → compiled FSM (maximum speed)
- Adaptively switch during simulation based on observed signal characteristics
