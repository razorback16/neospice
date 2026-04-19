"""Unit tests for the generic 8-pass C-to-C++ transformer."""

from __future__ import annotations

import textwrap

import pytest

from ngspice_migrate.transformer import Transformer, TransformerConfig


# ---------------------------------------------------------------------------
# Shared fixtures
# ---------------------------------------------------------------------------

def _dio_config() -> TransformerConfig:
    """A diode-model config used by most tests."""
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


@pytest.fixture
def dio_cfg() -> TransformerConfig:
    return _dio_config()


@pytest.fixture
def tx(dio_cfg) -> Transformer:
    return Transformer(dio_cfg)


# ---------------------------------------------------------------------------
# test_type_rename_instance
# ---------------------------------------------------------------------------

def test_type_rename_instance(tx):
    """DIO config renames DIOinstance -> DIOInstance, GENmodel -> DIOModel, etc."""
    src = textwrap.dedent("""\
        DIOinstance *here;
        DIOmodel *model;
        GENmodel *gm;
        GENinstance *gi;
        struct sDIOinstance foo;
        struct sDIOmodel bar;
    """)
    out = tx.apply_token_subs(src)
    assert "DIOInstance *here;" in out
    assert "DIOModel *model;" in out
    assert "DIOModel *gm;" in out
    assert "DIOInstance *gi;" in out
    assert "struct DIOInstance foo;" in out
    assert "struct DIOModel bar;" in out
    # Original names must be gone.
    assert "DIOinstance" not in out
    assert "DIOmodel" not in out
    assert "GENmodel" not in out
    assert "GENinstance" not in out


# ---------------------------------------------------------------------------
# test_matrix_stamp_rewrite
# ---------------------------------------------------------------------------

def test_matrix_stamp_rewrite(tx):
    """``*(here->DIOposPosPtr) += gspr;`` -> ``mat.add(...)``."""
    src = "    *(here->DIOposPosPtr) += gspr;\n"
    out = tx.rewrite_matrix_stamps(src)
    assert out.strip() == "mat.add(here->DIOposPosPtr, gspr);"


# ---------------------------------------------------------------------------
# test_matrix_stamp_minus_equals
# ---------------------------------------------------------------------------

def test_matrix_stamp_minus_equals(tx):
    """`-=` becomes negation."""
    src = "    *(here->DIOposNegPtr) -= gspr;\n"
    out = tx.rewrite_matrix_stamps(src)
    assert out.strip() == "mat.add(here->DIOposNegPtr, -(gspr));"


# ---------------------------------------------------------------------------
# test_error_return_rewrite
# ---------------------------------------------------------------------------

def test_error_return_rewrite(tx):
    """``return(OK);`` -> ``return 0;``."""
    src = "    return(OK);\n    return OK;\n"
    out = tx.apply_token_subs(src)
    assert out.count("return 0;") == 2
    assert "OK" not in out


# ---------------------------------------------------------------------------
# test_iferror_rewrite
# ---------------------------------------------------------------------------

def test_iferror_rewrite(tx):
    """SPfrontEnd->IFerror -> Shim::report_error."""
    src = '    (*(SPfrontEnd->IFerror))(ERR_WARNING, "bad value");\n'
    out = tx.apply_token_subs(src)
    assert "Shim::report_error" in out
    assert "SPfrontEnd" not in out
    assert "Shim::ERR_WARNING" in out


# ---------------------------------------------------------------------------
# test_knr_signature
# ---------------------------------------------------------------------------

def test_knr_signature():
    """K&R -> ANSI conversion."""
    src = textwrap.dedent("""\
        Foo(a, b)
        int a;
        double b;
        {
            return a;
        }
    """)
    out = Transformer.rewrite_knr_signatures(src)
    assert "Foo(int a, double b) {" in out
    # The old declaration lines must be gone.
    assert "int a;" not in out
    assert "double b;" not in out


# ---------------------------------------------------------------------------
# test_strip_omp_blocks
# ---------------------------------------------------------------------------

def test_strip_omp_blocks():
    """Removes USE_OMP #ifdef, keeps #else branch."""
    src = textwrap.dedent("""\
        before
        #ifdef USE_OMP
        omp_stuff();
        #else
        serial_stuff();
        #endif
        after
    """)
    out = Transformer.strip_omp_blocks(src)
    assert "omp_stuff" not in out
    assert "serial_stuff" in out
    assert "before" in out
    assert "after" in out
    # The #ifdef / #else / #endif lines for OMP must be gone.
    assert "#ifdef USE_OMP" not in out
    assert "#else" not in out
    assert "#endif" not in out


# ---------------------------------------------------------------------------
# test_namespace_wrap
# ---------------------------------------------------------------------------

