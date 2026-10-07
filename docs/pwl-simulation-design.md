# PWL Simulation Engine Design

**Date:** 2026-05-20
**Status:** Proposed. No PWL engine or SIMPLIS parser is implemented.

Interfaces, paths, syntax, and defaults below are design inputs.
The [unified architecture](unified-simulation-architecture.md) describes later
cross-mode coordination. Reconcile its device interfaces and syntax with this
pure-mode proposal before implementation.

## Overview

The proposed piecewise-linear (PWL) engine targets switching power circuits.
It would share circuit data, results, and C++/Python access with the SPICE engine.
SIMPLIS-style syntax is a parser target, not a compatibility claim.

PWL models approximate nonlinear devices with linear segments. Within one segment
combination, transient steps solve a linear system. Crossing a segment boundary
changes the active topology. Event detection, timestep error, and model error
require separate validation.

### What ships

These phases describe proposed scope, not delivered features.

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

Existing devices would default to `ANALOG_NR`. Proposed PWL devices return
`ANALOG_PWL`. Future digital devices return `DIGITAL`.

During `finalize()`, the circuit scans all devices and determines the simulation mode:

- **Pure ANALOG_NR** — existing SPICE engine (no changes)
- **Pure ANALOG_PWL** — PWL engine (new)
- **Pure DIGITAL** — digital event engine (future)
- **Mixed** — co-simulation coordinator (future)

Phase 1 would accept only pure-mode circuits. The proposed `.SIMULATOR SIMPLIS`
directive selects the new parser. Finalization checks that all devices use
`ANALOG_PWL`.

### Shared infrastructure

The proposal would reuse these components. Check integration changes before
implementation:

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

Preserve existing SPICE behavior and API contracts during integration.

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

**`pwl_stamp()`** stamps the active segment. Restamp when segment or companion
coefficients change.

**`pwl_check_event()`** checks for a segment crossing in the trial step. It returns
an estimated crossing time or `INFINITY`.

**`pwl_update_topology()`** updates the segment index after an accepted event.
A `true` result requests matrix preparation for the new topology.

**`pwl_segment_id()`** returns the active segment index. The combined indices
identify a topology for cache lookup.

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

Capacitor tables use charge versus voltage. Inductor tables use flux linkage
versus current. Segment slopes define capacitance or inductance. Continuous
offsets are required to preserve:

- **Charge continuity** at capacitor segment boundaries — no spurious charge injection when capacitance changes with voltage
- **Flux continuity** at inductor segment boundaries when inductance changes with current (saturation)

The Q-V and flux-current tables make continuity an explicit model constraint.
Check stored energy and history updates separately.

### Segment table auto-extension

The proposal extrapolates the outermost segments to positive and negative
infinity. Check whether that extrapolation remains physically useful outside
the declared model range.

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

**Linear solve within a topology.** Newton iteration is unnecessary for a fixed
segment combination. Singular matrices, inconsistent switching states, event
cascades, and timestep rejection still need explicit failure handling.

**Reactive companion models.** Capacitors and inductors use the active segment's
constant slope. Their trapezoidal companions depend on the timestep and accepted
charge or flux history. Preserve segment offsets and continuity when changing state.

**Topology caching.** Segment IDs identify the active device configuration.
Factor reuse also requires the same matrix values, including timestep-dependent
companions. A topology hash alone cannot validate numeric factors.

**Event bisection.** Bracket crossings and refine them to a declared time tolerance.
Interpolation estimates a crossing, but transient state must remain consistent
at the accepted event time. Test multiple and near-simultaneous crossings.

### Adaptive time stepping

Candidate timestep defaults are:

- Start at `min(tstop / 1000, user_step)` when a user step exists.
- Allow growth up to `tstop / 50` and at most twice the last accepted step.
- After a topology change, reduce toward `initial_dt / 100`.
- Land on scheduled source breakpoints.

These rules still need LTE control, minimum-step failure handling, and validation.
Reuse the SPICE breakpoint machinery where its semantics match.

## POP Analysis (Phase 2)

POP uses a shooting method to find a periodic state. It repeats transient
simulation while updating initial conditions until the state repeats within tolerance.

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

The proposed state vector samples capacitor voltages and inductor currents.
Include additional device memory when needed to define the periodic orbit.

### Initial condition update strategies

The engine tries progressively more aggressive methods:

1. **Simple fixed-point iteration:** `x0_next = x1`. Use the end-of-cycle state as the next starting state. Works when the Poincare map is contractive (most well-designed converters).

2. **Accelerated fixed-point (Aitken delta-squared):** If relative error does not
   decrease for three consecutive passes, apply Aitken acceleration using three iterates.

