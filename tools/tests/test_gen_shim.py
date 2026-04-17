"""Tests for the shim layer generator (gen_shim.py)."""

from __future__ import annotations

from dataclasses import dataclass

import pytest

from ngspice_migrate.gen_shim import generate_shim_cpp, generate_shim_hpp


# ---------------------------------------------------------------------------
# Stub descriptor
# ---------------------------------------------------------------------------

@dataclass
class StubDescriptor:
    namespace: str = "dio"
    neospice_name: str = "dio"
    has_internal_nodes: bool = False
    state_count: int = 0


@pytest.fixture
def desc() -> StubDescriptor:
    return StubDescriptor()


# ---------------------------------------------------------------------------
# Header tests
# ---------------------------------------------------------------------------

class TestShimHpp:
    def test_namespace(self, desc: StubDescriptor) -> None:
        hpp = generate_shim_hpp(desc)
        assert "namespace neospice::dio" in hpp
        assert "#pragma once" in hpp
        assert "struct Ckt" in hpp
        assert "class Matrix" in hpp

    def test_pragma_once_is_first_line(self, desc: StubDescriptor) -> None:
        hpp = generate_shim_hpp(desc)
        assert hpp.startswith("#pragma once\n")

    def test_true_false_macros(self, desc: StubDescriptor) -> None:
        hpp = generate_shim_hpp(desc)
        assert "#define TRUE  1" in hpp
        assert "#define FALSE 0" in hpp

    def test_error_codes(self, desc: StubDescriptor) -> None:
        hpp = generate_shim_hpp(desc)
        assert "OK" in hpp
        assert "E_BADPARM" in hpp
        assert "E_PARMRANGE" in hpp
        assert "E_NOMEM" in hpp
        assert "E_UNSUPP" in hpp

    def test_if_value_struct(self, desc: StubDescriptor) -> None:
        hpp = generate_shim_hpp(desc)
        assert "struct IfValue" in hpp
        assert "iValue" in hpp
        assert "rValue" in hpp
        assert "sValue" in hpp
        assert "numValue" in hpp
        assert "rVec" in hpp

    def test_if_parm_struct(self, desc: StubDescriptor) -> None:
        hpp = generate_shim_hpp(desc)
        assert "struct IfParm" in hpp
        assert "IF_REAL" in hpp
        assert "IF_INTEGER" in hpp
        assert "IF_STRING" in hpp
        assert "IF_FLAG" in hpp
        assert "IF_REALVEC" in hpp
        assert "IF_ASK" in hpp
        assert "IF_SET" in hpp
        assert "IF_REDUNDANT" in hpp

    def test_ckt_fields(self, desc: StubDescriptor) -> None:
        hpp = generate_shim_hpp(desc)
        for field in [
            "CKTtemp", "CKTnomTemp", "CKTgmin", "CKTreltol", "CKTabstol",
            "CKTvoltTol", "CKTmode", "CKTbadMos3", "CKTnumStates",
            "CKTnoncon", "CKTbypass", "CKTdelta", "CKTdeltaOld",
            "CKTag", "CKTorder", "CKTstate0", "CKTstate1", "CKTstate2",
            "CKTrhs", "CKTrhsOld", "CKTinternalNodeCounter",
            "add_internal_node",
        ]:
            assert field in hpp, f"Missing Ckt field: {field}"

    def test_mode_flags(self, desc: StubDescriptor) -> None:
        hpp = generate_shim_hpp(desc)
        for flag in [
            "MODETRAN", "MODEAC", "MODEDCOP", "MODETRANOP",
            "MODEDCTRANCURVE", "MODEINITFLOAT", "MODEINITJCT",
            "MODEINITFIX", "MODEINITSMSIG", "MODEINITTRAN",
            "MODEINITPRED", "MODEUIC", "MODEBYPASS",
        ]:
            assert flag in hpp, f"Missing mode flag: {flag}"

    def test_matrix_class(self, desc: StubDescriptor) -> None:
        hpp = generate_shim_hpp(desc)
        assert "make_elt" in hpp
        assert "resolve_offsets" in hpp
        assert "clear" in hpp
        assert "reservation_journal" in hpp

    def test_report_error(self, desc: StubDescriptor) -> None:
        hpp = generate_shim_hpp(desc)
        assert "report_error" in hpp
        assert "ERR_WARNING" in hpp
        assert "ERR_FATAL" in hpp

    def test_free_tmalloc(self, desc: StubDescriptor) -> None:
        hpp = generate_shim_hpp(desc)
        assert "FREE" in hpp
        assert "tmalloc" in hpp

    def test_has_ni_integrate_when_stateful(self, desc: StubDescriptor) -> None:
        desc.state_count = 29
        hpp = generate_shim_hpp(desc)
        assert "NIintegrate" in hpp

    def test_no_ni_integrate_when_stateless(self, desc: StubDescriptor) -> None:
        desc.state_count = 0
        hpp = generate_shim_hpp(desc)
        assert "NIintegrate" not in hpp

    def test_has_internal_nodes(self, desc: StubDescriptor) -> None:
        desc.has_internal_nodes = True
        hpp = generate_shim_hpp(desc)
        assert "node_alloc" in hpp
        assert "add_internal_node" in hpp

    def test_no_functional_without_internal_nodes(self, desc: StubDescriptor) -> None:
        desc.has_internal_nodes = False
        hpp = generate_shim_hpp(desc)
        assert "<functional>" not in hpp

    def test_functional_with_internal_nodes(self, desc: StubDescriptor) -> None:
        desc.has_internal_nodes = True
        hpp = generate_shim_hpp(desc)
        assert "<functional>" in hpp

    def test_no_node_alloc_without_internal_nodes(self, desc: StubDescriptor) -> None:
        desc.has_internal_nodes = False
        hpp = generate_shim_hpp(desc)
        assert "node_alloc" not in hpp

    def test_different_namespace(self) -> None:
        desc = StubDescriptor(namespace="bsim4v7", neospice_name="bsim4v7",
                              has_internal_nodes=True, state_count=29)
        hpp = generate_shim_hpp(desc)
        assert "namespace neospice::bsim4v7" in hpp
        assert "NIintegrate" in hpp
        assert "node_alloc" in hpp

    def test_closing_namespace(self, desc: StubDescriptor) -> None:
        hpp = generate_shim_hpp(desc)
        assert "} // namespace Shim" in hpp
        assert f"}} // namespace neospice::{desc.namespace}" in hpp


