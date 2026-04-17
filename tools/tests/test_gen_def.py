"""Unit tests for the def.h -> def.hpp generator."""

from __future__ import annotations

import textwrap
from dataclasses import dataclass

import pytest

from ngspice_migrate.gen_def import generate_def_hpp


# ---------------------------------------------------------------------------
# Minimal stub for ModelDescriptor (mirrors the fields used by gen_def)
# ---------------------------------------------------------------------------

@dataclass
class _Descriptor:
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


@pytest.fixture
def desc() -> _Descriptor:
    """Standard diode descriptor for tests."""
    return _Descriptor(
        prefix="DIO",
        neospice_name="dio",
        namespace="dio",
        instance_struct="DIOinstance",
        model_struct="DIOmodel",
        instance_tag="sDIOinstance",
        model_tag="sDIOmodel",
        cpp_instance="DIOInstance",
        cpp_model="DIOModel",
    )


# ---------------------------------------------------------------------------
# test_struct_rename
# ---------------------------------------------------------------------------

def test_struct_rename(desc):
    """Instance typedef -> C++ struct, matrix pointers -> MatrixOffset, etc."""
    src = textwrap.dedent("""\
        typedef struct sDIOinstance {
            struct sDIOmodel *DIOmodPtr;
            struct sDIOinstance *DIOnextInstance;
            IFuid DIOname;
            double *DIOposPosPtr;
            double *DIOnegNegPtr;
            double DIOcap;
        } DIOinstance;
    """)
    result = generate_def_hpp(src, desc)

    assert "#pragma once" in result
    assert "namespace neospice::dio" in result
    assert "struct DIOInstance {" in result
    assert "MatrixOffset DIOposPosPtr" in result
    assert "MatrixOffset DIOnegNegPtr" in result
    assert "double DIOcap;" in result  # non-pointer field unchanged
    assert "const char *DIOname;" in result
    assert '#include "ngspice' not in result


# ---------------------------------------------------------------------------
# test_model_struct_rename
# ---------------------------------------------------------------------------

def test_model_struct_rename(desc):
    """Model typedef -> C++ struct."""
    src = textwrap.dedent("""\
        typedef struct sDIOmodel {
            int DIOtype;
            struct sDIOmodel *DIOnextModel;
        } DIOmodel;
    """)
    result = generate_def_hpp(src, desc)

    assert "struct DIOModel {" in result
    assert "DIOModel *DIOnextModel;" in result
    assert "typedef" not in result


# ---------------------------------------------------------------------------
# test_strip_ngspice_includes
# ---------------------------------------------------------------------------

def test_strip_ngspice_includes(desc):
    """All ngspice / system includes are removed."""
    src = textwrap.dedent("""\
        #include "ngspice/ngspice.h"
        #include "ngspice/cktdefs.h"
        #include "ifsim.h"
        #include "gendefs.h"
        #include <stdio.h>
        #include <math.h>
        typedef struct sDIOinstance {
            double DIOcap;
        } DIOinstance;
    """)
    result = generate_def_hpp(src, desc)

    assert '#include "ngspice/' not in result
    assert '#include "ifsim.h"' not in result
    assert '#include "gendefs.h"' not in result
    assert "#include <stdio.h>" not in result
    assert "#include <math.h>" not in result
    # The core/matrix.hpp include *is* present
    assert '#include "core/matrix.hpp"' in result


# ---------------------------------------------------------------------------
# test_remove_include_guard
# ---------------------------------------------------------------------------

def test_remove_include_guard(desc):
    """Old #ifndef / #define guard and trailing #endif are stripped."""
    src = textwrap.dedent("""\
        #ifndef DIO_DEF_H
        #define DIO_DEF_H

        typedef struct sDIOinstance {
            double DIOcap;
        } DIOinstance;

        #endif /* DIO_DEF_H */
    """)
    result = generate_def_hpp(src, desc)

    assert "#pragma once" in result
    assert "#ifndef DIO_DEF_H" not in result
    assert "#define DIO_DEF_H" not in result
    # The trailing #endif for the guard should be gone
    assert "#endif" not in result


# ---------------------------------------------------------------------------
# test_replace_ifuid
# ---------------------------------------------------------------------------

def test_replace_ifuid(desc):
    """IFuid -> const char *."""
    src = textwrap.dedent("""\
        typedef struct sDIOinstance {
            IFuid DIOname;
        } DIOinstance;
    """)
    result = generate_def_hpp(src, desc)

    assert "const char *DIOname;" in result
    assert "IFuid" not in result


# ---------------------------------------------------------------------------
# test_replace_gen_types
# ---------------------------------------------------------------------------

def test_replace_gen_types(desc):
    """GENmodel / GENinstance -> C++ types."""
    src = textwrap.dedent("""\
        typedef struct sDIOinstance {
            GENmodel *genmod;
            GENinstance *geninst;
        } DIOinstance;
    """)
    result = generate_def_hpp(src, desc)

    assert "DIOModel *genmod;" in result
    assert "DIOInstance *geninst;" in result
    assert "GENmodel" not in result
    assert "GENinstance" not in result


# ---------------------------------------------------------------------------
# test_matrix_offset_init
# ---------------------------------------------------------------------------

def test_matrix_offset_init(desc):
    """MatrixOffset fields are initialised to -1."""
    src = textwrap.dedent("""\
        typedef struct sDIOinstance {
            double *DIOposPosPtr;
        } DIOinstance;
    """)
    result = generate_def_hpp(src, desc)

    assert "neospice::MatrixOffset DIOposPosPtr{-1};" in result


# ---------------------------------------------------------------------------
# test_double_non_ptr_unchanged
# ---------------------------------------------------------------------------

