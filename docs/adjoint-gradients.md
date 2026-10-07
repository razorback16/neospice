# Adjoint sensitivity

`Simulator::sensitivity(circuit, outputs, parameters)` computes DC output values
and a dense Jacobian indexed `[output][parameter]`. It solves one operating
point, assembles and factors its Jacobian, and performs one transpose solve per
output. The number of extra linear solves does not grow with the number of
selected parameters. `adjoint_solves` records the actual transpose solve count.
Parameter derivatives are analytical stamps. The existing `.sens` /
`run_sens()` finite-difference interface remains available unchanged.

```python
import numpy as np
import neospice as ns

result = ns.sensitivity('divider.cir', ['v(out)', 'i(v1)'], ['r1', 'r2', 'v1'])
assert result.status.converged
jacobian = np.asarray(result.jacobian)
# jacobian[0, 0] is d V(out) / d R1, in volts per ohm.
```

Outputs accept node voltage, differential voltage (`v(a,b)`), and currents for
devices that own MNA branch variables (for example voltage sources and
inductors). `i(r1)` is not a branch variable and is rejected. Ground is supported.
Names are case-insensitive. Result axes contain canonical names such as
`r1:resistance` and `v1:dc`.

DC parameters are nominal resistance, capacitance and inductance and independent
source DC values. Empty parameter lists select all these device types.
R/C/L accept `:resistance`, `:capacitance`, `:inductance`. Source parameters
accept `:dc`. DC capacitance/inductance derivatives are zero. A waveform source
without an explicit DC value does not use its stored DC parameter, so that
parameter's derivative is zero. Resistance derivatives include temperature,
scale and instance-multiplier factors.

DC circuits may include nonlinear devices. Their existing voltage Jacobians
supply the operating-point linearization. This API does not differentiate
semiconductor model parameters. Derivatives are local to the selected operating
point and implemented expression branch. No derivative is promised across a
switching threshold, a piecewise boundary or an operating-point branch change.

## Complex AC gradients

```python
result = ns.sensitivity_ac(
    'filter.cir', ['v(out)'], ['r1', 'c1', 'v1:ac_mag'], [100, 1000, 10000]
)
assert result.status.converged
jacobian = np.asarray(result.jacobian)  # [frequency][output][parameter]
y = result.values[0][0]
dy_dp = jacobian[0, 0, 0]
# Magnitude derivative at nonzero y:
d_magnitude = np.real(np.conj(y) * dy_dp) / abs(y)
```

C++ uses `sensitivity_ac(circuit, outputs, parameters, frequencies)` with the
same axes. Frequencies must be finite and positive. Order is preserved.
The result is the ordinary complex derivative, not magnitude, phase or dB.
The adjoint equation uses the plain transpose, without complex conjugation.

AC gradients currently require a **linear circuit**. Supported parameters are
R/C/uncoupled-L nominal values and independent sources' `:ac_mag`, `:ac_phase`
(in degrees) and `:dc`. An unsuffixed source name selects `:ac_mag`. Source DC
derivatives are zero in this linear scope. A fixed `RAC` overrides the nominal
resistance for AC, so its nominal-resistance derivative is zero. Coupled-inductor
value derivatives and nonlinear bias-dependent AC gradients are explicitly
rejected. Full model-parameter differentiation remains future work.

## Errors and state

Unknown/duplicate/unsupported parameters and outputs raise input errors.
Requested passive values that need a scale ratio must have finite, nonzero
nominal values. Nonconvergence and singular factorization raise `SimulationError`.
With `circuit.options.no_throw=true`, results have `status.converged=false` and
empty values/Jacobians. Check status before using a gradient in an optimizer.
The baseline solve and device linearization update circuit state. The operating
point cache is cleared on exit so a subsequent analysis recomputes valid state.
No parameter perturbations or value changes are made by the gradient methods.

The cost statement concerns linear solve counts, not measured wall-clock speed.
Many-output/full-state Jacobians still require one adjoint per output.
