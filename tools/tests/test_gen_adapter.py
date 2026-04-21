"""Tests for the device adapter generator (gen_adapter.py)."""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import List, Optional

import pytest

from ngspice_migrate.gen_adapter import (
    extract_output_params,
    extract_tstalloc_fields,
    generate_adapter_cpp,
    generate_adapter_hpp,
)


# ---------------------------------------------------------------------------
# Stub dataclasses matching the _Desc protocol
# ---------------------------------------------------------------------------

@dataclass
class StubTerminal:
    name: str
    field: str


@dataclass
class StubGeomParam:
    name: str
    field: str
    given: str
    default: str = ""
    always_given: bool = False


@dataclass
class StubCleanupLinkedList:
    field: str
    next_field: str


@dataclass
class StubVersionStamp:
    field: str
    given_field: str
    value: str


@dataclass
class StubDescriptor:
    prefix: str = "DIO"
    neospice_name: str = "dio"
    namespace: str = "dio"
    cpp_instance: str = "DIOInstance"
    cpp_model: str = "DIOModel"
    terminals: List[StubTerminal] = field(default_factory=lambda: [
        StubTerminal(name="pos", field="DIOposNode"),
        StubTerminal(name="neg", field="DIOnegNode"),
    ])
    state_count: int = 7
    state_base_field: str = "DIOstate"
    next_instance_field: str = "DIOnextInstance"
    instances_field: str = "DIOinstances"
    next_model_field: str = "DIOnextModel"
    model_ptr_field: str = "DIOmodPtr"
    name_field: str = "DIOname"
    setup_function: str = "DIOsetup"
    temp_function: str = "DIOtemp"
    load_function: str = "DIOload"
    geometry: List[StubGeomParam] = field(default_factory=lambda: [
        StubGeomParam(name="area", field="DIOarea", given="DIOareaGiven", default="1.0"),
    ])
    has_internal_nodes: bool = True
    cleanup_linked_lists: List[StubCleanupLinkedList] = field(default_factory=list)
    version_stamp: Optional[StubVersionStamp] = None
    matrix_ptr_suffix: str = "Ptr"


@pytest.fixture
def desc() -> StubDescriptor:
    return StubDescriptor()


# ---------------------------------------------------------------------------
# HPP tests
# ---------------------------------------------------------------------------

