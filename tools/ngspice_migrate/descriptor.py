"""YAML descriptor loader for per-model migration configuration.

Each ngspice device model is described by a YAML file that lists its C
struct names, terminal list, source files, geometry parameters, etc.
:func:`load_descriptor` reads one such file and returns a validated
:class:`ModelDescriptor` dataclass which can in turn produce the
:class:`TransformerConfig` consumed by the 8-pass translator pipeline.
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Dict, List

import yaml

from ngspice_migrate.transformer import TransformerConfig


# ---------------------------------------------------------------------------
# Supporting dataclasses
# ---------------------------------------------------------------------------

@dataclass
class Terminal:
    """One external terminal of the device (e.g. drain, gate, source, bulk)."""

    name: str
    field: str


@dataclass
class CleanupLinkedList:
    """A malloc'd linked list in the model struct that needs freeing."""

    field: str       # e.g. "pSizeDependParamKnot"
    next_field: str  # e.g. "pNext"


@dataclass
class VersionStamp:
    """Optional version field to set in make() if not already given."""

    field: str        # e.g. "BSIM4versionGiven"
    given_field: str  # e.g. "BSIM4versionGiven"
    value: str        # e.g. "4.7.0"


@dataclass
class GeomParam:
    """One geometry parameter with an optional default value."""

    name: str
    field: str
    given: str
    default: str = ""
    always_given: bool = False


# ---------------------------------------------------------------------------
# Main descriptor
# ---------------------------------------------------------------------------

@dataclass
class ModelDescriptor:
    """Complete description of a single ngspice device model."""

    # Core identity ----------------------------------------------------------
    prefix: str
    neospice_name: str
    namespace: str

    # C struct names ---------------------------------------------------------
    instance_struct: str
    model_struct: str
    instance_tag: str
    model_tag: str

    # C++ replacement names --------------------------------------------------
    cpp_instance: str
    cpp_model: str

    # Generic base struct names ----------------------------------------------
    gen_instance: str = "GENinstance"
    gen_model: str = "GENmodel"

    # Terminals --------------------------------------------------------------
    terminals: List[Terminal] = field(default_factory=list)

    # State vector -----------------------------------------------------------
    state_count: int = 0
    state_base_field: str = ""

    # Linked-list / pointer fields -------------------------------------------
    next_instance_field: str = ""
    instances_field: str = ""
    next_model_field: str = ""
    model_ptr_field: str = ""
    name_field: str = ""

    # Source files (role -> filename) -----------------------------------------
    source_files: Dict[str, str] = field(default_factory=dict)
    skip_files: List[str] = field(default_factory=list)
    defines: List[str] = field(default_factory=list)

    # Feature flags ----------------------------------------------------------
    has_internal_nodes: bool = False

    # Key function names -----------------------------------------------------
    setup_function: str = ""
    temp_function: str = ""
    load_function: str = ""

    # Geometry parameters ----------------------------------------------------
    geometry: List[GeomParam] = field(default_factory=list)

    # Matrix stamp suffix ----------------------------------------------------
    matrix_ptr_suffix: str = "Ptr"

    # Destructor cleanup — linked lists in model struct to free --------------
    cleanup_linked_lists: List[CleanupLinkedList] = field(default_factory=list)

    # Version stamp — set in make() if not already given ---------------------
    version_stamp: VersionStamp | None = None

    # -----------------------------------------------------------------------
    # Conversion to TransformerConfig
    # -----------------------------------------------------------------------

    def to_transformer_config(self) -> TransformerConfig:
        """Build a :class:`TransformerConfig` suitable for the 8-pass pipeline."""
        return TransformerConfig(
            instance_struct=self.instance_struct,
            model_struct=self.model_struct,
            instance_tag=self.instance_tag,
            model_tag=self.model_tag,
            cpp_instance=self.cpp_instance,
            cpp_model=self.cpp_model,
            gen_instance=self.gen_instance,
            gen_model=self.gen_model,
            prefix=self.prefix,
            namespace=self.namespace,
            defines=list(self.defines),
            stamp_field_pattern=rf"{re.escape(self.prefix)}[A-Za-z0-9_]+{re.escape(self.matrix_ptr_suffix)}",
        )


