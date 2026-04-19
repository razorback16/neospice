# ngspice Model Migration Tool Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a Python tool that mechanically translates any ngspice C device model into a compilable neospice C++ module, producing translated UCB source files, a shim layer, a device adapter, and a CMakeLists.txt — with zero manual editing for the common case.

**Architecture:** A model-descriptor YAML file declares per-model metadata (prefix, terminal nodes, file mappings, state vars). A generic 8-pass text transformer (generalised from `tools/bsim4_translate.py`) rewrites each `.c` source file. Code generators emit the shim, adapter, and build file from templates parameterised by the descriptor. The tool validates its own output by counting TSTALLOC/RESOLVE pairs and stamp-site rewrites.

**Tech Stack:** Python 3.10+ (stdlib only: `re`, `argparse`, `pathlib`, `yaml` from PyYAML — the one allowed dependency), Jinja2-style string templates (hand-rolled to stay stdlib-only).

---

## Terminology

| Term | Meaning |
|------|---------|
| **descriptor** | YAML file describing one ngspice model (prefix, files, terminals, etc.) |
| **transformer** | The 8-pass C→C++ text rewriter (generalised `bsim4_translate.py`) |
| **generator** | Template-driven code emitters for shim, adapter, CMakeLists, _def.hpp |
| **model prefix** | The string that prefixes all identifiers in a model: `BSIM4v7`, `DIO`, `BJT`, `MOS1`, etc. |

## File Structure

### New files (tool)

| File | Responsibility |
|------|---------------|
| `tools/ngspice_migrate/__init__.py` | Package marker |
| `tools/ngspice_migrate/__main__.py` | CLI entry point: parse args, load descriptor, orchestrate pipeline |
| `tools/ngspice_migrate/descriptor.py` | Load and validate YAML model descriptor |
| `tools/ngspice_migrate/transformer.py` | Generic 8-pass C→C++ text transformer (refactored from `bsim4_translate.py`) |
| `tools/ngspice_migrate/gen_def.py` | Parse `*def.h` → emit `*_def.hpp` |
| `tools/ngspice_migrate/gen_shim.py` | Emit shim `.hpp`/`.cpp` from template |
| `tools/ngspice_migrate/gen_adapter.py` | Emit device adapter `.hpp`/`.cpp` from template |
| `tools/ngspice_migrate/gen_cmake.py` | Emit `CMakeLists.txt` for the output module |
| `tools/ngspice_migrate/validation.py` | Post-generation checks: TSTALLOC/RESOLVE count, stamp coverage |
| `tools/ngspice_migrate/templates/` | String template fragments (shim, adapter, cmake) |
| `tools/descriptors/bsim4v7.yaml` | Descriptor for BSIM4v7 (validates tool against known-good output) |
| `tools/descriptors/dio.yaml` | Descriptor for Diode model (first new migration target) |

### Test files

| File | Responsibility |
|------|---------------|
| `tools/tests/test_transformer.py` | Unit tests for the 8-pass transformer |
| `tools/tests/test_descriptor.py` | YAML loading/validation tests |
| `tools/tests/test_gen_def.py` | Tests for def.h → def.hpp conversion |
| `tools/tests/test_gen_shim.py` | Tests for shim generation |
| `tools/tests/test_gen_adapter.py` | Tests for adapter generation |
| `tools/tests/test_validation.py` | Tests for post-generation validation |
| `tools/tests/test_bsim4v7_roundtrip.py` | Regression: generate BSIM4v7 → diff against existing hand-port |

### Existing files (reference only — not modified by tool development)

| File | Relevance |
|------|-----------|
| `tools/bsim4_translate.py` | Source of truth for transformer logic; refactored into `transformer.py` |
| `src/devices/bsim4v7/` | Reference output for BSIM4v7 roundtrip validation |
| `src/devices/device.hpp` | Device interface contract the adapter must satisfy |

---

## Model Descriptor Format

Each model gets a YAML file describing its ngspice identity and neospice mapping:

```yaml
# tools/descriptors/bsim4v7.yaml
model:
  ngspice_prefix: "BSIM4v7"          # prefix on all C identifiers
  neospice_name: "bsim4v7"           # output directory name under src/devices/
  neospice_namespace: "bsim4v7"      # C++ namespace suffix (neospice::<this>)

  # Original C struct names (before translation)
  instance_struct: "BSIM4v7instance"  # typedef in *def.h
  model_struct: "BSIM4v7model"        # typedef in *def.h
  instance_tag: "sBSIM4v7instance"    # struct tag in *def.h
  model_tag: "sBSIM4v7model"          # struct tag in *def.h

  # Translated C++ struct names
  cpp_instance: "BSIM4v7Instance"
  cpp_model: "BSIM4v7Model"

  # GEN base types (replaced by cpp_instance/cpp_model)
  gen_instance: "GENinstance"
  gen_model: "GENmodel"

  terminals:
    - { name: "d", field: "BSIM4v7dNode" }
    - { name: "g", field: "BSIM4v7gNodeExt" }
    - { name: "s", field: "BSIM4v7sNode" }
    - { name: "b", field: "BSIM4v7bNode" }

  state_count: 29                     # number of state variables per instance
  state_base_field: "BSIM4v7states"   # field name for state base offset

  # Instance chain fields (for splicing in evaluate)
  next_instance_field: "BSIM4v7nextInstance"
  instances_field: "BSIM4v7instances"
  next_model_field: "BSIM4v7nextModel"
  model_ptr_field: "BSIM4v7modPtr"
  name_field: "BSIM4v7name"

  # Matrix pointer fields — auto-extracted from def.h if omitted
  # (fields ending in "Ptr" of type MatrixOffset/double*)
  matrix_ptr_suffix: "Ptr"

  # Source files to translate (relative to ngspice model directory)
  source_files:
    setup: "b4v7set.c"
    load: "b4v7ld.c"
    temp: "b4v7temp.c"
    check: "b4v7check.c"
    mpar: "b4v7mpar.c"
    param: "b4v7par.c"
    geo: "b4v7geo.c"
    devsup: "b4v7.c"        # DEVpnjlim/DEVfetlim/DEVlimvds

  # Files to skip (not needed for neospice)
  skip_files:
    - "b4v7acld.c"    # AC load (future)
    - "b4v7noi.c"     # noise (future)
    - "b4v7pzld.c"    # pole-zero
    - "b4v7cvtest.c"  # convergence test (ngspice-specific)
    - "b4v7ask.c"     # parameter query
    - "b4v7mask.c"    # model parameter query
    - "b4v7del.c"     # delete
    - "b4v7dest.c"    # destroy
    - "b4v7getic.c"   # get IC
    - "b4v7trunc.c"   # truncation
    - "b4v7soachk.c"  # SOA check
    - "bsim4v7init.c" # init table

  # Preprocessor defines to inject at top of translated files
  defines:
    - "PREDICTOR"

  # Internal node allocation pattern (CKTmkVolt calls in setup)
  has_internal_nodes: true

  # Functions that the load file calls (declared in adapter, defined in translated files)
  setup_function: "BSIM4setup"
  temp_function: "BSIM4temp"
  load_function: "BSIM4load"

  # Geometry parameters (for adapter factory's Geom struct)
  geometry:
    - { name: "W", field: "BSIM4v7w", given: "BSIM4v7wGiven", default: "1e-6" }
    - { name: "L", field: "BSIM4v7l", given: "BSIM4v7lGiven", default: "1e-7" }
    - { name: "NF", field: "BSIM4v7nf", given: "BSIM4v7nfGiven", default: "1.0" }
```

---

## Task 1: Refactor `bsim4_translate.py` into a generic transformer module

The existing `tools/bsim4_translate.py` hardcodes BSIM4-specific identifiers. We extract its 8-pass pipeline into a configurable class.

**Files:**
- Create: `tools/ngspice_migrate/__init__.py`
- Create: `tools/ngspice_migrate/transformer.py`
- Create: `tools/tests/test_transformer.py`
- Reference: `tools/bsim4_translate.py`

- [ ] **Step 1: Write failing test for generic token substitution**

```python
# tools/tests/test_transformer.py
import pytest
from ngspice_migrate.transformer import Transformer, TransformerConfig

def test_type_rename_instance():
    cfg = TransformerConfig(
        instance_struct="DIOinstance",
        model_struct="DIOmodel",
        instance_tag="sDIOinstance",
        model_tag="sDIOmodel",
        cpp_instance="DIOInstance",
        cpp_model="DIOModel",
        gen_instance="GENinstance",
        gen_model="GENmodel",
        prefix="DIO",
        namespace="dio",
        defines=[],
    )
    t = Transformer(cfg)
    src = "DIOinstance *here; DIOmodel *model; GENmodel *inModel;"
    result = t.apply_token_subs(src)
    assert "DIOInstance *here" in result
    assert "DIOModel *model" in result
    assert "DIOModel *inModel" in result
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cd /home/subhagato/Codes/spice-cpp && python -m pytest tools/tests/test_transformer.py::test_type_rename_instance -v
```
Expected: FAIL with `ModuleNotFoundError: No module named 'ngspice_migrate'`

- [ ] **Step 3: Create package and implement Transformer**