class TestAdapterHpp:
    def test_adapter_hpp_structure(self, desc: StubDescriptor) -> None:
        hpp = generate_adapter_hpp(desc)
        assert "#pragma once" in hpp
        assert "class DIODevice : public Device" in hpp
        assert "void declare_internal_nodes(Circuit& ckt) override;" in hpp
        assert "int32_t state_vars() const override { return 7;" in hpp
        assert "struct Geom {" in hpp

    def test_adapter_hpp_terminal_params(self, desc: StubDescriptor) -> None:
        hpp = generate_adapter_hpp(desc)
        assert "int32_t n_pos" in hpp
        assert "int32_t n_neg" in hpp

    def test_model_card_struct(self, desc: StubDescriptor) -> None:
        hpp = generate_adapter_hpp(desc)
        assert "struct DIOModelCard" in hpp
        assert "DIOModel ucb{};" in hpp

    def test_pragma_once_is_first_line(self, desc: StubDescriptor) -> None:
        hpp = generate_adapter_hpp(desc)
        assert hpp.startswith("#pragma once\n")

    def test_includes(self, desc: StubDescriptor) -> None:
        hpp = generate_adapter_hpp(desc)
        assert '#include "devices/device.hpp"' in hpp
        assert '#include "devices/dio/dio_def.hpp"' in hpp
        assert '#include "devices/dio/dio_shim.hpp"' in hpp
        assert "#include <memory>" in hpp
        assert "#include <utility>" in hpp
        assert "#include <vector>" in hpp

    def test_namespace(self, desc: StubDescriptor) -> None:
        hpp = generate_adapter_hpp(desc)
        assert "namespace neospice {" in hpp
        assert "} // namespace neospice" in hpp

    def test_model_card_non_copyable(self, desc: StubDescriptor) -> None:
        hpp = generate_adapter_hpp(desc)
        assert "DIOModelCard(const DIOModelCard&)            = delete;" in hpp
        assert "DIOModelCard& operator=(const DIOModelCard&) = delete;" in hpp

    def test_model_card_destructor(self, desc: StubDescriptor) -> None:
        hpp = generate_adapter_hpp(desc)
        assert "~DIOModelCard();" in hpp

    def test_factory_method(self, desc: StubDescriptor) -> None:
        hpp = generate_adapter_hpp(desc)
        assert "static std::unique_ptr<DIODevice> make(" in hpp
        assert "DIOModelCard& shared_card" in hpp

    def test_device_interface_methods(self, desc: StubDescriptor) -> None:
        hpp = generate_adapter_hpp(desc)
        assert "void stamp_pattern(SparsityBuilder& builder) const override;" in hpp
        assert "void assign_offsets(const SparsityPattern& pattern) override;" in hpp
        assert "void evaluate(const std::vector<double>& voltages," in hpp
        assert "void set_state_ptrs(double* s0, double* s1, double* s2, int32_t base) override;" in hpp
        assert "void reset_temp() override { temp_done_ = false; }" in hpp

    def test_private_members(self, desc: StubDescriptor) -> None:
        hpp = generate_adapter_hpp(desc)
        assert "mutable dio::DIOInstance inst_{};" in hpp
        assert "dio::DIOModel* model_ = nullptr;" in hpp
        assert "std::vector<std::pair<int,int>> journal_;" in hpp
        assert "double* state0_ = nullptr;" in hpp
        assert "double* state1_ = nullptr;" in hpp
        assert "double* state2_ = nullptr;" in hpp
        assert "int32_t state_base_ = -1;" in hpp
        assert "mutable bool temp_done_ = false;" in hpp
        assert "int32_t max_neo_node_ = -1;" in hpp
        assert "std::vector<double> ghost_voltages_;" in hpp
        assert "std::vector<double> ghost_rhs_;" in hpp

    def test_geom_struct_fields(self, desc: StubDescriptor) -> None:
        hpp = generate_adapter_hpp(desc)
        assert "double area = 1.0;" in hpp

    def test_explicit_constructor(self, desc: StubDescriptor) -> None:
        hpp = generate_adapter_hpp(desc)
        assert "explicit DIODevice(std::string name) : Device(std::move(name)) {}" in hpp

    def test_different_prefix(self) -> None:
        desc = StubDescriptor(
            prefix="BJT",
            neospice_name="bjt",
            namespace="bjt",
            cpp_instance="BJTInstance",
            cpp_model="BJTModel",
            terminals=[
                StubTerminal(name="col", field="BJTcolNode"),
                StubTerminal(name="base", field="BJTbaseNode"),
                StubTerminal(name="emit", field="BJTemitNode"),
            ],
            state_count=12,
            state_base_field="BJTstate",
            next_instance_field="BJTnextInstance",
            instances_field="BJTinstances",
            next_model_field="BJTnextModel",
            model_ptr_field="BJTmodPtr",
            name_field="BJTname",
            setup_function="BJTsetup",
            temp_function="BJTtemp",
            load_function="BJTload",
            geometry=[],
            has_internal_nodes=False,
        )
        hpp = generate_adapter_hpp(desc)
        assert "class BJTDevice : public Device" in hpp
        assert "struct BJTModelCard" in hpp
        assert "BJTModel ucb{};" in hpp
        assert "int32_t state_vars() const override { return 12;" in hpp
        assert "int32_t n_col" in hpp
        assert "int32_t n_base" in hpp
        assert "int32_t n_emit" in hpp
        assert '#include "devices/bjt/bjt_def.hpp"' in hpp


# ---------------------------------------------------------------------------
# CPP tests
# ---------------------------------------------------------------------------

