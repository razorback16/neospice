#pragma once
#include "core/matrix.hpp"
#include <cstdint>
#include <cstdio>
#include <functional>
#include <utility>
#include <vector>

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
        struct {
            int numValue = 0;
            struct { double *rVec = nullptr; } vec;
        } v{};
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
    // Fields mirror the CKTcircuit members read by b4temp.c, b4set.c, and
    // b4ld.c.  Bit values for CKTmode flags match ngspice cktdefs.h exactly.
    struct Ckt {
        double CKTtemp       = 300.15;  // K; 27 C default
        double CKTnomTemp    = 300.15;
        double CKTgmin       = 1e-12;
        double CKTreltol     = 1e-3;
        double CKTabstol     = 1e-12;
        double CKTvoltTol    = 1e-6;
        int    CKTmode       = 0;        // MODEDC | MODETRAN etc. (bit flags)
        int    CKTbadMos3    = 0;        // UCB convention: unused here
        int    CKTnumStates  = 0;        // running counter updated in BSIM4setup

        // UCB's non-convergence hint counter. b4ld.c increments this when a
        // bypass/convergence check fails; the Newton driver owns the field
        // (it is *not* zeroed per load). For DC-only Phase-1b the adapter
        // leaves it at 0; transient Newton may later read it.
        int    CKTnoncon     = 0;
        // UCB bypass-mode flag. 0 disables the bypass optimization in b4ld.c
        // (the correct default for our DC/transient flows).
        int    CKTbypass     = 0;

        // Transient integrator state
        double  CKTdelta        = 0.0;
        double  CKTdeltaOld[8]  = {};   // UCB uses indices [0..5]
        double  CKTag[8]        = {};   // integrator coefficients (UCB reads [0..1])
        int     CKTorder        = 1;

        // State vector ring (bound by the device adapter each load call).
        // Length = total state size across all devices; indexed by inst->BSIM4states + offset.
        double *CKTstate0 = nullptr;
        double *CKTstate1 = nullptr;
        double *CKTstate2 = nullptr;

        // Residual / previous-iterate pointers (bound by adapter each load call).
        double *CKTrhs    = nullptr;
        double *CKTrhsOld = nullptr;

        // Matrix binding (bound by adapter each load call). UCB uses ckt->CKTmatrix
        // indirectly; our translated code stamps through mat directly.
        neospice::NumericMatrix *mat = nullptr;

        // Internal node registrar
        int CKTinternalNodeCounter = 1000;
        int add_internal_node(const char *name);

        // Node allocation callback. When set, add_internal_node delegates
        // to this instead of incrementing the stub counter. The callback
        // receives the UCB node name and must return a UCB-convention index
        // (>= 1 for real nodes, 0 for ground).
        std::function<int(const char*)> node_alloc;
    };

    // --- CKTmode bit flags (values match ngspice cktdefs.h exactly) --------
    // old 'mode' parameters
    constexpr int MODE             = 0x3;    // AC | TRAN mask
    constexpr int MODETRAN         = 0x1;
    constexpr int MODEAC           = 0x2;
    // old 'modedc' parameters
    constexpr int MODEDC           = 0x70;   // DC mask (DCOP | TRANOP | DCTRAN)
    constexpr int MODEDCOP         = 0x10;
    constexpr int MODETRANOP       = 0x20;
    constexpr int MODEDCTRANCURVE  = 0x40;
    // old 'initf' parameters
    constexpr int INITF            = 0x3f00;
    constexpr int MODEINITFLOAT    = 0x100;
    constexpr int MODEINITJCT      = 0x200;
    constexpr int MODEINITFIX      = 0x400;
    constexpr int MODEINITSMSIG    = 0x800;
    constexpr int MODEINITTRAN     = 0x1000;
    constexpr int MODEINITPRED     = 0x2000;
    // old 'nosolv' parameter
    constexpr int MODEUIC          = 0x10000;
    // bypass flag (not in ngspice CKTmode — kept for translated code compatibility)
    constexpr int MODEBYPASS       = 0x1000000;

    // --- SMPmatrix replacement ---------------------------------------------
    // UCB calls SMPmakeElt(matrix, row, col) to reserve a sparse entry and
    // get back a (double *) into the matrix's internal storage. We replace
    // that pointer with a MatrixOffset into our NumericMatrix, resolved by
    // the SparsityBuilder the caller passes in.
    class Matrix {
    public:
        Matrix(neospice::SparsityBuilder &builder) : builder_(builder) {}

        /// Reserve a (row, col) position. Returns a sequential reservation ID
        /// (0, 1, 2, ...) for later resolution, or -1 if either index is ground.
        /// NOTE: in Phase-1a semantics the return was a "MatrixOffset" but it was
        /// a 0-sentinel. In Phase-1b it is a distinct ID into the journal.
        neospice::MatrixOffset make_elt(int row, int col);

        /// After the SparsityPattern is built, replay the journal and return
        /// the real MatrixOffset for each reservation (ground reservations get -1).
        std::vector<neospice::MatrixOffset>
        resolve_offsets(const neospice::SparsityPattern &pat) const;

        /// Reset the journal — useful if the same Shim::Matrix is reused across
        /// multiple stamp_pattern() calls (it is not, but leave the hook).
        void clear() { journal_.clear(); }

        /// Read-only access to the reservation journal.  Used by the
        /// BSIM4v7Device adapter which owns the shifted-coord convention
        /// and needs to walk reservations itself.  Not intended for
        /// production code outside the adapter.
        const std::vector<std::pair<int,int>>& reservation_journal() const { return journal_; }

    private:
        neospice::SparsityBuilder &builder_;
        std::vector<std::pair<int,int>> journal_;  // in reservation order
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

    // --- Implicit integrator -----------------------------------------------
    // Port of ngspice src/ckt/niintegr.c::NIintegrate, specialised to Gear
    // (BE is Gear order 1).  Neospice does not yet carry a per-circuit
    // `integrateMethod` field, so we always use Gear; a future trap
    // extension would add an enum to Shim::Ckt and switch here.
    //
    // Shape matches UCB call sites in bsim4v7_load.cpp exactly:
    //   NIintegrate(ckt, &geq, &ceq, cap, qcap)
    // where qcap is the state-vector offset of the charge and ccap=qcap+1
    // holds the numerical current.  BSIM4 passes cap=0.0 at every call site
    // (b4ld.c derives geq separately via analytic dq/dv) so this routine
    // effectively just runs the Gear summation over CKTstate{0,1,2}.
    int NIintegrate(Ckt *ckt, double *geq, double *ceq,
                    double cap, int qcap);
} // namespace Shim

} // namespace neospice::bsim4v7
