# PWL Simulation Engine Design

**Date:** 2026-05-20
**Status:** Proposed — Phase 1 (transient engine) targeted first

## Overview

neospice gains a SIMPLIS-compatible piecewise-linear (PWL) simulation engine for switching power supply design. The PWL engine runs alongside the existing SPICE engine within a unified architecture designed to eventually support digital and mixed-signal simulation as well.

PWL simulation replaces Newton-Raphson iteration with event-driven linear solves. Every nonlinear device is approximated by straight-line segments — within each segment the circuit is linear and solvable in one shot. The simulator steps from one linear topology to the next when devices cross segment boundaries. This eliminates convergence failures and delivers 10-50x speedups over SPICE for switching converters.

### What ships

| Phase | Capability | Depends on |
|-------|-----------|------------|
| Phase 1 | PWL transient engine, 8 device primitives, SIMPLIS deck parser, topology caching | — |
| Phase 2 | POP (Periodic Operating Point) analysis — shooting method for steady-state | Phase 1 |
| Phase 3 | Time-domain AC analysis — Bode plots from full switching model | Phase 2 |

### Future phases (not in this spec)

- Digital event engine (Verilog gates, D flip-flops, logic primitives)
- Mixed-signal co-simulation coordinator (analog-digital boundary, time sync)
- SPICE-to-PWL model extraction (auto-convert SPICE MOSFET/diode to PWL segments)

## Architecture

### Simulation domain model

Every device declares which simulation paradigm it uses:

```cpp
enum class SimDomain {
    ANALOG_NR,    // Newton-Raphson nonlinear (existing SPICE devices)
    ANALOG_PWL,   // Piecewise-linear (SIMPLIS-style devices)
    DIGITAL,      // Discrete event (future)
};
```

The existing `Device` class gains one new virtual method:

```cpp
virtual SimDomain sim_domain() const { return SimDomain::ANALOG_NR; }
```

All 32 existing devices return `ANALOG_NR` by default — zero changes needed. New PWL devices return `ANALOG_PWL`. Future digital gates will return `DIGITAL`.

During `finalize()`, the circuit scans all devices and determines the simulation mode:

- **Pure ANALOG_NR** — existing SPICE engine (no changes)
- **Pure ANALOG_PWL** — PWL engine (new)
- **Pure DIGITAL** — digital event engine (future)
- **Mixed** — co-simulation coordinator (future)

For now only pure-mode circuits are supported. The user declares `.SIMULATOR SIMPLIS` in the netlist to activate PWL mode. The parser routes to the SIMPLIS deck parser and `finalize()` validates that all devices are `ANALOG_PWL`.

### Shared infrastructure

The PWL engine reuses core neospice infrastructure without modification:

| Component | Role in PWL engine |
|-----------|-------------------|
| `NeoSolver` | Sparse LU factorize/solve for linear MNA systems |
| `SparsityPattern` / `NumericMatrix` | Matrix assembly and storage |
| `TransientResult` / `ACResult` | Result containers |
| `.raw` file writer | Output format |
| Python bindings | API access |
| Expression evaluator | Preprocessor variable/expression evaluation |

### File layout

```
src/
├── core/           # shared by both engines (unchanged)
│   ├── matrix.hpp / .cpp
│   ├── neo_solver.hpp / .cpp
│   ├── transient.hpp / .cpp      # existing SPICE transient
│   └── ...
├── devices/        # existing SPICE devices (unchanged)
│   ├── device.hpp                # gains SimDomain + PWL virtual methods
│   └── ...
├── pwl/
│   ├── devices/
│   │   ├── pwl_resistor.hpp / .cpp
│   │   ├── pwl_capacitor.hpp / .cpp
│   │   ├── pwl_inductor.hpp / .cpp
│   │   ├── pwl_switch.hpp / .cpp
│   │   ├── pwl_diode.hpp / .cpp
│   │   ├── pwl_mosfet.hpp / .cpp
│   │   └── pwl_source.hpp / .cpp
│   ├── pwl_transient.hpp / .cpp      # event-driven simulation loop
│   ├── pwl_event.hpp / .cpp          # event detection + bisection
│   ├── pwl_topology_cache.hpp / .cpp # topology hash -> cached factorization
│   ├── pop.hpp / .cpp                # POP analysis (Phase 2)
│   └── pwl_ac.hpp / .cpp             # time-domain AC (Phase 3)
├── parser/
│   ├── netlist_parser.hpp / .cpp     # existing SPICE parser (unchanged)
│   └── simplis_parser.hpp / .cpp     # new SIMPLIS deck parser
```

