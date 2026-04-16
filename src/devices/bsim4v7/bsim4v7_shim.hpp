#pragma once
#include "core/matrix.hpp"
#include <cstdint>
#include <cstdio>

namespace neospice::bsim4v7 {

// UCB gendefs.h Boolean constants (used throughout the UCB model files).
#ifndef TRUE
#define TRUE  1
#endif
#ifndef FALSE
#define FALSE 0
#endif

// --- Error codes (subset of UCB sperror.h used by Phase 1a files) -----------
namespace Shim {
    constexpr int OK          = 0;
    constexpr int E_BADPARM   = -1;
    constexpr int E_PARMRANGE = -2;
    constexpr int E_NOMEM     = -3;
    constexpr int E_UNSUPP    = -4;

    // --- UCB IFvalue replacement -------------------------------------------
    // UCB's IFvalue is a tagged union of all parameter types. We mirror the
    // subset the model actually uses: iValue, rValue, sValue (string), vValue
    // (vector of double for IFparseTree). Tag is implicit by which accessor
    // is used; the parameter table tells the caller which to read.
    struct IfValue {
        int         iValue = 0;
        double      rValue = 0.0;
        const char *sValue = nullptr;
        // Vector value (for string arrays / real arrays):
        // UCB's IFvalue uses v.numValue and v.vec.rVec; we mirror that shape.
        struct VecHolder { double *rVec = nullptr; };
        struct { int numValue = 0; VecHolder vec; } v{};
    };

    // --- UCB IFparm replacement --------------------------------------------
    struct IfParm {
        const char *keyword;
        int         id;
        int         dataType;   // UCB's IF_FLAG / IF_REAL / IF_INTEGER / IF_STRING etc.
        const char *description;
    };
    // UCB dataType flag bits we honour in Phase 1a:
    constexpr int IF_REAL    = 0x01;
    constexpr int IF_INTEGER = 0x02;
    constexpr int IF_STRING  = 0x04;
    constexpr int IF_FLAG    = 0x08;
    constexpr int IF_REALVEC = 0x10;    // real vector (IC parameter)
    constexpr int IF_ASK     = 0x100;
    constexpr int IF_SET     = 0x200;
    constexpr int IF_REDUNDANT = 0x400;  // UCB uses for aliases

    // --- CKTcircuit replacement --------------------------------------------
    // Only the fields that b4temp.c + b4set.c actually read are declared here.
    // b4ld.c will extend this in Phase 1b (state vectors, integrator coeffs).
    struct Ckt {
        double CKTtemp       = 300.15;  // K; 27 C default
        double CKTnomTemp    = 300.15;
        double CKTgmin       = 1e-12;
        double CKTreltol     = 1e-3;
        double CKTabstol     = 1e-12;
        double CKTvoltTol    = 1e-6;
        int    CKTmode       = 0;        // MODEDC | MODETRAN etc. (bit flags)
        int    CKTbadMos3    = 0;        // UCB convention: unused here
        // Phase-1b extensions (declared now so BSIM4setup can reference when it
        // allocates state offsets):
        int    CKTnumStates  = 0;        // running counter updated in BSIM4setup
        // Phase-1b transient state vectors (stub fields for DEVpred / b4ld):
        double  CKTdelta        = 0.0;
        double  CKTdeltaOld[8]  = {};    // integrator time-step history
        double *CKTstate1       = nullptr;
        double *CKTstate2       = nullptr;
    };

    // --- SMPmatrix replacement ---------------------------------------------
    // UCB calls SMPmakeElt(matrix, row, col) to reserve a sparse entry and
    // get back a (double *) into the matrix's internal storage. We replace
    // that pointer with a MatrixOffset into our NumericMatrix, resolved by
    // the SparsityBuilder the caller passes in.
    class Matrix {
    public:
        Matrix(neospice::SparsityBuilder &builder) : builder_(builder) {}
        // make_elt: reserve (row, col) and return the offset. Grounds (-1)
        // return -1 so the caller can skip.
        neospice::MatrixOffset make_elt(int row, int col);
    private:
        neospice::SparsityBuilder &builder_;
    };

    // --- Error reporting stub ----------------------------------------------
    // UCB calls SPfrontEnd->IFerror(ERR_WARNING, fmt, varargs). We log to
    // stderr. The translated code uses Shim::report_error(level, fmt, ...).
    [[gnu::format(printf, 2, 3)]]
    void report_error(int level, const char *fmt, ...);
    constexpr int ERR_WARNING = 1;
    constexpr int ERR_FATAL   = 2;

    // --- UCB FREE / tmalloc replacement -----------------------------------
    // UCB allocates doubles with tmalloc(sizeof(double)*n). We use new[]/delete[].
    // Keep helper inline so the translated code can call Shim::FREE(ptr) verbatim.
    template <typename T>
    inline void FREE(T *&p) { delete[] p; p = nullptr; }
    template <typename T>
    inline T *tmalloc(std::size_t n) { return new T[n](); }
} // namespace Shim

} // namespace neospice::bsim4v7
