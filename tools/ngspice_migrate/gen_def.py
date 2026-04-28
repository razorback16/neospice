"""Generator that parses ngspice *def.h files and emits clean C++ *_def.hpp files.

Given the raw text of an ngspice ``typedef struct sXXXinstance { ... } XXXinstance;``
header and a :class:`ModelDescriptor`, this module applies a sequence of regex-based
transformations to produce a C++ header suitable for the neospice build:

1. Strip ngspice / system includes
2. Remove old include guards (``#ifndef`` / ``#define`` / trailing ``#endif``)
3. Rename instance and model ``typedef struct`` to plain ``struct CppName``
4. Replace struct tags in pointer declarations
5. Replace ``double *`` matrix-pointer fields with ``neospice::MatrixOffset``
6. Replace ``IFuid`` with ``const char *``
7. Replace ``GENmodel`` / ``GENinstance`` references with C++ types
8. Wrap in ``#pragma once``, includes, and ``namespace neospice::<ns>``
"""

from __future__ import annotations

import re
from dataclasses import dataclass
from typing import Optional

# Try importing the real ModelDescriptor; fall back to a minimal stub so the
# module can be used / tested standalone before descriptor.py is merged.
try:
    from .descriptor import ModelDescriptor  # type: ignore[import-untyped]
except (ImportError, ModuleNotFoundError):

    @dataclass
    class ModelDescriptor:  # type: ignore[no-redef]
        """Minimal stub matching the fields used by gen_def."""

        prefix: str
        neospice_name: str
        namespace: str
        instance_struct: str
        model_struct: str
        instance_tag: str
        model_tag: str
        cpp_instance: str
        cpp_model: str
        gen_instance: str = "GENinstance"
        gen_model: str = "GENmodel"
        matrix_ptr_suffix: str = "Ptr"
        setup_function: str = ""
        temp_function: str = ""
        load_function: str = ""