3. **Quasi-Newton (Broyden):** Estimate the periodicity-residual Jacobian from
   successive passes. Apply an update and retain the approximation for later passes.

### Convergence reporting

Each POP pass reports per-variable convergence:

```
POP Pass 3: Vcap1=12.0001V (err=2.3e-5), IL1=1.500A (err=1.1e-6)
POP Pass 7: Vcap1=12.0000V (err=2.5e-13), IL1=1.500A (err=1.8e-14) -- converged
```

## Time-Domain AC Analysis (Phase 3)

This proposed analysis perturbs the periodic switching solution in the time
domain. SPICE AC instead linearizes at a DC operating point. Define which
frequency-response components the result reports and how switching harmonics affect them.

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

Periodic sources preserve the switching orbit. Aperiodic sources retain their
settled values. Perturbation sources activate only during the frequency sweep.

### Frequency response extraction

Estimate the phasor over the final one or two perturbation periods. The sketch
below assumes uniform samples. Adaptive steps require uniform resampling or
time-weighted integration. Check window length, settling, and spectral leakage:

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

### Performance evaluation

Each AC frequency needs a transient run from the periodic operating point.
Measure the cost of those runs, topology-cache behavior, and response accuracy
across the full frequency sweep before making a performance claim.

### Result format

Reuse `ACResult`, the raw-file writer, and Python access where their semantics
fit. Label the operating point as periodic so users can distinguish this analysis
from SPICE AC.

## SIMPLIS Deck Parser

### Overview

A new parser reads SIMPLIS `.deck` files and produces a `Circuit` populated with PWL devices. It handles the SIMPLIS netlist syntax, preprocessor directives, and model library format.

### Pipeline

```
.deck file -> Preprocessor -> Netlist parser -> Circuit with PWL devices
```

The proposed load path detects `.SIMULATOR SIMPLIS` and selects the new parser.
Both parsers would produce a `Circuit`.

### Preprocessor directives

| Directive | Purpose | Example |
|-----------|---------|---------|
| `.VAR` | Local variable definition | `.VAR RLoad=2.5` |
| `.GLOBALVAR` | Hierarchical global variable | `.GLOBALVAR Fsw=500k` |
| `.IF` / `.ELSE` / `.ENDIF` | Conditional inclusion | `.IF {USE_DCM == 1}` |
| `.INCLUDE` | Include external file | `.INCLUDE "buck_model.lib"` |
| `.SIMULATOR SIMPLIS` | Marks file as SIMPLIS model | Required in model libraries |
| `{expr}` | Inline expression evaluation | `R1 1 2 {RLoad * 0.5}` |

Resolve variables and conditionals before device parsing. Reuse the expression
evaluator where precedence, scope, and function semantics match the source dialect.

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
- **Integration tests:** Complete switching converter circuits (buck, boost, forward) — compare transient waveforms against qualified analytical solutions and ngspice 47 where comparable
- **Topology caching tests:** Verify cache hits, factorization reuse, hash correctness
- **Event bisection accuracy:** Verify crossing times within tolerance on circuits with known exact solutions (e.g., LC oscillator has analytic solution)
- **Parser tests:** Round-trip SIMPLIS .deck files through parser, verify correct device instantiation and parameter values

### Phase 2 validation

- **POP convergence:** Verify convergence to steady-state on buck/boost converters, check ripple against qualified periodic solutions
- **POP trigger accuracy:** Test rising/falling edge detection on various gate drive signals
- **Convergence escalation:** Verify that Aitken/Broyden methods activate when simple iteration stalls

### Phase 3 validation

- **Bode plot accuracy:** Voltage-mode buck control loop — compare gain/phase against qualified periodic-response data across frequency
- **Source classification:** Verify periodic sources keep switching, aperiodic sources freeze, AC sources inject correctly
- **DFT extraction accuracy:** Test on circuits with known analytic transfer functions

## Comparison with SIMPLIS

This table records intended scope. It does not certify SIMPLIS compatibility.
ngspice 47 is the sole behavioral reference for comparable SPICE analyses.

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
| Schematic GUI | SIMetrix | Circuit Lab exists. PWL UI support is not implemented. |

## References

- SIMPLIS Reference Manual, SIMetrix Technologies / Keysight
- T. Wilson, "Piecewise-Linear Simulation of Switched Networks," AT&T Bell Labs (late 1980s)
- M. Rewienski, J. White, "A Trajectory Piecewise-Linear Approach to Model Order Reduction," MIT (2003)
- R. Kao, "Piecewise Linear Models for Switch-Level Simulation," Stanford (1992)