def test_namespace_wrap(tx):
    """Correct namespace, includes, using directive."""
    out = tx.wrap("int x = 1;\n", "/* banner */")
    assert '#define PREDICTOR' not in out
    assert '/* banner */' in out
    assert '#include "devices/dio/dio_def.hpp"' in out
    assert '#include "devices/dio/dio_shim.hpp"' in out
    assert '#include <cmath>' in out
    assert '#include <cstdio>' in out
    assert 'namespace neospice::dio {' in out
    assert 'using namespace Shim;' in out
    assert '} // namespace neospice::dio' in out


# ---------------------------------------------------------------------------
# test_full_translate_simple
# ---------------------------------------------------------------------------

def test_full_translate_simple():
    """End-to-end: namespace, mat.add, return 0, type renames, stripped includes."""
    src = textwrap.dedent("""\
        /* UCB banner */
        #include "ngspice/ngspice.h"
        #include "ngspice/cktdefs.h"

        int load(DIOinstance *here, DIOmodel *model)
        {
            *(here->DIOposPosPtr) += gspr;
            return(OK);
        }
    """)
    cfg = TransformerConfig(
        instance_struct="DIOinstance",
        model_struct="DIOmodel",
        instance_tag="sDIOinstance",
        model_tag="sDIOmodel",
        cpp_instance="DIOInstance",
        cpp_model="DIOModel",
        prefix="DIO",
        namespace="dio",
        defines=[],
    )
    tx = Transformer(cfg)
    out = tx.translate(src)

    # Namespace present
    assert "namespace neospice::dio {" in out
    assert "} // namespace neospice::dio" in out

    # mat.add rewrite
    assert "mat.add(here->DIOposPosPtr, gspr);" in out
    assert "*(here->" not in out

    # return 0
    assert "return 0;" in out
    assert "return(OK)" not in out

    # Type renames
    assert "DIOInstance" in out
    assert "DIOModel" in out
    # Original C names must be gone from non-banner/non-comment areas
    # (banner is kept verbatim, so we check the body portion)
    body_start = out.index("using namespace Shim;")
    body = out[body_start:]
    assert "DIOinstance" not in body
    assert "DIOmodel" not in body

    # Stripped ngspice includes
    assert '#include "ngspice/' not in out

    # New includes present
    assert '#include "devices/dio/dio_def.hpp"' in out
    assert '#include "devices/dio/dio_shim.hpp"' in out

    # Banner preserved
    assert "/* UCB banner */" in out


# ---------------------------------------------------------------------------
# Additional edge-case tests
# ---------------------------------------------------------------------------

def test_protect_and_restore_literals():
    """String literals inside comments should not be mangled by token subs."""
    src = '/* DIOinstance in a comment */ x = "DIOinstance literal";\n'
    protected, restore = Transformer.protect_literals(src)
    # Sentinels should have replaced the comment and string.
    assert "DIOinstance" not in protected
    restored = restore(protected)
    assert restored == src


def test_cktcircuit_rename(tx):
    """CKTcircuit -> Shim::Ckt."""
    src = "CKTcircuit *ckt;\n"
    out = tx.apply_token_subs(src)
    assert "Shim::Ckt *ckt;" in out


def test_stamp_with_outer_parens(tx):
    """Stamp wrapped in outer parens: ``(*(here->Ptr) += expr);``."""
    src = "    (*(here->DIOposPosPtr) += gspr);\n"
    out = tx.rewrite_matrix_stamps(src)
    assert out.strip() == "mat.add(here->DIOposPosPtr, gspr);"


def test_multiline_stamp(tx):
    """Multi-line stamp expression."""
    src = "    *(here->DIOposPosPtr) += (a\n        + b);\n"
    out = tx.rewrite_matrix_stamps(src)
    assert "mat.add(here->DIOposPosPtr," in out
    assert "a" in out
    assert "b" in out


def test_split_banner():
    """Banner is everything before first #include."""
    src = "/* banner line 1 */\n/* banner line 2 */\n#include <foo.h>\ncode();\n"
    banner, rest = Transformer.split_banner(src)
    assert "banner line 1" in banner
    assert "banner line 2" in banner
    assert "#include" in rest
    assert "code();" in rest


def test_strip_c_std_headers(tx):
    """<stdio.h> and <math.h> are stripped by _strip_ngspice_includes."""
    src = '#include <stdio.h>\n#include <math.h>\nint x;\n'
    out = Transformer._strip_ngspice_includes(src)
    assert "<stdio.h>" not in out
    assert "<math.h>" not in out
    assert "int x;" in out


def test_tmalloc_rewrite(tx):
    """tmalloc(sizeof(double) * n) -> Shim::tmalloc<double>(n)."""
    src = "p = tmalloc(sizeof(double) * count);\n"
    out = tx.apply_token_subs(src)
    assert "Shim::tmalloc<double>(count)" in out


def test_free_rewrite(tx):
    """FREE( -> Shim::FREE(."""
    src = "FREE(ptr);\n"
    out = tx.apply_token_subs(src)
    assert "Shim::FREE(ptr);" in out