def generate_def_hpp(src: str, desc: ModelDescriptor) -> str:
    """Transform an ngspice ``*def.h`` source into a C++ ``*_def.hpp`` header.

    Parameters
    ----------
    src:
        Raw contents of the ngspice def.h file.
    desc:
        Model descriptor carrying all naming information.

    Returns
    -------
    str
        The transformed C++ header text.
    """
    text = src

    # -----------------------------------------------------------------
    # 1. Strip ngspice / system includes
    # -----------------------------------------------------------------
    # Remove #include "ngspice/..."
    text = re.sub(
        r'^[ \t]*#\s*include\s+"ngspice/[^"]*"\s*\n',
        "",
        text,
        flags=re.MULTILINE,
    )
    # Remove #include "*.h"  (any bare .h header)
    text = re.sub(
        r'^[ \t]*#\s*include\s+"[^"]*\.h"\s*\n',
        "",
        text,
        flags=re.MULTILINE,
    )
    # Remove #include <...>
    text = re.sub(
        r'^[ \t]*#\s*include\s+<[^>]*>\s*\n',
        "",
        text,
        flags=re.MULTILINE,
    )

    # -----------------------------------------------------------------
    # 2. Remove old include guard (#ifndef FOO / #define FOO ... #endif)
    # -----------------------------------------------------------------
    # Strip the leading #ifndef GUARD / #define GUARD pair (first occurrence)
    text = re.sub(
        r'^[ \t]*#\s*ifndef\s+\w+\s*\n[ \t]*#\s*define\s+\w+\s*\n',
        "",
        text,
        count=1,
        flags=re.MULTILINE,
    )
    # Strip the trailing #endif that closed the guard.
    # We look for the *last* #endif (possibly followed by a comment) at EOF.
    text = re.sub(
        r'\n[ \t]*#\s*endif\b[^\n]*\s*\Z',
        "\n",
        text,
    )

    # -----------------------------------------------------------------
    # 3. Rename instance typedef struct
    # -----------------------------------------------------------------
    # ``typedef struct sXXXinstance {`` -> ``struct CppInstance {``
    text = re.sub(
        r'\btypedef\s+struct\s+'
        + re.escape(desc.instance_tag)
        + r'\s*\{',
        f"struct {desc.cpp_instance} {{",
        text,
    )
    # Closing: ``} XXXinstance ;`` -> ``};``
    text = re.sub(
        r'\}\s*' + re.escape(desc.instance_struct) + r'\s*;',
        "};",
        text,
    )

    # -----------------------------------------------------------------
    # 4. Rename model typedef struct
    # -----------------------------------------------------------------
    text = re.sub(
        r'\btypedef\s+struct\s+'
        + re.escape(desc.model_tag)
        + r'\s*\{',
        f"struct {desc.cpp_model} {{",
        text,
    )
    text = re.sub(
        r'\}\s*' + re.escape(desc.model_struct) + r'\s*;',
        "};",
        text,
    )

    # -----------------------------------------------------------------
    # 5. Replace struct tags in pointer declarations
    # -----------------------------------------------------------------
    text = re.sub(
        r'\bstruct\s+' + re.escape(desc.instance_tag) + r'\b',
        desc.cpp_instance,
        text,
    )
    text = re.sub(
        r'\bstruct\s+' + re.escape(desc.model_tag) + r'\b',
        desc.cpp_model,
        text,
    )

    # -----------------------------------------------------------------
    # 5b. Replace bare typedef names (not preceded by 'struct')
    # -----------------------------------------------------------------
    # After removing the typedef closing lines in steps 3-4, bare uses of
    # the old typedef names (e.g. ``BSIM4v7instance *BSIM4v7instances;``)
    # must be renamed to the C++ struct names.
    text = re.sub(
        r'\b' + re.escape(desc.instance_struct) + r'\b',
        desc.cpp_instance,
        text,
    )
    text = re.sub(
        r'\b' + re.escape(desc.model_struct) + r'\b',
        desc.cpp_model,
        text,
    )

    # -----------------------------------------------------------------
    # 6. Replace double * matrix pointer fields with MatrixOffset
    # -----------------------------------------------------------------
    # Match lines like:  ``    double *PREFIXfooBarPtr;``
    # where the field name ends with ``desc.matrix_ptr_suffix`` (default "Ptr").
    suffix = re.escape(desc.matrix_ptr_suffix)
    text = re.sub(
        r'^([ \t]*)double\s+\*\s*(\w+' + suffix + r')\s*;',
        r'\1neospice::MatrixOffset \2{-1};',
        text,
        flags=re.MULTILINE,
    )

    # -----------------------------------------------------------------
    # 6b. Add #undef before distortion-coefficient #defines
    # -----------------------------------------------------------------
    # ngspice device headers define short macro names (cdr_x, capgs2, ggs1,
    # etc.) that map to device-specific arrays.  When multiple device headers
    # coexist in one TU the macros collide.  Prefixing each #define with
    # #undef silences -Wmacro-redefined.
    def _add_undef(m: re.Match) -> str:
        indent = m.group(1)
        name = m.group(2)
        tabs = m.group(3)
        rest = m.group(4)
        return f"{indent}#undef\t{name}\n{indent}#define\t{name}{tabs}{rest}"

    text = re.sub(
        r'^([ \t]*)#define\t(\w+)(\t+)(\w+dCoeffs\[.+)$',
        _add_undef,
        text,
        flags=re.MULTILINE,
    )

    # -----------------------------------------------------------------
    # 7. Replace IFuid with const char *
    # -----------------------------------------------------------------
    # Consume trailing whitespace so ``IFuid DIOname`` becomes
    # ``const char *DIOname`` rather than ``const char * DIOname``.
    text = re.sub(r'\bIFuid\s+', 'const char *', text)
    # Fallback for bare IFuid without following whitespace (unlikely but safe)
    text = re.sub(r'\bIFuid\b', 'const char *', text)

    # -----------------------------------------------------------------
    # 7b. Convert char * struct fields to const char *
    # -----------------------------------------------------------------
    text = re.sub(
        r'^([ \t]+)char\s+\*',
        r'\1const char *',
        text,
        flags=re.MULTILINE,
    )

    # -----------------------------------------------------------------
    # 8. Replace GENmodel / GENinstance references
    # -----------------------------------------------------------------
    text = re.sub(
        r'\b' + re.escape(desc.gen_model) + r'\b',
        desc.cpp_model,
        text,
    )
    text = re.sub(
        r'\b' + re.escape(desc.gen_instance) + r'\b',
        desc.cpp_instance,
        text,
    )

    # -----------------------------------------------------------------
    # 8b. Replace CKTcircuit with forward-declared Shim::Ckt
    # -----------------------------------------------------------------
    text = re.sub(r'\bCKTcircuit\s*\*', 'Shim::Ckt *', text)

    # -----------------------------------------------------------------
    # 9. Wrap in #pragma once, includes, namespace
    # -----------------------------------------------------------------
    nstatvars_guard = (
        "#ifndef NSTATVARS\n"
        "#define NSTATVARS 3\n"
        "#endif\n"
        "\n"
    )

    wrapped = (
        "#pragma once\n"
        "\n"
        '#include "core/matrix.hpp"\n'
        "\n"
        + nstatvars_guard
        + f"namespace neospice::{desc.namespace} {{\n"
        "\n"
        "namespace Shim { struct Ckt; class Matrix; }\n"
        "\n"
        f"struct {desc.cpp_model};\n"
        "\n"
        + text.strip()
        + "\n"
        "\n"
        "// --- Parameter tables and dispatchers (defined in devsup/mpar) ------\n"
        f"namespace Shim {{ struct IfParm; struct IfValue; }}\n"
        f"extern Shim::IfParm {desc.prefix}pTable[];\n"
        f"extern int {desc.prefix}pTSize;\n"
        f"extern Shim::IfParm {desc.prefix}mPTable[];\n"
        f"extern int {desc.prefix}mPTSize;\n"
        f"int {desc.prefix}mParam(int param, Shim::IfValue *value, {desc.cpp_model} *model);\n"
        f"int {desc.prefix}param(int param, Shim::IfValue *value, {desc.cpp_instance} *inst, Shim::IfValue *select);\n"
        "\n"
        "// --- UCB entry points (defined in setup/temp/load .cpp) ------\n"
        + (f"int {desc.setup_function}(Shim::Matrix*, {desc.cpp_model}*, Shim::Ckt*, int*);\n" if desc.setup_function else "")
        + (f"int {desc.temp_function}({desc.cpp_model}*, Shim::Ckt*);\n" if desc.temp_function else "")
        + (f"int {desc.load_function}({desc.cpp_model}*, Shim::Ckt*);\n" if desc.load_function else "")
        + "\n"
        f"}} // namespace neospice::{desc.namespace}\n"
    )

    return wrapped


def extract_matrix_offset_fields(def_text: str) -> list[str]:
    """Extract field names typed as neospice::MatrixOffset from def header text.

    Returns list of field names in order of first occurrence.
    """
    return list(dict.fromkeys(re.findall(r'neospice::MatrixOffset\s+(\w+)\s*\{', def_text)))
