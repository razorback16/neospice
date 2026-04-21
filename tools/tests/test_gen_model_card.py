"""Tests for the model card conversion generator (gen_model_card.py)."""

from __future__ import annotations

from dataclasses import dataclass

from ngspice_migrate.gen_model_card import generate_model_card_hpp, generate_model_card_cpp


@dataclass
class StubModelType:
    spice_name: str
    flag_field: str
    flag_value: int


@dataclass
class StubTerminal:
    name: str
    field: str


@dataclass
class StubDescriptor:
    prefix: str = "DIO"
    neospice_name: str = "dio"
    namespace: str = "dio"
    cpp_instance: str = "DIOInstance"
    cpp_model: str = "DIOModel"
    terminals: list = None
    model_types: list = None

    def __post_init__(self):
        if self.terminals is None:
            self.terminals = [
                StubTerminal(name="pos", field="DIOposNode"),
                StubTerminal(name="neg", field="DIOnegNode"),
            ]
        if self.model_types is None:
            self.model_types = []


def make_desc():
    desc = StubDescriptor()
    desc.model_types = [
        StubModelType(spice_name="nmos", flag_field="BSIM4v7type", flag_value=1),
        StubModelType(spice_name="pmos", flag_field="BSIM4v7type", flag_value=-1),
    ]
    return desc


def test_model_card_hpp_has_function_decl():
    hpp = generate_model_card_hpp(make_desc())
    assert "to_dio_card" in hpp
    assert "ModelCard" in hpp
    assert "#pragma once" in hpp


def test_model_card_hpp_includes():
    hpp = generate_model_card_hpp(make_desc())
    assert '#include "devices/dio/dio_device.hpp"' in hpp
    assert '#include "parser/model_cards.hpp"' in hpp
    assert "#include <memory>" in hpp


def test_model_card_hpp_namespace():
    hpp = generate_model_card_hpp(make_desc())
    assert "namespace neospice {" in hpp
    assert "} // namespace neospice" in hpp


def test_model_card_hpp_return_type():
    hpp = generate_model_card_hpp(make_desc())
    assert "std::unique_ptr<DIOModelCard>" in hpp


def test_model_card_cpp_has_type_dispatch():
    cpp = generate_model_card_cpp(make_desc())
    assert '"nmos"' in cpp
    assert '"pmos"' in cpp
    assert "mPTable" in cpp
    assert "mParam" in cpp


def test_model_card_cpp_includes():
    cpp = generate_model_card_cpp(make_desc())
    assert '#include "devices/dio/dio_model_card.hpp"' in cpp
    assert '#include "devices/dio/dio_def.hpp"' in cpp
    assert '#include "devices/dio/dio_shim.hpp"' in cpp


def test_model_card_cpp_namespace():
    cpp = generate_model_card_cpp(make_desc())
    assert "namespace neospice {" in cpp
    assert "} // namespace neospice" in cpp


def test_model_card_cpp_flag_field():
    cpp = generate_model_card_cpp(make_desc())
    assert "ucb.BSIM4v7type = 1" in cpp
    assert "ucb.BSIM4v7type = -1" in cpp
    assert "BSIM4v7typeGiven = 1" in cpp


def test_model_card_cpp_param_loop():
    cpp = generate_model_card_cpp(make_desc())
    assert "for (const auto& [lkey, val] : card.params)" in cpp
    assert 'if (lkey == "level") continue;' in cpp


def test_model_card_cpp_type_error():
    cpp = generate_model_card_cpp(make_desc())
    assert "NMOS/PMOS" in cpp
    assert "ParseError" in cpp


def test_model_card_cpp_no_types():
    """When no model_types, skip type dispatch entirely."""
    desc = StubDescriptor()
    desc.model_types = []
    cpp = generate_model_card_cpp(desc)
    assert "card.type" not in cpp
    # Should still have the param loop
    assert "mPTable" in cpp
    assert "mParam" in cpp
