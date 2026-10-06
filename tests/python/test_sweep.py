import gc
import math

import neospice as ns
import pytest

DECK = 'Divider\n.param r=1k\nV1 in 0 6\nR1 in out {r}\nR2 out 0 1k\n.op\n.end\n'


def test_parallel_sweep_and_result_lifetimes():
    batch = ns.sweep(DECK, [{'r': 1000}, {'r': 2000}, {'oops': 3}], workers=2)
    samples = batch.samples
    del batch
    gc.collect()
    assert samples[0].result.dc.voltage('out') == pytest.approx(3)
    result = samples[1].result
    assert samples[2].error
    assert samples[2].result is None
    del samples
    gc.collect()
    assert result.dc.voltage('out') == pytest.approx(2)


def test_monte_carlo_and_statistics():
    param = ns.ParameterVariation()
    param.parameter, param.nominal, param.spread = 'r', 1000, 100
    a = ns.monte_carlo(DECK, [param], n=25, seed=9, workers=1)
    b = ns.monte_carlo(DECK, [param], n=25, seed=9, workers=4)
    assert [s.point.parameters for s in a.samples] == [s.point.parameters for s in b.samples]
    av = [s.result.dc.voltage('out') for s in a.samples]
    bv = [s.result.dc.voltage('out') for s in b.samples]
    assert av == bv
    stats = ns.summarize_samples(av, 0, 6, 5)
    assert stats.count == 25
    assert stats.yield_fraction == 1
    assert stats.mean == pytest.approx(sum(av)/25)
    assert sum(stats.histogram) == 25
    assert math.isfinite(stats.standard_deviation)


def test_temperature_and_device_values():
    point = ns.SweepPoint()
    point.device_values = {'r2': 2000}
    point.temperature_celsius = 70
    batch = ns.sweep(DECK, [point], workers=1)
    assert batch.samples[0].result.dc.voltage('out') == pytest.approx(4)


def test_include_relative_to_file(tmp_path):
    (tmp_path / 'values.inc').write_text('.param r=1k\n')
    deck = tmp_path / 'divider.cir'
    deck.write_text(DECK.replace('.param r=1k', '.include values.inc'))
    batch = ns.sweep(str(deck), [{'r': 3000}], workers=1)
    assert not batch.samples[0].error
    assert batch.samples[0].result.dc.voltage('out') == pytest.approx(1.5)
