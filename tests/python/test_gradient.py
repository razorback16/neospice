import numpy as np
import pytest
import neospice as ns

DECK = 'RC\nV1 in 0 DC 2 AC 1\nR1 in out 1k\nR2 out 0 1k\nC1 out 0 1u\n.op\n.end\n'


def test_dc_jacobian_for_optimizer():
    result = ns.sensitivity(DECK, ['v(out)', 'i(v1)'], ['r1', 'r2', 'v1'])
    assert result.status.converged
    assert result.adjoint_solves == 2
    assert result.parameters == ['r1:resistance', 'r2:resistance', 'v1:dc']
    jacobian = np.asarray(result.jacobian)
    assert jacobian.shape == (2, 3)
    assert jacobian[0] == pytest.approx([-0.0005, 0.0005, 0.5])


def test_complex_jacobian_axes():
    result = ns.sensitivity_ac(DECK, ['v(out)'], ['r1', 'c1'], [100, 1000])
    assert result.status.converged
    assert np.asarray(result.jacobian).shape == (2, 1, 2)
    for i, f in enumerate(result.frequency):
        jw = 2j * np.pi * f
        denominator = 2 + jw * 1e-3
        assert result.values[i][0] == pytest.approx(1 / denominator)
        assert result.jacobian[i][0][1] == pytest.approx(-jw * 1000 / denominator**2)


def test_gradient_rejects_unsupported_parameter():
    with pytest.raises(ValueError):
        ns.sensitivity(DECK, ['v(out)'], ['r1:tc1'])
