"""Tests for the test scaffolding generator (gen_test.py)."""

from __future__ import annotations

import re
from pathlib import Path

import pytest
from ngspice_migrate.descriptor import load_descriptor
from ngspice_migrate.gen_test import (
    generate_test_cmake, generate_test_compare, generate_test_dc,
    generate_test_transient, generate_circuits,
)
from tests.test_gen_adapter import StubDescriptor


def test_test_cmake_has_target():
    cmake = generate_test_cmake(StubDescriptor())
    # StubDescriptor has neospice_name="dio", prefix="DIO"
    assert "test_dio_compare" in cmake
    assert "neospice_lib" in cmake
    assert "GTest" in cmake or "gtest" in cmake
    assert "${NGSPICE_LIBRARIES}" in cmake
    assert "${NGSPICE_INCLUDE_DIRS}" in cmake
    assert "NGSPICE_BINARY" not in cmake


def test_test_dc_has_fixture():
    dc = generate_test_dc(StubDescriptor())
    assert "NgspiceRunner" in dc
    assert "compare_dc" in dc
    assert "TEST_F" in dc
    assert dc == generate_test_compare(StubDescriptor())
    assert "std::make_unique<NgspiceRunner>()" in dc
    assert "GTEST_SKIP" not in dc


def test_legacy_transient_stub_explicitly_reports_no_coverage():
    tr = generate_test_transient(StubDescriptor())
    assert "No transient scaffold is generated" in tr
    assert "TEST_F" not in tr


def test_generated_comparisons_have_corresponding_circuits():
    desc = StubDescriptor()
    source = generate_test_compare(desc)
    circuits = generate_circuits(desc)
    assert set(circuits) == {"dio_dc_op.cir", "dio_ac.cir"}
    for filename in circuits:
        assert filename in source
        assert "D1" in circuits[filename]
    assert "compare_dc(ng_result, cs_result" in source
    assert "compare_ac(ng_result, cs_result" in source


@pytest.mark.parametrize("name", ["dio", "bjt", "jfet2", "mos1", "hfet1"])
def test_test_names_are_valid_cpp_identifiers(name):
    desc = load_descriptor(Path(__file__).parents[1] / "descriptors" / f"{name}.yaml")
    source = generate_test_compare(desc)
    tests = re.findall(r"TEST_F\(([^,]+), ([^)]+)\)", source)
    assert len(tests) >= 2
    assert len(set(tests)) == len(tests)
    for fixture, test in tests:
        assert re.fullmatch(r"[A-Za-z_]\w*", fixture)
        assert re.fullmatch(r"[A-Za-z_]\w*", test)
