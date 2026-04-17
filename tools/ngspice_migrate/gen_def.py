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
    # 7. Replace IFuid with const char *
    # -----------------------------------------------------------------
    # Consume trailing whitespace so ``IFuid DIOname`` becomes
    # ``const char *DIOname`` rather than ``const char * DIOname``.
    text = re.sub(r'\bIFuid\s+', 'const char *', text)
    # Fallback for bare IFuid without following whitespace (unlikely but safe)
    text = re.sub(r'\bIFuid\b', 'const char *', text)

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
    # 9. Wrap in #pragma once, includes, namespace
    # -----------------------------------------------------------------
    wrapped = (
        "#pragma once\n"
        "\n"
        '#include "core/matrix.hpp"\n'
        "\n"
        f"namespace neospice::{desc.namespace} {{\n"
        "\n"
        + text.strip()
        + "\n"
        "\n"
        f"}} // namespace neospice::{desc.namespace}\n"
    )

    return wrapped
