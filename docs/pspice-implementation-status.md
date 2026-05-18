## PSpice Model Language Specification: Implementation Status

### Legend
- **DONE** = Implemented with PSpice-compatible syntax
- **DIFFERENT SYNTAX** = Functionality exists but PSpice syntax is not supported
- **PARTIAL** = Some aspects implemented, some missing
- **NOT IMPL** = Not implemented at all

---

### 1. Lexical / Tokenizer (Spec Step 1)

| Feature                                    | Status   | Notes                             |
| ------------------------------------------ | -------- | --------------------------------- |
| `*` line comments                          | **DONE** |                                   |
| `;` inline comments                        | **DONE** |                                   |
| `+` continuation lines                     | **DONE** |                                   |
| `$` inline comments                        | **DONE** | ngspice extension, also supported |
| Numeric suffixes (F,P,N,U,MIL,M,K,MEG,G,T) | **DONE** | Full longest-match recognition    |
| Case insensitivity                         | **DONE** |                                   |
| Curly-brace expressions `{...}`            | **DONE** |                                   |

---

### 2. Expressions & Parameters (Spec Steps 2-3)

| Feature                                      | Status               | Notes                                                                  |
| -------------------------------------------- | -------------------- | ---------------------------------------------------------------------- |
| `.PARAM` command                             | **DONE**             | Multi-assignment, dependency-ordered resolution                        |
| `.FUNC` command                              | **DONE**             | Textual substitution, local scope in subcircuits                       |
| Arithmetic ops (+, -, *, /, **)              | **DONE**             |                                                                        |
| Relational ops (==, !=, <, <=, >, >=)        | **DONE**             |                                                                        |
| Logical ops (\|, ^, &, ~)                    | **DONE**             |                                                                        |
| **Intrinsic functions in .PARAM evaluator:** |                      |                                                                        |
| ABS, ACOS, ASIN, ATAN, ATAN2                 | **DONE**             |                                                                        |
| COS, COSH, SIN, SINH, TAN, TANH              | **DONE**             |                                                                        |
| EXP, LOG, LOG10, SQRT                        | **DONE**             |                                                                        |
| IF(t,x,y)                                    | **DONE**             |                                                                        |
| LIMIT, MIN, MAX                              | **DONE**             |                                                                        |
| PWR, PWRS, SGN, STP                          | **DONE**             |                                                                        |
| TABLE (as function)                          | **DONE**             |                                                                        |
| ACOSH, ASINH, ATANH                          | **DIFFERENT SYNTAX** | Implemented in ASRC/B-source expressions only, not in .PARAM evaluator |
| ARCTAN                                       | **NOT IMPL**         | Alias for ATAN; easy to add                                            |
| DDT(x), SDT(x)                               | **DIFFERENT SYNTAX** | Implemented in ASRC expressions (DDT, IDT), not in .PARAM              |
| SCHEDULE(x1,y1,...)                          | **NOT IMPL**         |                                                                        |
| IMG(x), M(x), P(x), R(x)                     | **NOT IMPL**         | Complex-number functions                                               |
| **System variables:**                        |                      |                                                                        |
| TEMP                                         | **DIFFERENT SYNTAX** | Available as `__temper__` in ASRC, not in .PARAM                       |
| TIME                                         | **DIFFERENT SYNTAX** | Available as `__time__` in ASRC, not in .PARAM                         |
| PI                                           | **NOT IMPL**         | Not a recognized constant in .PARAM                                    |
| RELTOL, ABSTOL, VNTOL, CHGTOL, GMIN          | **NOT IMPL**         | Not accessible in expressions                                          |
| **Behavioral-only functions:**               |                      |                                                                        |
| CEIL, FLOOR                                  | **DIFFERENT SYNTAX** | In ASRC parser only, not in .PARAM                                     |
| ZERO(expr), ONE(expr)                        | **NOT IMPL**         | Side-effect carrier functions                                          |
| INTQ(arg)                                    | **NOT IMPL**         | Integer test                                                           |
| DELTA(n), TIME(n), STATE(n,src)              | **NOT IMPL**         | History access functions                                               |
| BREAK(time)                                  | **NOT IMPL**         | Simulator breakpoint scheduling                                        |

---

### 3. `.MODEL` Extensions (Spec Step 4)

