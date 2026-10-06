"""Tune a divider with DC adjoint gradients; only NumPy/neospice are required."""
import neospice as ns

sim = ns.Simulator()
circuit = sim.parse('Divider\nV1 in 0 10\nR1 in out 1k\nR2 out 0 1k\n.op\n.end\n')
resistance = 1000.0
for _ in range(12):
    result = sim.sensitivity(circuit, ['v(out)'], ['r2'])
    if not result.status.converged:
        raise RuntimeError(result.status.warnings)
    error = result.values[0] - 6.0
    if abs(error) < 1e-9:
        break
    derivative = result.jacobian[0][0]
    resistance = max(1.0, resistance - error / derivative)
    circuit.set_param('r2', resistance)
print(f'R2 = {resistance:.6f} ohm; V(out) = {result.values[0]:.9f} V')
