# Transient compatibility with ngspice 47

The required reference is ngspice47. The current full C++ suite passes the
JFET2, MOS3, VBIC, RLC, LTRA RC and long RC/RLC transient regressions without
changing their comparison tolerances. See [current results](joss-progress.md).

Verified corrections include initial breakpoint reduction, backward-Euler
restart after a breakpoint, accepted PULSE corner scheduling, MOS3 post-solve
convergence, and transient operating-point history/state transfer. Output
interpolation affects sampling, not the integration trajectory. Reference and
native adaptive grids are both checked by the [comparator](validation-methods.md).

UIC now loads prescribed initial state without solving a DC operating point,
retains the UIC flag during time stepping and starts output at the first
accepted step. Capacitor node `.ic` values supply omitted instance ICs. The
current-integrator regression checks analytical ramps and repeated runs; VBIC
delay comparisons cover UIC and ordinary startup for both polarities.

These passing fixtures do not establish complete transient compatibility.
TLV3201 still has a strict pointwise discrepancy despite passing its original
edge/DC-port contract. CoolMOS source-step and transient-OP fallback behavior
also require investigation. Successful fallback alone does not prove a
stationary DC solution or an independently checked KCL residual.

Relevant evidence and scope:

- [TLV3201 validation](tlv3201-validation.md).
- [Diode and CoolMOS investigation](diode-coolmos-investigation.md).
- [RFF70N06 operating point](rff70n06-investigation.md).
- [Source and frequency behavior](source-compatibility.md).

Earlier trace narratives are preserved in the
[pre-cleanup documentation archive](evidence/joss/2026-09-11-pre-ngspice47-only-docs.tar.gz).
Current work targets47 exclusively.