| Feature                                               | Status               | Notes                                                                   |
| ----------------------------------------------------- | -------------------- | ----------------------------------------------------------------------- |
| Basic `.MODEL name TYPE(params...)`                   | **DONE**             |                                                                         |
| PSpice type aliases (RES, CAP, IND, VSWITCH, ISWITCH) | **DONE**             | Mapped to internal types                                                |
| `AKO:` inheritance                                    | **DONE**             | Multi-pass chain resolution, cycle detection                            |
| `DEV`/`LOT` tolerance annotations                     | **DONE**             | Parsed and preserved; not used in simulation                            |
| Distribution name on DEV/LOT                          | **DONE**             | Preserved as metadata                                                   |
| T_MEASURED, T_ABS, T_REL_GLOBAL, T_REL_LOCAL          | **DONE**             | Parsed and stored                                                       |
| PSpice model types: GASFET                            | **DIFFERENT SYNTAX** | Device exists as Z-prefix MESFET/HFET, not as PSpice `B` prefix GaAsFET |
| PSpice model types: NIGBT                             | **NOT IMPL**         | No IGBT device                                                          |
| PSpice model types: LPNP                              | **NOT IMPL**         | No lateral PNP keyword                                                  |
| PSpice model types: DINPUT, DOUTPUT, UIO              | **NOT IMPL**         | Digital interface models                                                |
| PSpice model types: CORE, TRN                         | **NOT IMPL**         | Nonlinear core; TRN transmission line model                             |

---

### 4. `.SUBCKT` / `X` Extensions (Spec Step 2)

| Feature                                      | Status      | Notes                                                                           |
| -------------------------------------------- | ----------- | ------------------------------------------------------------------------------- |
| `.SUBCKT name node1 node2 ...`               | **DONE**    |                                                                                 |
| `PARAMS:` on `.SUBCKT` definition            | **DONE**    |                                                                                 |
| `PARAMS:` on `X` call overrides              | **PARTIAL** | Bare `key=val` works; explicit `PARAMS:` keyword on X lines not always required |
| `OPTIONAL:` keyword                          | **PARTIAL** | Parsed and stored, but optional-node semantics not implemented during expansion |
| `TEXT:` keyword                              | **PARTIAL** | Parsed and stored, but text parameter semantics not implemented                 |
| Nested `.SUBCKT` definitions                 | **DONE**    | Stack-based depth tracking                                                      |
| Recursive expansion (max 100 depth)          | **DONE**    |                                                                                 |
| Parameter scoping (global -> subckt -> call) | **DONE**    |                                                                                 |
| `.GLOBAL` nodes                              | **DONE**    |                                                                                 |

---

### 5. ABM Forms on E/G (Spec Step 5)

| Feature                               | Status       | Notes                                    |
| ------------------------------------- | ------------ | ---------------------------------------- |
| `E/G ... VALUE={expr}`                | **DONE**     | Lowered to ASRC device                   |
| `E/G ... TABLE{expr} = (x,y)...`      | **DONE**     | PWL interpolation with endpoint clamping |
| `E/G ... POLY(N)`                     | **DONE**     | Multi-variate polynomial                 |
| `E/G ... LAPLACE{expr} = {transform}` | **NOT IMPL** | s-domain filter                          |
| `E/G ... FREQ{expr} = ...`            | **NOT IMPL** | Frequency-domain tabulated response      |
| `E/G ... CHEBYSHEV{expr} = ...`       | **NOT IMPL** | Automatic filter synthesis               |
| `E ... F={expr}` (flux source)        | **NOT IMPL** | PSpice dφ/dt extension                   |
| `G ... Q={expr}` (charge source)      | **NOT IMPL** | PSpice dq/dt extension                   |

---

### 6. Analysis Commands (Spec Step 7)

| Feature                               | Status       | PSpice Syntax Match | Notes                             |
| ------------------------------------- | ------------ | ------------------- | --------------------------------- |
| `.AC DEC\|OCT\|LIN npts fstart fstop` | **DONE**     | Yes                 |                                   |
| `.DC LIN src start stop step`         | **DONE**     | Yes                 |                                   |
| `.DC` nested sweeps (2 variables)     | **DONE**     | Yes                 |                                   |
| `.DC DEC\|OCT src start stop points`  | **NOT IMPL** | —                   | Log DC sweep                      |
| `.DC src LIST val1 val2...`           | **NOT IMPL** | —                   | List sweep                        |
| `.DC MODTYPE MODNAME(PARAM)` sweep    | **NOT IMPL** | —                   | Model parameter sweep             |
| `.DC TEMP start stop step`            | **NOT IMPL** | —                   | Temperature sweep                 |
| `.DC PARAM name start stop step`      | **NOT IMPL** | —                   | Global parameter sweep            |
| `.TRAN tstep tstop [tstart [dtmax]]`  | **DONE**     | Yes                 |                                   |
| `.TRAN UIC`                           | **DONE**     | Yes                 | Applies IC= on devices            |
| `.TRAN SKIPBP`                        | **NOT IMPL** | —                   | Skip bias point                   |
| `.TRAN/OP`                            | **NOT IMPL** | —                   | Detailed bias point for transient |
| `.NOISE V(out) Vsrc [interval]`       | **DONE**     | Yes                 |                                   |
| `.OP`                                 | **DONE**     | Yes                 |                                   |
| `.TF V(out) Vsrc`                     | **DONE**     | Yes                 |                                   |
| `.SENS V(out)`                        | **PARTIAL**  | Yes                 | Only R/V/I perturbation, DC only  |
| `.FOUR freq [nharmonics] signal`      | **DONE**     | Yes                 |                                   |
| `.PZ`                                 | **PARTIAL**  | —                   | Poles only; zeros not implemented |