def test_double_non_ptr_unchanged(desc):
    """Plain ``double`` fields (not pointers ending in Ptr) are left alone."""
    src = textwrap.dedent("""\
        typedef struct sDIOinstance {
            double DIOvoltage;
            double *DIOposNegPtr;
        } DIOinstance;
    """)
    result = generate_def_hpp(src, desc)

    assert "double DIOvoltage;" in result
    assert "MatrixOffset DIOposNegPtr" in result


# ---------------------------------------------------------------------------
# test_namespace_closing
# ---------------------------------------------------------------------------

def test_namespace_closing(desc):
    """Output has correct namespace opening and closing."""
    src = textwrap.dedent("""\
        typedef struct sDIOinstance {
            int x;
        } DIOinstance;
    """)
    result = generate_def_hpp(src, desc)

    assert "namespace neospice::dio {" in result
    assert "} // namespace neospice::dio" in result


# ---------------------------------------------------------------------------
# test_struct_tag_in_pointer_decl
# ---------------------------------------------------------------------------

def test_struct_tag_in_pointer_decl(desc):
    """``struct sDIOinstance *`` -> ``DIOInstance *``."""
    src = textwrap.dedent("""\
        typedef struct sDIOinstance {
            struct sDIOinstance *DIOnextInstance;
            struct sDIOmodel *DIOmodPtr;
        } DIOinstance;
    """)
    result = generate_def_hpp(src, desc)

    assert "DIOInstance *DIOnextInstance;" in result
    assert "DIOModel *DIOmodPtr;" in result
    assert "struct sDIO" not in result


# ---------------------------------------------------------------------------
# test_full_round_trip
# ---------------------------------------------------------------------------

def test_full_round_trip(desc):
    """End-to-end test with a realistic snippet containing both structs."""
    src = textwrap.dedent("""\
        #ifndef DIODEF_H
        #define DIODEF_H

        #include "ngspice/ngspice.h"
        #include "ifsim.h"
        #include <stdio.h>

        typedef struct sDIOinstance {
            struct sDIOmodel *DIOmodPtr;
            struct sDIOinstance *DIOnextInstance;
            IFuid DIOname;
            int DIOstates;
            double DIOcap;
            double *DIOposPosPtr;
            double *DIOnegNegPtr;
            double *DIOposNegPtr;
            double *DIOnegPosPtr;
        } DIOinstance;

        typedef struct sDIOmodel {
            GENmodel gen;
            struct sDIOmodel *DIOnextModel;
            int DIOtype;
            double DIOsatCur;
        } DIOmodel;

        #endif /* DIODEF_H */
    """)
    result = generate_def_hpp(src, desc)

    # Outer structure
    assert "#pragma once" in result
    assert '#include "core/matrix.hpp"' in result
    assert "namespace neospice::dio {" in result
    assert "} // namespace neospice::dio" in result

    # Instance struct
    assert "struct DIOInstance {" in result
    assert "DIOModel *DIOmodPtr;" in result
    assert "DIOInstance *DIOnextInstance;" in result
    assert "const char *DIOname;" in result
    assert "int DIOstates;" in result
    assert "double DIOcap;" in result
    assert "neospice::MatrixOffset DIOposPosPtr{-1};" in result
    assert "neospice::MatrixOffset DIOnegNegPtr{-1};" in result
    assert "neospice::MatrixOffset DIOposNegPtr{-1};" in result
    assert "neospice::MatrixOffset DIOnegPosPtr{-1};" in result

    # Model struct
    assert "struct DIOModel {" in result
    assert "DIOModel *DIOnextModel;" in result
    assert "int DIOtype;" in result
    assert "double DIOsatCur;" in result

    # Removed artifacts
    assert "typedef" not in result
    assert "#ifndef" not in result
    assert "#define DIODEF_H" not in result
    assert "#endif" not in result
    assert '#include "ngspice/' not in result
    assert '#include "ifsim.h"' not in result
    assert "#include <stdio.h>" not in result
    assert "IFuid" not in result
    assert "GENmodel" not in result
    assert "struct sDIO" not in result


# ---------------------------------------------------------------------------
# test_custom_matrix_suffix
# ---------------------------------------------------------------------------

def test_custom_matrix_suffix():
    """A descriptor with a non-default matrix_ptr_suffix works correctly."""
    d = _Descriptor(
        prefix="RES",
        neospice_name="res",
        namespace="res",
        instance_struct="RESinstance",
        model_struct="RESmodel",
        instance_tag="sRESinstance",
        model_tag="sRESmodel",
        cpp_instance="RESInstance",
        cpp_model="RESModel",
        matrix_ptr_suffix="Eq",
    )
    src = textwrap.dedent("""\
        typedef struct sRESinstance {
            double *RESposposEq;
            double *RESnegnegEq;
            double RESvalue;
        } RESinstance;
    """)
    result = generate_def_hpp(src, d)

    assert "neospice::MatrixOffset RESposposEq{-1};" in result
    assert "neospice::MatrixOffset RESnegnegEq{-1};" in result
    assert "double RESvalue;" in result


# ---------------------------------------------------------------------------
# test_double_ptr_without_suffix_unchanged
# ---------------------------------------------------------------------------

def test_double_ptr_without_suffix_unchanged(desc):
    """``double *`` fields that do NOT end in the matrix suffix stay as-is."""
    src = textwrap.dedent("""\
        typedef struct sDIOinstance {
            double *DIOworkArea;
            double *DIOposPosPtr;
        } DIOinstance;
    """)
    result = generate_def_hpp(src, desc)

    # Non-Ptr double pointer is left alone
    assert "double *DIOworkArea;" in result
    # Ptr-suffixed one is converted
    assert "neospice::MatrixOffset DIOposPosPtr{-1};" in result