### What changes in existing code

- `device.hpp` — adds `sim_domain()` + 4 PWL virtual methods (all with default no-op implementations, fully backward compatible)
- `circuit.cpp` — `finalize()` gains domain classification logic
- `CMakeLists.txt` — adds `src/pwl/` sources

All 32 existing SPICE devices, the SPICE transient/DC/AC/noise engines, the existing netlist parser, NeoSolver, and result types remain untouched.

## PWL Device Interface

PWL devices don't iterate — they switch between linear segments. The existing `Device` class gains a parallel set of virtual methods that only PWL devices override:

```cpp
struct PwlSegment {
    double breakpoint;   // threshold where segment changes
    double slope;        // conductance (R), capacitance (C), or inductance (L)
    double offset;       // y-intercept of the linear segment
};

virtual void pwl_stamp(NumericMatrix& mat, std::vector<double>& rhs) {}

virtual double pwl_check_event(const std::vector<double>& voltages,
                                double t, double dt) { return INFINITY; }

virtual bool pwl_update_topology(const std::vector<double>& voltages) {
    return false;
}

virtual int32_t pwl_segment_id() const { return 0; }
```

### Method responsibilities

**`pwl_stamp()`** — Stamps the device's linear MNA contribution for its current segment. Called once per topology change (not per timestep within the same topology).

**`pwl_check_event()`** — Tests whether any internal variable has crossed a segment breakpoint during the last timestep. Returns the estimated crossing time for bisection, or `INFINITY` if no event occurred.

**`pwl_update_topology()`** — Called after an event is confirmed. Updates the device's internal segment index. Returns `true` if the topology actually changed (triggering a matrix re-stamp and re-factorization).

**`pwl_segment_id()`** — Returns the device's current segment index. The engine concatenates all devices' segment IDs into a topology hash for matrix factorization caching.

### PWL device primitives

| Device | Segment space | Slope meaning | SIMPLIS equivalent |
|--------|--------------|---------------|-------------------|
| `PwlResistor` | I vs V | Conductance (1/R) | PWL resistor |
| `PwlCapacitor` | Q vs V | Capacitance (C) | PWL capacitor |
| `PwlInductor` | lambda vs I | Inductance (L) | PWL inductor |
| `PwlSwitch` | 2-state: Ron/Roff | Conductance | Ideal switch |
| `PwlDiode` | Multi-segment I-V | Conductance per region | PWL diode |
| `PwlMosfet` | Ron/Roff + PWL Cgs | Conductance + capacitance | PWL MOSFET |
| `PwlVSource` | DC, pulse, PWL waveform | — | Voltage source |
| `PwlISource` | DC, pulse, PWL waveform | — | Current source |

Each PWL device stores a `std::vector<PwlSegment>` table and a `current_segment_` index.

### Charge and flux continuity

PWL capacitors are defined in Q-V space (charge vs voltage) and PWL inductors in lambda-I space (flux linkage vs current). The slope of each segment gives a constant capacitance or inductance within that region. This formulation guarantees:

- **Charge continuity** at capacitor segment boundaries — no spurious charge injection when capacitance changes with voltage
- **Flux continuity** at inductor segment boundaries — no spurious energy gain/loss when inductance changes with current (saturation)

This is a deliberate choice matching SIMPLIS's design. Defining capacitors as C(V) directly can cause charge discontinuities; the Q-V formulation prevents this by construction.

### Segment table auto-extension

SIMPLIS automatically extends the outermost PWL segments to positive and negative infinity. neospice does the same: the leftmost and rightmost segments are extrapolated linearly, ensuring the model is defined for all possible voltages/currents with no undefined regions.

## PWL Transient Engine (Phase 1)

### Core simulation loop

```
pwl_transient(circuit, tstop):
    t = 0, dt = initial_dt
    topology_changed = true

    while t < tstop:
        if topology_changed:
            stamp MNA matrix for current segment combination
            add companion models for C and L (trapezoidal)
            LU factorize (or retrieve from topology cache)
            topology_changed = false
        else:
            update RHS for new time point
            refactorize if dt changed (companion values depend on dt)

        solve linear system (single solve, no iteration)

        // Event detection
        t_event = INFINITY
        for each device:
            t_cross = device.pwl_check_event(solution, t, dt)
            t_event = min(t_event, t_cross)

        if t_event <= t + dt:
            bisect to find exact crossing time
            advance to crossing, update topology
            topology_changed = true
        else:
            accept step, record solution, advance t += dt
            adapt dt for next step
```