---

### 7. Simulation Control (Spec Step 8)

| Feature                                      | Status               | Notes                                                                      |
| -------------------------------------------- | -------------------- | -------------------------------------------------------------------------- |
| `.OPTIONS` (basic options)                   | **DONE**             | reltol, abstol, vntol, gmin, trtol, chgtol, temp, tnom, itl1, itl4, method |
| `.OPTIONS` flag options (ACCT, EXPAND, etc.) | **NOT IMPL**         | Silently ignored                                                           |
| `.OPTIONS STEPGMIN`                          | **DIFFERENT SYNTAX** | Gmin stepping implemented as automatic convergence aid                     |
| `.OPTIONS SCHEDULE()` time-varying           | **NOT IMPL**         |                                                                            |
| `.STEP LIN param start stop step`            | **DONE**             | Linear sweep only                                                          |
| `.STEP DEC\|OCT param start stop pts`        | **NOT IMPL**         | Log step sweep                                                             |
| `.STEP param LIST val1 val2...`              | **NOT IMPL**         | List step sweep                                                            |
| `.STEP TEMP`                                 | **NOT IMPL**         | Temperature stepping                                                       |
| `.TEMP val1 val2...`                         | **NOT IMPL**         | Multi-temperature analysis                                                 |
| `.MC` Monte Carlo                            | **NOT IMPL**         |                                                                            |
| `.WCASE` worst-case                          | **NOT IMPL**         |                                                                            |
| `.DISTRIBUTION`                              | **NOT IMPL**         |                                                                            |
| `.AUTOCONVERGE`                              | **NOT IMPL**         | (Convergence aids exist but not PSpice syntax)                             |
| `.DMFACTOR`                                  | **NOT IMPL**         |                                                                            |

---

### 8. Analog Devices (Spec Step 9)

| Device                    | Status               | PSpice Extensions Missing                                                                                                  |
| ------------------------- | -------------------- | -------------------------------------------------------------------------------------------------------------------------- |
| **R** (Resistor)          | **DONE**             | Missing: **TCE** (exponential temp coeff)                                                                                  |
| **C** (Capacitor)         | **PARTIAL**          | **VC1/VC2** parsed but not applied in simulation                                                                           |
| **L** (Inductor)          | **PARTIAL**          | Missing: **IL1/IL2** (current coefficients), **winding form** (TURNS, RESIS)                                               |
| **D** (Diode)             | **DONE**             | Full ngspice diode model                                                                                                   |
| **Q** (BJT)               | **DONE**             | Level 1 GP + VBIC. Missing: **LPNP** type keyword, **Level 2 Mextram**                                                     |
| **J** (JFET)              | **DONE**             | Level 1 + Level 2 PS. Missing: **BETATCE**                                                                                 |
| **M** (MOSFET)            | **DONE**             | Levels 1,3,8,9,10,14,49,58,61,68,73. Missing: **Level 2** (MOS2), **Level 5** (EKV), **Level 6** (BSIM1)                   |
| **B** (GaAsFET)           | **DIFFERENT SYNTAX** | Exists as **Z-prefix** MESFET/HFET (ngspice convention); PSpice uses B-prefix which conflicts with ASRC                    |
| **Z** (IGBT)              | **NOT IMPL**         | NIGBT model not implemented at all                                                                                         |
| **E** (VCVS)              | **PARTIAL**          | Linear + POLY + VALUE + TABLE. Missing: **LAPLACE, FREQ, CHEBYSHEV, F= flux source**                                       |
| **G** (VCCS)              | **PARTIAL**          | Linear + POLY + VALUE + TABLE. Missing: **LAPLACE, FREQ, CHEBYSHEV, Q= charge source**                                     |
| **F** (CCCS)              | **DONE**             | Linear + POLY                                                                                                              |
| **H** (CCVS)              | **DONE**             | Linear + POLY                                                                                                              |
| **V/I** (Sources)         | **PARTIAL**          | DC, AC, PULSE, SIN, EXP, SFFM, PWL, AM. Missing: **PWL REPEAT/ENDREPEAT, PWL FILE, TIME_SCALE_FACTOR, VALUE_SCALE_FACTOR** |
| **S** (V-switch)          | **DONE**             | VSWITCH model. Missing: **S_ST** short-transition model                                                                    |
| **W** (I-switch)          | **DONE**             | ISWITCH model. Missing: **W_ST** short-transition model                                                                    |
| **T** (Transmission line) | **DONE**             | Ideal lossless (Z0, TD, F, NL). Lossy via LTRA (O element)                                                                 |
| **K** (Coupling)          | **PARTIAL**          | Linear mutual coupling. Missing: **nonlinear core** (Jiles-Atherton), **T-line coupling** (Lm, Cm)                         |
| **X** (Subcircuit)        | **DONE**             | With PARAMS: support                                                                                                       |
| **B** (ASRC)              | **DONE**             | Full behavioral V/I source with auto-differentiation                                                                       |
| **O** (LTRA)              | **DONE**             | Full lossy transmission line (RLC, RC, RG, LC, RL)                                                                         |

