# Remaining Accuracy Fixes — Design Spec

## Goal

Close the remaining accuracy gaps between neospice and ngspice for custom device models. After the first round of 16 device accuracy fixes, three categories of work remain: timestep precision at switching edges, missing ASRC built-in variables/functions, and parameter completeness for passives and transmission lines.

BSIM4 is explicitly out of scope — it has its own rebuild track.

## Current State

All 24 ngspice comparison tests pass. Three transient tests still require loose tolerances due to edge-timing mismatch at PULSE/SIN transitions:

| Test | Tolerance | Measured Error | Root Cause |
|------|-----------|----------------|------------|
| DiodeRectifier | 15% rel | ~10.7% | SIN source breakpoints + LTE near diode turn-on |
| CMOSInverter | 25% rel | ~23.6% | PULSE edges (100ps rise/fall), no TL breakpoints |
| CMOSInverterWithR | 50% rel | ~44.5% | Same + RDSMOD/RGATEMOD parasitics |

Additionally, several ASRC features and device parameters are unimplemented, which would cause failures on netlists that use them.

## Phase 1: Timestep Precision at Switching Edges

### 1a. Transmission Line Breakpoints at t = k*TD

**Problem:** ngspice inserts breakpoints at multiples of TD for transmission lines (see `TRAsetup` in ngspice source). neospice doesn't — the transient solver's `collect_breakpoints()` only queries VSource and ISource devices.

**Design:**
- Add `get_breakpoints(double tstart, double tstop)` method to `TransmissionLine`
- Returns `{TD, 2*TD, 3*TD, ...}` up to tstop
- Wire into `collect_breakpoints()` in transient.cpp alongside the existing VSource/ISource collection
- Use `add_source_breakpoint()` (not just `add_breakpoint()`) so the post-crossing dt reduction at line 485-489 triggers

### 1b. Switch Timestep Control (compute_trunc)

**Problem:** The switch device has `device_converged()` (returns `!state_changed_`) but no `compute_trunc()`. After a switch state change, the timestep controller doesn't know to reduce dt for the next step. ngspice's switch device (swload.c) relies on the general timestep controller's breakpoint mechanism plus MODEINITTRAN state rotation to handle this.