```python
# tools/ngspice_migrate/__init__.py
"""ngspice model migration tool for neospice."""

# tools/ngspice_migrate/transformer.py
"""Generic 8-pass C→C++ text transformer.

Refactored from tools/bsim4_translate.py. The pipeline is identical but
all BSIM4-specific identifiers are replaced by TransformerConfig fields.
"""
from __future__ import annotations

import re
from dataclasses import dataclass, field
from typing import Callable, List, Tuple


@dataclass
class TransformerConfig:
    """Per-model configuration for the 8-pass transformer."""
    instance_struct: str      # e.g. "DIOinstance"
    model_struct: str         # e.g. "DIOmodel"
    instance_tag: str         # e.g. "sDIOinstance"
    model_tag: str            # e.g. "sDIOmodel"
    cpp_instance: str         # e.g. "DIOInstance"
    cpp_model: str            # e.g. "DIOModel"
    gen_instance: str         # e.g. "GENinstance"
    gen_model: str            # e.g. "GENmodel"
    prefix: str               # e.g. "DIO" — used in stamp regex
    namespace: str            # e.g. "dio"
    defines: List[str] = field(default_factory=list)
    # Stamp regex: fields ending in Ptr belonging to here->
    # Default pattern covers all ngspice models: <PREFIX>*Ptr
    stamp_field_pattern: str = ""  # auto-set to r"<prefix>[A-Za-z0-9_]+Ptr"

    def __post_init__(self):
        if not self.stamp_field_pattern:
            self.stamp_field_pattern = self.prefix + r"[A-Za-z0-9_]+Ptr"


class Transformer:
    """8-pass C→C++ transformer parameterised by TransformerConfig."""

    def __init__(self, cfg: TransformerConfig):
        self.cfg = cfg
        self._build_token_subs()
        self._build_stamp_regex()

    # -- Pass 1: strip USE_OMP blocks ----------------------------------------
    def strip_omp_blocks(self, src: str) -> str:
        # (identical to bsim4_translate.py::strip_omp_blocks — copy verbatim)
        ...  # full implementation copied from bsim4_translate.py

    # -- Pass 2: split banner ------------------------------------------------
    def split_banner(self, src: str) -> Tuple[str, str]:
        ...  # identical to bsim4_translate.py::split_banner

    # -- Pass 3: protect literals --------------------------------------------
    def protect_literals(self, src: str) -> Tuple[str, Callable[[str], str]]:
        ...  # identical to bsim4_translate.py::protect_literals

    # -- Pass 4: token substitutions -----------------------------------------
    def _build_token_subs(self):
        c = self.cfg
        self._token_subs = [
            # Rule B: struct-tag forms
            (re.compile(r"\bstruct\s+" + re.escape(c.instance_tag) + r"\b"),
             "struct " + c.cpp_instance),
            (re.compile(r"\bstruct\s+" + re.escape(c.model_tag) + r"\b"),
             "struct " + c.cpp_model),
            # Rule B: type aliases
            (re.compile(r"\b" + re.escape(c.instance_struct) + r"\b"), c.cpp_instance),
            (re.compile(r"\b" + re.escape(c.model_struct) + r"\b"), c.cpp_model),
            (re.compile(r"\b" + re.escape(c.gen_model) + r"\b"), c.cpp_model),
            (re.compile(r"\b" + re.escape(c.gen_instance) + r"\b"), c.cpp_instance),
            (re.compile(r"\bCKTcircuit\b"), "Shim::Ckt"),
            (re.compile(r"\bIFvalue\b"), "Shim::IfValue"),
            (re.compile(r"\bIFuid\b"), "const char *"),
            (re.compile(r"\bSMPmatrix\b"), "Shim::Matrix"),
            # Rule C: drop redundant casts
            (re.compile(r"\(\s*" + re.escape(c.cpp_model) + r"\s*\*\s*\)\s*inModel\b"), "inModel"),
            (re.compile(r"\(\s*" + re.escape(c.cpp_instance) + r"\s*\*\s*\)\s*inInst\b"), "inInst"),
            # Rule D: error-code returns
            (re.compile(r"\breturn\s*\(\s*OK\s*\)\s*;"), "return 0;"),
            (re.compile(r"\breturn\s+OK\s*;"), "return 0;"),
            (re.compile(r"\breturn\s*\(\s*E_BADPARM\s*\)\s*;"), "return Shim::E_BADPARM;"),
            (re.compile(r"\breturn\s*\(\s*E_PARMRANGE\s*\)\s*;"), "return Shim::E_PARMRANGE;"),
            (re.compile(r"\breturn\s*\(\s*E_NOMEM\s*\)\s*;"), "return Shim::E_NOMEM;"),
            # Rule F: tmalloc / FREE
            (re.compile(r"\btmalloc\s*\(\s*sizeof\s*\(\s*double\s*\)\s*\*\s*([^)]+?)\s*\)"),
             r"Shim::tmalloc<double>(\1)"),
            (re.compile(r"\bFREE\s*\("), "Shim::FREE("),
        ]

    def apply_token_subs(self, src: str) -> str:
        for patt, repl in self._token_subs:
            src = patt.sub(repl, src)
        src = self._rewrite_iferror(src)
        return src

    def _rewrite_iferror(self, src: str) -> str:
        patt_a = re.compile(r"\(\s*\*\s*\(\s*SPfrontEnd\s*->\s*IFerror\s*\)\s*\)\s*")
        src = patt_a.sub("Shim::report_error", src)
        patt_b = re.compile(r"\bSPfrontEnd\s*->\s*IFerror\b")
        src = patt_b.sub("Shim::report_error", src)
        src = re.sub(r"\bERR_WARNING\b", "Shim::ERR_WARNING", src)
        src = re.sub(r"\bERR_FATAL\b", "Shim::ERR_FATAL", src)
        return src

    # -- Pass 5: matrix stamp rewriting --------------------------------------
    def _build_stamp_regex(self):
        self._stamp_start = re.compile(
            r"(?P<indent>[ \t]*)"
            r"(?P<outer_open>\(?)"
            r"\*\s*\(\s*here\s*->\s*"
            r"(?P<field>" + self.cfg.stamp_field_pattern + r")"
            r"\s*\)\s*"
            r"(?P<op>\+=|-=|=)\s*",
        )

    def rewrite_matrix_stamps(self, src: str) -> str:
        ...  # identical logic to bsim4_translate.py::rewrite_matrix_stamps
             # but uses self._stamp_start

    # -- Pass 6: K&R signatures ---------------------------------------------
    def rewrite_knr_signatures(self, src: str) -> str:
        ...  # identical to bsim4_translate.py::rewrite_knr_signatures

    # -- Pass 7: unprotect ---------------------------------------------------
    # (handled by the restore callable from pass 3)

    # -- Pass 8: wrap --------------------------------------------------------
    def wrap(self, src: str, banner: str) -> str:
        c = self.cfg
        header_prefix = "".join(f"#define {d}\n" for d in c.defines)
        ns = f"neospice::{c.namespace}"
        translated_header = (
            "\n"
            f"// Translated to C++ for neospice by tools/ngspice_migrate.\n"
            "\n"
            f'#include "devices/{c.namespace}/{c.namespace}_def.hpp"\n'
            f'#include "devices/{c.namespace}/{c.namespace}_shim.hpp"\n'
            "#include <cmath>\n"
            "#include <cstdio>\n"
            "\n"
            f"namespace {ns} {{\n"
            "\n"
            "using namespace Shim;\n"
        )
        footer = f"\n}} // namespace {ns}\n"
        banner_out = banner.rstrip() + "\n"
        body = src if src.startswith("\n") else "\n" + src
        if not body.endswith("\n"):
            body += "\n"
        return header_prefix + banner_out + translated_header + body + footer

    # -- Full pipeline -------------------------------------------------------
    def translate(self, source_text: str) -> str:
        src = self.strip_omp_blocks(source_text)
        banner, rest = self.split_banner(src)
        protected, restore = self.protect_literals(rest)
        protected = self.apply_token_subs(protected)
        protected = self.rewrite_matrix_stamps(protected)
        protected = self.rewrite_knr_signatures(protected)
        body = restore(protected)
        body = self._annotate_tstalloc(body)
        body = self._strip_ngspice_includes(body)
        return self.wrap(body, banner)

    def _annotate_tstalloc(self, src: str) -> str:
        m = re.search(r"(^|\n)([ \t]*)TSTALLOC\s*\(", src)
        if not m:
            return src
        indent = m.group(2)
        todo = f"{m.group(1)}{indent}/* TODO(ngspice_migrate): TSTALLOC macro kept as-is; adapter redefines it. */\n"
        return src[:m.start()] + todo + src[m.start():]

    def _strip_ngspice_includes(self, src: str) -> str:
        # Remove ngspice headers and standard C headers replaced by C++ equivalents
        patterns = [
            r'^[ \t]*#\s*include\s+"ngspice/[^"]+"\s*\n',
            r'^[ \t]*#\s*include\s+"[^"]*defs?\.h"\s*\n',
            r'^[ \t]*#\s*include\s+<stdio\.h>\s*\n',
            r'^[ \t]*#\s*include\s+<math\.h>\s*\n',
            r'^[ \t]*#\s*include\s+"spice\.h"\s*\n',
            r'^[ \t]*#\s*include\s+"suffix\.h"\s*\n',
        ]
        for p in patterns:
            src = re.sub(p, "", src, flags=re.MULTILINE)
        return src
```

Implementation note: the pass implementations (strip_omp_blocks, protect_literals, rewrite_matrix_stamps, rewrite_knr_signatures) are **copied verbatim** from `bsim4_translate.py` with the only change being that `_STAMP_START` is built dynamically from `self.cfg.stamp_field_pattern` instead of hardcoding `BSIM4`.

- [ ] **Step 4: Run test to verify it passes**

```bash
cd /home/subhagato/Codes/spice-cpp && python -m pytest tools/tests/test_transformer.py -v
```
Expected: PASS

- [ ] **Step 5: Write additional transformer tests**

```python
# tools/tests/test_transformer.py (append)

def make_dio_config():
    return TransformerConfig(
        instance_struct="DIOinstance",
        model_struct="DIOmodel",
        instance_tag="sDIOinstance",
        model_tag="sDIOmodel",
        cpp_instance="DIOInstance",
        cpp_model="DIOModel",
        gen_instance="GENinstance",
        gen_model="GENmodel",
        prefix="DIO",
        namespace="dio",
        defines=[],
    )

def test_matrix_stamp_rewrite():
    t = Transformer(make_dio_config())
    src = '    *(here->DIOposPosPtr) += gspr;'
    protected, restore = t.protect_literals(src)
    result = t.rewrite_matrix_stamps(protected)
    result = restore(result)
    assert "mat.add(here->DIOposPosPtr, gspr);" in result

def test_matrix_stamp_minus_equals():
    t = Transformer(make_dio_config())
    src = '    *(here->DIOposPosPrimePtr) -= gspr;'
    protected, restore = t.protect_literals(src)
    result = t.rewrite_matrix_stamps(protected)
    result = restore(result)
    assert "mat.add(here->DIOposPosPrimePtr, -(gspr));" in result

def test_error_return_rewrite():
    t = Transformer(make_dio_config())
    result = t.apply_token_subs("return(OK);")
    assert result == "return 0;"

def test_iferror_rewrite():
    t = Transformer(make_dio_config())
    src = "(*(SPfrontEnd->IFerror))(ERR_WARNING, msg)"
    result = t.apply_token_subs(src)
    assert "Shim::report_error(Shim::ERR_WARNING, msg)" in result

def test_knr_signature():
    t = Transformer(make_dio_config())
    src = "\nDIOload(inModel, ckt)\nDIOModel *inModel;\nShim::Ckt *ckt;\n{"
    result = t.rewrite_knr_signatures(src)
    assert "DIOload(DIOModel *inModel, Shim::Ckt *ckt) {" in result

def test_strip_omp_blocks():
    t = Transformer(make_dio_config())
    src = "before\n#ifdef USE_OMP\nomp_code\n#else\nserial_code\n#endif\nafter"
    result = t.strip_omp_blocks(src)
    assert "omp_code" not in result
    assert "serial_code" in result
    assert "before" in result
    assert "after" in result

def test_namespace_wrap():
    t = Transformer(make_dio_config())
    result = t.wrap("\nint x;\n", "/* banner */")
    assert "namespace neospice::dio {" in result
    assert "using namespace Shim;" in result
    assert '#include "devices/dio/dio_def.hpp"' in result

def test_full_translate_simple():
    t = Transformer(make_dio_config())
    src = '''/* Copyright */
#include "ngspice/cktdefs.h"
#include "diodefs.h"

int DIOload(GENmodel *inModel, CKTcircuit *ckt)
{
    DIOmodel *model = (DIOmodel *)inModel;
    *(here->DIOposPosPtr) += gspr;
    return(OK);
}
'''
    result = t.translate(src)
    assert "namespace neospice::dio" in result
    assert "mat.add(here->DIOposPosPtr, gspr);" in result
    assert "return 0;" in result
    assert "DIOModel *model = inModel;" in result
    assert '#include "ngspice' not in result
```

