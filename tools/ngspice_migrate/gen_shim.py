"""Shim layer generator for migrated ngspice device models.

Emits a C++ header (hpp) and implementation (cpp) that provide the
compatibility shim between UCB ngspice conventions (CKTcircuit,
SMPmakeElt, IFvalue, NIintegrate, ...) and the neospice core.

The generated code is ~90 % identical across models; only the outer
namespace and conditional features (NIintegrate when ``state_count > 0``,
``node_alloc`` callback when ``has_internal_nodes``) vary.
"""

from __future__ import annotations

from typing import Protocol


# ---------------------------------------------------------------------------
# Minimal protocol for the descriptor fields we consume
# ---------------------------------------------------------------------------

class _Desc(Protocol):
    namespace: str
    neospice_name: str
    has_internal_nodes: bool
    state_count: int


# ---------------------------------------------------------------------------
# Header generator
# ---------------------------------------------------------------------------

def generate_shim_hpp(desc: _Desc) -> str:
    """Return the complete text of the shim header file."""
    parts: list[str] = []

    # --- Preamble ----------------------------------------------------------
    parts.append("#pragma once\n")
    parts.append('#include "core/matrix.hpp"\n')
    parts.append("#include <cstdint>\n")
    parts.append("#include <cstdio>\n")
    if desc.has_internal_nodes:
        parts.append("#include <functional>\n")
    parts.append("#include <utility>\n")
    parts.append("#include <vector>\n")
    parts.append("\n")

    # --- Outer namespace ---------------------------------------------------
    parts.append(f"namespace neospice::{desc.namespace} {{\n")
    parts.append("\n")

    # --- TRUE / FALSE macros -----------------------------------------------
    parts.append("// UCB gendefs.h Boolean constants (used throughout the UCB model files).\n")
    parts.append("#ifndef TRUE\n")
    parts.append("#define TRUE  1\n")
    parts.append("#endif\n")
    parts.append("#ifndef FALSE\n")
    parts.append("#define FALSE 0\n")
    parts.append("#endif\n")
    parts.append("\n")

    # --- Error codes -------------------------------------------------------
    parts.append("// --- Error codes (subset of UCB sperror.h used by Phase 1a files) -----------\n")
    parts.append("namespace Shim {\n")
    parts.append("    constexpr int OK          = 0;\n")
    parts.append("    constexpr int E_BADPARM   = -1;\n")
    parts.append("    constexpr int E_PARMRANGE = -2;\n")
    parts.append("    constexpr int E_NOMEM     = -3;\n")
    parts.append("    constexpr int E_UNSUPP    = -4;\n")
    parts.append("\n")

    # --- IfValue -----------------------------------------------------------
    parts.append("    // --- UCB IFvalue replacement -------------------------------------------\n")
    parts.append("    struct IfValue {\n")
    parts.append("        int         iValue = 0;\n")
    parts.append("        double      rValue = 0.0;\n")
    parts.append("        const char *sValue = nullptr;\n")
    parts.append("        struct {\n")
    parts.append("            int numValue = 0;\n")
    parts.append("            struct { double *rVec = nullptr; } vec;\n")
    parts.append("        } v{};\n")
    parts.append("    };\n")
    parts.append("\n")

    # --- IfParm + datatype flags -------------------------------------------
    parts.append("    // --- UCB IFparm replacement --------------------------------------------\n")
    parts.append("    struct IfParm {\n")
    parts.append("        const char *keyword;\n")
    parts.append("        int         id;\n")
    parts.append("        int         dataType;\n")
    parts.append("        const char *description;\n")
    parts.append("    };\n")
    parts.append("    constexpr int IF_REAL    = 0x01;\n")
    parts.append("    constexpr int IF_INTEGER = 0x02;\n")
    parts.append("    constexpr int IF_STRING  = 0x04;\n")
    parts.append("    constexpr int IF_FLAG    = 0x08;\n")
    parts.append("    constexpr int IF_REALVEC = 0x10;\n")
    parts.append("    constexpr int IF_ASK     = 0x100;\n")
    parts.append("    constexpr int IF_SET     = 0x200;\n")
    parts.append("    constexpr int IF_REDUNDANT = 0x400;\n")
    parts.append("\n")

    # --- Ckt struct --------------------------------------------------------
    parts.append("    // --- CKTcircuit replacement --------------------------------------------\n")
    parts.append("    struct Ckt {\n")
    parts.append("        double CKTtemp       = 300.15;\n")
    parts.append("        double CKTnomTemp    = 300.15;\n")
    parts.append("        double CKTgmin       = 1e-12;\n")
    parts.append("        double CKTreltol     = 1e-3;\n")
    parts.append("        double CKTabstol     = 1e-12;\n")
    parts.append("        double CKTvoltTol    = 1e-6;\n")
    parts.append("        int    CKTmode       = 0;\n")
    parts.append("        int    CKTbadMos3    = 0;\n")
    parts.append("        int    CKTnumStates  = 0;\n")
    parts.append("        int    CKTnoncon     = 0;\n")
    parts.append("        int    CKTbypass     = 0;\n")
    parts.append("\n")
    parts.append("        // Transient integrator state\n")
    parts.append("        double  CKTdelta        = 0.0;\n")
    parts.append("        double  CKTdeltaOld[8]  = {};\n")
    parts.append("        double  CKTag[8]        = {};\n")
    parts.append("        int     CKTorder        = 1;\n")
    parts.append("\n")
    parts.append("        double *CKTstate0 = nullptr;\n")
    parts.append("        double *CKTstate1 = nullptr;\n")
    parts.append("        double *CKTstate2 = nullptr;\n")
    parts.append("\n")
    parts.append("        double *CKTrhs    = nullptr;\n")
    parts.append("        double *CKTrhsOld = nullptr;\n")
    parts.append("\n")
    parts.append("        neospice::NumericMatrix *mat = nullptr;\n")
    parts.append("\n")
    parts.append("        int CKTinternalNodeCounter = 1000;\n")
    parts.append("        int add_internal_node(const char *name);\n")
    if desc.has_internal_nodes:
        parts.append("\n")
        parts.append("        std::function<int(const char*)> node_alloc;\n")
    parts.append("    };\n")
    parts.append("\n")

    # --- CKTmode bit flags -------------------------------------------------
    parts.append("    // --- CKTmode bit flags (values match ngspice cktdefs.h exactly) --------\n")
    parts.append("    constexpr int MODE             = 0x3;\n")
    parts.append("    constexpr int MODETRAN         = 0x1;\n")
    parts.append("    constexpr int MODEAC           = 0x2;\n")
    parts.append("    constexpr int MODEDC           = 0x70;\n")
    parts.append("    constexpr int MODEDCOP         = 0x10;\n")
    parts.append("    constexpr int MODETRANOP       = 0x20;\n")
    parts.append("    constexpr int MODEDCTRANCURVE  = 0x40;\n")
    parts.append("    constexpr int INITF            = 0x3f00;\n")
    parts.append("    constexpr int MODEINITFLOAT    = 0x100;\n")
    parts.append("    constexpr int MODEINITJCT      = 0x200;\n")
    parts.append("    constexpr int MODEINITFIX      = 0x400;\n")
    parts.append("    constexpr int MODEINITSMSIG    = 0x800;\n")
    parts.append("    constexpr int MODEINITTRAN     = 0x1000;\n")
    parts.append("    constexpr int MODEINITPRED     = 0x2000;\n")
    parts.append("    constexpr int MODEUIC          = 0x10000;\n")
    parts.append("    constexpr int MODEBYPASS       = 0x1000000;\n")
    parts.append("\n")

    # --- Matrix class ------------------------------------------------------
    parts.append("    // --- SMPmatrix replacement ---------------------------------------------\n")
    parts.append("    class Matrix {\n")
    parts.append("    public:\n")
    parts.append("        Matrix(neospice::SparsityBuilder &builder) : builder_(builder) {}\n")
    parts.append("\n")
    parts.append("        neospice::MatrixOffset make_elt(int row, int col);\n")
    parts.append("\n")
    parts.append("        std::vector<neospice::MatrixOffset>\n")
    parts.append("        resolve_offsets(const neospice::SparsityPattern &pat) const;\n")
    parts.append("\n")
    parts.append("        void clear() { journal_.clear(); }\n")
    parts.append("\n")
    parts.append("        const std::vector<std::pair<int,int>>& reservation_journal() const { return journal_; }\n")
    parts.append("\n")
    parts.append("    private:\n")
    parts.append("        neospice::SparsityBuilder &builder_;\n")
    parts.append("        std::vector<std::pair<int,int>> journal_;\n")
    parts.append("    };\n")
    parts.append("\n")

    # --- report_error ------------------------------------------------------
    parts.append("    // --- Error reporting stub ----------------------------------------------\n")
    parts.append("    [[gnu::format(printf, 2, 3)]]\n")
    parts.append("    void report_error(int level, const char *fmt, ...);\n")
    parts.append("    constexpr int ERR_WARNING = 1;\n")
    parts.append("    constexpr int ERR_FATAL   = 2;\n")
    parts.append("\n")

    # --- FREE / tmalloc ----------------------------------------------------
    parts.append("    // --- UCB FREE / tmalloc replacement -----------------------------------\n")
    parts.append("    template <typename T>\n")
    parts.append("    inline void FREE(T *&p) { delete[] p; p = nullptr; }\n")
    parts.append("    template <typename T>\n")
    parts.append("    inline T *tmalloc(std::size_t n) { return new T[n](); }\n")

    # --- NIintegrate (conditional) -----------------------------------------
    if desc.state_count > 0:
        parts.append("\n")
        parts.append("    // --- Implicit integrator -----------------------------------------------\n")
        parts.append("    int NIintegrate(Ckt *ckt, double *geq, double *ceq,\n")
        parts.append("                    double cap, int qcap);\n")

    # --- Close namespaces --------------------------------------------------
    parts.append("} // namespace Shim\n")
    parts.append("\n")
    parts.append(f"}} // namespace neospice::{desc.namespace}\n")

    return "".join(parts)


