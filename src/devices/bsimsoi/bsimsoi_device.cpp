#include "devices/bsimsoi/bsimsoi_device.hpp"

#include "core/circuit.hpp"        // Circuit::node, tls_integrator_ctx
#include "core/types.hpp"          // SimOptions defaults

#include <cmath>
#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <string>

// Forward declarations for translated UCB functions.
namespace neospice::bsimsoi {
    int B4SOIsetup(Shim::Matrix*, B4SOIModel*, Shim::Ckt*, int*);
    int B4SOItemp(B4SOIModel*, Shim::Ckt*);
    int B4SOIload(B4SOIModel*, Shim::Ckt*);
}

namespace neospice {

using namespace neospice::bsimsoi;

// ---------------------------------------------------------------------------
// B4SOIModelCard destructor
// ---------------------------------------------------------------------------
B4SOIModelCard::~B4SOIModelCard() {
    auto* p = ucb.pSizeDependParamKnot;
    while (p) {
        auto* next = p->pNext;
        std::free(p);
        p = next;
    }
    ucb.pSizeDependParamKnot = nullptr;
}

// ---------------------------------------------------------------------------
// Neospice -> UCB node translation.
// Neospice uses GROUND_INTERNAL = -1 for ground and consecutive non-negative
// indices for real nodes.  UCB uses 0 for ground and >=1 for real nodes.
// ---------------------------------------------------------------------------
static inline int neo_to_ucb(int32_t neo) {
    return (neo < 0) ? 0 : (neo + 1);
}

// ---------------------------------------------------------------------------
// make
// ---------------------------------------------------------------------------
std::unique_ptr<B4SOIDevice>
B4SOIDevice::make(std::string name,
        int32_t n_d, int32_t n_g, int32_t n_s, int32_t n_e, int32_t n_p, int32_t n_b,
        const Geom& geom, B4SOIModelCard& shared_card) {
    std::unique_ptr<B4SOIDevice> dev(new B4SOIDevice(std::move(name)));
    dev->model_ = &shared_card.ucb;

    if (!shared_card.ucb.B4SOIversionGiven) {
        shared_card.ucb.B4SOIversion = 4.4;
        shared_card.ucb.B4SOIversionGiven = 1;
    }

    auto& inst = dev->inst_;
    inst.B4SOIname = dev->name().c_str();
    inst.B4SOImodPtr = dev->model_;

    // Node wiring (UCB convention).
    inst.B4SOIdNode = neo_to_ucb(n_d);
    inst.B4SOIgNodeExt = neo_to_ucb(n_g);
    inst.B4SOIsNode = neo_to_ucb(n_s);
    inst.B4SOIeNode = neo_to_ucb(n_e);
    inst.B4SOIpNode = neo_to_ucb(n_p);
    inst.B4SOIbNode = neo_to_ucb(n_b);

    // Geometry.
    inst.B4SOIw = geom.W;
    inst.B4SOIwGiven = 1;
    inst.B4SOIl = geom.L;
    inst.B4SOIlGiven = 1;
    inst.B4SOIm = geom.M;
    inst.B4SOImGiven = 1;
    inst.B4SOIdrainArea = geom.AD;
    inst.B4SOIdrainAreaGiven = (geom.AD != 0.0) ? 1 : 0;
    inst.B4SOIsourceArea = geom.AS;
    inst.B4SOIsourceAreaGiven = (geom.AS != 0.0) ? 1 : 0;
    inst.B4SOIdrainPerimeter = geom.PD;
    inst.B4SOIdrainPerimeterGiven = (geom.PD != 0.0) ? 1 : 0;
    inst.B4SOIsourcePerimeter = geom.PS;
    inst.B4SOIsourcePerimeterGiven = (geom.PS != 0.0) ? 1 : 0;
    inst.B4SOIdrainSquares = geom.NRD;
    inst.B4SOIdrainSquaresGiven = (geom.NRD != 0.0) ? 1 : 0;
    inst.B4SOIsourceSquares = geom.NRS;
    inst.B4SOIsourceSquaresGiven = (geom.NRS != 0.0) ? 1 : 0;
    inst.B4SOIsa = geom.SA;
    inst.B4SOIsaGiven = (geom.SA != 0.0) ? 1 : 0;
    inst.B4SOIsb = geom.SB;
    inst.B4SOIsbGiven = (geom.SB != 0.0) ? 1 : 0;
    inst.B4SOIsd = geom.SD;
    inst.B4SOIsdGiven = (geom.SD != 0.0) ? 1 : 0;
    inst.B4SOInf = geom.NF;
    inst.B4SOInfGiven = 1;

    // Thread onto the shared model's instance list.
    inst.B4SOInextInstance = shared_card.ucb.B4SOIinstances;
    shared_card.ucb.B4SOIinstances = &inst;

    // Remember the widest real node index for ghost array sizing.
    int32_t widest = -1;
    for (int32_t n : {n_d, n_g, n_s, n_e, n_p, n_b}) if (n > widest) widest = n;
    dev->max_neo_node_ = widest;

    return dev;
}

// ---------------------------------------------------------------------------
// declare_internal_nodes
// ---------------------------------------------------------------------------
void B4SOIDevice::declare_internal_nodes(Circuit& ckt) {
    SparsityBuilder scratch(1);
    Shim::Matrix shim_matrix(scratch);

    Shim::Ckt setup_ckt;
    setup_ckt.CKTtemp    = T_NOMINAL;
    setup_ckt.CKTnomTemp = T_NOMINAL;
    setup_ckt.CKTinternalNodeCounter = 1000;

    setup_ckt.node_alloc = [&ckt, this](const char* name) -> int {
        std::string full = "__" + name_ + "_" + name;
        int32_t neo = ckt.node(full);
        return neo + 1;  // UCB convention: ground=0, real>=1
    };

    int states = 0;
    int rc = B4SOIsetup(&shim_matrix, model_, &setup_ckt, &states);
    if (rc != Shim::OK) {
        throw std::runtime_error("B4SOIsetup failed with rc=" + std::to_string(rc));
    }

    const auto& journal = shim_matrix.reservation_journal();
    journal_.assign(journal.begin(), journal.end());

    // Recompute max_neo_node_ to cover internal nodes.
    for (auto [r, c] : journal_) {
        int mx = std::max(r, c);
        if (mx > 0) {
            int32_t neo = mx - 1;
            if (neo > max_neo_node_) max_neo_node_ = neo;
        }
    }
}

// ---------------------------------------------------------------------------
// stamp_pattern
// ---------------------------------------------------------------------------
void B4SOIDevice::stamp_pattern(SparsityBuilder& builder) const {
    for (auto [r, c] : journal_) {
        if (r <= 0 || c <= 0) continue;
        builder.add(r - 1, c - 1);
    }
}

// ---------------------------------------------------------------------------
// assign_offsets
// ---------------------------------------------------------------------------
void B4SOIDevice::assign_offsets(const SparsityPattern& pattern) {
    std::vector<MatrixOffset> offsets(journal_.size(), -1);
    for (std::size_t i = 0; i < journal_.size(); ++i) {
        auto [r, c] = journal_[i];
        if (r <= 0 || c <= 0) continue;
        offsets[i] = pattern.offset(r - 1, c - 1);
    }

#define RESOLVE(f)                                                       \
    do {                                                                 \
        if (inst_.f >= 0)                                                \
            inst_.f = offsets[inst_.f];                                  \
    } while (0)

    RESOLVE(B4SOIDBdpPtr);
    RESOLVE(B4SOIDBdbPtr);
    RESOLVE(B4SOIDBbPtr);
    RESOLVE(B4SOIDPdbPtr);
    RESOLVE(B4SOISBspPtr);
    RESOLVE(B4SOISBbPtr);
    RESOLVE(B4SOISBsbPtr);
    RESOLVE(B4SOISPsbPtr);
    RESOLVE(B4SOIBsbPtr);
    RESOLVE(B4SOIBdbPtr);
    RESOLVE(B4SOIDgPtr);
    RESOLVE(B4SOIDspPtr);
    RESOLVE(B4SOIDbPtr);
    RESOLVE(B4SOISdpPtr);
    RESOLVE(B4SOISgPtr);
    RESOLVE(B4SOISbPtr);
    RESOLVE(B4SOIPgPtr);
    RESOLVE(B4SOIGpPtr);
    RESOLVE(B4SOIGgmPtr);
    RESOLVE(B4SOIGgePtr);
    RESOLVE(B4SOIGMdpPtr);
    RESOLVE(B4SOIGMgPtr);
    RESOLVE(B4SOIGMspPtr);
    RESOLVE(B4SOIGMgmPtr);
    RESOLVE(B4SOIGMgePtr);
    RESOLVE(B4SOIGMePtr);
    RESOLVE(B4SOIGMbPtr);
    RESOLVE(B4SOISPgmPtr);
    RESOLVE(B4SOIDPgmPtr);
    RESOLVE(B4SOIEgmPtr);
    RESOLVE(B4SOIGEdpPtr);
    RESOLVE(B4SOIGEgPtr);
    RESOLVE(B4SOIGEgmPtr);
    RESOLVE(B4SOIGEgePtr);
    RESOLVE(B4SOIGEspPtr);
    RESOLVE(B4SOIGEbPtr);
    RESOLVE(B4SOIGePtr);
    RESOLVE(B4SOIDPePtr);
    RESOLVE(B4SOISPePtr);
    RESOLVE(B4SOIEePtr);
    RESOLVE(B4SOIEbPtr);
    RESOLVE(B4SOIBePtr);
    RESOLVE(B4SOIEgPtr);
    RESOLVE(B4SOIEdpPtr);
    RESOLVE(B4SOIEspPtr);
    RESOLVE(B4SOITemptempPtr);
    RESOLVE(B4SOITempdpPtr);
    RESOLVE(B4SOITempspPtr);
    RESOLVE(B4SOITempgPtr);
    RESOLVE(B4SOITempbPtr);
    RESOLVE(B4SOITempePtr);
    RESOLVE(B4SOIGtempPtr);
    RESOLVE(B4SOIDPtempPtr);
    RESOLVE(B4SOISPtempPtr);
    RESOLVE(B4SOIEtempPtr);
    RESOLVE(B4SOIBtempPtr);
    RESOLVE(B4SOIPtempPtr);
    RESOLVE(B4SOIBpPtr);
    RESOLVE(B4SOIPbPtr);
    RESOLVE(B4SOIPpPtr);
    RESOLVE(B4SOIDdPtr);
    RESOLVE(B4SOIGgPtr);
    RESOLVE(B4SOISsPtr);
    RESOLVE(B4SOIBbPtr);
    RESOLVE(B4SOIDPdpPtr);
    RESOLVE(B4SOISPspPtr);
    RESOLVE(B4SOIDdpPtr);
    RESOLVE(B4SOIGbPtr);
    RESOLVE(B4SOIGdpPtr);
    RESOLVE(B4SOIGspPtr);
    RESOLVE(B4SOISspPtr);
    RESOLVE(B4SOIBdpPtr);
    RESOLVE(B4SOIBspPtr);
    RESOLVE(B4SOIDPspPtr);
    RESOLVE(B4SOIDPdPtr);
    RESOLVE(B4SOIBgPtr);
    RESOLVE(B4SOIDPgPtr);
    RESOLVE(B4SOISPgPtr);
    RESOLVE(B4SOISPsPtr);
    RESOLVE(B4SOIDPbPtr);
    RESOLVE(B4SOISPbPtr);
    RESOLVE(B4SOISPdpPtr);
    RESOLVE(B4SOIVbsPtr);
    RESOLVE(B4SOIIdsPtr);
    RESOLVE(B4SOIIcPtr);
    RESOLVE(B4SOIIbsPtr);
    RESOLVE(B4SOIIbdPtr);
    RESOLVE(B4SOIIiiPtr);
    RESOLVE(B4SOIIgPtr);
    RESOLVE(B4SOIGiggPtr);
    RESOLVE(B4SOIGigdPtr);
    RESOLVE(B4SOIGigbPtr);
    RESOLVE(B4SOIIgidlPtr);
    RESOLVE(B4SOIItunPtr);
    RESOLVE(B4SOIIbpPtr);
    RESOLVE(B4SOICbbPtr);
    RESOLVE(B4SOICbdPtr);
    RESOLVE(B4SOICbgPtr);
    RESOLVE(B4SOIqbPtr);
    RESOLVE(B4SOIQbfPtr);
    RESOLVE(B4SOIQjsPtr);
    RESOLVE(B4SOIQjdPtr);

#undef RESOLVE
}

// ---------------------------------------------------------------------------
// set_state_ptrs
// ---------------------------------------------------------------------------
void B4SOIDevice::set_state_ptrs(double* s0, double* s1, double* s2, int32_t base) {
    state0_ = s0;
    state1_ = s1;
    state2_ = s2;
    state_base_ = base;
    inst_.B4SOIstates = base;
}

// ---------------------------------------------------------------------------
// evaluate
// ---------------------------------------------------------------------------
void B4SOIDevice::evaluate(const std::vector<double>& voltages,
                             NumericMatrix& mat,
                             std::vector<double>& rhs) {
    const int n_real = static_cast<int>(rhs.size());
    const int n_ghost = (max_neo_node_ >= 0 ? max_neo_node_ + 1 : 0) + 1;

    ghost_voltages_.assign(n_ghost, 0.0);
    ghost_rhs_.assign(n_ghost, 0.0);
    for (int32_t k = 0; k <= max_neo_node_ && k < n_real; ++k) {
        ghost_voltages_[k + 1] = voltages[k];
    }

    Shim::Ckt ckt;

    // Integrator context.
    const IntegratorCtx* ic = tls_integrator_ctx;
    if (ic) {
        ckt.CKTmode  = ic->mode;
        ckt.CKTorder = ic->order;
        ckt.CKTdelta = ic->delta;
        for (int i = 0; i < 8; ++i) ckt.CKTag[i]       = ic->ag[i];
        for (int i = 0; i < 8; ++i) ckt.CKTdeltaOld[i] = ic->delta_old[i];
    } else {
        ckt.CKTmode  = 0x10 | 0x200;  // MODEDCOP | MODEINITJCT
        ckt.CKTorder = 1;
    }

    // SimOptions.
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

    // State ring.
    ckt.CKTstate0 = state0_;
    ckt.CKTstate1 = state1_;
    ckt.CKTstate2 = state2_;

    // Ghost rhs / old-iterate pointers.
    ckt.CKTrhs    = ghost_rhs_.data();
    ckt.CKTrhsOld = ghost_voltages_.data();
    ckt.mat       = &mat;

    // First-call B4SOItemp.
    if (!temp_done_) {
        int rc = B4SOItemp(model_, &ckt);
        if (rc != Shim::OK) {
            throw std::runtime_error("B4SOItemp failed with rc=" + std::to_string(rc));
        }
        temp_done_ = true;
    }

    // Splice this instance as sole member, call load, then restore.
    B4SOIInstance* saved_head      = model_->B4SOIinstances;
    B4SOIInstance* saved_next_inst = inst_.B4SOInextInstance;
    B4SOIModel*    saved_next_mod  = model_->B4SOInextModel;
    model_->B4SOIinstances  = &inst_;
    inst_.B4SOInextInstance = nullptr;
    model_->B4SOInextModel  = nullptr;
    int rc = B4SOIload(model_, &ckt);
    model_->B4SOIinstances  = saved_head;
    inst_.B4SOInextInstance = saved_next_inst;
    model_->B4SOInextModel  = saved_next_mod;
    if (rc != Shim::OK) {
        throw std::runtime_error("B4SOIload failed with rc=" + std::to_string(rc));
    }

    last_noncon_ = ckt.CKTnoncon;

    // Fold ghost rhs contributions back into the real rhs.
    for (int32_t k = 1; k <= max_neo_node_ + 1 && k < n_ghost; ++k) {
        if (k - 1 < n_real) rhs[k - 1] += ghost_rhs_[k];
    }
}

// ---------------------------------------------------------------------------
// ac_stamp — 67 G entries + 35 C entries extracted from ngspice AC load
// ---------------------------------------------------------------------------
void B4SOIDevice::ac_stamp(const std::vector<double>& /*voltages*/,
                             NumericMatrix& G,
                             NumericMatrix& C) {
    auto& here = inst_;
    auto* model = model_;
    const double m = here.B4SOIm;

    // TODO: Declare local variables for conductances and capacitances.
    // These should be read from instance fields populated by the DC load.
    // Example: const double gm = here.PREFIX_gm;

    // --- G matrix (conductance) entries ---
    // Extracted 67 real-part stamps from ngspice AC load.
    // G: inst_.B4SOIGEgePtr += m * geltd;
    // G: inst_.B4SOIGgPtr += m * (geltd + gigg + gIgtotg);
    // G: inst_.B4SOIGdpPtr += m * (gigd + gIgtotd);
    // G: inst_.B4SOIGspPtr += m * (gigs + gIgtots);
    // G: inst_.B4SOIGEgePtr += m * gcrg;
    // G: inst_.B4SOIGEgPtr += m * gcrgg;
    // G: inst_.B4SOIGEdpPtr += m * gcrgd;
    // G: inst_.B4SOIGEspPtr += m * gcrgs;
    // G: inst_.B4SOIGEbPtr += m * gcrgb;
    // G: inst_.B4SOIGEgePtr += m * geltd;
    // G: inst_.B4SOIGMgmPtr += m * (geltd + gcrg);
    // G: inst_.B4SOIGMdpPtr += m * gcrgd;
    // G: inst_.B4SOIGMgPtr += m * gcrgg;
    // G: inst_.B4SOIGMspPtr += m * gcrgs;
    // G: inst_.B4SOIGMbPtr += m * gcrgb;
    // G: inst_.B4SOIGgPtr += m * (gigg + gIgtotg);
    // G: inst_.B4SOIGdpPtr += m * (gigd + gIgtotd);
    // G: inst_.B4SOIGspPtr += m * (gigs + gIgtots);
    // G: inst_.B4SOIGbPtr += m * (gigb + gIgtotb);
    // G: inst_.B4SOIDgPtr += m * gdtotg);
    // G: inst_.B4SOIDspPtr += m * gdtots);
    // G: inst_.B4SOISdpPtr += m * gstotd);
    // G: inst_.B4SOISgPtr += m * gstotg);
    // G: inst_.B4SOIDbPtr += m * gdtotb);
    // G: inst_.B4SOISbPtr += m * gstotb);
    // G: inst_.B4SOIDPePtr += m * (Gme + gddpe);
    // G: inst_.B4SOISPePtr += m * (gsspe - Gme);
    // G: inst_.B4SOIGePtr += m * gige;
    // G: inst_.B4SOIEePtr += 0.0;
    // G: inst_.B4SOIDPgPtr += m * (Gm + gddpg - gIdtotg -gdtotg);
    // G: inst_.B4SOIBePtr += m * gbbe;
    // G: inst_.B4SOIBgPtr += m * (gbbg - gigg);
    // G: inst_.B4SOIBdpPtr += m * (gbbdp - gigd);
    // G: inst_.B4SOIBspPtr += m * (gbbsp - gigs);
    // G: inst_.B4SOIBbPtr += m * (gbbb - gigb);
    // G: inst_.B4SOIDPtempPtr += m * (GmT + gddpT);
    // G: inst_.B4SOISPtempPtr += m * (-GmT + gsspT);
    // G: inst_.B4SOIBtempPtr += m * (gbbT - gigT);
    // G: inst_.B4SOIGtempPtr += m * gigT;
    // G: inst_.B4SOITemptempPtr += m * (gTtt + 1/here->pParam->B4SOIrth);
    // G: inst_.B4SOITempgPtr += m * gTtg;
    // G: inst_.B4SOITempbPtr += m * gTtb;
    // G: inst_.B4SOITempdpPtr += m * gTtdp;
    // G: inst_.B4SOITempspPtr += m * gTtsp;
    // G: inst_.B4SOITempePtr += m * gTte;
    // G: inst_.B4SOIDdPtr += m * (gdpr + gdtot);
    // G: inst_.B4SOISsPtr += m * (gspr + gstot);
    // G: inst_.B4SOIPbPtr += m * gppb);
    // G: inst_.B4SOIPpPtr += m * gppp);
    // G: inst_.B4SOIGgPtr += gigpg);
    // G: inst_.B4SOIGpPtr += m * gigpp);
    // G: inst_.B4SOIGbPtr += m * gigpp);
    // G: inst_.B4SOIVbsPtr += 1;
    // G: inst_.B4SOIIdsPtr += 1;
    // G: inst_.B4SOIIcPtr += 1;
    // G: inst_.B4SOIIbsPtr += 1;
    // G: inst_.B4SOIIbdPtr += 1;
    // G: inst_.B4SOIIiiPtr += 1;
    // G: inst_.B4SOIIgidlPtr += 1;
    // G: inst_.B4SOIItunPtr += 1;
    // G: inst_.B4SOIIbpPtr += 1;
    // G: inst_.B4SOICbgPtr += 1;
    // G: inst_.B4SOICbbPtr += 1;
    // G: inst_.B4SOICbdPtr += 1;
    // G: inst_.B4SOIQbfPtr += 1;
    // G: inst_.B4SOIQjsPtr += 1;
    // G: inst_.B4SOIQjdPtr += 1;

    // --- C matrix (capacitance) entries ---
    // Extracted 35 imaginary-part stamps from ngspice AC load.
    // NOTE: ngspice stamps *(ptr+1) += value where value = cap * omega.
    // neospice C matrix is multiplied by omega by the AC solver,
    // so stamp the capacitance value directly (without omega).
    // C: inst_.B4SOIGMgmPtr += m * xcgmgmb;  // divide by omega for C matrix
    // C: inst_.B4SOIGMdpPtr += m * xcgmdb;  // divide by omega for C matrix
    // C: inst_.B4SOIGMspPtr += m * xcgmsb;  // divide by omega for C matrix
    // C: inst_.B4SOIGMePtr += m * xcgmeb;  // divide by omega for C matrix
    // C: inst_.B4SOIDPgmPtr += m * xcdgmb;  // divide by omega for C matrix
    // C: inst_.B4SOISPgmPtr += m * xcsgmb;  // divide by omega for C matrix
    // C: inst_.B4SOIEgmPtr += m * xcegmb;  // divide by omega for C matrix
    // C: inst_.B4SOIEdpPtr += m * xcedb;  // divide by omega for C matrix
    // C: inst_.B4SOIEspPtr += m * xcesb;  // divide by omega for C matrix
    // C: inst_.B4SOIDPePtr += m * xcdeb;  // divide by omega for C matrix
    // C: inst_.B4SOISPePtr += m * xcseb;  // divide by omega for C matrix
    // C: inst_.B4SOIEgPtr += m * xcegb;  // divide by omega for C matrix
    // C: inst_.B4SOIGePtr += m * xcgeb;  // divide by omega for C matrix
    // C: inst_.B4SOIEePtr += m * xceeb;  // divide by omega for C matrix
    // C: inst_.B4SOIGgPtr += m * xcggb;  // divide by omega for C matrix
    // C: inst_.B4SOIGdpPtr += m * xcgdb;  // divide by omega for C matrix
    // C: inst_.B4SOIGspPtr += m * xcgsb;  // divide by omega for C matrix
    // C: inst_.B4SOIDPgPtr += m * xcdgb;  // divide by omega for C matrix
    // C: inst_.B4SOIDPdpPtr += m * xcddb;  // divide by omega for C matrix
    // C: inst_.B4SOIDPspPtr += m * xcdsb;  // divide by omega for C matrix
    // C: inst_.B4SOISPgPtr += m * xcsgb;  // divide by omega for C matrix
    // C: inst_.B4SOISPdpPtr += m * xcsdb;  // divide by omega for C matrix
    // C: inst_.B4SOISPspPtr += m * xcssb;  // divide by omega for C matrix
    // C: inst_.B4SOIBePtr += m * xcbeb;  // divide by omega for C matrix
    // C: inst_.B4SOIBgPtr += m * xcbgb;  // divide by omega for C matrix
    // C: inst_.B4SOIBdpPtr += m * xcbdb;  // divide by omega for C matrix
    // C: inst_.B4SOIBspPtr += m * xcbsb;  // divide by omega for C matrix
    // C: inst_.B4SOITemptempPtr += m * xcTt;  // divide by omega for C matrix
    // C: inst_.B4SOIDPtempPtr += m * xcdT;  // divide by omega for C matrix
    // C: inst_.B4SOISPtempPtr += m * xcsT;  // divide by omega for C matrix
    // C: inst_.B4SOIBtempPtr += m * xcbT;  // divide by omega for C matrix
    // C: inst_.B4SOIEtempPtr += m * xceT;  // divide by omega for C matrix
    // C: inst_.B4SOIGtempPtr += m * xcgT;  // divide by omega for C matrix
    // C: inst_.B4SOIDBdbPtr += m * xcjdbdp);  // divide by omega for C matrix
    // C: inst_.B4SOISBsbPtr += m * xcjsbsp);  // divide by omega for C matrix

    // TODO: Implement the stamp logic by reading instance fields,
    // computing y-parameters, and stamping into G and C matrices.
    // See hisim2_device.cpp or bsim4v7_device.cpp for reference.
    (void)here; (void)model; (void)m;
}

// ---------------------------------------------------------------------------
// compute_trunc
// ---------------------------------------------------------------------------
double B4SOIDevice::compute_trunc(const IntegratorCtx& ctx,
                               const SimOptions& opts) const {
    if (ctx.order < 2 || ctx.delta <= 0.0) return 1e30;
    if (!state0_ || !state1_ || !state2_) return 1e30;

    const double h0 = ctx.delta;
    const double h1 = ctx.delta_old[1];
    if (h1 <= 0.0) return 1e30;

    double dt_min = 1e30;
    const double lte_coeff = ctx.lte_coefficient();

    { // charge offset: inst_.B4SOIqb
        const int qcap = inst_.B4SOIqb;
        const double q0 = state0_[qcap], q1 = state1_[qcap], q2 = state2_[qcap];
        const double dd2 = ((q0 - q1) / h0 - (q1 - q2) / h1) / (h0 + h1);
        const double tol = opts.chgtol + opts.reltol * std::max(std::abs(q0), std::abs(q1));
        if (tol > 0.0 && std::abs(dd2) > 1e-30) {
            dt_min = std::min(dt_min, std::sqrt(opts.trtol * tol / (lte_coeff * std::abs(dd2))));
        }
    }
    { // charge offset: inst_.B4SOIqg
        const int qcap = inst_.B4SOIqg;
        const double q0 = state0_[qcap], q1 = state1_[qcap], q2 = state2_[qcap];
        const double dd2 = ((q0 - q1) / h0 - (q1 - q2) / h1) / (h0 + h1);
        const double tol = opts.chgtol + opts.reltol * std::max(std::abs(q0), std::abs(q1));
        if (tol > 0.0 && std::abs(dd2) > 1e-30) {
            dt_min = std::min(dt_min, std::sqrt(opts.trtol * tol / (lte_coeff * std::abs(dd2))));
        }
    }
    { // charge offset: inst_.B4SOIqd
        const int qcap = inst_.B4SOIqd;
        const double q0 = state0_[qcap], q1 = state1_[qcap], q2 = state2_[qcap];
        const double dd2 = ((q0 - q1) / h0 - (q1 - q2) / h1) / (h0 + h1);
        const double tol = opts.chgtol + opts.reltol * std::max(std::abs(q0), std::abs(q1));
        if (tol > 0.0 && std::abs(dd2) > 1e-30) {
            dt_min = std::min(dt_min, std::sqrt(opts.trtol * tol / (lte_coeff * std::abs(dd2))));
        }
    }
    { // charge offset: inst_.B4SOIqe
        const int qcap = inst_.B4SOIqe;
        const double q0 = state0_[qcap], q1 = state1_[qcap], q2 = state2_[qcap];
        const double dd2 = ((q0 - q1) / h0 - (q1 - q2) / h1) / (h0 + h1);
        const double tol = opts.chgtol + opts.reltol * std::max(std::abs(q0), std::abs(q1));
        if (tol > 0.0 && std::abs(dd2) > 1e-30) {
            dt_min = std::min(dt_min, std::sqrt(opts.trtol * tol / (lte_coeff * std::abs(dd2))));
        }
    }
    { // charge offset: inst_.B4SOIqth
        const int qcap = inst_.B4SOIqth;
        const double q0 = state0_[qcap], q1 = state1_[qcap], q2 = state2_[qcap];
        const double dd2 = ((q0 - q1) / h0 - (q1 - q2) / h1) / (h0 + h1);
        const double tol = opts.chgtol + opts.reltol * std::max(std::abs(q0), std::abs(q1));
        if (tol > 0.0 && std::abs(dd2) > 1e-30) {
            dt_min = std::min(dt_min, std::sqrt(opts.trtol * tol / (lte_coeff * std::abs(dd2))));
        }
    }
    { // charge offset: inst_.B4SOIqgmid
        const int qcap = inst_.B4SOIqgmid;
        const double q0 = state0_[qcap], q1 = state1_[qcap], q2 = state2_[qcap];
        const double dd2 = ((q0 - q1) / h0 - (q1 - q2) / h1) / (h0 + h1);
        const double tol = opts.chgtol + opts.reltol * std::max(std::abs(q0), std::abs(q1));
        if (tol > 0.0 && std::abs(dd2) > 1e-30) {
            dt_min = std::min(dt_min, std::sqrt(opts.trtol * tol / (lte_coeff * std::abs(dd2))));
        }
    }
    { // charge offset: inst_.B4SOIqbs
        const int qcap = inst_.B4SOIqbs;
        const double q0 = state0_[qcap], q1 = state1_[qcap], q2 = state2_[qcap];
        const double dd2 = ((q0 - q1) / h0 - (q1 - q2) / h1) / (h0 + h1);
        const double tol = opts.chgtol + opts.reltol * std::max(std::abs(q0), std::abs(q1));
        if (tol > 0.0 && std::abs(dd2) > 1e-30) {
            dt_min = std::min(dt_min, std::sqrt(opts.trtol * tol / (lte_coeff * std::abs(dd2))));
        }
    }
    { // charge offset: inst_.B4SOIqbd
        const int qcap = inst_.B4SOIqbd;
        const double q0 = state0_[qcap], q1 = state1_[qcap], q2 = state2_[qcap];
        const double dd2 = ((q0 - q1) / h0 - (q1 - q2) / h1) / (h0 + h1);
        const double tol = opts.chgtol + opts.reltol * std::max(std::abs(q0), std::abs(q1));
        if (tol > 0.0 && std::abs(dd2) > 1e-30) {
            dt_min = std::min(dt_min, std::sqrt(opts.trtol * tol / (lte_coeff * std::abs(dd2))));
        }
    }
    return dt_min;
}

// ---------------------------------------------------------------------------
// device_converged
// ---------------------------------------------------------------------------
bool B4SOIDevice::device_converged() const {
    return last_noncon_ == 0;
}

// ---------------------------------------------------------------------------
// str_tolower helper
// ---------------------------------------------------------------------------
static std::string str_tolower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// ---------------------------------------------------------------------------
// query_param
// ---------------------------------------------------------------------------
std::optional<double> B4SOIDevice::query_param(const std::string& name) const {
    const std::string key = str_tolower(name);
    const double m = inst_.B4SOIm;

    // --- Operating-point parameters (scaled by multiplier m) ---
    if (key == "gm")                        return inst_.B4SOIgm * m;
    if (key == "gds")                       return inst_.B4SOIgds * m;
    if (key == "gmbs")                      return inst_.B4SOIgmbs * m;
    if (key == "vth" || key == "von")       return inst_.B4SOIvon;
    if (key == "vdsat")                     return inst_.B4SOIvdsat;
    if (key == "id" || key == "ids")        return inst_.B4SOIcd * m;
    if (key == "ibs")                       return inst_.B4SOIibs * m;
    if (key == "ibd")                       return inst_.B4SOIibd * m;
    if (key == "iii" || key == "isub")      return inst_.B4SOIiii * m;
    if (key == "ig")                        return inst_.B4SOIig * m;
    if (key == "igidl")                     return inst_.B4SOIigidl * m;
    if (key == "igisl")                     return inst_.B4SOIigisl * m;
    if (key == "igcs")                      return inst_.B4SOIIgcs * m;
    if (key == "igcd")                      return inst_.B4SOIIgcd * m;
    if (key == "igs")                       return inst_.B4SOIIgs * m;
    if (key == "igd")                       return inst_.B4SOIIgd * m;
    if (key == "igb")                       return inst_.B4SOIIgb * m;

    // --- Capacitances ---
    if (key == "cbg")                       return inst_.B4SOIcbg * m;
    if (key == "cbb")                       return inst_.B4SOIcbb * m;
    if (key == "cbd")                       return inst_.B4SOIcbd * m;

    // --- Charges ---
    if (key == "qg" && state0_)             return state0_[state_base_ + 14] * m;
    if (key == "qb" && state0_)             return state0_[state_base_ + 12] * m;
    if (key == "qd" && state0_)             return state0_[state_base_ + 16] * m;
    if (key == "qe" && state0_)             return state0_[state_base_ + 18] * m;

    // --- Terminal voltages from state vector ---
    if (state0_ && state_base_ >= 0) {
        if (key == "vbd")                   return state0_[state_base_ + 0];
        if (key == "vbs")                   return state0_[state_base_ + 1];
        if (key == "vgs")                   return state0_[state_base_ + 2];
        if (key == "vds")                   return state0_[state_base_ + 3];
        if (key == "ves")                   return state0_[state_base_ + 4];
        if (key == "vps")                   return state0_[state_base_ + 5];
    }

    // --- Geometry (no multiplier) ---
    if (key == "w")                         return inst_.B4SOIw;
    if (key == "l")                         return inst_.B4SOIl;
    if (key == "m")                         return inst_.B4SOIm;
    if (key == "nf")                        return inst_.B4SOInf;
    if (key == "sa")                        return inst_.B4SOIsa;
    if (key == "sb")                        return inst_.B4SOIsb;
    if (key == "sd")                        return inst_.B4SOIsd;
    if (key == "ad")                        return inst_.B4SOIdrainArea;
    if (key == "as")                        return inst_.B4SOIsourceArea;
    if (key == "pd")                        return inst_.B4SOIdrainPerimeter;
    if (key == "ps")                        return inst_.B4SOIsourcePerimeter;
    if (key == "nrd")                       return inst_.B4SOIdrainSquares;
    if (key == "nrs")                       return inst_.B4SOIsourceSquares;
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// noise_sources — TODO: implement device noise model
// ---------------------------------------------------------------------------
std::vector<Device::NoiseSource> B4SOIDevice::noise_sources(
        double /*freq*/, const std::vector<double>& /*dc_solution*/) const {
    // TODO: Port noise sources from the ngspice b4soinoi.c file.
    // Common noise types:
    //   Thermal: 4*k*T*G  (conductance noise)
    //   Shot:    2*q*|I|  (junction current noise)
    //   Flicker: KF*|I|^AF / f^EF  (1/f noise)
    return {};
}

// ---------------------------------------------------------------------------
// set_ic — store instance initial conditions from M-card ic= parameter
// ---------------------------------------------------------------------------
void B4SOIDevice::set_ic(double vds, bool vds_given,
                          double vgs, bool vgs_given,
                          double vbs, bool vbs_given) {
    if (vds_given) { inst_.B4SOIicVDS = vds; inst_.B4SOIicVDSGiven = 1; }
    if (vgs_given) { inst_.B4SOIicVGS = vgs; inst_.B4SOIicVGSGiven = 1; }
    if (vbs_given) { inst_.B4SOIicVBS = vbs; inst_.B4SOIicVBSGiven = 1; }
}

} // namespace neospice