- [ ] **Step 6: Run tests**

```bash
cd /home/subhagato/Codes/spice-cpp && python -m pytest tools/tests/test_transformer.py -v
```
Expected: All PASS

- [ ] **Step 7: Commit**

```bash
cd /home/subhagato/Codes/spice-cpp
git add tools/ngspice_migrate/__init__.py tools/ngspice_migrate/transformer.py tools/tests/test_transformer.py
git commit -m "feat(tools): generic 8-pass C→C++ transformer for ngspice models"
```

---

## Task 2: Model descriptor loader

**Files:**
- Create: `tools/ngspice_migrate/descriptor.py`
- Create: `tools/tests/test_descriptor.py`
- Create: `tools/descriptors/bsim4v7.yaml`

- [ ] **Step 1: Write failing test**

```python
# tools/tests/test_descriptor.py
import pytest
from pathlib import Path
from ngspice_migrate.descriptor import load_descriptor, ModelDescriptor

FIXTURES = Path(__file__).parent.parent / "descriptors"

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

def test_descriptor_builds_transformer_config():
    desc = load_descriptor(FIXTURES / "bsim4v7.yaml")
    cfg = desc.to_transformer_config()
    assert cfg.instance_struct == "BSIM4v7instance"
    assert cfg.cpp_instance == "BSIM4v7Instance"
    assert cfg.namespace == "bsim4v7"
    assert cfg.stamp_field_pattern == r"BSIM4v7[A-Za-z0-9_]+Ptr"

def test_missing_required_field():
    import tempfile, yaml
    bad = {"model": {"ngspice_prefix": "FOO"}}
    with tempfile.NamedTemporaryFile(suffix=".yaml", mode="w") as f:
        yaml.dump(bad, f)
        f.flush()
        with pytest.raises(ValueError, match="neospice_name"):
            load_descriptor(Path(f.name))
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cd /home/subhagato/Codes/spice-cpp && python -m pytest tools/tests/test_descriptor.py -v
```
Expected: FAIL

- [ ] **Step 3: Implement descriptor.py**

```python
# tools/ngspice_migrate/descriptor.py
"""Load and validate YAML model descriptors."""
from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional

import yaml

from .transformer import TransformerConfig


@dataclass
class Terminal:
    name: str
    field: str


@dataclass
class GeomParam:
    name: str
    field: str
    given: str
    default: str


@dataclass
class ModelDescriptor:
    prefix: str                      # e.g. "BSIM4v7"
    neospice_name: str               # e.g. "bsim4v7"
    namespace: str                   # e.g. "bsim4v7"
    instance_struct: str             # e.g. "BSIM4v7instance"
    model_struct: str                # e.g. "BSIM4v7model"
    instance_tag: str                # e.g. "sBSIM4v7instance"
    model_tag: str                   # e.g. "sBSIM4v7model"
    cpp_instance: str                # e.g. "BSIM4v7Instance"
    cpp_model: str                   # e.g. "BSIM4v7Model"
    gen_instance: str
    gen_model: str
    terminals: List[Terminal]
    state_count: int
    state_base_field: str
    next_instance_field: str
    instances_field: str
    next_model_field: str
    model_ptr_field: str
    name_field: str
    source_files: Dict[str, str]
    skip_files: List[str] = field(default_factory=list)
    defines: List[str] = field(default_factory=list)
    has_internal_nodes: bool = False
    setup_function: str = ""
    temp_function: str = ""
    load_function: str = ""
    geometry: List[GeomParam] = field(default_factory=list)
    matrix_ptr_suffix: str = "Ptr"

    def to_transformer_config(self) -> TransformerConfig:
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
            defines=self.defines,
        )


_REQUIRED = ["ngspice_prefix", "neospice_name", "terminals", "source_files",
             "state_count", "state_base_field"]


def load_descriptor(path: Path) -> ModelDescriptor:
    with open(path) as f:
        raw = yaml.safe_load(f)
    m = raw.get("model", {})
    for key in _REQUIRED:
        if key not in m:
            raise ValueError(f"Missing required field: {key}")

    prefix = m["ngspice_prefix"]
    return ModelDescriptor(
        prefix=prefix,
        neospice_name=m["neospice_name"],
        namespace=m.get("neospice_namespace", m["neospice_name"]),
        instance_struct=m.get("instance_struct", f"{prefix}instance"),
        model_struct=m.get("model_struct", f"{prefix}model"),
        instance_tag=m.get("instance_tag", f"s{prefix}instance"),
        model_tag=m.get("model_tag", f"s{prefix}model"),
        cpp_instance=m.get("cpp_instance", f"{prefix}Instance"),
        cpp_model=m.get("cpp_model", f"{prefix}Model"),
        gen_instance=m.get("gen_instance", "GENinstance"),
        gen_model=m.get("gen_model", "GENmodel"),
        terminals=[Terminal(**t) for t in m["terminals"]],
        state_count=m["state_count"],
        state_base_field=m["state_base_field"],
        next_instance_field=m.get("next_instance_field", f"{prefix}nextInstance"),
        instances_field=m.get("instances_field", f"{prefix}instances"),
        next_model_field=m.get("next_model_field", f"{prefix}nextModel"),
        model_ptr_field=m.get("model_ptr_field", f"{prefix}modPtr"),
        name_field=m.get("name_field", f"{prefix}name"),
        source_files=m["source_files"],
        skip_files=m.get("skip_files", []),
        defines=m.get("defines", []),
        has_internal_nodes=m.get("has_internal_nodes", False),
        setup_function=m.get("setup_function", ""),
        temp_function=m.get("temp_function", ""),
        load_function=m.get("load_function", ""),
        geometry=[GeomParam(**g) for g in m.get("geometry", [])],
        matrix_ptr_suffix=m.get("matrix_ptr_suffix", "Ptr"),
    )
```

- [ ] **Step 4: Write the BSIM4v7 descriptor YAML**

Write `tools/descriptors/bsim4v7.yaml` using the format shown in the Descriptor Format section above (exact content for BSIM4v7 model).

- [ ] **Step 5: Run tests**

```bash
cd /home/subhagato/Codes/spice-cpp && python -m pytest tools/tests/test_descriptor.py -v
```
Expected: All PASS

- [ ] **Step 6: Commit**

```bash
cd /home/subhagato/Codes/spice-cpp
git add tools/ngspice_migrate/descriptor.py tools/tests/test_descriptor.py tools/descriptors/bsim4v7.yaml
git commit -m "feat(tools): model descriptor loader with BSIM4v7 YAML"
```

---

## Task 3: def.h → def.hpp generator

Parse the ngspice `*def.h` file and emit a clean C++ `*_def.hpp`. The key transformations:
- Strip ngspice includes (`#include "ngspice/..."`)
- Replace `typedef struct sXXX { ... } XXX;` with `struct XXXCpp { ... };`
- Replace `double *` matrix pointer fields with `neospice::MatrixOffset`
- Replace `IFuid` with `const char *`
- Add `#pragma once` and namespace wrapper
- Preserve all field names, macros, and conditional compilation

**Files:**
- Create: `tools/ngspice_migrate/gen_def.py`
- Create: `tools/tests/test_gen_def.py`

- [ ] **Step 1: Write failing test**

```python
# tools/tests/test_gen_def.py
from ngspice_migrate.gen_def import generate_def_hpp
from ngspice_migrate.descriptor import ModelDescriptor, Terminal

def make_test_desc():
    return ModelDescriptor(
        prefix="DIO", neospice_name="dio", namespace="dio",
        instance_struct="DIOinstance", model_struct="DIOmodel",
        instance_tag="sDIOinstance", model_tag="sDIOmodel",
        cpp_instance="DIOInstance", cpp_model="DIOModel",
        gen_instance="GENinstance", gen_model="GENmodel",
        terminals=[Terminal("pos", "DIOposNode"), Terminal("neg", "DIOnegNode")],
        state_count=7, state_base_field="DIOstate",
        next_instance_field="DIOnextInstance", instances_field="DIOinstances",
        next_model_field="DIOnextModel", model_ptr_field="DIOmodPtr",
        name_field="DIOname", source_files={}, matrix_ptr_suffix="Ptr",
    )

def test_struct_rename():
    desc = make_test_desc()
    src = '''typedef struct sDIOinstance {
    struct sDIOmodel *DIOmodPtr;
    struct sDIOinstance *DIOnextInstance;
    IFuid DIOname;
    double *DIOposPosPtr;
    double *DIOnegNegPtr;
    double DIOcap;
} DIOinstance;'''
    result = generate_def_hpp(src, desc)
    assert "#pragma once" in result
    assert "namespace neospice::dio" in result
    assert "struct DIOInstance {" in result
    assert "MatrixOffset DIOposPosPtr" in result
    assert "MatrixOffset DIOnegNegPtr" in result
    assert "double DIOcap;" in result  # non-pointer field unchanged
    assert "const char *DIOname;" in result
    assert '#include "ngspice' not in result

def test_model_struct_rename():
    desc = make_test_desc()
    src = '''typedef struct sDIOmodel {
    int DIOtype;
    struct sDIOmodel *DIOnextModel;
} DIOmodel;'''
    result = generate_def_hpp(src, desc)
    assert "struct DIOModel {" in result
```

- [ ] **Step 2: Run test, verify fail**

```bash
cd /home/subhagato/Codes/spice-cpp && python -m pytest tools/tests/test_gen_def.py -v
```

- [ ] **Step 3: Implement gen_def.py**

