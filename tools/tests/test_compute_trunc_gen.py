"""Tests for compute_trunc generation in gen_adapter.py."""

from ngspice_migrate.gen_adapter import extract_charge_offsets


def test_extract_charge_offsets_from_load():
    """Extract charge state offsets from NIintegrate calls in translated load."""
    load_source = """
    error = NIintegrate(ckt,&geq,&ceq,capbd,here->BSIM4v7qbd);
    if(error) return(error);
    error = NIintegrate(ckt,&geq,&ceq,capbs,here->BSIM4v7qbs);
    if(error) return(error);
    error = NIintegrate(ckt,&gcqgb,&cqgate,cqgate,here->BSIM4v7qg);
    """
    offsets = extract_charge_offsets(load_source, "BSIM4v7")
    assert "BSIM4v7qbd" in offsets
    assert "BSIM4v7qbs" in offsets
    assert "BSIM4v7qg" in offsets


def test_extract_charge_offsets_empty():
    """No NIintegrate calls returns empty list."""
    offsets = extract_charge_offsets("x = 1;", "DIO")
    assert offsets == []


def test_generated_compute_trunc_has_lte():
    """Generated compute_trunc contains LTE formula when charge offsets found."""
    from ngspice_migrate.gen_adapter import _gen_compute_trunc
    code = _gen_compute_trunc(None, ["DIOcapCharge"], [])
    assert "lte_coefficient" in code
    assert "dd2" in code
    assert "dt_min" in code
    assert "inst_.DIOcapCharge" in code
    assert "TODO" not in code


def test_generated_compute_trunc_uses_override():
    """charge_states descriptor override uses numeric offsets."""
    from ngspice_migrate.gen_adapter import _gen_compute_trunc
    code = _gen_compute_trunc(None, [], [3])
    assert "state_base_ + 3" in code
    assert "TODO" not in code
