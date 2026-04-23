## neospice Capabilities

neospice is a full-featured SPICE circuit simulator written in C++ with a clean programmatic API. Here's what it provides:

### Analysis Types (8)
- **DC Operating Point** (.op) and **DC Sweep** (.dc) — nested two-parameter sweeps
- **Transient** (.tran) — adaptive timestepping with Trap/Gear-2/BE integration
- **AC Small-Signal** (.ac) — DEC/OCT/LIN frequency sweeps with G/C matrix caching; NQS-compatible via per-frequency device hooks
- **Noise** (.noise) — adjoint-method output/input-referred spectral density with correlated source support
- **Transfer Function** (.tf) — gain + input/output impedance
- **Sensitivity** (.sens) — DC sensitivity to all circuit parameters
- **Pole-Zero** (.pz) — transfer function poles and zeros
- **Fourier** (.four) — harmonic decomposition + THD
- **Parameter Sweep** (.step) — nested sweeps of any parameter or temperature

### Device Models (28 types)
| Category          | Devices                                                                                                                             |
| ----------------- | ----------------------------------------------------------------------------------------------------------------------------------- |
| Passives          | R (with TC, RAC, noise, flicker), C, L, K (mutual)                                                                                  |
| Sources           | V, I (DC/PULSE/SIN/PWL/EXP/SFFM/AM)                                                                                                 |
| Dependent         | E (VCVS), G (VCCS), F (CCCS), H (CCVS) — linear + POLY(N) + TABLE                                                                   |
| Behavioral        | B (ASRC) — expression-based with auto-differentiation for exact Jacobians, DDT, IDT, PWL, TABLE                                     |
| Switches          | S (voltage), W (current) — hysteresis                                                                                               |
| Transmission Line | T (lossless Branin model)                                                                                                           |
| Diode/BJT         | Diode, BJT (Gummel-Poon), **VBIC** (level 4/9/12/13)                                                                               |
| JFET/HFET         | JFET, **JFET2** (Parker-Skellern), **HFET1** (Curtice cubic), **HFET2** (Chalmers)                                                 |
| MOSFET            | MOS1 (level 1), **MOS3** (level 3), **MOS9** (level 9), **BSIM3v32** (level 49/v3.24), BSIM3 (level 49/v3.3), **BSIM4v7** (level 14, full UCB port: AC NQS, full noise with correlated gate noise), **BSIMSOI** (level 10/58, 6-terminal SOI), **HiSIM2** (level 61/68), **HiSIM_HV** (level 73, 5-terminal with self-heating) |

### Convergence & Numerical Features
- **Automatic fallback sequence**: Newton → GMIN stepping → source stepping → pseudo-transient continuation
- **Trap ringing detection** with automatic Gear-2 fallback
- **Breakpoint classification** (HARD vs SOFT) with adaptive step recovery
- **Configurable LTE reference modes** (per-node, max-all, max-per-signal)
- **Device-level LTE** on charge/flux state variables

### Netlist Features
`.param` expressions, `.subckt`/`.ends`, `.include`/`.lib`, `.global`, `.ic`, `.nodeset`, `.options`, `.func`, `.measure`, `.save`, `.step`, SPICE suffixes (k/m/u/n/p/f/T)

### Unique Features vs. Standard SPICE
- **Auto-differentiation** in B-source expressions (exact Jacobians, no numerical perturbation)
- **IDT()** function in B-sources (time integral with initial condition)
- **RAC** parameter on resistors (separate AC resistance)
- **AC G/C pre-caching** with NQS device hooks (matrices built once, per-frequency corrections via `ac_stamp_freq()`)
- **3 LTE reference modes** (inspired by Xyce's NEWLTE)

### C++ API
```cpp
neospice::Simulator sim;
auto ckt = sim.load("circuit.cir");   // or sim.parse(netlist_string)
auto result = sim.run(ckt);            // runs all analyses in the netlist
// Or individually: run_dc(), run_transient(), run_ac(), run_noise(), etc.
```

Results are returned as structured data (maps keyed by signal name like `"v(out)"`, `"i(v1)"`).

### Validation
845 tests including direct ngspice comparison tests across all analysis types, with tolerances as tight as 1e-6 relative error on simple circuits and sub-1% on CMOS inverter edge timing.