class TestAdapterCpp:
    def test_adapter_cpp_has_evaluate(self, desc: StubDescriptor) -> None:
        cpp = generate_adapter_cpp(desc)
        assert "DIODevice::evaluate" in cpp
        assert "ghost_rhs_" in cpp
        assert "DIOload" in cpp

    def test_adapter_cpp_has_splicing(self, desc: StubDescriptor) -> None:
        cpp = generate_adapter_cpp(desc)
        assert "DIOnextInstance" in cpp
        assert "DIOinstances" in cpp
        assert "DIOnextModel" in cpp

    def test_adapter_cpp_has_node_wiring(self, desc: StubDescriptor) -> None:
        cpp = generate_adapter_cpp(desc)
        assert "neo_to_ucb" in cpp
        assert "DIOposNode" in cpp

    def test_include_paths(self, desc: StubDescriptor) -> None:
        cpp = generate_adapter_cpp(desc)
        assert '#include "devices/dio/dio_device.hpp"' in cpp
        assert '#include "core/circuit.hpp"' in cpp
        assert '#include "core/types.hpp"' in cpp

    def test_namespace(self, desc: StubDescriptor) -> None:
        cpp = generate_adapter_cpp(desc)
        assert "namespace neospice {" in cpp
        assert "} // namespace neospice" in cpp
        assert "using namespace neospice::dio;" in cpp

    def test_forward_declaration(self, desc: StubDescriptor) -> None:
        cpp = generate_adapter_cpp(desc)
        assert "int DIOsetup(Shim::Matrix*, DIOModel*, Shim::Ckt*, int*);" in cpp
        assert "int DIOtemp(DIOModel*, Shim::Ckt*);" in cpp
        assert "int DIOload(DIOModel*, Shim::Ckt*);" in cpp

    def test_model_card_destructor(self, desc: StubDescriptor) -> None:
        cpp = generate_adapter_cpp(desc)
        assert "DIOModelCard::~DIOModelCard() = default;" in cpp

    def test_neo_to_ucb(self, desc: StubDescriptor) -> None:
        cpp = generate_adapter_cpp(desc)
        assert "static inline int neo_to_ucb(int32_t neo)" in cpp
        assert "(neo < 0) ? 0 : (neo + 1)" in cpp

    def test_make_factory(self, desc: StubDescriptor) -> None:
        cpp = generate_adapter_cpp(desc)
        assert "DIODevice::make(std::string name," in cpp
        assert "inst.DIOname = dev->name().c_str();" in cpp
        assert "inst.DIOmodPtr = dev->model_;" in cpp
        assert "inst.DIOposNode = neo_to_ucb(n_pos);" in cpp
        assert "inst.DIOnegNode = neo_to_ucb(n_neg);" in cpp

    def test_geometry_wiring(self, desc: StubDescriptor) -> None:
        cpp = generate_adapter_cpp(desc)
        assert "inst.DIOarea = geom.area;" in cpp
        assert "inst.DIOareaGiven" in cpp

    def test_instance_list_threading(self, desc: StubDescriptor) -> None:
        cpp = generate_adapter_cpp(desc)
        assert "inst.DIOnextInstance = shared_card.ucb.DIOinstances;" in cpp
        assert "shared_card.ucb.DIOinstances = &inst;" in cpp

    def test_declare_internal_nodes(self, desc: StubDescriptor) -> None:
        cpp = generate_adapter_cpp(desc)
        assert "DIODevice::declare_internal_nodes" in cpp
        assert "DIOsetup" in cpp
        assert "SparsityBuilder scratch(1);" in cpp
        assert "Shim::Matrix shim_matrix(scratch);" in cpp

    def test_internal_node_callback(self, desc: StubDescriptor) -> None:
        cpp = generate_adapter_cpp(desc)
        assert "node_alloc" in cpp
        assert "ckt.node(full)" in cpp

    def test_no_node_alloc_without_internal_nodes(self) -> None:
        desc = StubDescriptor(has_internal_nodes=False)
        cpp = generate_adapter_cpp(desc)
        assert "node_alloc" not in cpp

    def test_stamp_pattern(self, desc: StubDescriptor) -> None:
        cpp = generate_adapter_cpp(desc)
        assert "DIODevice::stamp_pattern" in cpp
        assert "builder.add(r - 1, c - 1);" in cpp

    def test_assign_offsets(self, desc: StubDescriptor) -> None:
        cpp = generate_adapter_cpp(desc)
        assert "DIODevice::assign_offsets" in cpp
        assert "#define RESOLVE(f)" in cpp
        assert "#undef RESOLVE" in cpp
        assert "pattern.offset(r - 1, c - 1)" in cpp

    def test_set_state_ptrs(self, desc: StubDescriptor) -> None:
        cpp = generate_adapter_cpp(desc)
        assert "DIODevice::set_state_ptrs" in cpp
        assert "inst_.DIOstate = base;" in cpp

    def test_evaluate_ghost_arrays(self, desc: StubDescriptor) -> None:
        cpp = generate_adapter_cpp(desc)
        assert "ghost_voltages_.assign(n_ghost, 0.0);" in cpp
        assert "ghost_rhs_.assign(n_ghost, 0.0);" in cpp

    def test_evaluate_integrator_context(self, desc: StubDescriptor) -> None:
        cpp = generate_adapter_cpp(desc)
        assert "const IntegratorCtx* ic = tls_integrator_ctx;" in cpp
        assert "ic->mode" in cpp
        assert "ic->order" in cpp

    def test_evaluate_sim_options(self, desc: StubDescriptor) -> None:
        cpp = generate_adapter_cpp(desc)
        assert "const SimOptions* sim_opts" in cpp
        assert "sim_opts->temp" in cpp
        assert "sim_opts->gmin" in cpp
        assert "sim_opts->reltol" in cpp
        assert "sim_opts->abstol" in cpp
        assert "sim_opts->vntol" in cpp

    def test_evaluate_temp_call(self, desc: StubDescriptor) -> None:
        cpp = generate_adapter_cpp(desc)
        assert "DIOtemp(model_, &ckt)" in cpp
        assert "temp_done_ = true;" in cpp

    def test_evaluate_chain_splicing(self, desc: StubDescriptor) -> None:
        cpp = generate_adapter_cpp(desc)
        # Check that we save, splice, call, and restore
        assert "saved_head" in cpp
        assert "saved_next_inst" in cpp
        assert "saved_next_mod" in cpp
        assert "model_->DIOinstances  = &inst_;" in cpp
        assert "inst_.DIOnextInstance = nullptr;" in cpp
        assert "model_->DIOnextModel  = nullptr;" in cpp
        # Restore
        assert "model_->DIOinstances  = saved_head;" in cpp
        assert "inst_.DIOnextInstance = saved_next_inst;" in cpp
        assert "model_->DIOnextModel  = saved_next_mod;" in cpp

    def test_evaluate_fold_ghost_rhs(self, desc: StubDescriptor) -> None:
        cpp = generate_adapter_cpp(desc)
        assert "rhs[k - 1] += ghost_rhs_[k];" in cpp

    def test_max_neo_node_computation(self, desc: StubDescriptor) -> None:
        cpp = generate_adapter_cpp(desc)
        assert "int32_t widest = -1;" in cpp
        assert "dev->max_neo_node_ = widest;" in cpp

    def test_different_prefix(self) -> None:
        desc = StubDescriptor(
            prefix="BJT",
            neospice_name="bjt",
            namespace="bjt",
            cpp_instance="BJTInstance",
            cpp_model="BJTModel",
            terminals=[
                StubTerminal(name="col", field="BJTcolNode"),
                StubTerminal(name="base", field="BJTbaseNode"),
                StubTerminal(name="emit", field="BJTemitNode"),
            ],
            state_count=12,
            state_base_field="BJTstate",
            next_instance_field="BJTnextInstance",
            instances_field="BJTinstances",
            next_model_field="BJTnextModel",
            model_ptr_field="BJTmodPtr",
            name_field="BJTname",
            setup_function="BJTsetup",
            temp_function="BJTtemp",
            load_function="BJTload",
            geometry=[],
            has_internal_nodes=True,
        )
        cpp = generate_adapter_cpp(desc)
        assert "BJTDevice::evaluate" in cpp
        assert "BJTDevice::make" in cpp
        assert "BJTload" in cpp
        assert "BJTtemp" in cpp
        assert "BJTsetup" in cpp
        assert "BJTcolNode" in cpp
        assert "BJTnextInstance" in cpp
        assert "BJTinstances" in cpp
        assert "BJTnextModel" in cpp


