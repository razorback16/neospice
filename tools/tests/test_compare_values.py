"""Regression tests for false MATCH outcomes in the KiCad comparator."""

import json
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from compare_kicad_models import compare_values


def test_requires_all_reference_outputs_not_just_intersection():
    ok, details = compare_values({'v(in)': 1.0}, {'v(in)': 1.0, 'v(out)': 0.5})
    assert not ok
    assert details[1]['var'] == 'v(out)'
    assert details[1]['status'] == 'missing'
    assert details[1]['ok'] is False


@pytest.mark.parametrize('neo,ng', [({}, {}), ({'v(a)': 1.0}, {'v(b)': 1.0}),
                                     ({'time': 1.0}, {'time': 1.0})])
def test_empty_or_disjoint_results_cannot_match(neo, ng):
    ok, details = compare_values(neo, ng)
    assert not ok
    assert any(d['ok'] is False for d in details)


@pytest.mark.parametrize('bad', [float('nan'), float('inf'), -float('inf')])
@pytest.mark.parametrize('reference_bad', [False, True])
def test_nonfinite_values_fail_with_json_safe_evidence(bad, reference_bad):
    neo, ng = ({'v(a)': 1.0}, {'v(a)': bad}) if reference_bad else (
        {'v(a)': bad}, {'v(a)': 1.0})
    ok, details = compare_values(neo, ng)
    assert not ok
    assert details[0]['status'] == 'nonfinite'
    json.dumps(details, allow_nan=False)


def test_finite_values_whose_difference_overflows_fail():
    ok, details = compare_values({'v(a)': 1e308}, {'v(a)': -1e308})
    assert not ok
    assert details[0]['status'] == 'overflow'
    json.dumps(details, allow_nan=False)


@pytest.mark.parametrize('internal', ['v(x1.internal)', 'v(m1#gate)', 'v(m1#body_diode)'])
def test_external_filter_is_explicit_and_successes_are_retained(internal):
    neo = {'v(out)': 1.0, 'v(extra)': 2.0}
    ng = {'v(out)': 1.0, internal: 3.0}
    ok, details = compare_values(neo, ng, external_only=True)
    assert ok
    assert len(details) == 1
    assert details[0]['var'] == 'v(out)' and details[0]['ok']
    assert not compare_values(neo, ng, external_only=False)[0]
    assert not compare_values({}, {internal: 3.0}, external_only=True)[0]


def test_retains_existing_voltage_and_current_tolerance():
    assert compare_values({'v(a)': 1.001}, {'v(a)': 1.0})[0]
    assert not compare_values({'v(a)': 1.01}, {'v(a)': 1.0})[0]
    assert compare_values({'i(v1)': 1e-9}, {'i(v1)': 0.0})[0]
    assert not compare_values({'i(v1)': 2e-9}, {'i(v1)': 0.0})[0]