# ---------------------------------------------------------------------------
# Implementation tests
# ---------------------------------------------------------------------------

class TestShimCpp:
    def test_namespace(self, desc: StubDescriptor) -> None:
        cpp = generate_shim_cpp(desc)
        assert "namespace neospice::dio::Shim" in cpp
        assert "make_elt" in cpp
        assert "resolve_offsets" in cpp

    def test_include_path(self, desc: StubDescriptor) -> None:
        cpp = generate_shim_cpp(desc)
        assert '#include "devices/dio/dio_shim.hpp"' in cpp

    def test_add_internal_node_without_callback(self, desc: StubDescriptor) -> None:
        desc.has_internal_nodes = False
        cpp = generate_shim_cpp(desc)
        assert "add_internal_node" in cpp
        assert "CKTinternalNodeCounter++" in cpp
        assert "node_alloc" not in cpp

    def test_add_internal_node_with_callback(self, desc: StubDescriptor) -> None:
        desc.has_internal_nodes = True
        cpp = generate_shim_cpp(desc)
        assert "node_alloc" in cpp

    def test_ni_integrate_present_when_stateful(self, desc: StubDescriptor) -> None:
        desc.state_count = 29
        cpp = generate_shim_cpp(desc)
        assert "NIintegrate" in cpp
        assert "CKTag" in cpp
        assert "CKTorder" in cpp

    def test_ni_integrate_absent_when_stateless(self, desc: StubDescriptor) -> None:
        desc.state_count = 0
        cpp = generate_shim_cpp(desc)
        assert "NIintegrate" not in cpp

    def test_report_error(self, desc: StubDescriptor) -> None:
        cpp = generate_shim_cpp(desc)
        assert "report_error" in cpp
        assert "vfprintf" in cpp

    def test_closing_namespace(self, desc: StubDescriptor) -> None:
        cpp = generate_shim_cpp(desc)
        assert f"}} // namespace neospice::{desc.namespace}::Shim" in cpp

    def test_different_namespace(self) -> None:
        desc = StubDescriptor(namespace="bsim4v7", neospice_name="bsim4v7",
                              has_internal_nodes=True, state_count=29)
        cpp = generate_shim_cpp(desc)
        assert "namespace neospice::bsim4v7::Shim" in cpp
        assert '#include "devices/bsim4v7/bsim4v7_shim.hpp"' in cpp
        assert "NIintegrate" in cpp
        assert "node_alloc" in cpp

    def test_make_elt_ground_check(self, desc: StubDescriptor) -> None:
        cpp = generate_shim_cpp(desc)
        assert "row < 0 || col < 0" in cpp

    def test_resolve_offsets_body(self, desc: StubDescriptor) -> None:
        cpp = generate_shim_cpp(desc)
        assert "pat.offset(r, c)" in cpp