```python
# tools/ngspice_migrate/gen_def.py
"""Parse ngspice *def.h and emit neospice *_def.hpp."""
from __future__ import annotations

import re
from .descriptor import ModelDescriptor


def generate_def_hpp(src: str, desc: ModelDescriptor) -> str:
    # Strip ngspice includes
    src = re.sub(r'^[ \t]*#\s*include\s+"ngspice/[^"]+"\s*\n', "", src, flags=re.MULTILINE)
    src = re.sub(r'^[ \t]*#\s*include\s+"[^"]*\.h"\s*\n', "", src, flags=re.MULTILINE)
    src = re.sub(r'^[ \t]*#\s*include\s+<[^>]+>\s*\n', "", src, flags=re.MULTILINE)

    # Remove old include guard (#ifndef FOO / #define FOO / ... / #endif)
    src = re.sub(r'^[ \t]*#\s*ifndef\s+\w+\s*\n[ \t]*#\s*define\s+\w+\s*\n', "", src, flags=re.MULTILINE)
    # Remove trailing #endif
    src = re.sub(r'\n[ \t]*#\s*endif\s*$', "", src)

    # Replace typedef struct sXXX { with struct CppXXX {
    src = re.sub(
        r"typedef\s+struct\s+" + re.escape(desc.instance_tag) + r"\s*\{",
        f"struct {desc.cpp_instance} {{",
        src,
    )
    src = re.sub(
        r"\}\s*" + re.escape(desc.instance_struct) + r"\s*;",
        "};",
        src,
    )
    src = re.sub(
        r"typedef\s+struct\s+" + re.escape(desc.model_tag) + r"\s*\{",
        f"struct {desc.cpp_model} {{",
        src,
    )
    src = re.sub(
        r"\}\s*" + re.escape(desc.model_struct) + r"\s*;",
        "};",
        src,
    )

    # Replace struct tags in pointer declarations
    src = re.sub(r"\bstruct\s+" + re.escape(desc.instance_tag) + r"\b", desc.cpp_instance, src)
    src = re.sub(r"\bstruct\s+" + re.escape(desc.model_tag) + r"\b", desc.cpp_model, src)
    src = re.sub(r"\b" + re.escape(desc.instance_struct) + r"\b", desc.cpp_instance, src)
    src = re.sub(r"\b" + re.escape(desc.model_struct) + r"\b", desc.cpp_model, src)

    # Replace double * matrix pointer fields with MatrixOffset
    # Pattern: double *PREFIXxxxxPtr; (with optional comment)
    ptr_pattern = re.compile(
        r"(\s*)double\s+\*(" + re.escape(desc.prefix) + r"[A-Za-z0-9_]*"
        + re.escape(desc.matrix_ptr_suffix) + r")\s*;",
    )
    src = ptr_pattern.sub(r"\1neospice::MatrixOffset \2 = -1;", src)

    # Replace IFuid with const char *
    src = re.sub(r"\bIFuid\b", "const char *", src)

    # Replace GENmodel/GENinstance references
    src = re.sub(r"\bGENmodel\b", desc.cpp_model, src)
    src = re.sub(r"\bGENinstance\b", desc.cpp_instance, src)

    # Assemble output
    header = (
        "#pragma once\n"
        '#include "core/matrix.hpp"\n'
        "\n"
        f"namespace neospice::{desc.namespace} {{\n"
        "\n"
    )
    footer = f"\n}} // namespace neospice::{desc.namespace}\n"
    return header + src.strip() + footer
```

- [ ] **Step 4: Run tests**

```bash
cd /home/subhagato/Codes/spice-cpp && python -m pytest tools/tests/test_gen_def.py -v
```
Expected: All PASS

- [ ] **Step 5: Commit**

```bash
cd /home/subhagato/Codes/spice-cpp
git add tools/ngspice_migrate/gen_def.py tools/tests/test_gen_def.py
git commit -m "feat(tools): def.h to def.hpp generator"
```

---

## Task 4: Shim layer generator

The shim layer is ~90% identical across models. The only model-specific parts are the namespace and any model-specific helper functions (like NIintegrate for charge-based models, or devsup limiting functions).

**Files:**
- Create: `tools/ngspice_migrate/gen_shim.py`
- Create: `tools/tests/test_gen_shim.py`

- [ ] **Step 1: Write failing test**

```python
# tools/tests/test_gen_shim.py
from ngspice_migrate.gen_shim import generate_shim_hpp, generate_shim_cpp
from ngspice_migrate.descriptor import ModelDescriptor, Terminal

def make_desc():
    return ModelDescriptor(
        prefix="DIO", neospice_name="dio", namespace="dio",
        instance_struct="DIOinstance", model_struct="DIOmodel",
        instance_tag="sDIOinstance", model_tag="sDIOmodel",
        cpp_instance="DIOInstance", cpp_model="DIOModel",
        gen_instance="GENinstance", gen_model="GENmodel",
        terminals=[Terminal("pos", "DIOposNode"), Terminal("neg", "DIOnegNode")],
        state_count=7, state_base_field="DIOstate",
        next_instance_field="DIOnextInstance", instances_field="DIOinstances",
        next_model_field="DIOnextModel", model_ptr_field="DIOmodPtr",
        name_field="DIOname", source_files={}, has_internal_nodes=False,
    )

def test_shim_hpp_namespace():
    hpp = generate_shim_hpp(make_desc())
    assert "namespace neospice::dio" in hpp
    assert "#pragma once" in hpp
    assert "struct Ckt" in hpp
    assert "class Matrix" in hpp
    assert "report_error" in hpp

def test_shim_cpp_namespace():
    cpp = generate_shim_cpp(make_desc())
    assert "namespace neospice::dio::Shim" in cpp
    assert "make_elt" in cpp
    assert "resolve_offsets" in cpp
    assert "report_error" in cpp

def test_shim_hpp_has_ni_integrate():
    desc = make_desc()
    desc.state_count = 29  # model with charge states
    hpp = generate_shim_hpp(desc)
    assert "NIintegrate" in hpp

def test_shim_hpp_has_internal_nodes():
    desc = make_desc()
    desc.has_internal_nodes = True
    hpp = generate_shim_hpp(desc)
    assert "node_alloc" in hpp
    assert "add_internal_node" in hpp
```

- [ ] **Step 2: Run test, verify fail**

```bash
cd /home/subhagato/Codes/spice-cpp && python -m pytest tools/tests/test_gen_shim.py -v
```

- [ ] **Step 3: Implement gen_shim.py**

The generator emits the shim header and source as string templates parameterised by the descriptor's namespace and feature flags. The shim body is identical to `bsim4v7_shim.hpp/cpp` with namespace and include paths substituted. NIintegrate is included when `state_count > 0`. Internal node support (node_alloc callback, add_internal_node) is included when `has_internal_nodes == True`.