### Key properties

**No Newton iteration.** Between topology changes the circuit is linear — each timestep requires exactly one linear solve. There are no convergence failures, no iteration limits, no damping heuristics.

**Reactive companion models.** PWL capacitors and inductors use standard trapezoidal companion models:

- Capacitor: `i = (2C/dt) * v - i_prev`, equivalent conductance `Geq = 2C/dt`
- Inductor: `v = (2L/dt) * i - v_prev`, equivalent resistance `Req = 2L/dt`

C and L are the slope of the current PWL segment (constant within that segment), so the companion is truly linear — no NR iteration needed to resolve it.

**Topology caching.** The engine maintains a hash map of `topology_id -> factored_matrix`. The topology ID is formed by concatenating all devices' `pwl_segment_id()` values. For a typical buck converter with ~5 distinct switching states, only 5 LU factorizations are performed for the entire simulation — every subsequent visit to a known topology is a cache hit (only forward/back substitution).

**Event bisection.** When a breakpoint crossing is detected, the engine bisects the time interval to locate the exact crossing time within tolerance (e.g., `1e-12 * dt`). Bisection interpolates the solution between pre-step and post-step values — efficient because interpolating a linear system is cheap.

### Adaptive time stepping

The initial timestep is `dt = tstop / 1000` (or the user-specified `.TRAN` step if smaller). Within a topology (no events), the timestep grows up to a configurable maximum (`tstop / 50`). On topology change the timestep is reset to `dt_min` (initial dt / 100) near the event and allowed to grow again. The growth factor is capped at 2x per accepted step. Source breakpoints (pulse edges, PWL waveform corners) are scheduled in advance and the timestep is shortened to land exactly on them, reusing the same breakpoint scheduling logic as the SPICE transient engine.

## POP Analysis (Phase 2)

POP finds the periodic steady-state of a switching converter without simulating thousands of startup cycles. It wraps the PWL transient engine in a shooting method that iteratively refines initial conditions until the state repeats within tolerance.

### Algorithm

**Phase A — Pre-POP transient.** Run a configurable number of switching cycles (e.g., 20) from user-specified initial conditions. This rough settling moves the circuit away from unrealistic starting states.

**Phase B — Core POP iteration (shooting method).**

```
x0 = state sampled at POP trigger after pre-POP transient

for pass = 1 to max_passes:
    simulate one switching period from x0
    x1 = state sampled at next POP trigger

    error = x1 - x0
    rel_error = max(|error[i]| / max(|x0[i]|, abstol))

    if rel_error < pop_tol:   // e.g., 1e-10
        converged, break

    x0 = update_initial_conditions(x0, x1, error, pass)
```

**Phase C — Record steady-state.** Reset time to zero, simulate display_cycles from the converged initial conditions, record waveforms.

### POP Trigger

A special device that defines the switching period boundary by monitoring a signal and detecting edge crossings:

```cpp
struct PopTrigger {
    std::string signal;      // node voltage to monitor (e.g., gate drive)
    double threshold;        // crossing threshold (e.g., 2.5V for 0-5V gate)
    EdgeType edge;           // RISING or FALLING
    double max_period;       // upper bound on expected switching period
};
```

The trigger detects the start of each switching cycle — the Poincare section where capacitor voltages and inductor currents are sampled for cycle-to-cycle comparison.

### State vector

POP samples all capacitor voltages and inductor currents at each trigger event. The state vector dimension equals `num_capacitors + num_inductors`. These are the slow variables that define the periodic orbit.

### Initial condition update strategies

The engine tries progressively more aggressive methods:

1. **Simple fixed-point iteration:** `x0_next = x1`. Use the end-of-cycle state as the next starting state. Works when the Poincare map is contractive (most well-designed converters).

2. **Accelerated fixed-point (Aitken delta-squared):** When simple iteration converges slowly (relative error not decreasing after 3 consecutive passes), apply Aitken acceleration using three successive iterates to extrapolate toward the fixed point.

3. **Quasi-Newton (Broyden):** For stubborn circuits, estimate the Jacobian of the periodicity residual from successive passes and apply a Newton-like update. Most robust but requires storing and updating the Jacobian approximation.

### Convergence reporting

Each POP pass reports per-variable convergence:

