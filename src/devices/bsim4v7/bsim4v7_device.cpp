#include "devices/bsim4v7/bsim4v7_device.hpp"

#include "core/circuit.hpp"        // Circuit::node, tls_integrator_ctx
#include "core/types.hpp"          // SimOptions defaults

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

    // Fold ghost rhs contributions back into the real rhs.
    for (int32_t k = 1; k <= max_neo_node_ + 1 && k < n_ghost; ++k) {
        if (k - 1 < n_real) rhs[k - 1] += ghost_rhs_[k];
    }
}

} // namespace neospice
