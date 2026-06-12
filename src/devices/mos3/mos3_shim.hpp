#pragma once
#include "core/matrix.hpp"
#include <cstdint>
#include <cstdio>
#include <functional>
#include <utility>
#include <vector>

namespace neospice::mos3 {

// UCB gendefs.h Boolean constants (used throughout the UCB model files).
#ifndef TRUE
#define TRUE  1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef OFF
#define OFF 0
#endif

// UCB physical constants (used in temp and load functions).
#ifndef CONSTKoverQ
#define CONSTKoverQ (1.38064852e-23/1.6021766208e-19)  // Boltzmann/charge  (eV/K)
#endif
#ifndef CONSTe
#define CONSTe 2.7182818284590452354 // Euler's number
#endif
#ifndef CONSTboltz
#define CONSTboltz 1.38064852e-23
#endif
#ifndef REFTEMP
#define REFTEMP 300.15
#endif

// --- Error codes (subset of UCB sperror.h used by Phase 1a files) -----------
namespace Shim {
    constexpr int OK          = 0;
    constexpr int E_BADPARM   = -1;
    constexpr int E_PARMRANGE = -2;
    constexpr int E_NOMEM     = -3;
    constexpr int E_UNSUPP    = -4;
    constexpr int E_PARMVAL  = -5;

    // --- UCB IFvalue replacement -------------------------------------------
    struct IfValue {
        int         iValue = 0;
        double      rValue = 0.0;
        const char *sValue = nullptr;
        struct {
            int numValue = 0;
            struct { double *rVec = nullptr; } vec;
        } v{};
    };

    // --- UCB IFparm replacement --------------------------------------------
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
    constexpr int IF_COMPLEX   = 0x20;

    // --- CKTcircuit replacement --------------------------------------------
    struct Ckt {
        double CKTtemp       = 300.15;
        double CKTnomTemp    = 300.15;
        double CKTgmin       = 1e-12;
        double CKTdeviceGainFact = 1.0;  // [3B] variable-gain homotopy: nonlinear-device scaling
        double CKTreltol     = 1e-3;
        double CKTabstol     = 1e-12;
        double CKTvoltTol    = 1e-6;
        int    CKTmode       = 0;
        int    CKTbadMos3    = 0;
        int    CKTnumStates  = 0;
        double CKTdefaultMosAD = 0.0;
        double CKTdefaultMosAS = 0.0;
        double CKTdefaultMosM = 1.0;
        double CKTdefaultMosL = 1e-4;
        double CKTdefaultMosW = 1e-4;
        int    CKTfixLimit = 0;
        int    CKTnoncon     = 0;
        int    CKTbypass     = 0;

        // Transient integrator state
        double  CKTdelta        = 0.0;
        double  CKTdeltaOld[8]  = {};
        double  CKTag[8]        = {};
        int     CKTorder        = 1;
        int     CKTintegrateMethod = 0;  // 0=trapezoidal, 1=gear
        double  xmu_ratio = 1.0;       // xmu/(1-xmu), precomputed

        double *CKTstate0 = nullptr;
        double *CKTstate1 = nullptr;
        double *CKTstate2 = nullptr;

        double *CKTrhs    = nullptr;
        double *CKTrhsOld = nullptr;

        neospice::NumericMatrix *mat = nullptr;

        void *CKTtroubleElt = nullptr;
        void *CKTsenInfo = nullptr;

        int CKTinternalNodeCounter = 1000;
        int CKTcopyNodesets = 0;

        // Stub for sensitivity gating (CKTcurJob->JOBtype check)
        struct { int JOBtype = 0; } *CKTcurJob = nullptr;

        int add_internal_node(const char *name);

        std::function<int(const char*)> node_alloc;
    };

    // --- CKTmode bit flags (values match ngspice cktdefs.h exactly) --------
    constexpr int MODE             = 0x3;
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

    // --- SMPmatrix replacement ---------------------------------------------
    class Matrix {
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

    // --- Error reporting stub ----------------------------------------------
    [[gnu::format(printf, 2, 3)]]
    void report_error(int level, const char *fmt, ...);
    constexpr int ERR_WARNING = 1;
    constexpr int ERR_FATAL   = 2;

    // --- UCB FREE / tmalloc replacement -----------------------------------
    template <typename T>
    inline void FREE(T *&p) { delete[] p; p = nullptr; }
    template <typename T>
    inline T *tmalloc(std::size_t n) { return new T[n](); }

    // --- UCB CKTdltNNum stub (no-op: neospice owns node cleanup) ----------
    inline void CKTdltNNum(Ckt * /*ckt*/, int /*node*/) {}

    // --- UCB CKTnode + CKTmkVolt replacement --------------------------------
    struct CKTnode {
        int number = 0;
        const char *name = nullptr;
        double *nodeset = nullptr;
        double *ic = nullptr;
        int nsGiven = 0;
        int icGiven = 0;
    };
    int CKTmkVolt(Ckt *ckt, CKTnode **node_out,
                  const char *basename, const char *suffix);
    inline int CKTinst2Node(Ckt * /*ckt*/, void * /*inst*/, int /*term*/,
                            CKTnode ** /*node*/, const char ** /*name*/) {
        return E_UNSUPP;
    }

    // --- Implicit integrator -----------------------------------------------
    int NIintegrate(Ckt *ckt, double *geq, double *ceq,
                    double cap, int qcap);
} // namespace Shim

// --- UCB device limiting functions (devsup.c) ---
double DEVlimvds  (double vnew, double vold);
double DEVpnjlim  (double vnew, double vold, double vt, double vcrit, int *icheck);
double DEVfetlim  (double vnew, double vold, double vto);

// --- Meyer capacitance model ---
void DEVqmeyer(double vgs, double vgd, double vgb, double von, double vdsat,
               double *capgs, double *capgd, double *capgb,
               double phi, double cox);

// --- MAX_EXP_ARG limit to prevent overflow ---
#ifndef MAX_EXP_ARG
#define MAX_EXP_ARG 709.0
#endif

} // namespace neospice::mos3
