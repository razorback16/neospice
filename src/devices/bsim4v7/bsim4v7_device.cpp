#include "devices/bsim4v7/bsim4v7_device.hpp"

#include "core/circuit.hpp"        // Circuit::node, tls_integrator_ctx
#include "core/types.hpp"          // SimOptions defaults

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <stdexcept>
#include <string>

// Forward declarations for translated UCB functions.
namespace neospice::bsim4v7 {
    int BSIM4v7setup(Shim::Matrix*, BSIM4v7Model*, Shim::Ckt*, int*);
    int BSIM4v7temp(BSIM4v7Model*, Shim::Ckt*);
    int BSIM4v7load(BSIM4v7Model*, Shim::Ckt*);
}

namespace neospice {

using namespace neospice::bsim4v7;

// ---------------------------------------------------------------------------
// BSIM4v7ModelCard destructor
// ---------------------------------------------------------------------------
BSIM4v7ModelCard::~BSIM4v7ModelCard() {
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
std::unique_ptr<BSIM4v7Device>
BSIM4v7Device::make(std::string name,
        int32_t n_d, int32_t n_g, int32_t n_s, int32_t n_b,
        const Geom& geom, BSIM4v7ModelCard& shared_card) {
    std::unique_ptr<BSIM4v7Device> dev(new BSIM4v7Device(std::move(name)));
    dev->model_ = &shared_card.ucb;

    if (!shared_card.ucb.BSIM4v7versionGiven) {
        shared_card.ucb.BSIM4v7version = "4.7.0";
        shared_card.ucb.BSIM4v7versionGiven = 1;
    }

    auto& inst = dev->inst_;
    inst.BSIM4v7name = dev->name().c_str();
    inst.BSIM4v7modPtr = dev->model_;

    // Node wiring (UCB convention).
    inst.BSIM4v7dNode = neo_to_ucb(n_d);
    inst.BSIM4v7gNodeExt = neo_to_ucb(n_g);
    inst.BSIM4v7sNode = neo_to_ucb(n_s);
    inst.BSIM4v7bNode = neo_to_ucb(n_b);

    // Geometry.
    inst.BSIM4v7w = geom.W;
    inst.BSIM4v7wGiven = 1;
    inst.BSIM4v7l = geom.L;
    inst.BSIM4v7lGiven = 1;
    inst.BSIM4v7nf = geom.NF;
    inst.BSIM4v7nfGiven = 1;
    inst.BSIM4v7drainArea = geom.AD;
    inst.BSIM4v7drainAreaGiven = (geom.AD != 0.0) ? 1 : 0;
    inst.BSIM4v7sourceArea = geom.AS;
    inst.BSIM4v7sourceAreaGiven = (geom.AS != 0.0) ? 1 : 0;
    inst.BSIM4v7drainPerimeter = geom.PD;
    inst.BSIM4v7drainPerimeterGiven = (geom.PD != 0.0) ? 1 : 0;
    inst.BSIM4v7sourcePerimeter = geom.PS;
    inst.BSIM4v7sourcePerimeterGiven = (geom.PS != 0.0) ? 1 : 0;
    inst.BSIM4v7drainSquares = geom.NRD;
    inst.BSIM4v7drainSquaresGiven = (geom.NRD != 0.0) ? 1 : 0;
    inst.BSIM4v7sourceSquares = geom.NRS;
    inst.BSIM4v7sourceSquaresGiven = (geom.NRS != 0.0) ? 1 : 0;
    inst.BSIM4v7sa = geom.SA;
    inst.BSIM4v7saGiven = (geom.SA != 0.0) ? 1 : 0;
    inst.BSIM4v7sb = geom.SB;
    inst.BSIM4v7sbGiven = (geom.SB != 0.0) ? 1 : 0;
    inst.BSIM4v7sd = geom.SD;
    inst.BSIM4v7sdGiven = (geom.SD != 0.0) ? 1 : 0;

    // Thread onto the shared model's instance list.
    inst.BSIM4v7nextInstance = shared_card.ucb.BSIM4v7instances;
    shared_card.ucb.BSIM4v7instances = &inst;

    // Remember the widest real node index for ghost array sizing.
    int32_t widest = -1;
    for (int32_t n : {n_d, n_g, n_s, n_b}) if (n > widest) widest = n;
    dev->max_neo_node_ = widest;

    return dev;
}

// ---------------------------------------------------------------------------
// declare_internal_nodes
// ---------------------------------------------------------------------------
void BSIM4v7Device::declare_internal_nodes(Circuit& ckt) {
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
    int rc = BSIM4v7setup(&shim_matrix, model_, &setup_ckt, &states);
    if (rc != Shim::OK) {
        throw std::runtime_error("BSIM4v7setup failed with rc=" + std::to_string(rc));
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
void BSIM4v7Device::stamp_pattern(SparsityBuilder& builder) const {
    for (auto [r, c] : journal_) {
        if (r <= 0 || c <= 0) continue;
        builder.add(r - 1, c - 1);
    }
}

// ---------------------------------------------------------------------------
// assign_offsets
// ---------------------------------------------------------------------------
void BSIM4v7Device::assign_offsets(const SparsityPattern& pattern) {
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

    RESOLVE(BSIM4v7DPbpPtr);
    RESOLVE(BSIM4v7GPbpPtr);
    RESOLVE(BSIM4v7SPbpPtr);
    RESOLVE(BSIM4v7BPdpPtr);
    RESOLVE(BSIM4v7BPgpPtr);
    RESOLVE(BSIM4v7BPspPtr);
    RESOLVE(BSIM4v7BPbpPtr);
    RESOLVE(BSIM4v7DdPtr);
    RESOLVE(BSIM4v7GPgpPtr);
    RESOLVE(BSIM4v7SsPtr);
    RESOLVE(BSIM4v7DPdpPtr);
    RESOLVE(BSIM4v7SPspPtr);
    RESOLVE(BSIM4v7DdpPtr);
    RESOLVE(BSIM4v7GPdpPtr);
    RESOLVE(BSIM4v7GPspPtr);
    RESOLVE(BSIM4v7SspPtr);
    RESOLVE(BSIM4v7DPspPtr);
    RESOLVE(BSIM4v7DPdPtr);
    RESOLVE(BSIM4v7DPgpPtr);
    RESOLVE(BSIM4v7SPgpPtr);
    RESOLVE(BSIM4v7SPsPtr);
    RESOLVE(BSIM4v7SPdpPtr);
    RESOLVE(BSIM4v7QqPtr);
    RESOLVE(BSIM4v7QbpPtr);
    RESOLVE(BSIM4v7QdpPtr);
    RESOLVE(BSIM4v7QspPtr);
    RESOLVE(BSIM4v7QgpPtr);
    RESOLVE(BSIM4v7DPqPtr);
    RESOLVE(BSIM4v7SPqPtr);
    RESOLVE(BSIM4v7GPqPtr);
    RESOLVE(BSIM4v7GEgePtr);
    RESOLVE(BSIM4v7GEgpPtr);
    RESOLVE(BSIM4v7GPgePtr);
    RESOLVE(BSIM4v7GEdpPtr);
    RESOLVE(BSIM4v7GEspPtr);
    RESOLVE(BSIM4v7GEbpPtr);
    RESOLVE(BSIM4v7GMdpPtr);
    RESOLVE(BSIM4v7GMgpPtr);
    RESOLVE(BSIM4v7GMgmPtr);
    RESOLVE(BSIM4v7GMgePtr);
    RESOLVE(BSIM4v7GMspPtr);
    RESOLVE(BSIM4v7GMbpPtr);
    RESOLVE(BSIM4v7DPgmPtr);
    RESOLVE(BSIM4v7GPgmPtr);
    RESOLVE(BSIM4v7GEgmPtr);
    RESOLVE(BSIM4v7SPgmPtr);
    RESOLVE(BSIM4v7BPgmPtr);
    RESOLVE(BSIM4v7DPdbPtr);
    RESOLVE(BSIM4v7SPsbPtr);
    RESOLVE(BSIM4v7DBdpPtr);
    RESOLVE(BSIM4v7DBdbPtr);
    RESOLVE(BSIM4v7DBbpPtr);
    RESOLVE(BSIM4v7DBbPtr);
    RESOLVE(BSIM4v7BPdbPtr);
    RESOLVE(BSIM4v7BPbPtr);
    RESOLVE(BSIM4v7BPsbPtr);
    RESOLVE(BSIM4v7SBspPtr);
    RESOLVE(BSIM4v7SBbpPtr);
    RESOLVE(BSIM4v7SBbPtr);
    RESOLVE(BSIM4v7SBsbPtr);
    RESOLVE(BSIM4v7BdbPtr);
    RESOLVE(BSIM4v7BbpPtr);
    RESOLVE(BSIM4v7BsbPtr);
    RESOLVE(BSIM4v7BbPtr);
    RESOLVE(BSIM4v7DgpPtr);
    RESOLVE(BSIM4v7DspPtr);
    RESOLVE(BSIM4v7DbpPtr);
    RESOLVE(BSIM4v7SdpPtr);
    RESOLVE(BSIM4v7SgpPtr);
    RESOLVE(BSIM4v7SbpPtr);

#undef RESOLVE
}

// ---------------------------------------------------------------------------
// set_state_ptrs
// ---------------------------------------------------------------------------
void BSIM4v7Device::set_state_ptrs(double* s0, double* s1, double* s2, int32_t base) {
    state0_ = s0;
    state1_ = s1;
    state2_ = s2;
    state_base_ = base;
    inst_.BSIM4v7states = base;
}

// ---------------------------------------------------------------------------
// evaluate
// ---------------------------------------------------------------------------
void BSIM4v7Device::evaluate(const std::vector<double>& voltages,
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
        ckt.CKTmode  = 0x70 | 0x200;  // MODEDC | MODEINITJCT
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

    // First-call BSIM4v7temp.
    if (!temp_done_) {
        int rc = BSIM4v7temp(model_, &ckt);
        if (rc != Shim::OK) {
            throw std::runtime_error("BSIM4v7temp failed with rc=" + std::to_string(rc));
        }
        temp_done_ = true;
    }

    // Splice this instance as sole member, call load, then restore.
    BSIM4v7Instance* saved_head      = model_->BSIM4v7instances;
    BSIM4v7Instance* saved_next_inst = inst_.BSIM4v7nextInstance;
    BSIM4v7Model*    saved_next_mod  = model_->BSIM4v7nextModel;
    model_->BSIM4v7instances  = &inst_;
    inst_.BSIM4v7nextInstance = nullptr;
    model_->BSIM4v7nextModel  = nullptr;
    int rc = BSIM4v7load(model_, &ckt);
    model_->BSIM4v7instances  = saved_head;
    inst_.BSIM4v7nextInstance = saved_next_inst;
    model_->BSIM4v7nextModel  = saved_next_mod;
    if (rc != Shim::OK) {
        throw std::runtime_error("BSIM4v7load failed with rc=" + std::to_string(rc));
    }

    // Capture the UCB convergence flag for device_converged().
    last_noncon_ = ckt.CKTnoncon;

    // Fold ghost rhs contributions back into the real rhs.
    for (int32_t k = 1; k <= max_neo_node_ + 1 && k < n_ghost; ++k) {
        if (k - 1 < n_real) rhs[k - 1] += ghost_rhs_[k];
    }
}

// ---------------------------------------------------------------------------
// device_converged
// ---------------------------------------------------------------------------
bool BSIM4v7Device::device_converged() const {
    return last_noncon_ == 0;
}

// ---------------------------------------------------------------------------
// set_ic
// ---------------------------------------------------------------------------
void BSIM4v7Device::set_ic(double vds, bool vds_given,
                            double vgs, bool vgs_given,
                            double vbs, bool vbs_given) {
    if (vds_given) { inst_.BSIM4v7icVDS = vds; inst_.BSIM4v7icVDSGiven = 1; }
    if (vgs_given) { inst_.BSIM4v7icVGS = vgs; inst_.BSIM4v7icVGSGiven = 1; }
    if (vbs_given) { inst_.BSIM4v7icVBS = vbs; inst_.BSIM4v7icVBSGiven = 1; }
}

// ---------------------------------------------------------------------------
// ac_stamp — linearized small-signal AC stamp (QS path only)
//
// Translates the ngspice BSIM4v7acLoad() complex-matrix stamping into
// separate G (conductance) and C (capacitance) matrices.  The AC solver
// combines them as (G + jωC) at each frequency point.
//
// In the ngspice code:
//   *(ptr)     += real_value  → stamp into G
//   *(ptr + 1) += xcXXX       where xcXXX = cap * omega → stamp cap into C
//
// For QS mode all imaginary conductance terms (gmi, gmbsi, gdsi, Cdgi, etc.)
// are zero, which greatly simplifies the translation: only real-part stamps
// go to G and the capacitance-derived stamps go to C.
// ---------------------------------------------------------------------------
void BSIM4v7Device::ac_stamp(const std::vector<double>& /*voltages*/,
                              NumericMatrix& G, NumericMatrix& C) {
    auto& inst = inst_;
    auto* model = model_;
    auto* pParam = inst.pParam;

    if (inst.BSIM4v7acnqsMod) {
        thread_local bool warned = false;
        if (!warned) {
            fprintf(stderr, "WARNING: BSIM4v7 acnqsMod != 0 (NQS) not supported "
                    "in AC analysis — results may be inaccurate for device '%s'\n",
                    name_.c_str());
            warned = true;
        }
        return;
    }

    const double m = inst.BSIM4v7m;

    // --- Capacitance values from instance (QS path) ---
    const double capbd = inst.BSIM4v7capbd;
    const double capbs = inst.BSIM4v7capbs;
    const double cgso  = inst.BSIM4v7cgso;
    const double cgdo  = inst.BSIM4v7cgdo;
    const double cgbo  = pParam->BSIM4v7cgbo;

    // Derived source-side charge-conservation capacitances
    const double Csd = -(inst.BSIM4v7cddb + inst.BSIM4v7cgdb + inst.BSIM4v7cbdb);
    const double Csg = -(inst.BSIM4v7cdgb + inst.BSIM4v7cggb + inst.BSIM4v7cbgb);
    const double Css = -(inst.BSIM4v7cdsb + inst.BSIM4v7cgsb + inst.BSIM4v7cbsb);

    // QS intrinsic capacitances
    double Cddr = inst.BSIM4v7cddb;
    double Cdgr = inst.BSIM4v7cdgb;
    double Cdsr = inst.BSIM4v7cdsb;
    double Cdbr = -(Cddr + Cdgr + Cdsr);

    double Csdr = Csd;
    double Csgr = Csg;
    double Cssr = Css;
    double Csbr = -(Csdr + Csgr + Cssr);

    double Cgdr = inst.BSIM4v7cgdb;
    double Cggr = inst.BSIM4v7cggb;
    double Cgsr = inst.BSIM4v7cgsb;

    // --- Conductance values ---
    const double gmr   = inst.BSIM4v7gm;
    const double gmbsr = inst.BSIM4v7gmbs;
    const double gdsr  = inst.BSIM4v7gds;

    // --- Mode-dependent variables ---
    double Gmr, Gmbsr, FwdSumr, RevSumr;
    double gbbdp, gbbsp, gbdpg, gbdpdp, gbdpb, gbdpsp;
    double gbspdp, gbspg, gbspb, gbspsp;
    double gIstotg, gIstotd, gIstots, gIstotb;
    double gIdtotg, gIdtotd, gIdtots, gIdtotb;
    double gIbtotg, gIbtotd, gIbtots, gIbtotb;
    double gIgtotg, gIgtotd, gIgtots, gIgtotb;
    double gcrg, gcrgd, gcrgg, gcrgs, gcrgb;

    // Capacitance stamps (without omega) for the C matrix
    double C_GPgp, C_GPdp, C_GPsp, C_GPbp;
    double C_DPdp, C_DPgp, C_DPsp, C_DPbp;
    double C_SPdp, C_SPgp, C_SPsp, C_SPbp;
    double C_BPdp, C_BPgp, C_BPsp, C_BPbp;

    // rgateMod == 3 extra caps
    double C_GMgm = 0, C_GMdp = 0, C_GMsp = 0, C_GMbp = 0;
    double C_DPgm = 0, C_SPgm = 0, C_BPgm = 0;

    // rbodyMod caps
    double C_dbdb = 0.0;
    double C_sbsb = 0.0;

    if (inst.BSIM4v7mode >= 0) {
        // Forward mode
        Gmr = gmr;
        Gmbsr = gmbsr;
        FwdSumr = Gmr + Gmbsr;
        RevSumr = 0.0;

        gbbdp = -(inst.BSIM4v7gbds);
        gbbsp = inst.BSIM4v7gbds + inst.BSIM4v7gbgs + inst.BSIM4v7gbbs;
        gbdpg = inst.BSIM4v7gbgs;
        gbdpdp = inst.BSIM4v7gbds;
        gbdpb = inst.BSIM4v7gbbs;
        gbdpsp = -(gbdpg + gbdpdp + gbdpb);

        gbspdp = 0.0;
        gbspg = 0.0;
        gbspb = 0.0;
        gbspsp = 0.0;

        // igcMod
        if (model->BSIM4v7igcMod) {
            gIstotg = inst.BSIM4v7gIgsg + inst.BSIM4v7gIgcsg;
            gIstotd = inst.BSIM4v7gIgcsd;
            gIstots = inst.BSIM4v7gIgss + inst.BSIM4v7gIgcss;
            gIstotb = inst.BSIM4v7gIgcsb;
            gIdtotg = inst.BSIM4v7gIgdg + inst.BSIM4v7gIgcdg;
            gIdtotd = inst.BSIM4v7gIgdd + inst.BSIM4v7gIgcdd;
            gIdtots = inst.BSIM4v7gIgcds;
            gIdtotb = inst.BSIM4v7gIgcdb;
        } else {
            gIstotg = gIstotd = gIstots = gIstotb = 0.0;
            gIdtotg = gIdtotd = gIdtots = gIdtotb = 0.0;
        }

        // igbMod
        if (model->BSIM4v7igbMod) {
            gIbtotg = inst.BSIM4v7gIgbg;
            gIbtotd = inst.BSIM4v7gIgbd;
            gIbtots = inst.BSIM4v7gIgbs;
            gIbtotb = inst.BSIM4v7gIgbb;
        } else {
            gIbtotg = gIbtotd = gIbtots = gIbtotb = 0.0;
        }

        if (model->BSIM4v7igcMod || model->BSIM4v7igbMod) {
            gIgtotg = gIstotg + gIdtotg + gIbtotg;
            gIgtotd = gIstotd + gIdtotd + gIbtotd;
            gIgtots = gIstots + gIdtots + gIbtots;
            gIgtotb = gIstotb + gIdtotb + gIbtotb;
        } else {
            gIgtotg = gIgtotd = gIgtots = gIgtotb = 0.0;
        }

        // gcrg (charge redistribution gate current) for rgateMod > 1
        double T0 = 0.0;
        if (inst.BSIM4v7rgateMod == 2)
            T0 = state0_[inst.BSIM4v7vges - inst.BSIM4v7states]
               - state0_[inst.BSIM4v7vgs - inst.BSIM4v7states];
        else if (inst.BSIM4v7rgateMod == 3)
            T0 = state0_[inst.BSIM4v7vgms - inst.BSIM4v7states]
               - state0_[inst.BSIM4v7vgs - inst.BSIM4v7states];

        if (inst.BSIM4v7rgateMod > 1) {
            gcrgd = inst.BSIM4v7gcrgd * T0;
            gcrgg = inst.BSIM4v7gcrgg * T0;
            gcrgs = inst.BSIM4v7gcrgs * T0;
            gcrgb = inst.BSIM4v7gcrgb * T0;
            gcrgg -= inst.BSIM4v7gcrg;
            gcrg = inst.BSIM4v7gcrg;
        } else {
            gcrg = gcrgd = gcrgg = gcrgs = gcrgb = 0.0;
        }

        // --- Build C matrix capacitance values (forward mode) ---
        if (inst.BSIM4v7rgateMod == 3) {
            C_GMgm = cgdo + cgso + cgbo;
            C_GMdp = -cgdo;
            C_GMsp = -cgso;
            C_GMbp = -cgbo;
            C_DPgm = -cgdo;  // xcdgmb = xcgmdb
            C_SPgm = -cgso;  // xcsgmb = xcgmsb
            C_BPgm = -cgbo;  // xcbgmb = xcgmbb

            C_GPgp = Cggr;
            C_GPdp = Cgdr;
            C_GPsp = Cgsr;
            C_GPbp = -(C_GPgp + C_GPdp + C_GPsp);

            C_DPgp = Cdgr;
            C_SPgp = Csgr;
            C_BPgp = inst.BSIM4v7cbgb;
        } else {
            C_GPgp = Cggr + cgdo + cgso + cgbo;
            C_GPdp = Cgdr - cgdo;
            C_GPsp = Cgsr - cgso;
            C_GPbp = -(C_GPgp + C_GPdp + C_GPsp);

            C_DPgp = Cdgr - cgdo;
            C_SPgp = Csgr - cgso;
            C_BPgp = inst.BSIM4v7cbgb - cgbo;

            C_DPgm = C_SPgm = C_BPgm = 0.0;
        }

        C_DPdp = Cddr + capbd + cgdo;
        C_DPsp = Cdsr;
        C_SPdp = Csdr;
        C_SPsp = capbs + cgso + Cssr;

        if (!inst.BSIM4v7rbodyMod) {
            C_DPbp = -(C_DPgp + C_DPdp + C_DPsp + C_DPgm);
            C_SPbp = -(C_SPgp + C_SPdp + C_SPsp + C_SPgm);
            C_BPdp = inst.BSIM4v7cbdb - capbd;
            C_BPsp = inst.BSIM4v7cbsb - capbs;
            C_dbdb = 0.0;
        } else {
            C_DPbp = Cdbr;
            C_SPbp = -(C_SPgp + C_SPdp + C_SPsp + C_SPgm) + capbs;
            C_BPdp = inst.BSIM4v7cbdb;
            C_BPsp = inst.BSIM4v7cbsb;
            C_dbdb = -capbd;
            C_sbsb = -capbs;
        }
        C_BPbp = -(C_BPdp + C_BPgp + C_BPsp + C_BPgm);

    } else {
        // Reverse mode
        Gmr = -gmr;
        Gmbsr = -gmbsr;
        FwdSumr = 0.0;
        RevSumr = -(Gmr + Gmbsr);

        gbbsp = -(inst.BSIM4v7gbds);
        gbbdp = inst.BSIM4v7gbds + inst.BSIM4v7gbgs + inst.BSIM4v7gbbs;

        gbdpg = 0.0;
        gbdpsp = 0.0;
        gbdpb = 0.0;
        gbdpdp = 0.0;

        gbspg = inst.BSIM4v7gbgs;
        gbspsp = inst.BSIM4v7gbds;
        gbspb = inst.BSIM4v7gbbs;
        gbspdp = -(gbspg + gbspsp + gbspb);

        if (model->BSIM4v7igcMod) {
            gIstotg = inst.BSIM4v7gIgsg + inst.BSIM4v7gIgcdg;
            gIstotd = inst.BSIM4v7gIgcds;
            gIstots = inst.BSIM4v7gIgss + inst.BSIM4v7gIgcdd;
            gIstotb = inst.BSIM4v7gIgcdb;
            gIdtotg = inst.BSIM4v7gIgdg + inst.BSIM4v7gIgcsg;
            gIdtotd = inst.BSIM4v7gIgdd + inst.BSIM4v7gIgcss;
            gIdtots = inst.BSIM4v7gIgcsd;
            gIdtotb = inst.BSIM4v7gIgcsb;
        } else {
            gIstotg = gIstotd = gIstots = gIstotb = 0.0;
            gIdtotg = gIdtotd = gIdtots = gIdtotb = 0.0;
        }

        if (model->BSIM4v7igbMod) {
            gIbtotg = inst.BSIM4v7gIgbg;
            gIbtotd = inst.BSIM4v7gIgbs;
            gIbtots = inst.BSIM4v7gIgbd;
            gIbtotb = inst.BSIM4v7gIgbb;
        } else {
            gIbtotg = gIbtotd = gIbtots = gIbtotb = 0.0;
        }

        if (model->BSIM4v7igcMod || model->BSIM4v7igbMod) {
            gIgtotg = gIstotg + gIdtotg + gIbtotg;
            gIgtotd = gIstotd + gIdtotd + gIbtotd;
            gIgtots = gIstots + gIdtots + gIbtots;
            gIgtotb = gIstotb + gIdtotb + gIbtotb;
        } else {
            gIgtotg = gIgtotd = gIgtots = gIgtotb = 0.0;
        }

        double T0 = 0.0;
        if (inst.BSIM4v7rgateMod == 2)
            T0 = state0_[inst.BSIM4v7vges - inst.BSIM4v7states]
               - state0_[inst.BSIM4v7vgs - inst.BSIM4v7states];
        else if (inst.BSIM4v7rgateMod == 3)
            T0 = state0_[inst.BSIM4v7vgms - inst.BSIM4v7states]
               - state0_[inst.BSIM4v7vgs - inst.BSIM4v7states];

        if (inst.BSIM4v7rgateMod > 1) {
            gcrgd = inst.BSIM4v7gcrgs * T0;  // swapped d<->s in reverse
            gcrgg = inst.BSIM4v7gcrgg * T0;
            gcrgs = inst.BSIM4v7gcrgd * T0;  // swapped d<->s in reverse
            gcrgb = inst.BSIM4v7gcrgb * T0;
            gcrgg -= inst.BSIM4v7gcrg;
            gcrg = inst.BSIM4v7gcrg;
        } else {
            gcrg = gcrgd = gcrgg = gcrgs = gcrgb = 0.0;
        }

        // --- Build C matrix capacitance values (reverse mode) ---
        if (inst.BSIM4v7rgateMod == 3) {
            C_GMgm = cgdo + cgso + cgbo;
            C_GMdp = -cgdo;
            C_GMsp = -cgso;
            C_GMbp = -cgbo;
            C_DPgm = -cgdo;
            C_SPgm = -cgso;
            C_BPgm = -cgbo;

            C_GPgp = Cggr;         // note: Cggr still from QS, same both modes
            C_GPdp = Cgsr;         // swapped: d <-> s
            C_GPsp = Cgdr;
            C_GPbp = -(C_GPgp + C_GPdp + C_GPsp);

            C_DPgp = Csgr;        // swapped
            C_SPgp = Cdgr;
            C_BPgp = inst.BSIM4v7cbgb;
        } else {
            C_GPgp = Cggr + cgdo + cgso + cgbo;
            C_GPdp = Cgsr - cgdo;  // swapped
            C_GPsp = Cgdr - cgso;
            C_GPbp = -(C_GPgp + C_GPdp + C_GPsp);

            C_DPgp = Csgr - cgdo;  // swapped
            C_SPgp = Cdgr - cgso;
            C_BPgp = inst.BSIM4v7cbgb - cgbo;

            C_DPgm = C_SPgm = C_BPgm = 0.0;
        }

        C_DPdp = capbd + cgdo + Cssr;       // swapped: Cssr replaces Cddr
        C_DPsp = Csdr;                       // swapped
        C_SPdp = Cdsr;                       // swapped
        C_SPsp = Cddr + capbs + cgso;        // swapped

        if (!inst.BSIM4v7rbodyMod) {
            C_DPbp = -(C_DPgp + C_DPdp + C_DPsp + C_DPgm);
            C_SPbp = -(C_SPgp + C_SPdp + C_SPsp + C_SPgm);
            C_BPdp = inst.BSIM4v7cbsb - capbd;  // swapped
            C_BPsp = inst.BSIM4v7cbdb - capbs;   // swapped
            C_dbdb = 0.0;
        } else {
            C_DPbp = -(C_DPgp + C_DPdp + C_DPsp + C_DPgm) + capbd;
            C_SPbp = Cdbr;                       // swapped
            C_BPdp = inst.BSIM4v7cbsb;           // swapped
            C_BPsp = inst.BSIM4v7cbdb;           // swapped
            C_dbdb = -capbd;
            C_sbsb = -capbs;
        }
        C_BPbp = -(C_BPgp + C_BPdp + C_BPsp + C_BPgm);
    }

    // --- rdsMod ---
    double gstot, gstotd, gstotg, gstots, gstotb;
    double gdtot, gdtotd, gdtotg, gdtots, gdtotb;
    if (model->BSIM4v7rdsMod == 1) {
        gstot  = inst.BSIM4v7gstot;
        gstotd = inst.BSIM4v7gstotd;
        gstotg = inst.BSIM4v7gstotg;
        gstots = inst.BSIM4v7gstots - gstot;
        gstotb = inst.BSIM4v7gstotb;
        gdtot  = inst.BSIM4v7gdtot;
        gdtotd = inst.BSIM4v7gdtotd - gdtot;
        gdtotg = inst.BSIM4v7gdtotg;
        gdtots = inst.BSIM4v7gdtots;
        gdtotb = inst.BSIM4v7gdtotb;
    } else {
        gstot = gstotd = gstotg = gstots = gstotb = 0.0;
        gdtot = gdtotd = gdtotg = gdtots = gdtotb = 0.0;
    }

    // --- External resistance conductances ---
    double gdpr, gspr;
    if (!model->BSIM4v7rdsMod) {
        gdpr = inst.BSIM4v7drainConductance;
        gspr = inst.BSIM4v7sourceConductance;
    } else {
        gdpr = gspr = 0.0;
    }

    // --- Junction conductances ---
    double gjbd, gjbs;
    if (!inst.BSIM4v7rbodyMod) {
        gjbd = inst.BSIM4v7gbd;
        gjbs = inst.BSIM4v7gbs;
    } else {
        gjbd = gjbs = 0.0;
    }

    const double geltd = inst.BSIM4v7grgeltd;

    // =====================================================================
    // STAMP G and C matrices
    // =====================================================================

    // --- Gate network stamps (rgateMod variants) ---
    if (inst.BSIM4v7rgateMod == 1) {
        // GE-GP resistive path
        G.add(inst.BSIM4v7GEgePtr, m * geltd);
        G.add(inst.BSIM4v7GPgePtr, m * (-geltd));
        G.add(inst.BSIM4v7GEgpPtr, m * (-geltd));

        // GP row
        C.add(inst.BSIM4v7GPgpPtr, m * C_GPgp);
        G.add(inst.BSIM4v7GPgpPtr, m * (geltd + gIgtotg));
        C.add(inst.BSIM4v7GPdpPtr, m * C_GPdp);
        G.add(inst.BSIM4v7GPdpPtr, m * gIgtotd);
        C.add(inst.BSIM4v7GPspPtr, m * C_GPsp);
        G.add(inst.BSIM4v7GPspPtr, m * gIgtots);
        C.add(inst.BSIM4v7GPbpPtr, m * C_GPbp);
        G.add(inst.BSIM4v7GPbpPtr, m * gIgtotb);
    } else if (inst.BSIM4v7rgateMod == 2) {
        // GE row
        G.add(inst.BSIM4v7GEgePtr, m * gcrg);
        G.add(inst.BSIM4v7GEgpPtr, m * gcrgg);
        G.add(inst.BSIM4v7GEdpPtr, m * gcrgd);
        G.add(inst.BSIM4v7GEspPtr, m * gcrgs);
        G.add(inst.BSIM4v7GEbpPtr, m * gcrgb);

        // GP row
        G.add(inst.BSIM4v7GPgePtr, m * (-gcrg));
        C.add(inst.BSIM4v7GPgpPtr, m * C_GPgp);
        G.add(inst.BSIM4v7GPgpPtr, m * (-(gcrgg - gIgtotg)));
        C.add(inst.BSIM4v7GPdpPtr, m * C_GPdp);
        G.add(inst.BSIM4v7GPdpPtr, m * (-(gcrgd - gIgtotd)));
        C.add(inst.BSIM4v7GPspPtr, m * C_GPsp);
        G.add(inst.BSIM4v7GPspPtr, m * (-(gcrgs - gIgtots)));
        C.add(inst.BSIM4v7GPbpPtr, m * C_GPbp);
        G.add(inst.BSIM4v7GPbpPtr, m * (-(gcrgb - gIgtotb)));
    } else if (inst.BSIM4v7rgateMod == 3) {
        // GE-GM resistive path
        G.add(inst.BSIM4v7GEgePtr, m * geltd);
        G.add(inst.BSIM4v7GEgmPtr, m * (-geltd));
        G.add(inst.BSIM4v7GMgePtr, m * (-geltd));

        // GM row
        G.add(inst.BSIM4v7GMgmPtr, m * (geltd + gcrg));
        C.add(inst.BSIM4v7GMgmPtr, m * C_GMgm);
        G.add(inst.BSIM4v7GMdpPtr, m * gcrgd);
        C.add(inst.BSIM4v7GMdpPtr, m * C_GMdp);
        G.add(inst.BSIM4v7GMgpPtr, m * gcrgg);
        G.add(inst.BSIM4v7GMspPtr, m * gcrgs);
        C.add(inst.BSIM4v7GMspPtr, m * C_GMsp);
        G.add(inst.BSIM4v7GMbpPtr, m * gcrgb);
        C.add(inst.BSIM4v7GMbpPtr, m * C_GMbp);

        // Cross-stamps from DP/SP/BP to GM and GP to GM
        C.add(inst.BSIM4v7DPgmPtr, m * C_DPgm);
        G.add(inst.BSIM4v7GPgmPtr, m * (-gcrg));
        C.add(inst.BSIM4v7SPgmPtr, m * C_SPgm);
        C.add(inst.BSIM4v7BPgmPtr, m * C_BPgm);

        // GP row (rgateMod 3 path)
        G.add(inst.BSIM4v7GPgpPtr, m * (-(gcrgg - gIgtotg)));
        C.add(inst.BSIM4v7GPgpPtr, m * C_GPgp);
        G.add(inst.BSIM4v7GPdpPtr, m * (-(gcrgd - gIgtotd)));
        C.add(inst.BSIM4v7GPdpPtr, m * C_GPdp);
        G.add(inst.BSIM4v7GPspPtr, m * (-(gcrgs - gIgtots)));
        C.add(inst.BSIM4v7GPspPtr, m * C_GPsp);
        G.add(inst.BSIM4v7GPbpPtr, m * (-(gcrgb - gIgtotb)));
        C.add(inst.BSIM4v7GPbpPtr, m * C_GPbp);
    } else {
        // rgateMod == 0: no gate resistance
        C.add(inst.BSIM4v7GPgpPtr, m * C_GPgp);
        G.add(inst.BSIM4v7GPgpPtr, m * gIgtotg);
        C.add(inst.BSIM4v7GPdpPtr, m * C_GPdp);
        G.add(inst.BSIM4v7GPdpPtr, m * gIgtotd);
        C.add(inst.BSIM4v7GPspPtr, m * C_GPsp);
        G.add(inst.BSIM4v7GPspPtr, m * gIgtots);
        C.add(inst.BSIM4v7GPbpPtr, m * C_GPbp);
        G.add(inst.BSIM4v7GPbpPtr, m * gIgtotb);
    }

    // --- rdsMod stamps on D/S external nodes ---
    if (model->BSIM4v7rdsMod) {
        G.add(inst.BSIM4v7DgpPtr, m * gdtotg);
        G.add(inst.BSIM4v7DspPtr, m * gdtots);
        G.add(inst.BSIM4v7DbpPtr, m * gdtotb);
        G.add(inst.BSIM4v7SdpPtr, m * gstotd);
        G.add(inst.BSIM4v7SgpPtr, m * gstotg);
        G.add(inst.BSIM4v7SbpPtr, m * gstotb);
    }

    // --- DP (drain prime) row ---
    // In ngspice: *(ptr+1) += m * (xcddbr + gdsi + RevSumi)
    // QS: gdsi = 0, RevSumi = 0 in forward; gdsi = 0, RevSumi = 0 in reverse
    // So C stamp is just C_DPdp, and G stamp includes RevSumr (which is 0 fwd)
    C.add(inst.BSIM4v7DPdpPtr, m * C_DPdp);
    G.add(inst.BSIM4v7DPdpPtr, m * (gdpr + gdsr + inst.BSIM4v7gbd
                - gdtotd + RevSumr + gbdpdp - gIdtotd));
    G.add(inst.BSIM4v7DPdPtr, m * (-(gdpr + gdtot)));
    C.add(inst.BSIM4v7DPgpPtr, m * C_DPgp);
    G.add(inst.BSIM4v7DPgpPtr, m * (Gmr - gdtotg + gbdpg - gIdtotg));
    C.add(inst.BSIM4v7DPspPtr, m * C_DPsp);
    G.add(inst.BSIM4v7DPspPtr, m * (-(gdsr + FwdSumr + gdtots - gbdpsp + gIdtots)));
    C.add(inst.BSIM4v7DPbpPtr, m * C_DPbp);
    G.add(inst.BSIM4v7DPbpPtr, m * (-(gjbd + gdtotb - Gmbsr - gbdpb + gIdtotb)));

    // --- D (drain external) row ---
    G.add(inst.BSIM4v7DdpPtr, m * (-(gdpr - gdtotd)));
    G.add(inst.BSIM4v7DdPtr, m * (gdpr + gdtot));

    // --- SP (source prime) row ---
    C.add(inst.BSIM4v7SPdpPtr, m * C_SPdp);
    G.add(inst.BSIM4v7SPdpPtr, m * (-(gdsr + gstotd + RevSumr - gbspdp + gIstotd)));
    C.add(inst.BSIM4v7SPgpPtr, m * C_SPgp);
    G.add(inst.BSIM4v7SPgpPtr, m * (-(Gmr + gstotg - gbspg + gIstotg)));
    C.add(inst.BSIM4v7SPspPtr, m * C_SPsp);
    G.add(inst.BSIM4v7SPspPtr, m * (gspr + gdsr + inst.BSIM4v7gbs
                - gstots + FwdSumr + gbspsp - gIstots));
    G.add(inst.BSIM4v7SPsPtr, m * (-(gspr + gstot)));
    C.add(inst.BSIM4v7SPbpPtr, m * C_SPbp);
    G.add(inst.BSIM4v7SPbpPtr, m * (-(gjbs + gstotb + Gmbsr - gbspb + gIstotb)));

    // --- S (source external) row ---
    G.add(inst.BSIM4v7SspPtr, m * (-(gspr - gstots)));
    G.add(inst.BSIM4v7SsPtr, m * (gspr + gstot));

    // --- BP (bulk prime) row ---
    C.add(inst.BSIM4v7BPdpPtr, m * C_BPdp);
    G.add(inst.BSIM4v7BPdpPtr, m * (-(gjbd - gbbdp + gIbtotd)));
    C.add(inst.BSIM4v7BPgpPtr, m * C_BPgp);
    G.add(inst.BSIM4v7BPgpPtr, m * (-(inst.BSIM4v7gbgs + gIbtotg)));
    C.add(inst.BSIM4v7BPspPtr, m * C_BPsp);
    G.add(inst.BSIM4v7BPspPtr, m * (-(gjbs - gbbsp + gIbtots)));
    C.add(inst.BSIM4v7BPbpPtr, m * C_BPbp);
    G.add(inst.BSIM4v7BPbpPtr, m * (gjbd + gjbs - inst.BSIM4v7gbbs - gIbtotb));

    // --- GIDL stamps (conductance only, into G matrix) ---
    const double ggidld = inst.BSIM4v7ggidld;
    const double ggidlg = inst.BSIM4v7ggidlg;
    const double ggidlb = inst.BSIM4v7ggidlb;
    const double ggislg = inst.BSIM4v7ggislg;
    const double ggisls = inst.BSIM4v7ggisls;
    const double ggislb = inst.BSIM4v7ggislb;

    // stamp gidl
    G.add(inst.BSIM4v7DPdpPtr, m * ggidld);
    G.add(inst.BSIM4v7DPgpPtr, m * ggidlg);
    G.add(inst.BSIM4v7DPspPtr, m * (-((ggidlg + ggidld) + ggidlb)));
    G.add(inst.BSIM4v7DPbpPtr, m * ggidlb);
    G.add(inst.BSIM4v7BPdpPtr, m * (-ggidld));
    G.add(inst.BSIM4v7BPgpPtr, m * (-ggidlg));
    G.add(inst.BSIM4v7BPspPtr, m * ((ggidlg + ggidld) + ggidlb));
    G.add(inst.BSIM4v7BPbpPtr, m * (-ggidlb));

    // stamp gisl
    G.add(inst.BSIM4v7SPdpPtr, m * (-((ggisls + ggislg) + ggislb)));
    G.add(inst.BSIM4v7SPgpPtr, m * ggislg);
    G.add(inst.BSIM4v7SPspPtr, m * ggisls);
    G.add(inst.BSIM4v7SPbpPtr, m * ggislb);
    G.add(inst.BSIM4v7BPdpPtr, m * ((ggislg + ggisls) + ggislb));
    G.add(inst.BSIM4v7BPgpPtr, m * (-ggislg));
    G.add(inst.BSIM4v7BPspPtr, m * (-ggisls));
    G.add(inst.BSIM4v7BPbpPtr, m * (-ggislb));

    // --- rbodyMod stamps ---
    if (inst.BSIM4v7rbodyMod) {
        C.add(inst.BSIM4v7DPdbPtr, m * C_dbdb);
        G.add(inst.BSIM4v7DPdbPtr, m * (-inst.BSIM4v7gbd));
        C.add(inst.BSIM4v7SPsbPtr, m * C_sbsb);
        G.add(inst.BSIM4v7SPsbPtr, m * (-inst.BSIM4v7gbs));

        C.add(inst.BSIM4v7DBdpPtr, m * C_dbdb);
        G.add(inst.BSIM4v7DBdpPtr, m * (-inst.BSIM4v7gbd));
        C.add(inst.BSIM4v7DBdbPtr, m * (-C_dbdb));
        G.add(inst.BSIM4v7DBdbPtr, m * (inst.BSIM4v7gbd + inst.BSIM4v7grbpd
                                         + inst.BSIM4v7grbdb));
        G.add(inst.BSIM4v7DBbpPtr, m * (-inst.BSIM4v7grbpd));
        G.add(inst.BSIM4v7DBbPtr,  m * (-inst.BSIM4v7grbdb));

        G.add(inst.BSIM4v7BPdbPtr, m * (-inst.BSIM4v7grbpd));
        G.add(inst.BSIM4v7BPbPtr,  m * (-inst.BSIM4v7grbpb));
        G.add(inst.BSIM4v7BPsbPtr, m * (-inst.BSIM4v7grbps));
        G.add(inst.BSIM4v7BPbpPtr, m * (inst.BSIM4v7grbpd + inst.BSIM4v7grbps
                                         + inst.BSIM4v7grbpb));

        C.add(inst.BSIM4v7SBspPtr, m * C_sbsb);
        G.add(inst.BSIM4v7SBspPtr, m * (-inst.BSIM4v7gbs));
        G.add(inst.BSIM4v7SBbpPtr, m * (-inst.BSIM4v7grbps));
        G.add(inst.BSIM4v7SBbPtr,  m * (-inst.BSIM4v7grbsb));
        C.add(inst.BSIM4v7SBsbPtr, m * (-C_sbsb));
        G.add(inst.BSIM4v7SBsbPtr, m * (inst.BSIM4v7gbs
                                         + inst.BSIM4v7grbps + inst.BSIM4v7grbsb));

        G.add(inst.BSIM4v7BdbPtr, m * (-inst.BSIM4v7grbdb));
        G.add(inst.BSIM4v7BbpPtr, m * (-inst.BSIM4v7grbpb));
        G.add(inst.BSIM4v7BsbPtr, m * (-inst.BSIM4v7grbsb));
        G.add(inst.BSIM4v7BbPtr,  m * (inst.BSIM4v7grbsb + inst.BSIM4v7grbdb
                                        + inst.BSIM4v7grbpb));
    }

    // --- trnqsMod: keep Q node non-singular ---
    if (inst.BSIM4v7trnqsMod) {
        G.add(inst.BSIM4v7QqPtr, m * 1.0);
    }
}

// ---------------------------------------------------------------------------
// compute_trunc — device-specific local truncation error for time stepping
//
// Mirrors ngspice's CKTterr() called from BSIM4v7trunc().  For each charge
// state variable we:
//   1. Compute a tolerance from the derivative (ccap = qcap+1) and the charge.
//   2. Build divided differences from the state ring buffer.
//   3. Estimate a suggested timestep using the LTE bound.
//   4. Return the minimum across all charge variables.
// ---------------------------------------------------------------------------
double BSIM4v7Device::compute_trunc(const IntegratorCtx& ctx,
                                     const SimOptions& opts) const {
    // Only meaningful for order >= 2 with valid state history.
    if (ctx.order < 2 || ctx.delta <= 0.0)
        return 1e30;

    if (!state0_ || !state1_ || !state2_)
        return 1e30;

    // Gear-2 LTE coefficient:  for BDF2, the leading error term is
    // (2/9) * h^3 * y'''  ≈  factor * dd[0]  where dd[0] is the 3rd
    // divided difference scaled by step sizes.  ngspice uses:
    //   gearCoeff = {0.5, 2.0/9.0}   (indices 0,1 for order 1,2)
    //   trapCoeff = {0.5, 1.0/12.0}
    // We use Gear-2 (the simulator's method after 2 steps).
    const double lte_coeff = 2.0 / 9.0;

    const double h  = ctx.delta;
    const double h1 = ctx.delta_old[1];
    if (h1 <= 0.0)
        return 1e30;

    // Collect charge offsets to check.
    // Each charge q has its derivative (current) at q+1 in the state vector.
    // Offsets are relative to instance base (BSIM4v7states).
    int charge_offsets[7];
    int ncharges = 0;

    // Always check: qb(11), qg(13), qd(15)
    charge_offsets[ncharges++] = 11;  // qb
    charge_offsets[ncharges++] = 13;  // qg
    charge_offsets[ncharges++] = 15;  // qd

    // Conditional charges
    if (inst_.BSIM4v7rgateMod == 3)
        charge_offsets[ncharges++] = 17;  // qgmid
    if (inst_.BSIM4v7rbodyMod) {
        charge_offsets[ncharges++] = 19;  // qbs
        charge_offsets[ncharges++] = 21;  // qbd
    }
    if (inst_.BSIM4v7trnqsMod)
        charge_offsets[ncharges++] = 25;  // qcdump

    double dt_min = 1e30;

    for (int i = 0; i < ncharges; ++i) {
        const int qcap = state_base_ + charge_offsets[i];
        const int ccap = qcap + 1;  // derivative index

        // ---- 1. Tolerance (mirrors CKTterr) ----
        const double ccap0 = state0_[ccap];
        const double ccap1 = state1_[ccap];
        double diff_tol = opts.abstol + opts.reltol * std::max(std::abs(ccap0),
                                                                std::abs(ccap1));

        const double qcap0 = state0_[qcap];
        const double qcap1 = state1_[qcap];
        double chargetol = opts.reltol * std::max({std::abs(qcap0),
                                                    std::abs(qcap1),
                                                    opts.chgtol}) / h;
        double tol = std::max(diff_tol, chargetol);
        if (tol <= 0.0)
            continue;

        // ---- 2. Divided differences for order 2 ----
        // 2nd divided difference from 3 state snapshots (state0/state1/state2).
        // 1st DD: (q0-q1)/h,  (q1-q2)/h1
        // 2nd DD: (dd1_0 - dd1_1) / (h + h1)
        const double qcap2 = state2_[qcap];
        double dd1_0 = (qcap0 - qcap1) / h;
        double dd1_1 = (qcap1 - qcap2) / h1;
        double dd2 = (dd1_0 - dd1_1) / (h + h1);

        // ---- 3. Compute suggested timestep ----
        double lte_est = lte_coeff * std::abs(dd2);
        if (lte_est <= opts.abstol)
            continue;  // negligible error — no constraint from this charge

        // del = trtol * tol / lte_est;  for order 2: del = sqrt(del)
        double del = opts.trtol * tol / lte_est;
        del = std::sqrt(del);

        if (del < dt_min)
            dt_min = del;
    }

    return dt_min;
}

// ---------------------------------------------------------------------------
// query_param — post-simulation parameter query (ask/mask)
// ---------------------------------------------------------------------------
static std::string str_tolower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

std::optional<double>
BSIM4v7Device::query_param(const std::string& name) const {
    const std::string key = str_tolower(name);
    const double m = inst_.BSIM4v7m;

    // --- Operating-point parameters (scaled by multiplier m) ---
    if (key == "gm")                        return inst_.BSIM4v7gm * m;
    if (key == "gds")                       return inst_.BSIM4v7gds * m;
    if (key == "gmbs")                      return inst_.BSIM4v7gmbs * m;
    if (key == "vth" || key == "von")       return inst_.BSIM4v7von;
    if (key == "vdsat")                     return inst_.BSIM4v7vdsat;
    if (key == "id" || key == "cd")         return inst_.BSIM4v7cd * m;
    if (key == "ibs" || key == "cbs")       return inst_.BSIM4v7cbs * m;
    if (key == "ibd" || key == "cbd")       return inst_.BSIM4v7cbd * m;
    if (key == "gbd")                       return inst_.BSIM4v7gbd * m;
    if (key == "gbs")                       return inst_.BSIM4v7gbs * m;

    // --- Capacitances ---
    if (key == "cgg")                       return inst_.BSIM4v7cggb * m;
    if (key == "cgd")                       return inst_.BSIM4v7cgdb * m;
    if (key == "cgs")                       return inst_.BSIM4v7cgsb * m;
    if (key == "cdg")                       return inst_.BSIM4v7cdgb * m;
    if (key == "cdd")                       return inst_.BSIM4v7cddb * m;
    if (key == "cds")                       return inst_.BSIM4v7cdsb * m;

    // --- Charges ---
    if (key == "qg")                        return inst_.BSIM4v7qgate * m;
    if (key == "qd")                        return inst_.BSIM4v7qdrn * m;
    if (key == "qb")                        return inst_.BSIM4v7qbulk * m;
    if (key == "qs")                        return inst_.BSIM4v7qsrc * m;

    // --- Junction capacitances ---
    if (key == "capbd")                     return inst_.BSIM4v7capbd * m;
    if (key == "capbs")                     return inst_.BSIM4v7capbs * m;

    // --- Terminal voltages from state vector (offsets 0-3) ---
    if (state0_ && state_base_ >= 0) {
        if (key == "vbd")                   return state0_[state_base_ + 0];
        if (key == "vbs")                   return state0_[state_base_ + 1];
        if (key == "vgs")                   return state0_[state_base_ + 2];
        if (key == "vds")                   return state0_[state_base_ + 3];
    }

    // --- Geometry (no multiplier) ---
    if (key == "w")                         return inst_.BSIM4v7w;
    if (key == "l")                         return inst_.BSIM4v7l;
    if (key == "nf")                        return inst_.BSIM4v7nf;
    if (key == "m")                         return inst_.BSIM4v7m;

    return std::nullopt;  // unrecognized parameter
}

} // namespace neospice