```python
# tools/ngspice_migrate/gen_shim.py
"""Generate the Shim layer (hpp + cpp) for a migrated model."""
from __future__ import annotations

from .descriptor import ModelDescriptor


def generate_shim_hpp(desc: ModelDescriptor) -> str:
    ns = desc.namespace
    parts = [
        '#pragma once\n',
        '#include "core/matrix.hpp"\n',
        '#include <cstdint>\n',
        '#include <cstdio>\n',
    ]
    if desc.has_internal_nodes:
        parts.append('#include <functional>\n')
    parts += [
        '#include <utility>\n',
        '#include <vector>\n',
        '\n',
        f'namespace neospice::{ns} {{\n',
        '\n',
        '#ifndef TRUE\n#define TRUE  1\n#endif\n',
        '#ifndef FALSE\n#define FALSE 0\n#endif\n',
        '\n',
        'namespace Shim {\n',
    ]

    # Error codes
    parts.append(_ERROR_CODES)

    # IfValue, IfParm, datatype flags
    parts.append(_IF_TYPES)

    # Ckt struct
    parts.append(_ckt_struct(desc))

    # CKTmode flags
    parts.append(_MODE_FLAGS)

    # Matrix class
    parts.append(_MATRIX_CLASS)

    # report_error
    parts.append(_REPORT_ERROR)

    # FREE / tmalloc
    parts.append(_ALLOC_HELPERS)

    # NIintegrate (if model has state vars)
    if desc.state_count > 0:
        parts.append(_NI_INTEGRATE_DECL)

    parts.append('} // namespace Shim\n\n')
    parts.append(f'}} // namespace neospice::{ns}\n')

    return "".join(parts)


def generate_shim_cpp(desc: ModelDescriptor) -> str:
    ns = desc.namespace
    parts = [
        f'#include "devices/{ns}/{ns}_shim.hpp"\n',
        '#include <cstdarg>\n',
        '\n',
        f'namespace neospice::{ns}::Shim {{\n',
        '\n',
        _MATRIX_IMPL,
        '\n',
    ]
    if desc.has_internal_nodes:
        parts.append(_ADD_INTERNAL_NODE_IMPL)
        parts.append('\n')
    else:
        parts.append(_ADD_INTERNAL_NODE_STUB)
        parts.append('\n')

    if desc.state_count > 0:
        parts.append(_NI_INTEGRATE_IMPL)
        parts.append('\n')

    parts.append(_REPORT_ERROR_IMPL)
    parts.append('\n')
    parts.append(f'}} // namespace neospice::{ns}::Shim\n')

    return "".join(parts)


# --- Template fragments (identical to bsim4v7_shim content) ---

_ERROR_CODES = """
    constexpr int OK          = 0;
    constexpr int E_BADPARM   = -1;
    constexpr int E_PARMRANGE = -2;
    constexpr int E_NOMEM     = -3;
    constexpr int E_UNSUPP    = -4;

"""

_IF_TYPES = """    struct IfValue {
        int         iValue = 0;
        double      rValue = 0.0;
        const char *sValue = nullptr;
        struct {
            int numValue = 0;
            struct { double *rVec = nullptr; } vec;
        } v{};
    };

    struct IfParm {
        const char *keyword;
        int         id;
        int         dataType;
        const char *description;
    };
    constexpr int IF_REAL    = 0x01;
    constexpr int IF_INTEGER = 0x02;
    constexpr int IF_STRING  = 0x04;
    constexpr int IF_FLAG    = 0x08;
    constexpr int IF_REALVEC = 0x10;
    constexpr int IF_ASK     = 0x100;
    constexpr int IF_SET     = 0x200;
    constexpr int IF_REDUNDANT = 0x400;

"""


def _ckt_struct(desc: ModelDescriptor) -> str:
    node_alloc = ""
    if desc.has_internal_nodes:
        node_alloc = (
            "        std::function<int(const char*)> node_alloc;\n"
        )
    return f"""    struct Ckt {{
        double CKTtemp       = 300.15;
        double CKTnomTemp    = 300.15;
        double CKTgmin       = 1e-12;
        double CKTreltol     = 1e-3;
        double CKTabstol     = 1e-12;
        double CKTvoltTol    = 1e-6;
        int    CKTmode       = 0;
        int    CKTbadMos3    = 0;
        int    CKTnumStates  = 0;
        int    CKTnoncon     = 0;
        int    CKTbypass     = 0;
        double  CKTdelta        = 0.0;
        double  CKTdeltaOld[8]  = {{}};
        double  CKTag[8]        = {{}};
        int     CKTorder        = 1;
        double *CKTstate0 = nullptr;
        double *CKTstate1 = nullptr;
        double *CKTstate2 = nullptr;
        double *CKTrhs    = nullptr;
        double *CKTrhsOld = nullptr;
        neospice::NumericMatrix *mat = nullptr;
        int CKTinternalNodeCounter = 1000;
        int add_internal_node(const char *name);
{node_alloc}    }};

"""


_MODE_FLAGS = """    constexpr int MODE             = 0x3;
    constexpr int MODETRAN         = 0x1;
    constexpr int MODEAC           = 0x2;
    constexpr int MODEDC           = 0x70;
    constexpr int MODEDCOP         = 0x10;
    constexpr int MODETRANOP       = 0x20;
    constexpr int MODEDCTRANCURVE  = 0x40;
    constexpr int INITF            = 0x3f00;
    constexpr int MODEINITFLOAT    = 0x100;
    constexpr int MODEINITJCT      = 0x200;
    constexpr int MODEINITFIX      = 0x400;
    constexpr int MODEINITSMSIG    = 0x800;
    constexpr int MODEINITTRAN     = 0x1000;
    constexpr int MODEINITPRED     = 0x2000;
    constexpr int MODEUIC          = 0x10000;
    constexpr int MODEBYPASS       = 0x1000000;

"""

_MATRIX_CLASS = """    class Matrix {
    public:
        Matrix(neospice::SparsityBuilder &builder) : builder_(builder) {}
        neospice::MatrixOffset make_elt(int row, int col);
        std::vector<neospice::MatrixOffset>
        resolve_offsets(const neospice::SparsityPattern &pat) const;
        void clear() { journal_.clear(); }
        const std::vector<std::pair<int,int>>& reservation_journal() const { return journal_; }
    private:
        neospice::SparsityBuilder &builder_;
        std::vector<std::pair<int,int>> journal_;
    };

"""

_REPORT_ERROR = """    [[gnu::format(printf, 2, 3)]]
    void report_error(int level, const char *fmt, ...);
    constexpr int ERR_WARNING = 1;
    constexpr int ERR_FATAL   = 2;

"""

_ALLOC_HELPERS = """    template <typename T>
    inline void FREE(T *&p) { delete[] p; p = nullptr; }
    template <typename T>
    inline T *tmalloc(std::size_t n) { return new T[n](); }

"""

_NI_INTEGRATE_DECL = """    int NIintegrate(Ckt *ckt, double *geq, double *ceq,
                    double cap, int qcap);
"""

# --- Implementation fragments ---

_MATRIX_IMPL = """neospice::MatrixOffset Matrix::make_elt(int row, int col) {
    if (row < 0 || col < 0) {
        journal_.emplace_back(-1, -1);
        return -1;
    }
    builder_.add(row, col);
    neospice::MatrixOffset id = static_cast<neospice::MatrixOffset>(journal_.size());
    journal_.emplace_back(row, col);
    return id;
}

std::vector<neospice::MatrixOffset>
Matrix::resolve_offsets(const neospice::SparsityPattern &pat) const {
    std::vector<neospice::MatrixOffset> out;
    out.reserve(journal_.size());
    for (auto &[r, c] : journal_) {
        if (r < 0 || c < 0) out.push_back(-1);
        else out.push_back(pat.offset(r, c));
    }
    return out;
}
"""

_ADD_INTERNAL_NODE_IMPL = """int Ckt::add_internal_node(const char *name) {
    if (node_alloc) return node_alloc(name);
    return CKTinternalNodeCounter++;
}
"""

_ADD_INTERNAL_NODE_STUB = """int Ckt::add_internal_node(const char *name) {
    return CKTinternalNodeCounter++;
}
"""

_NI_INTEGRATE_IMPL = """int NIintegrate(Ckt *ckt, double *geq, double *ceq,
                double cap, int qcap) {
    const int ccap = qcap + 1;
    double *s0 = ckt->CKTstate0 + qcap;
    double *s1 = ckt->CKTstate1 + qcap;
    double *s2 = ckt->CKTstate2 + qcap;
    int order = ckt->CKTorder;
    if (order < 1) order = 1;
    if (order > 2) order = 2;
    double deriv = ckt->CKTag[0] * s0[0];
    if (order >= 1) deriv += ckt->CKTag[1] * s1[0];
    if (order >= 2) deriv += ckt->CKTag[2] * s2[0];
    s0[1] = deriv;
    *geq = ckt->CKTag[0] * cap;
    *ceq = s0[1] - (*geq) * s0[0];
    return OK;
}
"""

_REPORT_ERROR_IMPL = """void report_error(int /*level*/, const char *fmt, ...) {
    std::va_list ap;
    va_start(ap, fmt);
    std::vfprintf(stderr, fmt, ap);
    std::fputc('\\n', stderr);
    va_end(ap);
}
"""
```

- [ ] **Step 4: Run tests**

```bash
cd /home/subhagato/Codes/spice-cpp && python -m pytest tools/tests/test_gen_shim.py -v
```
Expected: All PASS

- [ ] **Step 5: Commit**

```bash
cd /home/subhagato/Codes/spice-cpp
git add tools/ngspice_migrate/gen_shim.py tools/tests/test_gen_shim.py
git commit -m "feat(tools): shim layer generator for migrated models"
```

---

## Task 5: Device adapter generator

The adapter bridges the neospice `Device` interface to the translated UCB code. It follows the same pattern as `bsim4v7_device.hpp/cpp` but parameterised by terminal count, state variables, matrix pointer fields, and function names.

**Files:**
- Create: `tools/ngspice_migrate/gen_adapter.py`
- Create: `tools/tests/test_gen_adapter.py`

- [ ] **Step 1: Write failing test**

```python
# tools/tests/test_gen_adapter.py
from ngspice_migrate.gen_adapter import generate_adapter_hpp, generate_adapter_cpp
from ngspice_migrate.descriptor import ModelDescriptor, Terminal, GeomParam

def make_desc():
    return ModelDescriptor(
        prefix="DIO", neospice_name="dio", namespace="dio",
        instance_struct="DIOinstance", model_struct="DIOmodel",
        instance_tag="sDIOinstance", model_tag="sDIOmodel",
        cpp_instance="DIOInstance", cpp_model="DIOModel",
        gen_instance="GENinstance", gen_model="GENmodel",
        terminals=[Terminal("pos", "DIOposNode"), Terminal("neg", "DIOnegNode")],
        state_count=7, state_base_field="DIOstate",
        next_instance_field="DIOnextInstance", instances_field="DIOinstances",
        next_model_field="DIOnextModel", model_ptr_field="DIOmodPtr",
        name_field="DIOname", source_files={"setup": "diosetup.c", "load": "dioload.c", "temp": "diotemp.c"},
        has_internal_nodes=True,
        setup_function="DIOsetup", temp_function="DIOtemp", load_function="DIOload",
        geometry=[
            GeomParam("area", "DIOarea", "DIOareaGiven", "1.0"),
            GeomParam("pj", "DIOpj", "DIOpjGiven", "0.0"),
        ],
    )

def test_adapter_hpp_structure():
    hpp = generate_adapter_hpp(make_desc())
    assert "#pragma once" in hpp
    assert "class DIODevice : public Device" in hpp
    assert "void declare_internal_nodes(Circuit& ckt) override;" in hpp
    assert "void stamp_pattern(SparsityBuilder& builder) const override;" in hpp
    assert "void evaluate(" in hpp
    assert "int32_t state_vars() const override { return 7;" in hpp
    assert "struct Geom {" in hpp
    assert "static std::unique_ptr<DIODevice> make(" in hpp

def test_adapter_hpp_terminal_params():
    hpp = generate_adapter_hpp(make_desc())
    # Factory should take terminal node indices
    assert "int32_t n_pos" in hpp
    assert "int32_t n_neg" in hpp

def test_adapter_cpp_has_evaluate():
    cpp = generate_adapter_cpp(make_desc())
    assert "DIODevice::evaluate" in cpp
    assert "ghost_rhs_" in cpp
    assert "ghost_voltages_" in cpp
    assert "DIOload" in cpp

def test_adapter_cpp_has_splicing():
    cpp = generate_adapter_cpp(make_desc())
    assert "DIOnextInstance" in cpp
    assert "DIOinstances" in cpp
    assert "DIOnextModel" in cpp
```

- [ ] **Step 2: Run test, verify fail**

```bash
cd /home/subhagato/Codes/spice-cpp && python -m pytest tools/tests/test_gen_adapter.py -v
```

- [ ] **Step 3: Implement gen_adapter.py**

The generator emits adapter `.hpp` and `.cpp` as formatted strings with descriptor fields substituted. The template follows the exact structure of `src/devices/bsim4v7/bsim4v7_device.hpp/cpp`:

- `*ModelCard` wrapper struct holding the UCB model aggregate
- `*Device` class inheriting `Device`, with:
  - `Geom` struct from descriptor geometry
  - `make()` factory taking terminal node indices + Geom + shared card ref
  - `declare_internal_nodes()` running UCB setup with node_alloc callback
  - `stamp_pattern()` / `assign_offsets()` from journal replay
  - `evaluate()` with ghost array folding and instance-chain splicing
  - `state_vars()` / `set_state_ptrs()`

The `assign_offsets()` method auto-generates RESOLVE macros by scanning the def.h for matrix pointer fields (fields matching `<prefix>*Ptr`). The generator takes an optional list of pointer field names; if not provided, it emits a TODO for the implementer to fill in from the generated `_def.hpp`.