# ---------------------------------------------------------------------------
# TSTALLOC extraction tests
# ---------------------------------------------------------------------------

class TestExtractTstallocFields:
    def test_extracts_field_names(self) -> None:
        setup_src = """
#define TSTALLOC(ptr,first,second) \\
    do { (here)->ptr = mat.make_elt(here->first, here->second); } while(0)

    TSTALLOC(DIOposPosPtr, DIOposNode, DIOposNode)
    TSTALLOC(DIOposNegPtr, DIOposNode, DIOnegNode)
    TSTALLOC(DIOnegPosPtr, DIOnegNode, DIOposNode)
    TSTALLOC(DIOnegNegPtr, DIOnegNode, DIOnegNode)
"""
        fields = extract_tstalloc_fields(setup_src)
        assert fields == [
            "DIOposPosPtr", "DIOposNegPtr",
            "DIOnegPosPtr", "DIOnegNegPtr",
        ]

    def test_skips_define_line(self) -> None:
        setup_src = '#define TSTALLOC(ptr,first,second) stuff'
        fields = extract_tstalloc_fields(setup_src)
        assert fields == []

    def test_deduplicates(self) -> None:
        setup_src = """
    TSTALLOC(DIOposPosPtr, DIOposNode, DIOposNode)
    TSTALLOC(DIOposPosPtr, DIOposNode, DIOposNode)
"""
        fields = extract_tstalloc_fields(setup_src)
        assert fields == ["DIOposPosPtr"]

    def test_empty_source(self) -> None:
        assert extract_tstalloc_fields("") == []

    def test_custom_suffix(self) -> None:
        setup_src = "    TSTALLOC(FooBarBinding, FooNode, BarNode)\n"
        assert extract_tstalloc_fields(setup_src, "Binding") == ["FooBarBinding"]
        assert extract_tstalloc_fields(setup_src, "Ptr") == []


