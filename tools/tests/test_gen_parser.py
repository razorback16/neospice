"""Tests for the parser integration helper generator (gen_parser.py)."""

from __future__ import annotations

from dataclasses import dataclass

from ngspice_migrate.gen_parser import generate_parser_hpp
from tests.test_gen_adapter import StubDescriptor, StubTerminal, StubGeomParam


@dataclass
class StubModelType:
    spice_name: str
    flag_field: str
    flag_value: int


def make_desc():
    desc = StubDescriptor()
    desc.model_types = [StubModelType("d", "", 0)]
    return desc


def test_parser_hpp_has_create_functions():
    hpp = generate_parser_hpp(make_desc())
    assert "create_dio_device" in hpp or "create_dio_model_card" in hpp
    assert "#pragma once" in hpp


def test_parser_hpp_has_terminal_info():
    hpp = generate_parser_hpp(make_desc())
    assert "pos" in hpp
    assert "neg" in hpp