```python
# tools/ngspice_migrate/gen_adapter.py
"""Generate device adapter hpp/cpp for a migrated ngspice model."""
from __future__ import annotations

from .descriptor import ModelDescriptor


def generate_adapter_hpp(desc: ModelDescriptor) -> str:
    ns = desc.namespace
    dev_class = f"{desc.prefix}Device"
    card_class = f"{desc.prefix}ModelCard"

    terminal_params = ", ".join(f"int32_t n_{t.name}" for t in desc.terminals)
    terminal_fields = "\n".join(f"    int32_t {t.field};" for t in desc.terminals)

    geom_fields = "\n".join(
        f"        double {g.name} = {g.default};"
        for g in desc.geometry
    ) if desc.geometry else "        // No geometry parameters defined"

    return f'''#pragma once
#include "devices/device.hpp"
#include "devices/{ns}/{ns}_def.hpp"
#include "devices/{ns}/{ns}_shim.hpp"
#include <memory>
#include <utility>
#include <vector>

namespace neospice {{

struct {card_class} {{
    {ns}::{desc.cpp_model} ucb{{}};
    {card_class}() = default;
    ~{card_class}();
    {card_class}(const {card_class}&) = delete;
    {card_class}& operator=(const {card_class}&) = delete;
}};

class {dev_class} : public Device {{
public:
    struct Geom {{
{geom_fields}
    }};

    static std::unique_ptr<{dev_class}> make(
        std::string name, {terminal_params},
        const Geom& geom, {card_class}& shared_card);

    void declare_internal_nodes(Circuit& ckt) override;
    void stamp_pattern(SparsityBuilder& builder) const override;
    void assign_offsets(const SparsityPattern& pattern) override;
    void evaluate(const std::vector<double>& voltages,
                  NumericMatrix& mat, std::vector<double>& rhs) override;

    int32_t state_vars() const override {{ return {desc.state_count}; }}
    void set_state_ptrs(double* s0, double* s1, double* s2, int32_t base) override;

private:
    explicit {dev_class}(std::string name) : Device(std::move(name)) {{}}

    mutable {ns}::{desc.cpp_instance} inst_{{}};
    {ns}::{desc.cpp_model}* model_ = nullptr;

    std::vector<std::pair<int,int>> journal_;
    double* state0_ = nullptr;
    double* state1_ = nullptr;
    double* state2_ = nullptr;
    int32_t state_base_ = -1;
    mutable bool temp_done_ = false;
    int32_t max_neo_node_ = -1;
    std::vector<double> ghost_voltages_;
    std::vector<double> ghost_rhs_;
}};

}} // namespace neospice
'''


def generate_adapter_cpp(desc: ModelDescriptor) -> str:
    ns = desc.namespace
    dev_class = f"{desc.prefix}Device"
    card_class = f"{desc.prefix}ModelCard"

    # Terminal node wiring in make()
    node_wiring = "\n".join(
        f"    inst.{t.field} = neo_to_ucb({t.name});"
        for t in desc.terminals
    )
    terminal_params = ", ".join(f"int32_t n_{t.name}" for t in desc.terminals)
    terminal_args = ", ".join(f"n_{t.name}" for t in desc.terminals)
    max_node_args = ", ".join(f"n_{t.name}" for t in desc.terminals)

    # Geometry wiring in make()
    geom_wiring = "\n".join(
        f"    inst.{g.field} = geom.{g.name}; inst.{g.given} = 1;"
        for g in desc.geometry
    ) if desc.geometry else "    // No geometry parameters to wire"

    return f'''#include "devices/{ns}/{ns}_device.hpp"
#include "core/circuit.hpp"
#include "core/types.hpp"
#include <cstdlib>
#include <stdexcept>
#include <string>

namespace neospice::{ns} {{
    int {desc.setup_function}({ns}::Shim::Matrix* matrix, {ns}::{desc.cpp_model}* model,
                              {ns}::Shim::Ckt* ckt, int* states);
    int {desc.temp_function}({ns}::{desc.cpp_model}* model, {ns}::Shim::Ckt* ckt);
    int {desc.load_function}({ns}::{desc.cpp_model}* model, {ns}::Shim::Ckt* ckt);
}}

namespace neospice {{

using namespace neospice::{ns};

{card_class}::~{card_class}() = default;

static inline int neo_to_ucb(int32_t neo) {{
    return (neo < 0) ? 0 : (neo + 1);
}}

std::unique_ptr<{dev_class}>
{dev_class}::make(std::string name, {terminal_params},
                  const Geom& geom, {card_class}& shared_card) {{
    std::unique_ptr<{dev_class}> dev(new {dev_class}(std::move(name)));
    dev->model_ = &shared_card.ucb;
    auto& inst = dev->inst_;
    inst.{desc.name_field} = dev->name().c_str();
    inst.{desc.model_ptr_field} = dev->model_;

{node_wiring}

{geom_wiring}

    inst.{desc.next_instance_field} = shared_card.ucb.{desc.instances_field};
    shared_card.ucb.{desc.instances_field} = &inst;

    int32_t widest = -1;
    for (int32_t n : {{{max_node_args}}}) if (n > widest) widest = n;
    dev->max_neo_node_ = widest;
    return dev;
}}

void {dev_class}::declare_internal_nodes(Circuit& ckt) {{
    SparsityBuilder scratch(1);
    {ns}::Shim::Matrix shim_matrix(scratch);
    {ns}::Shim::Ckt setup_ckt;
    setup_ckt.CKTtemp    = T_NOMINAL;
    setup_ckt.CKTnomTemp = T_NOMINAL;
    setup_ckt.CKTinternalNodeCounter = 1000;
    setup_ckt.node_alloc = [&ckt, this](const char* name) -> int {{
        std::string full = "__" + name_ + "_" + name;
        int32_t neo = ckt.node(full);
        return neo + 1;
    }};
    int states = 0;
    int rc = {desc.setup_function}(&shim_matrix, model_, &setup_ckt, &states);
    if (rc != {ns}::Shim::OK)
        throw std::runtime_error("{desc.setup_function} failed with rc=" + std::to_string(rc));
    const auto& journal = shim_matrix.reservation_journal();
    journal_.assign(journal.begin(), journal.end());
    for (auto [r, c] : journal_) {{
        int mx = std::max(r, c);
        if (mx > 0) {{
            int32_t neo = mx - 1;
            if (neo > max_neo_node_) max_neo_node_ = neo;
        }}
    }}
}}

void {dev_class}::stamp_pattern(SparsityBuilder& builder) const {{
    for (auto [r, c] : journal_) {{
        if (r <= 0 || c <= 0) continue;
        builder.add(r - 1, c - 1);
    }}
}}

void {dev_class}::assign_offsets(const SparsityPattern& pattern) {{
    std::vector<MatrixOffset> offsets(journal_.size(), -1);
    for (std::size_t i = 0; i < journal_.size(); ++i) {{
        auto [r, c] = journal_[i];
        if (r <= 0 || c <= 0) continue;
        offsets[i] = pattern.offset(r - 1, c - 1);
    }}
    // TODO(ngspice_migrate): Auto-generate RESOLVE calls from _def.hpp
    // matrix pointer fields. For now, scan the generated _def.hpp for fields
    // matching *Ptr and list them here.
    #define RESOLVE(f) do {{ if (inst_.f >= 0) inst_.f = offsets[inst_.f]; }} while (0)
    // RESOLVE calls go here — extracted from _def.hpp by the migration tool
    #undef RESOLVE
}}

void {dev_class}::set_state_ptrs(double* s0, double* s1, double* s2, int32_t base) {{
    state0_ = s0; state1_ = s1; state2_ = s2;
    state_base_ = base;
    inst_.{desc.state_base_field} = base;
}}

void {dev_class}::evaluate(const std::vector<double>& voltages,
                           NumericMatrix& mat, std::vector<double>& rhs) {{
    const int n_real = static_cast<int>(rhs.size());
    const int n_ghost = (max_neo_node_ >= 0 ? max_neo_node_ + 1 : 0) + 1;
    ghost_voltages_.assign(n_ghost, 0.0);
    ghost_rhs_.assign(n_ghost, 0.0);
    for (int32_t k = 0; k <= max_neo_node_ && k < n_real; ++k)
        ghost_voltages_[k + 1] = voltages[k];

    {ns}::Shim::Ckt ckt;
    const IntegratorCtx* ic = tls_integrator_ctx;
    if (ic) {{
        ckt.CKTmode  = ic->mode;
        ckt.CKTorder = ic->order;
        ckt.CKTdelta = ic->delta;
        for (int i = 0; i < 8; ++i) ckt.CKTag[i]       = ic->ag[i];
        for (int i = 0; i < 8; ++i) ckt.CKTdeltaOld[i] = ic->delta_old[i];
    }} else {{
        ckt.CKTmode  = 0x70 | 0x200;
        ckt.CKTorder = 1;
    }}
    const SimOptions* sim_opts = (ic ? ic->options : nullptr);
    SimOptions fallback;
    if (!sim_opts) sim_opts = &fallback;
    ckt.CKTtemp    = sim_opts->temp;
    ckt.CKTnomTemp = sim_opts->temp;
    ckt.CKTgmin    = sim_opts->gmin;
    ckt.CKTreltol  = sim_opts->reltol;
    ckt.CKTabstol  = sim_opts->abstol;
    ckt.CKTvoltTol = sim_opts->vntol;
    ckt.CKTbypass  = 0;
    ckt.CKTnoncon  = 0;
    ckt.CKTstate0 = state0_;
    ckt.CKTstate1 = state1_;
    ckt.CKTstate2 = state2_;
    ckt.CKTrhs    = ghost_rhs_.data();
    ckt.CKTrhsOld = ghost_voltages_.data();
    ckt.mat       = &mat;

    if (!temp_done_) {{
        int rc = {desc.temp_function}(model_, &ckt);
        if (rc != {ns}::Shim::OK)
            throw std::runtime_error("{desc.temp_function} failed with rc=" + std::to_string(rc));
        temp_done_ = true;
    }}

    // Splice instance chain for single-device evaluation
    {ns}::{desc.cpp_instance}* saved_head = model_->{desc.instances_field};
    {ns}::{desc.cpp_instance}* saved_next_inst = inst_.{desc.next_instance_field};
    {ns}::{desc.cpp_model}* saved_next_mod = model_->{desc.next_model_field};
    model_->{desc.instances_field} = &inst_;
    inst_.{desc.next_instance_field} = nullptr;
    model_->{desc.next_model_field} = nullptr;
    int rc = {desc.load_function}(model_, &ckt);
    model_->{desc.instances_field} = saved_head;
    inst_.{desc.next_instance_field} = saved_next_inst;
    model_->{desc.next_model_field} = saved_next_mod;
    if (rc != {ns}::Shim::OK)
        throw std::runtime_error("{desc.load_function} failed with rc=" + std::to_string(rc));

    for (int32_t k = 1; k <= max_neo_node_ + 1 && k < n_ghost; ++k)
        if (k - 1 < n_real) rhs[k - 1] += ghost_rhs_[k];
}}

}} // namespace neospice
'''
```