---

### 9. Output / Checkpoint Commands (Spec Step 11)

| Feature              | Status               | Notes                                                                          |
| -------------------- | -------------------- | ------------------------------------------------------------------------------ |
| `.PRINT`             | **DONE**             | DC, AC, TRAN, NOISE                                                            |
| `.PLOT`              | **DONE**             | ASCII waveform plot                                                            |
| `.PROBE`             | **DIFFERENT SYNTAX** | Not `.PROBE` command; `.SAVE` serves same purpose; `.raw` binary output exists |
| `.WATCH`             | **NOT IMPL**         |                                                                                |
| `.VECTOR`            | **NOT IMPL**         |                                                                                |
| `.SAVEBIAS`          | **NOT IMPL**         |                                                                                |
| `.LOADBIAS`          | **NOT IMPL**         |                                                                                |
| `.CHKPT`             | **NOT IMPL**         |                                                                                |
| `.RESTART`           | **NOT IMPL**         |                                                                                |
| `.MEAS` / `.MEASURE` | **DONE**             | Full support (not PSpice-specific but commonly used)                           |
| `.RAW` binary output | **DONE**             | Compatible with waveform viewers                                               |

---

### 10. Digital Devices (Spec Step 10)

| Feature                          | Status                                      |
| -------------------------------- | ------------------------------------------- |
| **U** (digital primitives)       | **NOT IMPL**                                |
| **N** (digital input interface)  | **NOT IMPL**                                |
| **O** (digital output interface) | **NOT IMPL** (O-prefix is LTRA in neospice) |
| Digital timing models            | **NOT IMPL**                                |
| I/O models (UIO)                 | **NOT IMPL**                                |
| `.EXTERNAL`                      | **NOT IMPL**                                |
| `.STIMLIB` / `.STIMULUS`         | **NOT IMPL**                                |
| `.ALIASES` / `.ENDALIASES`       | **NOT IMPL**                                |

---

### 11. Other PSpice Commands

| Feature                    | Status       |
| -------------------------- | ------------ |
| `.INC` / `.INCLUDE`        | **DONE**     |
| `.LIB` (with sections)     | **DONE**     |
| `.END` / `.ENDS` / `.ENDL` | **DONE**     |
| `.IC` (initial conditions) | **DONE**     |
| `.NODESET`                 | **DONE**     |
| `.TEXT` (text parameter)   | **NOT IMPL** |

---

### Summary: "We have it, but not PSpice syntax" (DIFFERENT SYNTAX)

These are features where **the underlying capability exists** but the **PSpice-specific syntax/access** is missing:

| Capability                    | What We Have                       | PSpice Syntax We're Missing                    |
| ----------------------------- | ---------------------------------- | ---------------------------------------------- |
| Hyperbolic arc trig           | ACOSH/ASINH/ATANH in ASRC B-source | Not available in `.PARAM` expressions          |
| DDT/SDT (time deriv/integral) | DDT/IDT in ASRC B-source           | Not available in `.PARAM` expressions          |
| CEIL/FLOOR                    | In ASRC B-source parser            | Not available in `.PARAM` expressions          |
| TIME variable                 | `__time__` in ASRC                 | Not accessible as `TIME` in `.PARAM` / `{...}` |
| TEMP variable                 | `__temper__` in ASRC               | Not accessible as `TEMP` in `.PARAM` / `{...}` |
| GaAsFET device                | Z-prefix MESFET/HFET1/HFET2        | PSpice uses B-prefix; mapping not in parser    |
| Gmin stepping                 | Automatic convergence aid          | Not `.OPTIONS STEPGMIN` or `.AUTOCONVERGE`     |
| Probe/save output             | `.SAVE` + `.raw` writer            | Not `.PROBE` command syntax                    |
| VC1/VC2 capacitor model       | Parsed and stored in model card    | Not applied in simulation equations            |

