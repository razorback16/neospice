"""Tests for the test scaffolding generator (gen_test.py)."""

from __future__ import annotations

from ngspice_migrate.gen_test import generate_test_cmake, generate_test_dc, generate_test_transient
from tests.test_gen_adapter import StubDescriptor


def test_test_cmake_has_target():
    cmake = generate_test_cmake(StubDescriptor())
    # StubDescriptor has neospice_name="dio", prefix="DIO"
    assert "test_dio_device" in cmake
    assert "neospice_lib" in cmake
    assert "GTest" in cmake or "gtest" in cmake


def test_test_dc_has_fixture():
    dc = generate_test_dc(StubDescriptor())
    assert "NgspiceRunner" in dc
    assert "compare_dc" in dc
    assert "TEST_F" in dc


def test_test_transient_has_fixture():
    tr = generate_test_transient(StubDescriptor())
    assert "NgspiceRunner" in tr
    assert "compare_transient" in tr
    assert "TEST_F" in tr
