"""Unit tests for the CMakeLists.txt generator."""

from __future__ import annotations

from dataclasses import dataclass

from ngspice_migrate.gen_cmake import generate_cmake


# ---------------------------------------------------------------------------
# Minimal stub for ModelDescriptor (mirrors the fields used by gen_cmake)
# ---------------------------------------------------------------------------

@dataclass
class _Descriptor:
    neospice_name: str


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

def test_cmake_has_object_library():
    desc = _Descriptor(neospice_name="dio")
    cmake = generate_cmake(
        desc,
        ["dio_shim.cpp", "dio_setup.cpp", "dio_load.cpp", "dio_device.cpp"],
    )
    assert "add_library(dio_obj OBJECT" in cmake
    assert "dio_shim.cpp" in cmake
    assert "dio_device.cpp" in cmake
    assert "target_include_directories" in cmake


def test_cmake_includes_all_files():
    desc = _Descriptor(neospice_name="bsim4v7")
    cmake = generate_cmake(desc, ["a.cpp", "b.cpp", "c.cpp"])
    assert "add_library(bsim4v7_obj OBJECT" in cmake
    for f in ("a.cpp", "b.cpp", "c.cpp"):
        assert f in cmake


def test_cmake_include_dir_uses_cmake_source_dir():
    desc = _Descriptor(neospice_name="res")
    cmake = generate_cmake(desc, ["res_load.cpp"])
    assert "${CMAKE_SOURCE_DIR}/src" in cmake