# ---------------------------------------------------------------------------
# Implementation generator
# ---------------------------------------------------------------------------

def generate_shim_cpp(desc: _Desc) -> str:
    """Return the complete text of the shim implementation file."""
    parts: list[str] = []

    ns = desc.namespace
    name = desc.neospice_name

    # --- Includes ----------------------------------------------------------
    parts.append(f'#include "devices/{name}/{name}_shim.hpp"\n')
    parts.append("#include <cstdarg>\n")
    parts.append("\n")

    # --- Namespace ---------------------------------------------------------
    parts.append(f"namespace neospice::{ns}::Shim {{\n")
    parts.append("\n")

    # --- Matrix::make_elt --------------------------------------------------
    parts.append("neospice::MatrixOffset Matrix::make_elt(int row, int col) {\n")
    parts.append("    if (row < 0 || col < 0) {\n")
    parts.append("        journal_.emplace_back(-1, -1);\n")
    parts.append("        return -1;\n")
    parts.append("    }\n")
    parts.append("    builder_.add(row, col);\n")
    parts.append("    neospice::MatrixOffset id = static_cast<neospice::MatrixOffset>(journal_.size());\n")
    parts.append("    journal_.emplace_back(row, col);\n")
    parts.append("    return id;\n")
    parts.append("}\n")
    parts.append("\n")

    # --- Matrix::resolve_offsets -------------------------------------------
    parts.append("std::vector<neospice::MatrixOffset>\n")
    parts.append("Matrix::resolve_offsets(const neospice::SparsityPattern &pat) const {\n")
    parts.append("    std::vector<neospice::MatrixOffset> out;\n")
    parts.append("    out.reserve(journal_.size());\n")
    parts.append("    for (auto &[r, c] : journal_) {\n")
    parts.append("        if (r < 0 || c < 0) out.push_back(-1);\n")
    parts.append("        else out.push_back(pat.offset(r, c));\n")
    parts.append("    }\n")
    parts.append("    return out;\n")
    parts.append("}\n")
    parts.append("\n")

    # --- Ckt::add_internal_node --------------------------------------------
    parts.append("int Ckt::add_internal_node(const char *name) {\n")
    if desc.has_internal_nodes:
        parts.append("    if (node_alloc) return node_alloc(name);\n")
    parts.append("    return CKTinternalNodeCounter++;\n")
    parts.append("}\n")

    # --- NIintegrate (conditional) -----------------------------------------
    if desc.state_count > 0:
        parts.append("\n")
        parts.append("int NIintegrate(Ckt *ckt, double *geq, double *ceq,\n")
        parts.append("                double cap, int qcap) {\n")
        parts.append("    const int ccap = qcap + 1;\n")
        parts.append("    double *s0 = ckt->CKTstate0 + qcap;\n")
        parts.append("    double *s1 = ckt->CKTstate1 + qcap;\n")
        parts.append("    double *s2 = ckt->CKTstate2 + qcap;\n")
        parts.append("\n")
        parts.append("    int order = ckt->CKTorder;\n")
        parts.append("    if (order < 1) order = 1;\n")
        parts.append("    if (order > 2) order = 2;\n")
        parts.append("\n")
        parts.append("    double deriv = ckt->CKTag[0] * s0[0];\n")
        parts.append("    if (order >= 1) deriv += ckt->CKTag[1] * s1[0];\n")
        parts.append("    if (order >= 2) deriv += ckt->CKTag[2] * s2[0];\n")
        parts.append("    s0[1] = deriv;\n")
        parts.append("\n")
        parts.append("    *geq = ckt->CKTag[0] * cap;\n")
        parts.append("    *ceq = s0[1] - (*geq) * s0[0];\n")
        parts.append("\n")
        parts.append("    return OK;\n")
        parts.append("}\n")

    # --- report_error ------------------------------------------------------
    parts.append("\n")
    parts.append("void report_error(int /*level*/, const char *fmt, ...) {\n")
    parts.append("    std::va_list ap;\n")
    parts.append("    va_start(ap, fmt);\n")
    parts.append("    std::vfprintf(stderr, fmt, ap);\n")
    parts.append("    std::fputc('\\n', stderr);\n")
    parts.append("    va_end(ap);\n")
    parts.append("}\n")
    parts.append("\n")

    # --- Close namespace ---------------------------------------------------
    parts.append(f"}} // namespace neospice::{ns}::Shim\n")

    return "".join(parts)