```
POP Pass 3: Vcap1=12.0001V (err=2.3e-5), IL1=1.500A (err=1.1e-6)
POP Pass 7: Vcap1=12.0000V (err=2.5e-13), IL1=1.500A (err=1.8e-14) -- converged
```

## Time-Domain AC Analysis (Phase 3)

SIMPLIS-style AC analysis works entirely in the time domain on the full switching model. Unlike SPICE's AC (linearize at DC operating point, solve frequency-domain matrix), this approach captures effects that averaged models miss: subharmonic oscillations, sampling effects, slope compensation interactions, and switching harmonic coupling.

### Algorithm

```
for each frequency in sweep(fstart, fstop, mode, npoints):
    restore POP steady-state as initial conditions

    inject small sinusoidal perturbation via the AC source
    (amplitude = ac_mag, small enough for linear approximation)

    simulate N switching periods with perturbation active
    N = max(min_cycles, ceil(f_switch / freq) + settle_cycles)

    extract gain and phase from time-domain response using
    single-bin DFT at the perturbation frequency

    H(freq) = output_phasor / input_phasor
    record (freq, |H| in dB, angle(H) in degrees)
```

### Source classification

Every source is classified before the AC sweep. This classification determines its behavior during AC analysis:

| Source type | During POP | During AC |
|------------|-----------|-----------|
| Periodic large-signal (PWM, gate drive, clock) | Active, switching | Active, switching — preserves real operating point |
| Aperiodic large-signal (load step, startup ramp) | Active | Frozen at DC value from POP |
| Small-signal AC (perturbation injection) | Inactive (zero) | Active — sinusoidal at sweep frequency |