- [ ] **Step 4: Run tests**

```bash
cd /home/subhagato/Codes/spice-cpp && python -m pytest tools/tests/test_gen_adapter.py -v
```
Expected: All PASS

- [ ] **Step 5: Commit**

```bash
cd /home/subhagato/Codes/spice-cpp
git add tools/ngspice_migrate/gen_adapter.py tools/tests/test_gen_adapter.py
git commit -m "feat(tools): device adapter generator for migrated models"
```

---

## Task 6: CMakeLists generator and post-generation validation

**Files:**
- Create: `tools/ngspice_migrate/gen_cmake.py`
- Create: `tools/ngspice_migrate/validation.py`
- Create: `tools/tests/test_gen_cmake.py`
- Create: `tools/tests/test_validation.py`

- [ ] **Step 1: Write failing test for CMake gen**

```python
# tools/tests/test_gen_cmake.py
from ngspice_migrate.gen_cmake import generate_cmake
from ngspice_migrate.descriptor import ModelDescriptor, Terminal

def make_desc():
    return ModelDescriptor(
        prefix="DIO", neospice_name="dio", namespace="dio",
        instance_struct="DIOinstance", model_struct="DIOmodel",
        instance_tag="sDIOinstance", model_tag="sDIOmodel",
        cpp_instance="DIOInstance", cpp_model="DIOModel",
        gen_instance="GENinstance", gen_model="GENmodel",
        terminals=[Terminal("pos", "DIOposNode"), Terminal("neg", "DIOnegNode")],
        state_count=7, state_base_field="DIOstate",
        next_instance_field="DIOnextInstance", instances_field="DIOinstances",
        next_model_field="DIOnextModel", model_ptr_field="DIOmodPtr",
        name_field="DIOname",
        source_files={"setup": "diosetup.c", "load": "dioload.c", "temp": "diotemp.c"},
    )

def test_cmake_has_object_library():
    cmake = generate_cmake(make_desc(), ["dio_shim.cpp", "dio_setup.cpp", "dio_load.cpp", "dio_temp.cpp", "dio_device.cpp"])
    assert "add_library(dio_obj OBJECT" in cmake
    assert "dio_shim.cpp" in cmake
    assert "dio_device.cpp" in cmake
    assert "target_include_directories" in cmake
```

- [ ] **Step 2: Write failing test for validation**

```python
# tools/tests/test_validation.py
from ngspice_migrate.validation import count_tstalloc_sites, count_stamp_rewrites

def test_count_tstalloc():
    setup_src = '''
#define TSTALLOC(ptr,first,second) \\
do { /* ... */ } while(0)
    TSTALLOC(DIOposPosPtr,DIOposNode,DIOposNode);
    TSTALLOC(DIOnegNegPtr,DIOnegNode,DIOnegNode);
    TSTALLOC(DIOposPrimePosPrimePtr,DIOposPrimeNode,DIOposPrimeNode);
'''
    assert count_tstalloc_sites(setup_src) == 3

def test_count_stamp_rewrites():
    load_src = '''
    mat.add(here->DIOposPosPtr, gspr);
    mat.add(here->DIOnegNegPtr, gd);
    mat.add(here->DIOposPrimePosPrimePtr, -(gd + gspr));
'''
    assert count_stamp_rewrites(load_src, "DIO") == 3
```

- [ ] **Step 3: Implement**

```python
# tools/ngspice_migrate/gen_cmake.py
"""Generate CMakeLists.txt for a migrated model."""
from __future__ import annotations

from .descriptor import ModelDescriptor


def generate_cmake(desc: ModelDescriptor, source_files: list[str]) -> str:
    ns = desc.neospice_name
    obj_name = f"{ns}_obj"
    sources = "\n".join(f"    {f}" for f in source_files)
    return f'''add_library({obj_name} OBJECT
{sources}
)
target_include_directories({obj_name} PUBLIC ${{CMAKE_SOURCE_DIR}}/src)
'''
```

```python
# tools/ngspice_migrate/validation.py
"""Post-generation validation checks."""
from __future__ import annotations

import re


def count_tstalloc_sites(setup_src: str) -> int:
    lines = setup_src.split("\n")
    count = 0
    for line in lines:
        stripped = line.strip()
        if stripped.startswith("#define"):
            continue
        if re.search(r"\bTSTALLOC\s*\(", stripped):
            count += 1
    return count


def count_stamp_rewrites(load_src: str, prefix: str) -> int:
    pattern = re.compile(r"\bmat\.add\s*\(\s*here\s*->\s*" + re.escape(prefix) + r"[A-Za-z0-9_]+Ptr")
    return len(pattern.findall(load_src))


def validate_migration(setup_src: str, load_src: str, prefix: str) -> list[str]:
    issues = []
    tstalloc = count_tstalloc_sites(setup_src)
    stamps = count_stamp_rewrites(load_src, prefix)

    # Check for unrewritten stamp sites
    unrewritten = re.findall(
        r"\*\s*\(\s*here\s*->\s*" + re.escape(prefix) + r"[A-Za-z0-9_]+Ptr\s*\)",
        load_src,
    )
    if unrewritten:
        issues.append(f"{len(unrewritten)} unrewritten stamp site(s) found in load file")

    # Report counts
    issues.append(f"INFO: {tstalloc} TSTALLOC sites in setup, {stamps} mat.add calls in load")

    return issues
```

- [ ] **Step 4: Run tests**

```bash
cd /home/subhagato/Codes/spice-cpp && python -m pytest tools/tests/test_gen_cmake.py tools/tests/test_validation.py -v
```
Expected: All PASS

- [ ] **Step 5: Commit**

```bash
cd /home/subhagato/Codes/spice-cpp
git add tools/ngspice_migrate/gen_cmake.py tools/ngspice_migrate/validation.py \
        tools/tests/test_gen_cmake.py tools/tests/test_validation.py
git commit -m "feat(tools): CMake generator and post-migration validation"
```

---

## Task 7: CLI orchestrator (`__main__.py`)

Wire all components together into a single CLI command.

**Files:**
- Create: `tools/ngspice_migrate/__main__.py`

- [ ] **Step 1: Write the CLI**

```python
# tools/ngspice_migrate/__main__.py
"""CLI entry point: python -m ngspice_migrate DESCRIPTOR NGSPICE_DIR OUTPUT_DIR"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

from .descriptor import load_descriptor
from .transformer import Transformer
from .gen_def import generate_def_hpp
from .gen_shim import generate_shim_hpp, generate_shim_cpp
from .gen_adapter import generate_adapter_hpp, generate_adapter_cpp
from .gen_cmake import generate_cmake
from .validation import validate_migration


def main() -> int:
    p = argparse.ArgumentParser(description="Migrate ngspice C model to neospice C++")
    p.add_argument("descriptor", help="Path to model descriptor YAML")
    p.add_argument("ngspice_dir", help="Path to ngspice model source directory")
    p.add_argument("output_dir", help="Output directory (e.g. src/devices/dio)")
    p.add_argument("--dry-run", action="store_true", help="Print what would be generated")
    args = p.parse_args()

    desc = load_descriptor(Path(args.descriptor))
    ng_dir = Path(args.ngspice_dir)
    out_dir = Path(args.output_dir)

    if not args.dry_run:
        out_dir.mkdir(parents=True, exist_ok=True)

    ns = desc.neospice_name
    cfg = desc.to_transformer_config()
    transformer = Transformer(cfg)

    generated_sources = []

    # 1. Translate each source file
    for role, filename in desc.source_files.items():
        src_path = ng_dir / filename
        if not src_path.exists():
            print(f"WARNING: {src_path} not found, skipping {role}", file=sys.stderr)
            continue
        raw = src_path.read_text(encoding="utf-8")
        translated = transformer.translate(raw)
        out_name = f"{ns}_{role}.cpp"
        out_path = out_dir / out_name
        if args.dry_run:
            print(f"Would write: {out_path} ({len(translated)} chars)")
        else:
            out_path.write_text(translated, encoding="utf-8")
            print(f"Wrote: {out_path}")
        generated_sources.append(out_name)

    # 2. Generate def.hpp from def.h
    def_h_candidates = list(ng_dir.glob("*def*.h"))
    if def_h_candidates:
        def_src = def_h_candidates[0].read_text(encoding="utf-8")
        def_hpp = generate_def_hpp(def_src, desc)
        def_path = out_dir / f"{ns}_def.hpp"
        if not args.dry_run:
            def_path.write_text(def_hpp, encoding="utf-8")
            print(f"Wrote: {def_path}")

    # 3. Generate shim
    shim_hpp = generate_shim_hpp(desc)
    shim_cpp = generate_shim_cpp(desc)
    if not args.dry_run:
        (out_dir / f"{ns}_shim.hpp").write_text(shim_hpp, encoding="utf-8")
        (out_dir / f"{ns}_shim.cpp").write_text(shim_cpp, encoding="utf-8")
        print(f"Wrote: {out_dir / f'{ns}_shim.hpp'}")
        print(f"Wrote: {out_dir / f'{ns}_shim.cpp'}")
    generated_sources.insert(0, f"{ns}_shim.cpp")

    # 4. Generate adapter
    adapter_hpp = generate_adapter_hpp(desc)
    adapter_cpp = generate_adapter_cpp(desc)
    if not args.dry_run:
        (out_dir / f"{ns}_device.hpp").write_text(adapter_hpp, encoding="utf-8")
        (out_dir / f"{ns}_device.cpp").write_text(adapter_cpp, encoding="utf-8")
        print(f"Wrote: {out_dir / f'{ns}_device.hpp'}")
        print(f"Wrote: {out_dir / f'{ns}_device.cpp'}")
    generated_sources.append(f"{ns}_device.cpp")

    # 5. Generate CMakeLists.txt
    cmake = generate_cmake(desc, generated_sources)
    if not args.dry_run:
        (out_dir / "CMakeLists.txt").write_text(cmake, encoding="utf-8")
        print(f"Wrote: {out_dir / 'CMakeLists.txt'}")

    # 6. Validation
    setup_file = out_dir / f"{ns}_setup.cpp"
    load_file = out_dir / f"{ns}_load.cpp"
    if not args.dry_run and setup_file.exists() and load_file.exists():
        issues = validate_migration(
            setup_file.read_text(), load_file.read_text(), desc.prefix,
        )
        for issue in issues:
            print(f"  {issue}")

    print(f"\nMigration complete: {len(generated_sources)} source files generated in {out_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 2: Test manually with BSIM4v7 (dry run)**

```bash
cd /home/subhagato/Codes/spice-cpp
python -m tools.ngspice_migrate \
    tools/descriptors/bsim4v7.yaml \
    /home/subhagato/Codes/ngspice/src/spicelib/devices/bsim4v7 \
    /tmp/bsim4v7_test \
    --dry-run