### Summary: "Not implemented at all"

These are features where **no corresponding capability** exists:

| Category              | Missing Features                                                                                                                            |
| --------------------- | ------------------------------------------------------------------------------------------------------------------------------------------- |
| **Expressions**       | SCHEDULE(), IMG/M/P/R (complex), PI constant, ZERO/ONE/INTQ/DELTA/STATE/BREAK, system vars (RELTOL/ABSTOL/VNTOL/CHGTOL/GMIN) in expressions |
| **ABM**               | LAPLACE, FREQ, CHEBYSHEV filters; flux (F=) and charge (Q=) sources                                                                         |
| **Devices**           | IGBT (Z/NIGBT), LPNP BJT type, MOS2/EKV/BSIM1 levels, S_ST/W_ST switch models, nonlinear magnetic core (K), T-line coupling (K with Lm/Cm)  |
| **Device params**     | TCE on R, IL1/IL2 on L, winding form on L, BETATCE on J                                                                                     |
| **DC sweep**          | DEC/OCT/LIST modes, model parameter sweep, temperature sweep, global parameter sweep                                                        |
| **Step**              | DEC/OCT/LIST modes, temperature step                                                                                                        |
| **Monte Carlo**       | `.MC`, `.WCASE`, `.DISTRIBUTION`                                                                                                            |
| **Source extensions** | PWL REPEAT/ENDREPEAT, PWL FILE, TIME/VALUE_SCALE_FACTOR                                                                                     |
| **Output/control**    | `.PROBE`, `.WATCH`, `.VECTOR`, `.SAVEBIAS`, `.LOADBIAS`, `.CHKPT`, `.RESTART`                                                               |
| **Digital**           | Entire digital simulation subsystem (U/N/O devices, timing models, I/O models, `.EXTERNAL`, `.STIMULUS`, `.STIMLIB`, `.ALIASES`)            |
| **Other**             | `.TEMP`, `.TEXT`, `.AUTOCONVERGE`, `.DMFACTOR`, PSpice OpAmp/Battery macromodels, Capture `PSpiceTemplate`                                  |

### By Spec Suggested Implementation Order

| Step | Description                                                             | Status                                                                                |
| ---- | ----------------------------------------------------------------------- | ------------------------------------------------------------------------------------- |
| 1    | Dialect-aware lexical: comments, continuations, suffixes, braces        | **DONE**                                                                              |
| 2    | `.PARAM`, `.SUBCKT` PARAMS:/OPTIONAL:/TEXT:, `X` PARAMS:                | **MOSTLY DONE** (OPTIONAL/TEXT semantics incomplete)                                  |
| 3    | `.FUNC`, scoped resolution, intrinsic checks, arity diagnostics         | **DONE**                                                                              |
| 4    | `.MODEL` AKO:, PSpice model types, tolerances, temperature metadata     | **MOSTLY DONE** (missing LPNP, NIGBT, CORE, TRN types)                                |
| 5    | ABM: VALUE, TABLE, LAPLACE, FREQ, CHEBYSHEV                             | **PARTIAL** (VALUE+TABLE done; LAPLACE/FREQ/CHEBYSHEV not)                            |
| 6    | Lowering passes + unsupported-construct diagnostics                     | **PARTIAL** (VALUE/TABLE lowered; no diagnostic framework for unsupported constructs) |
| 7    | Analysis commands (.AC, .DC, .TRAN, .NOISE, .OP, .TF, .SENS, .FOUR)     | **MOSTLY DONE** (DC sweep variants incomplete, SENS/PZ partial)                       |
| 8    | Simulation control (.OPTIONS, .STEP, .MC, .WCASE, .TEMP, .DISTRIBUTION) | **PARTIAL** (basic .OPTIONS + linear .STEP only)                                      |
| 9    | All analog device forms + PSpice extensions                             | **PARTIAL** (see device table above)                                                  |
| 10   | Digital devices (U, N, O)                                               | **NOT STARTED**                                                                       |
| 11   | Output/checkpoint commands                                              | **PARTIAL** (.PRINT/.PLOT/.RAW done; rest not)                                        |