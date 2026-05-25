#include "devices/hisimhv/hisimhv_device.hpp"

#include "core/circuit.hpp"        // Circuit::node, tls_integrator_ctx
#include "core/types.hpp"          // SimOptions defaults
#include "devices/ucb_device_init.hpp"
#include "devices/ucb_utils.hpp"

#include <cmath>
#include <optional>
#include <stdexcept>
#include <string>

// Forward declarations for translated UCB functions.
namespace neospice::hisimhv {
    int HSMHVsetup(Shim::Matrix*, HSMHVModel*, Shim::Ckt*, int*);
    int HSMHVtemp(HSMHVModel*, Shim::Ckt*);
    int HSMHVload(HSMHVModel*, Shim::Ckt*);
}

namespace neospice {

using namespace neospice::hisimhv;

// ---------------------------------------------------------------------------
// HSMHVModelCard destructor
// ---------------------------------------------------------------------------
HSMHVModelCard::~HSMHVModelCard() = default;

// ---------------------------------------------------------------------------
// make
// ---------------------------------------------------------------------------
std::unique_ptr<HSMHVDevice>
HSMHVDevice::make(std::string name,
        int32_t n_d, int32_t n_g, int32_t n_s, int32_t n_b, int32_t n_sub,
        const Geom& geom, HSMHVModelCard& shared_card) {
    std::unique_ptr<HSMHVDevice> dev(new HSMHVDevice(std::move(name)));
    dev->model_ = &shared_card.ucb;
    dev->ext_nodes_ = {n_d, n_g, n_s, n_b, n_sub};

    auto& inst = dev->inst_;
    inst.HSMHVname = dev->name().c_str();
    inst.HSMHVmodPtr = dev->model_;

    // Node wiring (UCB convention).
    inst.HSMHVdNode = neo_to_ucb(n_d);
    inst.HSMHVgNode = neo_to_ucb(n_g);
    inst.HSMHVsNode = neo_to_ucb(n_s);
    inst.HSMHVbNode = neo_to_ucb(n_b);
    // Substrate node: GROUND_INTERNAL-1 sentinel means "no substrate node"
    // (UCB -1 = unconnected).  Any real node uses the normal conversion.
    if (n_sub < GROUND_INTERNAL) {
        inst.HSMHVsubNode    = -1;  // UCB: unconnected
        inst.HSMHVsubNodeExt = -1;
    } else {
        inst.HSMHVsubNode    = neo_to_ucb(n_sub);
        inst.HSMHVsubNodeExt = neo_to_ucb(n_sub);
    }
    inst.HSMHVtempNodeExt = -1;  // no external temp node

    // Geometry.
    inst.HSMHV_w = geom.W;
    inst.HSMHV_w_Given = 1;
    inst.HSMHV_l = geom.L;
    inst.HSMHV_l_Given = 1;
    inst.HSMHV_m = geom.M;
    inst.HSMHV_m_Given = 1;
    inst.HSMHV_ad = geom.AD;
    inst.HSMHV_ad_Given = (geom.AD != 0.0) ? 1 : 0;
    inst.HSMHV_as = geom.AS;
    inst.HSMHV_as_Given = (geom.AS != 0.0) ? 1 : 0;
    inst.HSMHV_pd = geom.PD;
    inst.HSMHV_pd_Given = (geom.PD != 0.0) ? 1 : 0;
    inst.HSMHV_ps = geom.PS;
    inst.HSMHV_ps_Given = (geom.PS != 0.0) ? 1 : 0;
    inst.HSMHV_nrd = geom.NRD;
    inst.HSMHV_nrd_Given = (geom.NRD != 0.0) ? 1 : 0;
    inst.HSMHV_nrs = geom.NRS;
    inst.HSMHV_nrs_Given = (geom.NRS != 0.0) ? 1 : 0;
    inst.HSMHV_nf = geom.NF;
    inst.HSMHV_nf_Given = 1;

    // Thread onto the shared model's instance list.
    inst.HSMHVnextInstance = shared_card.ucb.HSMHVinstances;
    shared_card.ucb.HSMHVinstances = &inst;

    // Remember the widest real node index for ghost array sizing.
    int32_t widest = -1;
    for (int32_t n : {n_d, n_g, n_s, n_b}) if (n > widest) widest = n;
    if (n_sub >= GROUND_INTERNAL && n_sub > widest) widest = n_sub;
    dev->max_neo_node_ = widest;

    return dev;
}

// ---------------------------------------------------------------------------
// set_ic
// ---------------------------------------------------------------------------
void HSMHVDevice::set_ic(double vds, bool vds_given,
                          double vgs, bool vgs_given,
                          double vbs, bool vbs_given) {
    if (vds_given) { inst_.HSMHV_icVDS = vds; inst_.HSMHV_icVDS_Given = 1; }
    if (vgs_given) { inst_.HSMHV_icVGS = vgs; inst_.HSMHV_icVGS_Given = 1; }
    if (vbs_given) { inst_.HSMHV_icVBS = vbs; inst_.HSMHV_icVBS_Given = 1; }
}

// ---------------------------------------------------------------------------
// declare_internal_nodes
// ---------------------------------------------------------------------------
void HSMHVDevice::declare_internal_nodes(Circuit& ckt) {
    ucb_declare_internal_nodes<Shim::Matrix, Shim::Ckt>(
        ckt, name_,
        [this](Shim::Matrix& m, Shim::Ckt& c) {
            UCB_SPLICE_INSTANCE(HSMHV);
            int states = 0;
            int rc = HSMHVsetup(&m, model_, &c, &states);
            UCB_UNSPLICE_INSTANCE(HSMHV);
            return rc;
        },
        "HSMHVsetup", journal_, max_neo_node_);
}

// ---------------------------------------------------------------------------
// stamp_pattern
// ---------------------------------------------------------------------------
void HSMHVDevice::stamp_pattern(SparsityBuilder& builder) const {
    ucb_stamp_pattern(journal_, builder);
}

// ---------------------------------------------------------------------------
// assign_offsets
// ---------------------------------------------------------------------------
void HSMHVDevice::assign_offsets(const SparsityPattern& pattern) {
    const auto offsets = ucb_compute_offsets(journal_, pattern);

#define RESOLVE(f)                                                       \
    do {                                                                 \
        if (inst_.f >= 0)                                                \
            inst_.f = offsets[inst_.f];                                  \
    } while (0)

    RESOLVE(HSMHVGgPtr);
    RESOLVE(HSMHVGgpPtr);
    RESOLVE(HSMHVGPgPtr);
    RESOLVE(HSMHVGPgpPtr);
    RESOLVE(HSMHVGPdpPtr);
    RESOLVE(HSMHVGPspPtr);
    RESOLVE(HSMHVGPbpPtr);
    RESOLVE(HSMHVDPdPtr);
    RESOLVE(HSMHVDPdpPtr);
    RESOLVE(HSMHVDPgpPtr);
    RESOLVE(HSMHVDPspPtr);
    RESOLVE(HSMHVDPbpPtr);
    RESOLVE(HSMHVDdPtr);
    RESOLVE(HSMHVDdpPtr);
    RESOLVE(HSMHVDspPtr);
    RESOLVE(HSMHVDdbPtr);
    RESOLVE(HSMHVSPsPtr);
    RESOLVE(HSMHVSPspPtr);
    RESOLVE(HSMHVSPgpPtr);
    RESOLVE(HSMHVSPdpPtr);
    RESOLVE(HSMHVSPbpPtr);
    RESOLVE(HSMHVSsPtr);
    RESOLVE(HSMHVSspPtr);
    RESOLVE(HSMHVSdpPtr);
    RESOLVE(HSMHVSsbPtr);
    RESOLVE(HSMHVBPgpPtr);
    RESOLVE(HSMHVBPbpPtr);
    RESOLVE(HSMHVBPdPtr);
    RESOLVE(HSMHVBPdpPtr);
    RESOLVE(HSMHVBPspPtr);
    RESOLVE(HSMHVBPsPtr);
    RESOLVE(HSMHVBPbPtr);
    RESOLVE(HSMHVBPdbPtr);
    RESOLVE(HSMHVBPsbPtr);
    RESOLVE(HSMHVDBdPtr);
    RESOLVE(HSMHVDBdbPtr);
    RESOLVE(HSMHVDBbpPtr);
    RESOLVE(HSMHVSBsPtr);
    RESOLVE(HSMHVSBbpPtr);
    RESOLVE(HSMHVSBsbPtr);
    RESOLVE(HSMHVBbpPtr);
    RESOLVE(HSMHVBbPtr);
    RESOLVE(HSMHVTemptempPtr);
    RESOLVE(HSMHVTempdPtr);
    RESOLVE(HSMHVTempdpPtr);
    RESOLVE(HSMHVTempsPtr);
    RESOLVE(HSMHVTempspPtr);
    RESOLVE(HSMHVTempgpPtr);
    RESOLVE(HSMHVTempbpPtr);
    RESOLVE(HSMHVGPtempPtr);
    RESOLVE(HSMHVDPtempPtr);
    RESOLVE(HSMHVSPtempPtr);
    RESOLVE(HSMHVBPtempPtr);
    RESOLVE(HSMHVDBtempPtr);
    RESOLVE(HSMHVSBtempPtr);
    RESOLVE(HSMHVDgpPtr);
    RESOLVE(HSMHVDsPtr);
    RESOLVE(HSMHVDbpPtr);
    RESOLVE(HSMHVDtempPtr);
    RESOLVE(HSMHVDPsPtr);
    RESOLVE(HSMHVGPdPtr);
    RESOLVE(HSMHVGPsPtr);
    RESOLVE(HSMHVSdPtr);
    RESOLVE(HSMHVSgpPtr);
    RESOLVE(HSMHVSbpPtr);
    RESOLVE(HSMHVStempPtr);
    RESOLVE(HSMHVSPdPtr);
    RESOLVE(HSMHVDPqiPtr);
    RESOLVE(HSMHVGPqiPtr);
    RESOLVE(HSMHVGPqbPtr);
    RESOLVE(HSMHVSPqiPtr);
    RESOLVE(HSMHVBPqbPtr);
    RESOLVE(HSMHVQIdpPtr);
    RESOLVE(HSMHVQIgpPtr);
    RESOLVE(HSMHVQIspPtr);
    RESOLVE(HSMHVQIbpPtr);
    RESOLVE(HSMHVQIqiPtr);
    RESOLVE(HSMHVQBdpPtr);
    RESOLVE(HSMHVQBgpPtr);
    RESOLVE(HSMHVQBspPtr);
    RESOLVE(HSMHVQBbpPtr);
    RESOLVE(HSMHVQBqbPtr);
    RESOLVE(HSMHVQItempPtr);
    RESOLVE(HSMHVQBtempPtr);
    RESOLVE(HSMHVDsubPtr);
    RESOLVE(HSMHVDPsubPtr);
    RESOLVE(HSMHVSsubPtr);
    RESOLVE(HSMHVSPsubPtr);

#undef RESOLVE
}

// ---------------------------------------------------------------------------
// set_state_ptrs
// ---------------------------------------------------------------------------
void HSMHVDevice::set_state_ptrs(double* s0, double* s1, double* s2, int32_t base) {
    state0_ = s0;
    state1_ = s1;
    state2_ = s2;
    state_base_ = base;
    inst_.HSMHVstates = base;
}

// ---------------------------------------------------------------------------
// evaluate
// ---------------------------------------------------------------------------
void HSMHVDevice::evaluate(const std::vector<double>& voltages,
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
    ckt.CKTnomTemp = sim_opts->tnom;
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

    // First-call HSMHVtemp.
    if (!temp_done_) {
        int rc = HSMHVtemp(model_, &ckt);
        if (rc != Shim::OK) {
            throw std::runtime_error("HSMHVtemp failed with rc=" + std::to_string(rc));
        }
        temp_done_ = true;
    }

    // Splice this instance as sole member, call load, then restore.
    HSMHVInstance* saved_head      = model_->HSMHVinstances;
    HSMHVInstance* saved_next_inst = inst_.HSMHVnextInstance;
    HSMHVModel*    saved_next_mod  = model_->HSMHVnextModel;
    model_->HSMHVinstances  = &inst_;
    inst_.HSMHVnextInstance = nullptr;
    model_->HSMHVnextModel  = nullptr;
    int rc = HSMHVload(model_, &ckt);
    model_->HSMHVinstances  = saved_head;
    inst_.HSMHVnextInstance = saved_next_inst;
    model_->HSMHVnextModel  = saved_next_mod;
    if (rc != Shim::OK) {
        throw std::runtime_error("HSMHVload failed with rc=" + std::to_string(rc));
    }

    last_noncon_ = ckt.CKTnoncon;

    // Fold ghost rhs contributions back into the real rhs.
    for (int32_t k = 1; k <= max_neo_node_ + 1 && k < n_ghost; ++k) {
        if (k - 1 < n_real) rhs[k - 1] += ghost_rhs_[k];
    }
}

// ---------------------------------------------------------------------------
// ac_stamp — stamp G/C matrices using pre-computed ydc/ydyn arrays from DC load
// ---------------------------------------------------------------------------
void HSMHVDevice::ac_stamp(const std::vector<double>& /*voltages*/,
                             NumericMatrix& G,
                             NumericMatrix& C) {
    auto& here = inst_;
    auto* model = model_;
    int flg_subNode = here.HSMHVsubNode;
    int flg_nqs = model->HSMHV_conqs;

    // Local node index defines matching the load function.
#define dNode        0
#define dNodePrime   1
#define gNode        2
#define gNodePrime   3
#define sNode        4
#define sNodePrime   5
#define bNodePrime   6
#define bNode        7
#define dbNode       8
#define sbNode       9
#define subNode     10
#define tempNode    11
#define qiNode      12
#define qbNode      13

    // Drain
    G.add(here.HSMHVDdPtr, here.HSMHV_ydc_d[dNode]);
    C.add(here.HSMHVDdPtr, here.HSMHV_ydyn_d[dNode]);
    G.add(here.HSMHVDdpPtr, here.HSMHV_ydc_d[dNodePrime]);
    C.add(here.HSMHVDdpPtr, here.HSMHV_ydyn_d[dNodePrime]);
    G.add(here.HSMHVDgpPtr, here.HSMHV_ydc_d[gNodePrime]);
    C.add(here.HSMHVDgpPtr, here.HSMHV_ydyn_d[gNodePrime]);
    G.add(here.HSMHVDsPtr, here.HSMHV_ydc_d[sNode]);
    C.add(here.HSMHVDsPtr, here.HSMHV_ydyn_d[sNode]);
    G.add(here.HSMHVDbpPtr, here.HSMHV_ydc_d[bNodePrime]);
    C.add(here.HSMHVDbpPtr, here.HSMHV_ydyn_d[bNodePrime]);
    G.add(here.HSMHVDdbPtr, here.HSMHV_ydc_d[dbNode]);
    C.add(here.HSMHVDdbPtr, here.HSMHV_ydyn_d[dbNode]);
    if (flg_subNode > 0) G.add(here.HSMHVDsubPtr, here.HSMHV_ydc_d[subNode]);
    if (here.HSMHVtempNode > 0) {
        G.add(here.HSMHVDtempPtr, model->HSMHV_type * here.HSMHV_ydc_d[tempNode]);
        C.add(here.HSMHVDtempPtr, model->HSMHV_type * here.HSMHV_ydyn_d[tempNode]);
    }

    // Drain prime
    G.add(here.HSMHVDPdPtr, here.HSMHV_ydc_dP[dNode]);
    C.add(here.HSMHVDPdPtr, here.HSMHV_ydyn_dP[dNode]);
    G.add(here.HSMHVDPdpPtr, here.HSMHV_ydc_dP[dNodePrime]);
    C.add(here.HSMHVDPdpPtr, here.HSMHV_ydyn_dP[dNodePrime]);
    G.add(here.HSMHVDPgpPtr, here.HSMHV_ydc_dP[gNodePrime]);
    C.add(here.HSMHVDPgpPtr, here.HSMHV_ydyn_dP[gNodePrime]);
    G.add(here.HSMHVDPsPtr, here.HSMHV_ydc_dP[sNode]);
    C.add(here.HSMHVDPsPtr, here.HSMHV_ydyn_dP[sNode]);
    G.add(here.HSMHVDPspPtr, here.HSMHV_ydc_dP[sNodePrime]);
    C.add(here.HSMHVDPspPtr, here.HSMHV_ydyn_dP[sNodePrime]);
    G.add(here.HSMHVDPbpPtr, here.HSMHV_ydc_dP[bNodePrime]);
    C.add(here.HSMHVDPbpPtr, here.HSMHV_ydyn_dP[bNodePrime]);
    if (flg_subNode > 0) G.add(here.HSMHVDPsubPtr, here.HSMHV_ydc_dP[subNode]);
    if (here.HSMHVtempNode > 0) {
        G.add(here.HSMHVDPtempPtr, model->HSMHV_type * here.HSMHV_ydc_dP[tempNode]);
        C.add(here.HSMHVDPtempPtr, model->HSMHV_type * here.HSMHV_ydyn_dP[tempNode]);
    }
    if (flg_nqs) {
        G.add(here.HSMHVDPqiPtr, model->HSMHV_type * here.HSMHV_ydc_dP[qiNode]);
        C.add(here.HSMHVDPqiPtr, model->HSMHV_type * here.HSMHV_ydyn_dP[qiNode]);
    }

    // Gate
    G.add(here.HSMHVGgPtr, here.HSMHV_ydc_g[gNode]);
    C.add(here.HSMHVGgPtr, here.HSMHV_ydyn_g[gNode]);
    G.add(here.HSMHVGgpPtr, here.HSMHV_ydc_g[gNodePrime]);
    C.add(here.HSMHVGgpPtr, here.HSMHV_ydyn_g[gNodePrime]);

    // Gate prime
    G.add(here.HSMHVGPdPtr, here.HSMHV_ydc_gP[dNode]);
    C.add(here.HSMHVGPdPtr, here.HSMHV_ydyn_gP[dNode]);
    G.add(here.HSMHVGPdpPtr, here.HSMHV_ydc_gP[dNodePrime]);
    C.add(here.HSMHVGPdpPtr, here.HSMHV_ydyn_gP[dNodePrime]);
    G.add(here.HSMHVGPgPtr, here.HSMHV_ydc_gP[gNode]);
    C.add(here.HSMHVGPgPtr, here.HSMHV_ydyn_gP[gNode]);
    G.add(here.HSMHVGPgpPtr, here.HSMHV_ydc_gP[gNodePrime]);
    C.add(here.HSMHVGPgpPtr, here.HSMHV_ydyn_gP[gNodePrime]);
    G.add(here.HSMHVGPsPtr, here.HSMHV_ydc_gP[sNode]);
    C.add(here.HSMHVGPsPtr, here.HSMHV_ydyn_gP[sNode]);
    G.add(here.HSMHVGPspPtr, here.HSMHV_ydc_gP[sNodePrime]);
    C.add(here.HSMHVGPspPtr, here.HSMHV_ydyn_gP[sNodePrime]);
    G.add(here.HSMHVGPbpPtr, here.HSMHV_ydc_gP[bNodePrime]);
    C.add(here.HSMHVGPbpPtr, here.HSMHV_ydyn_gP[bNodePrime]);
    if (here.HSMHVtempNode > 0) {
        G.add(here.HSMHVGPtempPtr, model->HSMHV_type * here.HSMHV_ydc_gP[tempNode]);
        C.add(here.HSMHVGPtempPtr, model->HSMHV_type * here.HSMHV_ydyn_gP[tempNode]);
    }
    if (flg_nqs) {
        G.add(here.HSMHVGPqiPtr, model->HSMHV_type * here.HSMHV_ydc_gP[qiNode]);
        C.add(here.HSMHVGPqiPtr, model->HSMHV_type * here.HSMHV_ydyn_gP[qiNode]);
        G.add(here.HSMHVGPqbPtr, model->HSMHV_type * here.HSMHV_ydc_gP[qbNode]);
        C.add(here.HSMHVGPqbPtr, model->HSMHV_type * here.HSMHV_ydyn_gP[qbNode]);
    }

    // Source
    G.add(here.HSMHVSdPtr, here.HSMHV_ydc_s[dNode]);
    C.add(here.HSMHVSdPtr, here.HSMHV_ydyn_s[dNode]);
    G.add(here.HSMHVSgpPtr, here.HSMHV_ydc_s[gNodePrime]);
    C.add(here.HSMHVSgpPtr, here.HSMHV_ydyn_s[gNodePrime]);
    G.add(here.HSMHVSsPtr, here.HSMHV_ydc_s[sNode]);
    C.add(here.HSMHVSsPtr, here.HSMHV_ydyn_s[sNode]);
    G.add(here.HSMHVSspPtr, here.HSMHV_ydc_s[sNodePrime]);
    C.add(here.HSMHVSspPtr, here.HSMHV_ydyn_s[sNodePrime]);
    G.add(here.HSMHVSbpPtr, here.HSMHV_ydc_s[bNodePrime]);
    C.add(here.HSMHVSbpPtr, here.HSMHV_ydyn_s[bNodePrime]);
    G.add(here.HSMHVSsbPtr, here.HSMHV_ydc_s[sbNode]);
    C.add(here.HSMHVSsbPtr, here.HSMHV_ydyn_s[sbNode]);
    if (flg_subNode > 0) G.add(here.HSMHVSsubPtr, here.HSMHV_ydc_s[subNode]);
    if (here.HSMHVtempNode > 0) {
        G.add(here.HSMHVStempPtr, model->HSMHV_type * here.HSMHV_ydc_s[tempNode]);
        C.add(here.HSMHVStempPtr, model->HSMHV_type * here.HSMHV_ydyn_s[tempNode]);
    }

    // Source prime
    G.add(here.HSMHVSPdPtr, here.HSMHV_ydc_sP[dNode]);
    C.add(here.HSMHVSPdPtr, here.HSMHV_ydyn_sP[dNode]);
    G.add(here.HSMHVSPdpPtr, here.HSMHV_ydc_sP[dNodePrime]);
    C.add(here.HSMHVSPdpPtr, here.HSMHV_ydyn_sP[dNodePrime]);
    G.add(here.HSMHVSPgpPtr, here.HSMHV_ydc_sP[gNodePrime]);
    C.add(here.HSMHVSPgpPtr, here.HSMHV_ydyn_sP[gNodePrime]);
    G.add(here.HSMHVSPsPtr, here.HSMHV_ydc_sP[sNode]);
    C.add(here.HSMHVSPsPtr, here.HSMHV_ydyn_sP[sNode]);
    G.add(here.HSMHVSPspPtr, here.HSMHV_ydc_sP[sNodePrime]);
    C.add(here.HSMHVSPspPtr, here.HSMHV_ydyn_sP[sNodePrime]);
    G.add(here.HSMHVSPbpPtr, here.HSMHV_ydc_sP[bNodePrime]);
    C.add(here.HSMHVSPbpPtr, here.HSMHV_ydyn_sP[bNodePrime]);
    if (flg_subNode > 0) G.add(here.HSMHVSPsubPtr, here.HSMHV_ydc_sP[subNode]);
    if (here.HSMHVtempNode > 0) {
        G.add(here.HSMHVSPtempPtr, model->HSMHV_type * here.HSMHV_ydc_sP[tempNode]);
        C.add(here.HSMHVSPtempPtr, model->HSMHV_type * here.HSMHV_ydyn_sP[tempNode]);
    }
    if (flg_nqs) {
        G.add(here.HSMHVSPqiPtr, model->HSMHV_type * here.HSMHV_ydc_sP[qiNode]);
        C.add(here.HSMHVSPqiPtr, model->HSMHV_type * here.HSMHV_ydyn_sP[qiNode]);
    }

    // Bulk prime
    G.add(here.HSMHVBPdPtr, here.HSMHV_ydc_bP[dNode]);
    C.add(here.HSMHVBPdPtr, here.HSMHV_ydyn_bP[dNode]);
    G.add(here.HSMHVBPsPtr, here.HSMHV_ydc_bP[sNode]);
    C.add(here.HSMHVBPsPtr, here.HSMHV_ydyn_bP[sNode]);
    G.add(here.HSMHVBPdpPtr, here.HSMHV_ydc_bP[dNodePrime]);
    C.add(here.HSMHVBPdpPtr, here.HSMHV_ydyn_bP[dNodePrime]);
    G.add(here.HSMHVBPgpPtr, here.HSMHV_ydc_bP[gNodePrime]);
    C.add(here.HSMHVBPgpPtr, here.HSMHV_ydyn_bP[gNodePrime]);
    G.add(here.HSMHVBPspPtr, here.HSMHV_ydc_bP[sNodePrime]);
    C.add(here.HSMHVBPspPtr, here.HSMHV_ydyn_bP[sNodePrime]);
    G.add(here.HSMHVBPbpPtr, here.HSMHV_ydc_bP[bNodePrime]);
    C.add(here.HSMHVBPbpPtr, here.HSMHV_ydyn_bP[bNodePrime]);
    G.add(here.HSMHVBPbPtr, here.HSMHV_ydc_bP[bNode]);
    C.add(here.HSMHVBPbPtr, here.HSMHV_ydyn_bP[bNode]);
    G.add(here.HSMHVBPdbPtr, here.HSMHV_ydc_bP[dbNode]);
    C.add(here.HSMHVBPdbPtr, here.HSMHV_ydyn_bP[dbNode]);
    G.add(here.HSMHVBPsbPtr, here.HSMHV_ydc_bP[sbNode]);
    C.add(here.HSMHVBPsbPtr, here.HSMHV_ydyn_bP[sbNode]);
    if (here.HSMHVtempNode > 0) {
        G.add(here.HSMHVBPtempPtr, model->HSMHV_type * here.HSMHV_ydc_bP[tempNode]);
        C.add(here.HSMHVBPtempPtr, model->HSMHV_type * here.HSMHV_ydyn_bP[tempNode]);
    }
    if (flg_nqs) {
        G.add(here.HSMHVBPqbPtr, model->HSMHV_type * here.HSMHV_ydc_bP[qbNode]);
        C.add(here.HSMHVBPqbPtr, model->HSMHV_type * here.HSMHV_ydyn_bP[qbNode]);
    }

    // Bulk
    G.add(here.HSMHVBbpPtr, here.HSMHV_ydc_b[bNodePrime]);
    C.add(here.HSMHVBbpPtr, here.HSMHV_ydyn_b[bNodePrime]);
    G.add(here.HSMHVBbPtr, here.HSMHV_ydc_b[bNode]);
    C.add(here.HSMHVBbPtr, here.HSMHV_ydyn_b[bNode]);

    // Drain bulk
    G.add(here.HSMHVDBdPtr, here.HSMHV_ydc_db[dNode]);
    C.add(here.HSMHVDBdPtr, here.HSMHV_ydyn_db[dNode]);
    G.add(here.HSMHVDBbpPtr, here.HSMHV_ydc_db[bNodePrime]);
    C.add(here.HSMHVDBbpPtr, here.HSMHV_ydyn_db[bNodePrime]);
    G.add(here.HSMHVDBdbPtr, here.HSMHV_ydc_db[dbNode]);
    C.add(here.HSMHVDBdbPtr, here.HSMHV_ydyn_db[dbNode]);
    if (here.HSMHVtempNode > 0) {
        G.add(here.HSMHVDBtempPtr, model->HSMHV_type * here.HSMHV_ydc_db[tempNode]);
        C.add(here.HSMHVDBtempPtr, model->HSMHV_type * here.HSMHV_ydyn_db[tempNode]);
    }

    // Source bulk
    G.add(here.HSMHVSBsPtr, here.HSMHV_ydc_sb[sNode]);
    C.add(here.HSMHVSBsPtr, here.HSMHV_ydyn_sb[sNode]);
    G.add(here.HSMHVSBbpPtr, here.HSMHV_ydc_sb[bNodePrime]);
    C.add(here.HSMHVSBbpPtr, here.HSMHV_ydyn_sb[bNodePrime]);
    G.add(here.HSMHVSBsbPtr, here.HSMHV_ydc_sb[sbNode]);
    C.add(here.HSMHVSBsbPtr, here.HSMHV_ydyn_sb[sbNode]);
    if (here.HSMHVtempNode > 0) {
        G.add(here.HSMHVSBtempPtr, model->HSMHV_type * here.HSMHV_ydc_sb[tempNode]);
        C.add(here.HSMHVSBtempPtr, model->HSMHV_type * here.HSMHV_ydyn_sb[tempNode]);
    }

    // Temperature node
    if (here.HSMHVtempNode > 0) {
        G.add(here.HSMHVTempdPtr, model->HSMHV_type * here.HSMHV_ydc_t[dNode]);
        C.add(here.HSMHVTempdPtr, model->HSMHV_type * here.HSMHV_ydyn_t[dNode]);
        G.add(here.HSMHVTempdpPtr, model->HSMHV_type * here.HSMHV_ydc_t[dNodePrime]);
        C.add(here.HSMHVTempdpPtr, model->HSMHV_type * here.HSMHV_ydyn_t[dNodePrime]);
        G.add(here.HSMHVTempgpPtr, model->HSMHV_type * here.HSMHV_ydc_t[gNodePrime]);
        C.add(here.HSMHVTempgpPtr, model->HSMHV_type * here.HSMHV_ydyn_t[gNodePrime]);
        G.add(here.HSMHVTempsPtr, model->HSMHV_type * here.HSMHV_ydc_t[sNode]);
        C.add(here.HSMHVTempsPtr, model->HSMHV_type * here.HSMHV_ydyn_t[sNode]);
        G.add(here.HSMHVTempspPtr, model->HSMHV_type * here.HSMHV_ydc_t[sNodePrime]);
        C.add(here.HSMHVTempspPtr, model->HSMHV_type * here.HSMHV_ydyn_t[sNodePrime]);
        G.add(here.HSMHVTempbpPtr, model->HSMHV_type * here.HSMHV_ydc_t[bNodePrime]);
        C.add(here.HSMHVTempbpPtr, model->HSMHV_type * here.HSMHV_ydyn_t[bNodePrime]);
        G.add(here.HSMHVTemptempPtr, here.HSMHV_ydc_t[tempNode]);
        C.add(here.HSMHVTemptempPtr, here.HSMHV_ydyn_t[tempNode]);
    }

    // NQS nodes
    if (flg_nqs) {
        G.add(here.HSMHVQIdpPtr, model->HSMHV_type * here.HSMHV_ydc_qi[dNodePrime]);
        C.add(here.HSMHVQIdpPtr, model->HSMHV_type * here.HSMHV_ydyn_qi[dNodePrime]);
        G.add(here.HSMHVQIgpPtr, model->HSMHV_type * here.HSMHV_ydc_qi[gNodePrime]);
        C.add(here.HSMHVQIgpPtr, model->HSMHV_type * here.HSMHV_ydyn_qi[gNodePrime]);
        G.add(here.HSMHVQIspPtr, model->HSMHV_type * here.HSMHV_ydc_qi[sNodePrime]);
        C.add(here.HSMHVQIspPtr, model->HSMHV_type * here.HSMHV_ydyn_qi[sNodePrime]);
        G.add(here.HSMHVQIbpPtr, model->HSMHV_type * here.HSMHV_ydc_qi[bNodePrime]);
        C.add(here.HSMHVQIbpPtr, model->HSMHV_type * here.HSMHV_ydyn_qi[bNodePrime]);
        G.add(here.HSMHVQIqiPtr, here.HSMHV_ydc_qi[qiNode]);
        C.add(here.HSMHVQIqiPtr, here.HSMHV_ydyn_qi[qiNode]);
        if (here.HSMHVtempNode > 0) {
            G.add(here.HSMHVQItempPtr, here.HSMHV_ydc_qi[tempNode]);
            C.add(here.HSMHVQItempPtr, here.HSMHV_ydyn_qi[tempNode]);
        }
        G.add(here.HSMHVQBdpPtr, model->HSMHV_type * here.HSMHV_ydc_qb[dNodePrime]);
        C.add(here.HSMHVQBdpPtr, model->HSMHV_type * here.HSMHV_ydyn_qb[dNodePrime]);
        G.add(here.HSMHVQBgpPtr, model->HSMHV_type * here.HSMHV_ydc_qb[gNodePrime]);
        C.add(here.HSMHVQBgpPtr, model->HSMHV_type * here.HSMHV_ydyn_qb[gNodePrime]);
        G.add(here.HSMHVQBspPtr, model->HSMHV_type * here.HSMHV_ydc_qb[sNodePrime]);
        C.add(here.HSMHVQBspPtr, model->HSMHV_type * here.HSMHV_ydyn_qb[sNodePrime]);
        G.add(here.HSMHVQBbpPtr, model->HSMHV_type * here.HSMHV_ydc_qb[bNodePrime]);
        C.add(here.HSMHVQBbpPtr, model->HSMHV_type * here.HSMHV_ydyn_qb[bNodePrime]);
        G.add(here.HSMHVQBqbPtr, here.HSMHV_ydc_qb[qbNode]);
        C.add(here.HSMHVQBqbPtr, here.HSMHV_ydyn_qb[qbNode]);
        if (here.HSMHVtempNode > 0) {
            G.add(here.HSMHVQBtempPtr, here.HSMHV_ydc_qb[tempNode]);
            C.add(here.HSMHVQBtempPtr, here.HSMHV_ydyn_qb[tempNode]);
        }
    }

#undef dNode
#undef dNodePrime
#undef gNode
#undef gNodePrime
#undef sNode
#undef sNodePrime
#undef bNodePrime
#undef bNode
#undef dbNode
#undef sbNode
#undef subNode
#undef tempNode
#undef qiNode
#undef qbNode
}

// ---------------------------------------------------------------------------
// compute_trunc
// ---------------------------------------------------------------------------
double HSMHVDevice::compute_trunc(const IntegratorCtx& ctx,
                               const SimOptions& opts) const {
    if (ctx.order < 2 || ctx.delta <= 0.0) return 1e30;
    if (!state0_ || !state1_ || !state2_) return 1e30;

    const double h0 = ctx.delta;
    const double h1 = ctx.delta_old[1];
    if (h1 <= 0.0) return 1e30;

    double dt_min = 1e30;
    const double lte_coeff = ctx.lte_coefficient();

    { // charge offset: inst_.HSMHVqb
        const int qcap = inst_.HSMHVqb;
        const double q0 = state0_[qcap], q1 = state1_[qcap], q2 = state2_[qcap];
        const double dd2 = ((q0 - q1) / h0 - (q1 - q2) / h1) / (h0 + h1);
        const double tol = opts.chgtol + opts.reltol * std::max(std::abs(q0), std::abs(q1));
        if (tol > 0.0 && std::abs(dd2) > 1e-30) {
            dt_min = std::min(dt_min, std::sqrt(opts.trtol * tol / (lte_coeff * std::abs(dd2))));
        }
    }
    { // charge offset: inst_.HSMHVqg
        const int qcap = inst_.HSMHVqg;
        const double q0 = state0_[qcap], q1 = state1_[qcap], q2 = state2_[qcap];
        const double dd2 = ((q0 - q1) / h0 - (q1 - q2) / h1) / (h0 + h1);
        const double tol = opts.chgtol + opts.reltol * std::max(std::abs(q0), std::abs(q1));
        if (tol > 0.0 && std::abs(dd2) > 1e-30) {
            dt_min = std::min(dt_min, std::sqrt(opts.trtol * tol / (lte_coeff * std::abs(dd2))));
        }
    }
    { // charge offset: inst_.HSMHVqd
        const int qcap = inst_.HSMHVqd;
        const double q0 = state0_[qcap], q1 = state1_[qcap], q2 = state2_[qcap];
        const double dd2 = ((q0 - q1) / h0 - (q1 - q2) / h1) / (h0 + h1);
        const double tol = opts.chgtol + opts.reltol * std::max(std::abs(q0), std::abs(q1));
        if (tol > 0.0 && std::abs(dd2) > 1e-30) {
            dt_min = std::min(dt_min, std::sqrt(opts.trtol * tol / (lte_coeff * std::abs(dd2))));
        }
    }
    { // charge offset: inst_.HSMHVqbs
        const int qcap = inst_.HSMHVqbs;
        const double q0 = state0_[qcap], q1 = state1_[qcap], q2 = state2_[qcap];
        const double dd2 = ((q0 - q1) / h0 - (q1 - q2) / h1) / (h0 + h1);
        const double tol = opts.chgtol + opts.reltol * std::max(std::abs(q0), std::abs(q1));
        if (tol > 0.0 && std::abs(dd2) > 1e-30) {
            dt_min = std::min(dt_min, std::sqrt(opts.trtol * tol / (lte_coeff * std::abs(dd2))));
        }
    }
    { // charge offset: inst_.HSMHVqbd
        const int qcap = inst_.HSMHVqbd;
        const double q0 = state0_[qcap], q1 = state1_[qcap], q2 = state2_[qcap];
        const double dd2 = ((q0 - q1) / h0 - (q1 - q2) / h1) / (h0 + h1);
        const double tol = opts.chgtol + opts.reltol * std::max(std::abs(q0), std::abs(q1));
        if (tol > 0.0 && std::abs(dd2) > 1e-30) {
            dt_min = std::min(dt_min, std::sqrt(opts.trtol * tol / (lte_coeff * std::abs(dd2))));
        }
    }
    { // charge offset: inst_.HSMHVqth
        const int qcap = inst_.HSMHVqth;
        const double q0 = state0_[qcap], q1 = state1_[qcap], q2 = state2_[qcap];
        const double dd2 = ((q0 - q1) / h0 - (q1 - q2) / h1) / (h0 + h1);
        const double tol = opts.chgtol + opts.reltol * std::max(std::abs(q0), std::abs(q1));
        if (tol > 0.0 && std::abs(dd2) > 1e-30) {
            dt_min = std::min(dt_min, std::sqrt(opts.trtol * tol / (lte_coeff * std::abs(dd2))));
        }
    }
    { // charge offset: inst_.HSMHVqfd
        const int qcap = inst_.HSMHVqfd;
        const double q0 = state0_[qcap], q1 = state1_[qcap], q2 = state2_[qcap];
        const double dd2 = ((q0 - q1) / h0 - (q1 - q2) / h1) / (h0 + h1);
        const double tol = opts.chgtol + opts.reltol * std::max(std::abs(q0), std::abs(q1));
        if (tol > 0.0 && std::abs(dd2) > 1e-30) {
            dt_min = std::min(dt_min, std::sqrt(opts.trtol * tol / (lte_coeff * std::abs(dd2))));
        }
    }
    { // charge offset: inst_.HSMHVqfs
        const int qcap = inst_.HSMHVqfs;
        const double q0 = state0_[qcap], q1 = state1_[qcap], q2 = state2_[qcap];
        const double dd2 = ((q0 - q1) / h0 - (q1 - q2) / h1) / (h0 + h1);
        const double tol = opts.chgtol + opts.reltol * std::max(std::abs(q0), std::abs(q1));
        if (tol > 0.0 && std::abs(dd2) > 1e-30) {
            dt_min = std::min(dt_min, std::sqrt(opts.trtol * tol / (lte_coeff * std::abs(dd2))));
        }
    }
    { // charge offset: inst_.HSMHVqdE
        const int qcap = inst_.HSMHVqdE;
        const double q0 = state0_[qcap], q1 = state1_[qcap], q2 = state2_[qcap];
        const double dd2 = ((q0 - q1) / h0 - (q1 - q2) / h1) / (h0 + h1);
        const double tol = opts.chgtol + opts.reltol * std::max(std::abs(q0), std::abs(q1));
        if (tol > 0.0 && std::abs(dd2) > 1e-30) {
            dt_min = std::min(dt_min, std::sqrt(opts.trtol * tol / (lte_coeff * std::abs(dd2))));
        }
    }
    { // charge offset: inst_.HSMHVqi_nqs
        const int qcap = inst_.HSMHVqi_nqs;
        const double q0 = state0_[qcap], q1 = state1_[qcap], q2 = state2_[qcap];
        const double dd2 = ((q0 - q1) / h0 - (q1 - q2) / h1) / (h0 + h1);
        const double tol = opts.chgtol + opts.reltol * std::max(std::abs(q0), std::abs(q1));
        if (tol > 0.0 && std::abs(dd2) > 1e-30) {
            dt_min = std::min(dt_min, std::sqrt(opts.trtol * tol / (lte_coeff * std::abs(dd2))));
        }
    }
    { // charge offset: inst_.HSMHVqb_nqs
        const int qcap = inst_.HSMHVqb_nqs;
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
bool HSMHVDevice::device_converged() const {
    return last_noncon_ == 0;
}

// ---------------------------------------------------------------------------
// query_param
// ---------------------------------------------------------------------------
std::optional<double> HSMHVDevice::query_param(const std::string& name) const {
    const std::string key = str_tolower(name);
    const double m = inst_.HSMHV_m;

    // --- Operating-point parameters (scaled by multiplier m) ---
    if (key == "gm")                        return inst_.HSMHV_gm * m;
    if (key == "gds")                       return inst_.HSMHV_gds * m;
    if (key == "gmbs")                      return inst_.HSMHV_gmbs * m;
    if (key == "vth" || key == "von")       return inst_.HSMHV_von;
    if (key == "vdsat")                     return inst_.HSMHV_vdsat;
    if (key == "id" || key == "ids")        return inst_.HSMHV_ids * m;
    if (key == "ibs")                       return inst_.HSMHV_ibs * m;
    if (key == "ibd")                       return inst_.HSMHV_ibd * m;
    if (key == "isub")                      return inst_.HSMHV_isub * m;
    if (key == "gbd")                       return inst_.HSMHV_gbd * m;
    if (key == "gbs")                       return inst_.HSMHV_gbs * m;

    // --- Gate tunneling currents ---
    if (key == "igs")                       return inst_.HSMHV_igs * m;
    if (key == "igd")                       return inst_.HSMHV_igd * m;
    if (key == "igb")                       return inst_.HSMHV_igb * m;
    if (key == "igidl")                     return inst_.HSMHV_igidl * m;
    if (key == "igisl")                     return inst_.HSMHV_igisl * m;

    // --- Capacitances ---
    if (key == "cgg")                       return inst_.HSMHV_cggb * m;
    if (key == "cgd")                       return inst_.HSMHV_cgdb * m;
    if (key == "cgs")                       return inst_.HSMHV_cgsb * m;
    if (key == "cdg")                       return inst_.HSMHV_cdgb * m;
    if (key == "cbg")                       return inst_.HSMHV_cbgb * m;

    // --- Charges ---
    if (key == "qg")                        return inst_.HSMHV_qg * m;
    if (key == "qd")                        return inst_.HSMHV_qd * m;
    if (key == "qs")                        return inst_.HSMHV_qs * m;
    if (key == "qb")                        return inst_.HSMHV_qb * m;

    // --- Overlap caps ---
    if (key == "cgdo")                      return inst_.HSMHV_cgdo * m;
    if (key == "cgso")                      return inst_.HSMHV_cgso * m;
    if (key == "cgbo")                      return inst_.HSMHV_cgbo * m;

    // --- Junction capacitances ---
    if (key == "capbd")                     return inst_.HSMHV_capbd * m;
    if (key == "capbs")                     return inst_.HSMHV_capbs * m;

    // --- Terminal voltages from state vector ---
    if (state0_ && state_base_ >= 0) {
        if (key == "vbd")                   return state0_[state_base_ + 0];
        if (key == "vbs")                   return state0_[state_base_ + 1];
        if (key == "vgs")                   return state0_[state_base_ + 2];
        if (key == "vds")                   return state0_[state_base_ + 3];
    }

    // --- Geometry (no multiplier) ---
    if (key == "w")                         return inst_.HSMHV_w;
    if (key == "l")                         return inst_.HSMHV_l;
    if (key == "m")                         return inst_.HSMHV_m;
    if (key == "nf")                        return inst_.HSMHV_nf;

    return std::nullopt;
}

// ---------------------------------------------------------------------------
// noise_sources — HiSIM_HV noise model
//
// Implements noise sources from hsmhvnoi.c:
//   1. Drain/source series resistance thermal noise
//   2. Channel thermal noise (HiSIM model)
//   3. Flicker (1/f) noise (HiSIM model)
//   4. Induced gate noise (projected onto drain-source, correlated component)
//   5. Shot noise (Igs, Igd, Igb)
// ---------------------------------------------------------------------------
std::vector<Device::NoiseSource>
HSMHVDevice::noise_sources(double freq,
                            const std::vector<double>& /*dc_solution*/) const {
    const auto* model = model_;
    const auto& inst  = inst_;

    const double m     = inst.HSMHV_m;
    const double TTEMP = (inst.HSMHV_dtemp_Given) ? (sim_temp_ + inst.HSMHV_dtemp)
                       : sim_temp_;
    const double fourKT = 4.0 * BOLTZMANN * TTEMP;

    // Node indices (neospice convention)
    const int32_t dp_neo = ucb_to_neo(inst.HSMHVdNodePrime);
    const int32_t sp_neo = ucb_to_neo(inst.HSMHVsNodePrime);
    const int32_t d_neo  = ucb_to_neo(inst.HSMHVdNode);
    const int32_t s_neo  = ucb_to_neo(inst.HSMHVsNode);
    const int32_t gp_neo = ucb_to_neo(inst.HSMHVgNodePrime);
    const int32_t bp_neo = ucb_to_neo(inst.HSMHVbNodePrime);

    std::vector<NoiseSource> sources;
    sources.reserve(8);

    // -----------------------------------------------------------------------
    // 1. Drain / Source series resistance thermal noise
    //    ngspice: corsrd == 1 || corsrd == 3
    // -----------------------------------------------------------------------
    if (model->HSMHV_corsrd == 1 || model->HSMHV_corsrd == 3) {
        const double gdpr = inst.HSMHVdrainConductance;
        const double gspr = inst.HSMHVsourceConductance;

        if (gdpr > 0.0)
            sources.push_back({dp_neo, d_neo, fourKT * gdpr * m});
        if (gspr > 0.0)
            sources.push_back({sp_neo, s_neo, fourKT * gspr * m});
    }

    // -----------------------------------------------------------------------
    // 2. Channel thermal noise (HiSIM model)
    //    HSMHV_noithrml is already multiplied by Mfactor in eval code.
    //    Here we use 'inst.HSMHV_noithrml' which is Mfactor * Nthrml,
    //    so we do NOT multiply by 'm' again (m == Mfactor).
    // -----------------------------------------------------------------------
    double channel_noise = fourKT * inst.HSMHV_noithrml;
    if (channel_noise > 0.0)
        sources.push_back({dp_neo, sp_neo, channel_noise});

    // -----------------------------------------------------------------------
    // 3. Flicker (1/f) noise (HiSIM model)
    //    HSMHV_noiflick is already multiplied by Mfactor in eval code.
    //    ngspice divides by freq^falph at noise-analysis time.
    // -----------------------------------------------------------------------
    if (freq > 0.0 && inst.HSMHV_noiflick != 0.0) {
        double flicker = inst.HSMHV_noiflick / std::pow(freq, model->HSMHV_falph);
        if (flicker > 0.0)
            sources.push_back({dp_neo, sp_neo, flicker});
    }

    // -----------------------------------------------------------------------
    // 4. Induced gate noise (projected onto drain-source, correlated part)
    //    ngspice: noiigate * noicross^2 * freq^2  (between dp and sp)
    //    HSMHV_noiigate is already multiplied by Mfactor in eval code.
    // -----------------------------------------------------------------------
    if (freq > 0.0 && inst.HSMHV_noiigate > 0.0
        && inst.HSMHV_noicross != 0.0) {
        double ign = inst.HSMHV_noiigate
                   * inst.HSMHV_noicross * inst.HSMHV_noicross
                   * freq * freq;
        if (ign > 0.0)
            sources.push_back({dp_neo, sp_neo, ign});
    }

    // -----------------------------------------------------------------------
    // 5. Shot noise (gate tunneling: Igs, Igd, Igb)
    //    HSMHV_igs/igd/igb are already multiplied by Mfactor in eval code.
    // -----------------------------------------------------------------------
    const double CHARGE_Q = 1.60217663e-19;
    if (std::abs(inst.HSMHV_igs) > 0.0)
        sources.push_back({gp_neo, sp_neo, 2.0 * CHARGE_Q * std::abs(inst.HSMHV_igs)});
    if (std::abs(inst.HSMHV_igd) > 0.0)
        sources.push_back({gp_neo, dp_neo, 2.0 * CHARGE_Q * std::abs(inst.HSMHV_igd)});
    if (std::abs(inst.HSMHV_igb) > 0.0)
        sources.push_back({gp_neo, bp_neo, 2.0 * CHARGE_Q * std::abs(inst.HSMHV_igb)});

    return sources;
}

} // namespace neospice