# ---------------------------------------------------------------------------
# RESOLVE generation tests
# ---------------------------------------------------------------------------

class TestResolveGeneration:
    def test_resolve_calls_from_setup_source(self) -> None:
        desc = StubDescriptor()
        setup_src = """
    TSTALLOC(DIOposPosPtr, DIOposNode, DIOposNode)
    TSTALLOC(DIOnegNegPtr, DIOnegNode, DIOnegNode)
"""
        cpp = generate_adapter_cpp(desc, setup_source=setup_src)
        assert "RESOLVE(DIOposPosPtr);" in cpp
        assert "RESOLVE(DIOnegNegPtr);" in cpp
        assert "// TODO: add RESOLVE()" not in cpp

    def test_todo_when_no_setup_source(self) -> None:
        desc = StubDescriptor()
        cpp = generate_adapter_cpp(desc, setup_source="")
        assert "TODO" in cpp

    def test_todo_when_setup_has_no_tstalloc(self) -> None:
        desc = StubDescriptor()
        cpp = generate_adapter_cpp(desc, setup_source="int main() { return 0; }")
        assert "TODO" in cpp


# ---------------------------------------------------------------------------
# Destructor cleanup tests
# ---------------------------------------------------------------------------

class TestDestructorCleanup:
    def test_default_destructor_no_cleanup(self, desc: StubDescriptor) -> None:
        cpp = generate_adapter_cpp(desc)
        assert "DIOModelCard::~DIOModelCard() = default;" in cpp
        assert "std::free" not in cpp
        assert "#include <cstdlib>" not in cpp

    def test_destructor_with_cleanup(self) -> None:
        desc = StubDescriptor(
            cleanup_linked_lists=[
                StubCleanupLinkedList(field="pKnot", next_field="pNext"),
            ],
        )
        cpp = generate_adapter_cpp(desc)
        assert "DIOModelCard::~DIOModelCard() {" in cpp
        assert "auto* p = ucb.pKnot;" in cpp
        assert "auto* next = p->pNext;" in cpp
        assert "std::free(p);" in cpp
        assert "ucb.pKnot = nullptr;" in cpp
        assert "#include <cstdlib>" in cpp

    def test_destructor_with_multiple_cleanup(self) -> None:
        desc = StubDescriptor(
            cleanup_linked_lists=[
                StubCleanupLinkedList(field="listA", next_field="nextA"),
                StubCleanupLinkedList(field="listB", next_field="nextB"),
            ],
        )
        cpp = generate_adapter_cpp(desc)
        assert "ucb.listA = nullptr;" in cpp
        assert "ucb.listB = nullptr;" in cpp

    def test_hpp_move_deleted_with_cleanup(self) -> None:
        desc = StubDescriptor(
            cleanup_linked_lists=[
                StubCleanupLinkedList(field="pKnot", next_field="pNext"),
            ],
        )
        hpp = generate_adapter_hpp(desc)
        assert "DIOModelCard(DIOModelCard&&) noexcept = delete;" in hpp

    def test_hpp_no_move_delete_without_cleanup(self, desc: StubDescriptor) -> None:
        hpp = generate_adapter_hpp(desc)
        assert "DIOModelCard(DIOModelCard&&)" not in hpp


# ---------------------------------------------------------------------------
# Version stamp tests
# ---------------------------------------------------------------------------

class TestVersionStamp:
    def test_no_version_stamp(self, desc: StubDescriptor) -> None:
        cpp = generate_adapter_cpp(desc)
        assert "versionGiven" not in cpp

    def test_version_stamp_generated(self) -> None:
        desc = StubDescriptor(
            version_stamp=StubVersionStamp(
                field="DIOversion",
                given_field="DIOversionGiven",
                value="1.2.3",
            ),
        )
        cpp = generate_adapter_cpp(desc)
        assert 'shared_card.ucb.DIOversion = "1.2.3"' in cpp
        assert "shared_card.ucb.DIOversionGiven = 1;" in cpp
        assert "if (!shared_card.ucb.DIOversionGiven)" in cpp