**Design:**
- Add `compute_trunc()` override to VSwitch and CSwitch
- When `state_changed_` is true in the previous step, return a small dt (e.g., `0.1 * current_dt`) to force the solver to resolve the transition accurately
- When state is stable, return infinity (don't constrain the timestep)
- Wire VSwitch/CSwitch into the device LTE loop in transient.cpp (lines 446-461)

### 1c. Post-Breakpoint Timestep Reduction Tuning

**Problem:** The post-breakpoint dt reduction at transient.cpp:485-489 currently uses `0.1 * min(saved_delta, bp_gap)`. ngspice uses a similar factor but also resets integration order to 1. We keep order 2 (per comment in the code) because our charge integration diverges at order 1 near edges. The current approach works but may need tuning after TL breakpoints are added.

**Design:** No code change in this phase — this is monitoring. After 1a and 1b, re-measure the three loose-tolerance tests. If errors improve enough, tighten tolerances. If not, revisit the 0.1 factor or add adaptive order reduction.

## Phase 2: Missing ASRC Built-in Variables and Functions

### 2a. TEMPER Variable

**Problem:** ngspice ASRC expressions can reference `TEMPER` to get the simulation temperature in Celsius. neospice's expression parser throws "unknown identifier" on `TEMPER`.

**Design:**
- In `ExpressionParser::parse_primary()`, handle `lname == "temper"` the same way `"time"` is handled
- Use sentinel name `"__temper__"` in VarRef (like `"__time__"` for TIME)
- In `ASRCDevice` constructor, detect `__temper__` index (like `time_var_idx_`)
- In `fill_var_values()`, set `var_values_[temper_var_idx_] = sim_temp_celsius`
- Temperature comes from `IntegratorCtx::options->temp - 273.15` (convert K to C)

### 2b. HERTZ Variable

**Problem:** ngspice ASRC expressions can reference `HERTZ` to get the current AC analysis frequency. neospice throws "unknown identifier".

**Design:**
- Handle `lname == "hertz"` in parser, sentinel `"__hertz__"`
- Detect `hertz_var_idx_` in ASRCDevice constructor
- In `fill_var_values()` for AC: set from `IntegratorCtx::ac_freq` (need to add this field)
- In transient: set to 0.0 (HERTZ is only meaningful in AC analysis)
- Add `double ac_freq = 0.0` field to IntegratorCtx, set it in the AC solver loop

### 2c. PWL Function

**Problem:** ngspice ASRC supports `PWL(x, x1,y1, x2,y2, ...)` — piecewise-linear interpolation. neospice doesn't have this function in the expression parser.

**Design:**
- Add `NodeType::PWL` to the AST
- PWL node stores: one child (the x-argument) plus a vector of (x,y) breakpoints as constant data
- Parser: when `lname == "pwl"`, parse first arg as expression, then pairs of numeric literals
- Evaluator: linear interpolation between breakpoints, flat extrapolation beyond endpoints
- Derivative: piecewise constant (slope of current segment) w.r.t. the x-argument, chain-ruled with dx/d(var)

## Phase 3: Parameter Completeness

### 3a. Transmission Line IC= (Initial Conditions)

**Problem:** ngspice T-element supports `IC=V1,I1,V2,I2` to set initial port voltages and currents. neospice ignores IC= on the T element.

**Design:**
- Add `set_ic(double v1, double i1, double v2, double i2)` to TransmissionLine
- Parse `IC=v1,i1,v2,i2` in the T-element parser (netlist_parser.cpp:1669-1712)
- In `init_dc_state()`, if IC is given, use those values instead of the DC solution
- Seed history buffer with the IC values at t = -2*TD, -TD, 0

### 3b. Resistor AC Resistance (RAC=)

**Problem:** ngspice resistor model supports `RAC=` parameter (AC resistance, separate from DC). neospice's `ac_stamp()` uses the DC resistance.

**Design:**
- Add `double rac_ = -1.0` to Resistor (negative = not set, use DC resistance)
- Add `void set_rac(double r)` setter
- In `ac_stamp()`, use `rac_` if positive, otherwise use `resistance_eff_`
- Parse `RAC=` in the R-element parser and in R model card parameters

### 3c. R/C Model Cards (.model RMOD R / .model CMOD C)

**Problem:** ngspice supports `.model` cards for resistors and capacitors, allowing shared TC1/TC2/RAC parameters. neospice has no R or C model card dispatcher — the parser silently ignores `.model RMOD R(...)`.

**Design:**
- Add simple `ResistorModel` struct: `{tc1, tc2, rac, kf, af, tnom}` with defaults
- Add simple `CapacitorModel` struct: `{tc1, tc2, vc1, vc2, tnom}` with defaults
- Add `to_resistor_model()` and `to_capacitor_model()` dispatchers in model_cards
- In the parser, store R/C model cards in a map
- When parsing `R name n+ n- value modelname`, detect model reference and apply parameters
- Same for C elements
- Instance parameters override model parameters (ngspice precedence)

## Testing Strategy

Each phase produces measurable improvements:
- **Phase 1:** Re-measure DiodeRectifier, CMOSInverter, CMOSInverterWithR errors; tighten tolerances
- **Phase 2:** Add new test circuits using TEMPER, HERTZ, PWL in ASRC expressions; compare against ngspice
- **Phase 3:** Add test circuits with TL IC=, R RAC=, R/C model cards; compare against ngspice

## Out of Scope

- BSIM4 kernel rebuild (separate track)
- Lossy transmission line (LTRA) enhancements
- ASRC table() function (rarely used)