# ---------------------------------------------------------------------------
# YAML loader
# ---------------------------------------------------------------------------

_REQUIRED_FIELDS = frozenset({
    "ngspice_prefix",
    "neospice_name",
    "terminals",
    "source_files",
    "state_count",
    "state_base_field",
})


def load_descriptor(path: Path) -> ModelDescriptor:
    """Read a YAML descriptor file and return a validated :class:`ModelDescriptor`.

    Parameters
    ----------
    path:
        Path to the YAML file.  The file must contain a top-level ``model``
        mapping with at least the keys listed in :data:`_REQUIRED_FIELDS`.

    Raises
    ------
    ValueError
        If any required field is missing from the YAML.
    """
    with open(path, "r") as fh:
        raw: Dict[str, Any] = yaml.safe_load(fh)

    if "model" not in raw:
        raise ValueError(f"YAML file {path} missing top-level 'model' key")
    m: Dict[str, Any] = raw["model"]

    # Validate required fields
    missing = _REQUIRED_FIELDS - set(m.keys())
    if missing:
        raise ValueError(
            f"YAML descriptor {path} is missing required field(s): "
            + ", ".join(sorted(missing))
        )

    prefix = m["ngspice_prefix"]
    neospice_name = m["neospice_name"]
    namespace = m.get("neospice_namespace", neospice_name)

    terminals = [
        Terminal(name=t["name"], field=t["field"]) for t in m["terminals"]
    ]

    geometry = [
        GeomParam(
            name=g["name"],
            field=g["field"],
            given=g["given"],
            default=str(g.get("default", "")),
            always_given=bool(g.get("always_given", False)),
        )
        for g in m.get("geometry", [])
    ]

    cleanup_linked_lists = [
        CleanupLinkedList(field=c["field"], next_field=c["next_field"])
        for c in m.get("cleanup_linked_lists", [])
    ]

    version_stamp_raw = m.get("version_stamp")
    version_stamp = None
    if version_stamp_raw:
        version_stamp = VersionStamp(
            field=version_stamp_raw["field"],
            given_field=version_stamp_raw["given_field"],
            value=str(version_stamp_raw["value"]),
        )

    return ModelDescriptor(
        prefix=prefix,
        neospice_name=neospice_name,
        namespace=namespace,
        instance_struct=m.get("instance_struct", f"{prefix}instance"),
        model_struct=m.get("model_struct", f"{prefix}model"),
        instance_tag=m.get("instance_tag", f"s{prefix}instance"),
        model_tag=m.get("model_tag", f"s{prefix}model"),
        cpp_instance=m.get("cpp_instance", f"{prefix}Instance"),
        cpp_model=m.get("cpp_model", f"{prefix}Model"),
        gen_instance=m.get("gen_instance", "GENinstance"),
        gen_model=m.get("gen_model", "GENmodel"),
        terminals=terminals,
        state_count=int(m["state_count"]),
        state_base_field=m["state_base_field"],
        next_instance_field=m.get("next_instance_field", ""),
        instances_field=m.get("instances_field", ""),
        next_model_field=m.get("next_model_field", ""),
        model_ptr_field=m.get("model_ptr_field", ""),
        name_field=m.get("name_field", ""),
        source_files=dict(m["source_files"]),
        skip_files=list(m.get("skip_files", [])),
        defines=list(m.get("defines", [])),
        has_internal_nodes=bool(m.get("has_internal_nodes", False)),
        setup_function=m.get("setup_function", ""),
        temp_function=m.get("temp_function", ""),
        load_function=m.get("load_function", ""),
        geometry=geometry,
        matrix_ptr_suffix=m.get("matrix_ptr_suffix", "Ptr"),
        cleanup_linked_lists=cleanup_linked_lists,
        version_stamp=version_stamp,
    )
