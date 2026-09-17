"""Tests for deterministic KiCad file/model cohort selection."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from compare_kicad_models import (  # noqa: E402
    case_key,
    isolated_case_keys,
    parse_case_selector,
    status_transitions,
)


def test_parse_case_selector_preserves_exact_file_and_name():
    assert parse_case_selector('a/path::LM6121/NS') == ('a/path', 'LM6121/NS')


@pytest.mark.parametrize('value', ['missing-separator', '::name', 'file::'])
def test_parse_case_selector_rejects_incomplete_values(value):
    with pytest.raises(Exception):
        parse_case_selector(value)


def test_case_key_is_shared_by_saved_rows_and_generated_tests():
    row = {'file': 'models/a.lib', 'name': 'A'}
    test = ('subckt', 'A', '3-port', 'deck', 'models/a.lib')
    assert case_key(row) == case_key(test)


def test_status_transitions_are_exact_and_deterministic():
    baseline = [
        {'file': 'z.lib', 'name': 'DUP', 'status': 'NG_ONLY'},
        {'file': 'a.lib', 'name': 'DUP', 'status': 'MATCH'},
    ]
    current = [
        {'file': 'z.lib', 'name': 'DUP', 'status': 'MATCH'},
        {'file': 'a.lib', 'name': 'DUP', 'status': 'MISMATCH'},
        {'file': 'new.lib', 'name': 'NEW', 'status': 'MATCH'},
    ]
    assert status_transitions(current, baseline) == [
        ('a.lib', 'DUP', 'MATCH', 'MISMATCH'),
        ('z.lib', 'DUP', 'NG_ONLY', 'MATCH'),
    ]


def test_isolated_case_keys_preserve_saved_fixture_choice():
    baseline = [
        {'file': 'a.lib', 'name': 'A', 'isolated': True},
        {'file': 'b.lib', 'name': 'B', 'isolated': False},
        {'file': 'c.lib', 'name': 'C'},
    ]
    assert isolated_case_keys(baseline) == {('a.lib', 'A')}


@pytest.mark.parametrize('same_status', [False, True])
def test_legacy_transitions_reject_ambiguous_baselines(same_status):
    baseline = [
        {'file': 'a.lib', 'name': 'DUP', 'kind': 'model', 'status': 'MATCH'},
        {'file': 'a.lib', 'name': 'DUP', 'kind': 'subckt',
         'status': 'MATCH' if same_status else 'NG_ONLY'},
    ]
    with pytest.raises(ValueError, match='ambiguous legacy case'):
        status_transitions([], baseline)
    with pytest.raises(ValueError, match='ambiguous legacy case'):
        isolated_case_keys(baseline)


def test_legacy_transitions_reject_ambiguous_current_rows():
    row = {'file': 'a.lib', 'name': 'DUP', 'status': 'MATCH'}
    with pytest.raises(ValueError, match='ambiguous legacy case'):
        status_transitions([row, dict(row, status='MISMATCH')], [row])