# ---------------------------------------------------------------------------
# Geometry always_given tests
# ---------------------------------------------------------------------------

class TestGeometryGiven:
    def test_always_given_sets_1(self) -> None:
        desc = StubDescriptor(
            geometry=[
                StubGeomParam(name="W", field="W", given="WGiven",
                              default="1e-6", always_given=True),
            ],
        )
        cpp = generate_adapter_cpp(desc)
        assert "inst.WGiven = 1;" in cpp
        assert "!= 1e-6" not in cpp

    def test_conditional_given_compares_default(self) -> None:
        desc = StubDescriptor(
            geometry=[
                StubGeomParam(name="AD", field="AD", given="ADGiven",
                              default="0.0", always_given=False),
            ],
        )
        cpp = generate_adapter_cpp(desc)
        assert "inst.ADGiven = (geom.AD != 0.0) ? 1 : 0;" in cpp


# ---------------------------------------------------------------------------
# extract_output_params tests
# ---------------------------------------------------------------------------

class TestExtractOutputParams:
    def test_extract_output_params(self) -> None:
        """Extract output parameter names and fields from devsup pTable."""
        devsup_source = '''
Shim::IfParm DIOmPTable[] = {
 {"is",  DIO_IS,  IF_REAL, "model saturation current"},
};
Shim::IfParm DIOpTable[] = {
 IOP("area",   DIO_AREA,   IF_REAL, "Area factor"),
 OP("vd",      DIO_VOLTAGE, IF_REAL, "Diode voltage"),
 OPR("id",     DIO_CURRENT, IF_REAL, "Diode current"),
 OPR("gd",     DIO_CONDUCT, IF_REAL, "Diode conductance"),
};
'''
        params = extract_output_params(devsup_source, "DIO")
        names = [p['name'] for p in params]
        assert "vd" in names
        assert "id" in names
        assert "gd" in names
        assert "area" in names

    def test_empty_source(self) -> None:
        params = extract_output_params("", "DIO")
        assert params == []

    def test_is_output_flags(self) -> None:
        devsup_source = '''
 IOP("area",   DIO_AREA,   IF_REAL, "Area factor"),
 OP("vd",      DIO_VOLTAGE, IF_REAL, "Diode voltage"),
 OPR("id",     DIO_CURRENT, IF_REAL, "Diode current"),
'''
        params = extract_output_params(devsup_source, "DIO")
        by_name = {p['name']: p for p in params}
        assert by_name["area"]["is_output"] is False   # IOP is input-or-output (geometry)
        assert by_name["vd"]["is_output"] is True      # OP is pure output
        assert by_name["id"]["is_output"] is True      # OPR is pure output


# ---------------------------------------------------------------------------
# query_param generation tests
# ---------------------------------------------------------------------------

class TestQueryParamGeneration:
    def test_query_param_stub_without_devsup(self, desc: StubDescriptor) -> None:
        """Without devsup_source, query_param should have a TODO comment."""
        cpp = generate_adapter_cpp(desc)
        assert "query_param" in cpp
        assert "std::nullopt" in cpp

    def test_query_param_with_devsup(self, desc: StubDescriptor) -> None:
        """With devsup_source, query_param should have parameter mappings."""
        devsup_source = '''
Shim::IfParm DIOpTable[] = {
 IOP("area",   DIO_AREA,   IF_REAL, "Area factor"),
 OP("vd",      DIO_VOLTAGE, IF_REAL, "Diode voltage"),
 OPR("id",     DIO_CURRENT, IF_REAL, "Diode current"),
};
'''
        cpp = generate_adapter_cpp(desc, devsup_source=devsup_source)
        assert 'key == "vd"' in cpp
        assert 'key == "id"' in cpp
        assert 'key == "area"' in cpp
        assert "str_tolower" in cpp
        assert "std::nullopt" in cpp

    def test_query_param_output_vs_geometry(self, desc: StubDescriptor) -> None:
        """OP/OPR entries get TODO markers; IOP entries get direct field refs."""
        devsup_source = '''
 IOP("area",   DIO_AREA,   IF_REAL, "Area factor"),
 OP("vd",      DIO_VOLTAGE, IF_REAL, "Diode voltage"),
'''
        cpp = generate_adapter_cpp(desc, devsup_source=devsup_source)
        # OP entries should have TODO for field mapping
        assert 'TODO: map DIO_VOLTAGE' in cpp
        # IOP entries should reference inst_ field
        assert 'inst_.DIOarea' in cpp
