"""Unit tests for the YAML model descriptor loader."""

from __future__ import annotations

import textwrap
from pathlib import Path

import pytest
import yaml

from ngspice_migrate.descriptor import (
    GeomParam,
    ModelDescriptor,
    Terminal,
    load_descriptor,
)
from ngspice_migrate.transformer import TransformerConfig

# ---------------------------------------------------------------------------
# Fixtures
# ---------------------------------------------------------------------------

FIXTURES = Path(__file__).resolve().parent.parent / "descriptors"


# ---------------------------------------------------------------------------
# test_load_bsim4v7_descriptor
# ---------------------------------------------------------------------------


def test_load_bsim4v7_descriptor():
    desc = load_descriptor(FIXTURES / "bsim4v7.yaml")
    assert isinstance(desc, ModelDescriptor)
    assert desc.prefix == "BSIM4v7"
    assert desc.neospice_name == "bsim4v7"
    assert len(desc.terminals) == 4
    assert desc.terminals[0].name == "d"
    assert desc.state_count == 29
    assert "setup" in desc.source_files
    assert desc.source_files["setup"] == "b4v7set.c"


# ---------------------------------------------------------------------------
# test_descriptor_builds_transformer_config
# ---------------------------------------------------------------------------


def test_descriptor_builds_transformer_config():
    desc = load_descriptor(FIXTURES / "bsim4v7.yaml")
    cfg = desc.to_transformer_config()
    assert isinstance(cfg, TransformerConfig)
    assert cfg.instance_struct == "BSIM4v7instance"
    assert cfg.cpp_instance == "BSIM4v7Instance"
    assert cfg.namespace == "bsim4v7"
    assert cfg.prefix == "BSIM4v7"
    assert cfg.defines == ["PREDICTOR"]
    assert cfg.model_struct == "BSIM4v7model"
    assert cfg.instance_tag == "sBSIM4v7instance"
    assert cfg.model_tag == "sBSIM4v7model"
    assert cfg.cpp_model == "BSIM4v7Model"
    assert cfg.gen_instance == "GENinstance"
    assert cfg.gen_model == "GENmodel"
    # stamp_field_pattern should be auto-derived from prefix + suffix
    assert "BSIM4v7" in cfg.stamp_field_pattern
    assert "Ptr" in cfg.stamp_field_pattern


# ---------------------------------------------------------------------------
# test_missing_required_field
# ---------------------------------------------------------------------------


def test_missing_required_field(tmp_path):
    """Loading a YAML missing 'neospice_name' raises ValueError."""
    incomplete = {
        "model": {
            "ngspice_prefix": "FOO",
            # neospice_name is intentionally missing
            "terminals": [{"name": "a", "field": "FOOaNode"}],
            "source_files": {"setup": "foo_set.c"},
            "state_count": 4,
            "state_base_field": "FOOstates",
        }
    }
    p = tmp_path / "incomplete.yaml"
    p.write_text(yaml.dump(incomplete))
    with pytest.raises(ValueError, match="neospice_name"):
        load_descriptor(p)


# ---------------------------------------------------------------------------
# test_missing_model_key
# ---------------------------------------------------------------------------


def test_missing_model_key(tmp_path):
    """Loading a YAML with no top-level 'model' key raises ValueError."""
    p = tmp_path / "bad.yaml"
    p.write_text(yaml.dump({"ngspice_prefix": "X"}))
    with pytest.raises(ValueError, match="model"):
        load_descriptor(p)


# ---------------------------------------------------------------------------
# test_defaults_for_optional_fields
# ---------------------------------------------------------------------------


def test_defaults_for_optional_fields(tmp_path):
    """Optional fields get sensible defaults when omitted from YAML."""
    minimal = {
        "model": {
            "ngspice_prefix": "DIO",
            "neospice_name": "dio",
            "terminals": [{"name": "p", "field": "DIOpNode"}],
            "source_files": {"setup": "dioset.c"},
            "state_count": 5,
            "state_base_field": "DIOstates",
        }
    }
    p = tmp_path / "minimal.yaml"
    p.write_text(yaml.dump(minimal))
    desc = load_descriptor(p)
    # namespace defaults to neospice_name
    assert desc.namespace == "dio"
    # struct names default to prefix-based patterns
    assert desc.instance_struct == "DIOinstance"
    assert desc.model_struct == "DIOmodel"
    assert desc.instance_tag == "sDIOinstance"
    assert desc.model_tag == "sDIOmodel"
    assert desc.cpp_instance == "DIOInstance"
    assert desc.cpp_model == "DIOModel"
    assert desc.gen_instance == "GENinstance"
    assert desc.gen_model == "GENmodel"
    # boolean / list defaults
    assert desc.has_internal_nodes is False
    assert desc.skip_files == []
    assert desc.defines == []
    assert desc.geometry == []
    assert desc.matrix_ptr_suffix == "Ptr"
    assert desc.setup_function == ""


# ---------------------------------------------------------------------------
# test_bsim4v7_full_fields
# ---------------------------------------------------------------------------


def test_bsim4v7_full_fields():
    """Verify all fields of the BSIM4v7 descriptor are populated correctly."""
    desc = load_descriptor(FIXTURES / "bsim4v7.yaml")
    # terminals
    assert all(isinstance(t, Terminal) for t in desc.terminals)
    assert [t.name for t in desc.terminals] == ["d", "g", "s", "b"]
    assert desc.terminals[1].field == "BSIM4v7gNodeExt"
    # linked-list fields
    assert desc.next_instance_field == "BSIM4v7nextInstance"
    assert desc.instances_field == "BSIM4v7instances"
    assert desc.next_model_field == "BSIM4v7nextModel"
    assert desc.model_ptr_field == "BSIM4v7modPtr"
    assert desc.name_field == "BSIM4v7name"
    # skip files
    assert "b4v7acld.c" in desc.skip_files
    assert len(desc.skip_files) == 12
    # defines
    assert desc.defines == ["PREDICTOR"]
    # feature flags
    assert desc.has_internal_nodes is True
    # functions
    assert desc.setup_function == "BSIM4setup"
    assert desc.temp_function == "BSIM4temp"
    assert desc.load_function == "BSIM4load"
    # geometry
    assert len(desc.geometry) == 3
    assert all(isinstance(g, GeomParam) for g in desc.geometry)
    assert desc.geometry[0].name == "W"
    assert desc.geometry[0].default == "1e-6"
    assert desc.geometry[2].name == "NF"
    assert desc.geometry[2].given == "BSIM4v7nfGiven"