Periodic sources continue switching during AC so the Bode plot reflects the actual periodically time-varying system. Aperiodic sources are frozen because AC analysis assumes linearization around a steady operating point. Small-signal AC sources are zero during POP (they don't affect the operating point) and activate during the AC sweep.

### Frequency response extraction

At each frequency point the engine computes a single-bin DFT over the measurement window (last 1-2 perturbation periods). This is a simple dot product — no FFT required:

```cpp
complex<double> dft_at_freq(span<double> waveform, span<double> time,
                             double freq, double t_start, double t_end) {
    complex<double> sum = 0;
    for (int i = 0; i < N; i++) {
        if (time[i] < t_start || time[i] > t_end) continue;
        double phase = 2 * PI * freq * time[i];
        sum += waveform[i] * complex(cos(phase), -sin(phase));
    }
    return sum * (2.0 / N);
}
```

### Performance

AC analysis requires one transient simulation per frequency point. For a 100-point Bode plot that is 100 transient runs. Each run starts from the cached POP state and the PWL engine is fast (no NR iteration), so this is typically much faster than the manual alternative: creating an averaged model by hand and running SPICE AC on it. The topology cache from Phase 1 benefits AC as well — the same switching topologies repeat across all frequency sweeps.

### Result format

Reuses the existing `ACResult` structure — vectors of frequency, magnitude, and phase. Compatible with the `.raw` file writer and Python bindings. Users access results identically to SPICE AC analysis.

## SIMPLIS Deck Parser

### Overview

A new parser reads SIMPLIS `.deck` files and produces a `Circuit` populated with PWL devices. It handles the SIMPLIS netlist syntax, preprocessor directives, and model library format.

### Pipeline

```
.deck file -> Preprocessor -> Netlist parser -> Circuit with PWL devices
```

The top-level `Simulator::load()` or `neospice::run()` detects `.SIMULATOR SIMPLIS` in the input and routes to the SIMPLIS parser instead of the SPICE parser. Both parsers produce the same `Circuit` object.

### Preprocessor directives

| Directive | Purpose | Example |
|-----------|---------|---------|
| `.VAR` | Local variable definition | `.VAR RLoad=2.5` |
| `.GLOBALVAR` | Hierarchical global variable | `.GLOBALVAR Fsw=500k` |
| `.IF` / `.ELSE` / `.ENDIF` | Conditional inclusion | `.IF {USE_DCM == 1}` |
| `.INCLUDE` | Include external file | `.INCLUDE "buck_model.lib"` |
| `.SIMULATOR SIMPLIS` | Marks file as SIMPLIS model | Required in model libraries |
| `{expr}` | Inline expression evaluation | `R1 1 2 {RLoad * 0.5}` |

The preprocessor resolves all variables, evaluates expressions, and expands conditionals before device parsing. Expression evaluation reuses neospice's existing `.param` expression evaluator — the syntax differs slightly (curly braces instead of single quotes) but the math engine is the same.

### Device line syntax

```spice
* PWL resistor: name n+ n- PWL_R (v1,i1) (v2,i2) ...
R1 in out PWL_R (-1,-0.01) (0,0) (0.6,0.001) (0.7,10)

* PWL capacitor: name n+ n- PWL_C (v1,q1) (v2,q2) ...
C1 drain source PWL_C (0,0) (10,100n) (20,150n)

* PWL inductor: name n+ n- PWL_L (i1,lam1) (i2,lam2) ...
L1 sw out PWL_L (0,0) (5,50u) (10,55u)

* Switch: name n+ n- gate_n+ gate_n- SWITCH RON=x ROFF=y VT=z
S1 drain source gate gnd SWITCH RON=10m ROFF=1MEG VT=2.5

* MOSFET (SIMPLIS model reference):
M1 drain gate source source NMOS_MODEL

* Sources:
V1 vcc gnd 12
VPWM gate gnd PULSE 0 5 0 10n 10n 1u 2u

* POP trigger:
.POP_TRIGGER V(gate) RISING 2.5 MAX_PERIOD=2.1u

* Analyses:
.TRAN 100u
.POP CYCLES_BEFORE=20 TOLERANCE=1e-10
.AC DEC 50 10 1MEG
```

### Model library format

```spice
.SIMULATOR SIMPLIS
.SUBCKT NMOS_SW drain gate source
.MODEL NMOS_SW_R PWL_R ...
.MODEL NMOS_SW_C PWL_C ...
R_on drain source PWL_R (0,0) (1,100)
C_gs gate source 500p
.ENDS
```

### Subcircuit parameter passing

Uses SIMPLIS convention with the `vars:` keyword:

```spice
X1 in out gnd BUCK_CONVERTER vars: FSW=500k VOUT=3.3
```

The `vars:` keyword introduces parameter assignments, parsed into the same parameter map that the existing subcircuit expansion engine consumes.

## Testing Strategy

### Phase 1 validation

- **Unit tests:** Each PWL device type tested in isolation — verify segment switching, MNA stamps, event detection
- **Integration tests:** Complete switching converter circuits (buck, boost, forward) — compare transient waveforms against SIMPLIS reference data
- **Topology caching tests:** Verify cache hits, factorization reuse, hash correctness
- **Event bisection accuracy:** Verify crossing times within tolerance on circuits with known exact solutions (e.g., LC oscillator has analytic solution)
- **Parser tests:** Round-trip SIMPLIS .deck files through parser, verify correct device instantiation and parameter values

### Phase 2 validation

- **POP convergence:** Verify convergence to steady-state on buck/boost converters, check that ripple waveforms match SIMPLIS reference
- **POP trigger accuracy:** Test rising/falling edge detection on various gate drive signals
- **Convergence escalation:** Verify that Aitken/Broyden methods activate when simple iteration stalls

### Phase 3 validation

- **Bode plot accuracy:** Voltage-mode buck control loop — compare gain/phase against SIMPLIS reference across frequency
- **Source classification:** Verify periodic sources keep switching, aperiodic sources freeze, AC sources inject correctly
- **DFT extraction accuracy:** Test on circuits with known analytic transfer functions

## Comparison with SIMPLIS

| Capability | SIMPLIS | neospice PWL |
|-----------|---------|-------------|
| PWL R/C/L primitives | Yes | Phase 1 |
| PWL diode, MOSFET | Yes | Phase 1 |
| Event-driven transient | Yes | Phase 1 |
| Topology caching | Unknown (proprietary) | Phase 1 |
| POP analysis | Yes | Phase 2 |
| Time-domain AC | Yes | Phase 3 |
| SPICE-to-PWL auto-extraction | Yes (from SIMetrix SPICE models) | Future |
| Multi-level lossy inductor | Yes | Future (extend PwlInductor) |
| DVM test plans | Yes | Not planned (use neospice .measure instead) |
| Schematic GUI | SIMetrix | Not planned (neospice is an engine/library) |

## References

- SIMPLIS Reference Manual, SIMetrix Technologies / Keysight
- T. Wilson, "Piecewise-Linear Simulation of Switched Networks," AT&T Bell Labs (late 1980s)
- M. Rewienski, J. White, "A Trajectory Piecewise-Linear Approach to Model Order Reduction," MIT (2003)
- R. Kao, "Piecewise Linear Models for Switch-Level Simulation," Stanford (1992)
