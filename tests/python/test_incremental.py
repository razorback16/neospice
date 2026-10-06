import neospice as ns
import pytest


def test_slider_style_updates_reuse_symbolic():
    sim = ns.Simulator()
    c = sim.parse('Divider\nV1 in 0 DC 10 AC 1\nR1 in out 1k\nR2 out 0 1k\n.op\n.end\n')
    for value in [1000, 2000, 500]:
        c.update_param('r2', value)
        assert sim.re_solve(c).voltage('out') == pytest.approx(10*value/(1000+value))
        ac = sim.re_solve_ac(c, ns.ACMode.LIN, 1, 100, 100)
        assert ac.voltage('out')[0] == pytest.approx(value/(1000+value))
    stats = c.reuse_statistics()
    assert stats.dc_symbolic_analyses == 1
    assert stats.ac_symbolic_analyses == 1
    assert stats.dc_runs == 3
    assert stats.ac_runs == 3
    with pytest.raises(ValueError):
        c.update_param('r2', 0)
    c.clear_reuse_cache()
    assert c.reuse_statistics().dc_runs == 0