```
Expected: Lists files that would be generated, no errors

- [ ] **Step 3: Test full generation to temp directory**

```bash
cd /home/subhagato/Codes/spice-cpp
python -m tools.ngspice_migrate \
    tools/descriptors/bsim4v7.yaml \
    /home/subhagato/Codes/ngspice/src/spicelib/devices/bsim4v7 \
    /tmp/bsim4v7_test
```
Expected: All files generated, validation reports TSTALLOC/stamp counts

- [ ] **Step 4: Commit**

```bash
cd /home/subhagato/Codes/spice-cpp
git add tools/ngspice_migrate/__main__.py
git commit -m "feat(tools): CLI orchestrator for ngspice model migration"
```

---

## Task 8: BSIM4v7 roundtrip validation

Generate BSIM4v7 from ngspice source using the tool and diff against the existing hand-ported files. This validates that the generic tool produces equivalent output.

**Files:**
- Create: `tools/tests/test_bsim4v7_roundtrip.py`

- [ ] **Step 1: Write roundtrip test**

```python
# tools/tests/test_bsim4v7_roundtrip.py
"""Regression test: generate BSIM4v7 with the migration tool and compare
against the existing hand-ported files.

Accepted diffs (from Phase-1a/1b):
- Header/banner differences
- #undef trailers present in hand-port but not in tool output
- Minor whitespace/comment differences
- TSTALLOC macro handling differences

Unacceptable diffs:
- Missing mat.add() rewrites
- Wrong type substitutions
- Missing namespace wrapper
"""
import subprocess
import tempfile
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).parent.parent.parent
DESCRIPTOR = REPO_ROOT / "tools" / "descriptors" / "bsim4v7.yaml"
NGSPICE_DIR = Path("/home/subhagato/Codes/ngspice/src/spicelib/devices/bsim4v7")
EXISTING_DIR = REPO_ROOT / "src" / "devices" / "bsim4v7"


@pytest.mark.skipif(not NGSPICE_DIR.exists(), reason="ngspice source not available")
class TestBSIM4v7Roundtrip:

    def test_load_stamp_count_matches(self):
        """The number of mat.add() calls in the generated load file should
        match the existing hand-port."""
        import re
        from ngspice_migrate.descriptor import load_descriptor
        from ngspice_migrate.transformer import Transformer

        desc = load_descriptor(DESCRIPTOR)
        cfg = desc.to_transformer_config()
        t = Transformer(cfg)

        raw = (NGSPICE_DIR / "b4v7ld.c").read_text()
        generated = t.translate(raw)

        existing = (EXISTING_DIR / "bsim4v7_load.cpp").read_text()

        gen_stamps = len(re.findall(r"mat\.add\(here->", generated))
        existing_stamps = len(re.findall(r"mat\.add\(here->", existing))
        assert gen_stamps == existing_stamps, (
            f"Generated has {gen_stamps} stamp sites, existing has {existing_stamps}"
        )

    def test_type_substitutions_complete(self):
        """No raw ngspice types should remain in generated output."""
        from ngspice_migrate.descriptor import load_descriptor
        from ngspice_migrate.transformer import Transformer

        desc = load_descriptor(DESCRIPTOR)
        cfg = desc.to_transformer_config()
        t = Transformer(cfg)

        for role, filename in desc.source_files.items():
            src_path = NGSPICE_DIR / filename
            if not src_path.exists():
                continue
            generated = t.translate(src_path.read_text())

            # These should all be replaced
            assert "CKTcircuit" not in generated, f"{role}: CKTcircuit not replaced"
            assert "GENmodel" not in generated or "GENmodel" in generated.split("/*")[0], \
                f"{role}: GENmodel not replaced"
            assert "SMPmatrix" not in generated, f"{role}: SMPmatrix not replaced"

    def test_no_unrewritten_stamps(self):
        """All *(here->...Ptr) sites should be rewritten to mat.add()."""
        import re
        from ngspice_migrate.descriptor import load_descriptor
        from ngspice_migrate.transformer import Transformer

        desc = load_descriptor(DESCRIPTOR)
        cfg = desc.to_transformer_config()
        t = Transformer(cfg)

        raw = (NGSPICE_DIR / "b4v7ld.c").read_text()
        generated = t.translate(raw)

        unrewritten = re.findall(
            r"\*\s*\(\s*here\s*->\s*BSIM4v7[A-Za-z0-9_]+Ptr\s*\)\s*[+\-]?=",
            generated,
        )
        assert len(unrewritten) == 0, f"Unrewritten stamps: {unrewritten[:5]}"
```

- [ ] **Step 2: Run roundtrip test**

```bash
cd /home/subhagato/Codes/spice-cpp && PYTHONPATH=tools python -m pytest tools/tests/test_bsim4v7_roundtrip.py -v
```
Expected: All PASS

- [ ] **Step 3: Commit**

```bash
cd /home/subhagato/Codes/spice-cpp
git add tools/tests/test_bsim4v7_roundtrip.py
git commit -m "test(tools): BSIM4v7 roundtrip validation for migration tool"
```

---

## Task 9: Diode model descriptor and first migration test

Write a descriptor for the ngspice diode model and run the migration tool to generate a complete neospice diode module. This validates the tool works on a model other than BSIM4v7.

**Files:**
- Create: `tools/descriptors/dio.yaml`

- [ ] **Step 1: Write the diode descriptor**

```yaml
# tools/descriptors/dio.yaml
model:
  ngspice_prefix: "DIO"
  neospice_name: "dio"
  neospice_namespace: "dio"
  instance_struct: "DIOinstance"
  model_struct: "DIOmodel"
  instance_tag: "sDIOinstance"
  model_tag: "sDIOmodel"
  cpp_instance: "DIOInstance"
  cpp_model: "DIOModel"
  gen_instance: "GENinstance"
  gen_model: "GENmodel"
  terminals:
    - { name: "pos", field: "DIOposNode" }
    - { name: "neg", field: "DIOnegNode" }
  state_count: 7
  state_base_field: "DIOstate"
  next_instance_field: "DIOnextInstance"
  instances_field: "DIOinstances"
  next_model_field: "DIOnextModel"
  model_ptr_field: "DIOmodPtr"
  name_field: "DIOname"
  matrix_ptr_suffix: "Ptr"
  source_files:
    setup: "diosetup.c"
    load: "dioload.c"
    temp: "diotemp.c"
    param: "dioparam.c"
    mpar: "diompar.c"
  skip_files:
    - "dioacld.c"
    - "dionoise.c"
    - "diodisto.c"
    - "diodset.c"
    - "dioask.c"
    - "diomask.c"
    - "diodel.c"
    - "diodest.c"
    - "diogetic.c"
    - "diotrunc.c"
    - "dioconv.c"
    - "dioinit.c"
    - "diosoachk.c"
    - "diosacl.c"
    - "diosload.c"
    - "diosprt.c"
    - "diosset.c"
    - "diosupd.c"
    - "dio.c"
  defines: []
  has_internal_nodes: true
  setup_function: "DIOsetup"
  temp_function: "DIOtemp"
  load_function: "DIOload"
  geometry:
    - { name: "area", field: "DIOarea", given: "DIOareaGiven", default: "1.0" }
    - { name: "pj", field: "DIOpj", given: "DIOpjGiven", default: "0.0" }
    - { name: "m", field: "DIOm", given: "DIOmGiven", default: "1.0" }
```

- [ ] **Step 2: Run migration tool on diode model**

```bash
cd /home/subhagato/Codes/spice-cpp
python -m tools.ngspice_migrate \
    tools/descriptors/dio.yaml \
    /home/subhagato/Codes/ngspice/src/spicelib/devices/dio \
    /tmp/dio_test
```
Expected: All files generated successfully, validation shows TSTALLOC and stamp counts

- [ ] **Step 3: Inspect generated output for correctness**

```bash
# Check namespace wrapping
grep "namespace neospice::dio" /tmp/dio_test/dio_load.cpp

# Check stamp rewrites
grep "mat.add" /tmp/dio_test/dio_load.cpp | wc -l

# Check no unrewritten stamps
grep '*(here->' /tmp/dio_test/dio_load.cpp | grep -v '//' | head -5

# Check def.hpp
grep "struct DIOInstance" /tmp/dio_test/dio_def.hpp
grep "MatrixOffset" /tmp/dio_test/dio_def.hpp
```

- [ ] **Step 4: Commit**

```bash
cd /home/subhagato/Codes/spice-cpp
git add tools/descriptors/dio.yaml
git commit -m "feat(tools): diode model descriptor for migration tool"
```

---

## Summary

| Task | Component | Test Count |
|------|-----------|-----------|
| T1 | Generic transformer | 9 |
| T2 | Descriptor loader | 3 |
| T3 | def.h → def.hpp generator | 2 |
| T4 | Shim generator | 4 |
| T5 | Adapter generator | 4 |
| T6 | CMake + validation | 3 |
| T7 | CLI orchestrator | manual |
| T8 | BSIM4v7 roundtrip | 3 |
| T9 | Diode descriptor + test | manual |

**After completing all 9 tasks, running the tool on a new model is:**

```bash
# 1. Write a ~50-line YAML descriptor
# 2. Run one command:
python -m tools.ngspice_migrate descriptors/bjt.yaml /path/to/ngspice/bjt /tmp/bjt_output

# 3. Review generated output, tweak assign_offsets RESOLVE list if needed
# 4. Copy to src/devices/bjt/, add to parent CMakeLists, build and test
```

**What still requires manual work per model:**
1. Writing the YAML descriptor (~15 min of reading the model's def.h)
2. Reviewing the `assign_offsets()` RESOLVE list (auto-extractable from def.hpp in a future enhancement)
3. Wiring the model into the parser (model card creation, `.model` directive handling)
4. Writing ngspice-compared validation tests
5. Model-specific adapter tweaks (e.g. BSIM4's pSizeDependParamKnot cleanup, version stamping)

**What's fully automated:**
1. All source file translation (8-pass pipeline)
2. def.h → def.hpp conversion
3. Shim layer generation (hpp + cpp)
4. Device adapter scaffolding (hpp + cpp)
5. CMakeLists.txt
6. Post-generation validation (TSTALLOC/stamp counting, unrewritten stamp detection)
