#include "devices/mes/mes_device.hpp"

#include "core/circuit.hpp"        // Circuit::node, tls_integrator_ctx
#include "core/types.hpp"          // SimOptions defaults
#include "devices/ucb_device_init.hpp"
#include "devices/ucb_utils.hpp"

#include <cmath>
#include <optional>
#include <stdexcept>
#include <string>
#include "devices/ckt_terr.hpp"

// Forward declarations for translated UCB functions.
namespace neospice::mes {
    int MESsetup(Shim::Matrix*, MESModel*, Shim::Ckt*, int*);
    int MEStemp(MESModel*, Shim::Ckt*);
    int MESload(MESModel*, Shim::Ckt*);
}

namespace neospice {

using namespace neospice::mes;

// ---------------------------------------------------------------------------
// MESModelCard destructor
// ---------------------------------------------------------------------------
MESModelCard::~MESModelCard() = default;

// ---------------------------------------------------------------------------
// make
// ---------------------------------------------------------------------------
std::unique_ptr<MESDevice>
MESDevice::make(std::string name,
        int32_t n_drain, int32_t n_gate, int32_t n_source,
        const Geom& geom, MESModelCard& shared_card) {
    std::unique_ptr<MESDevice> dev(new MESDevice(std::move(name)));
    dev->model_ = &shared_card.ucb;
    dev->ext_nodes_ = {n_drain, n_gate, n_source};

    auto& inst = dev->inst_;
    inst.MESname = dev->name().c_str();
    inst.MESmodPtr = dev->model_;

    // Node wiring (UCB convention).
    inst.MESdrainNode = neo_to_ucb(n_drain);
    inst.MESgateNode = neo_to_ucb(n_gate);
    inst.MESsourceNode = neo_to_ucb(n_source);

    // Geometry.
    inst.MESarea = geom.area;
    inst.MESareaGiven = (geom.area != 1.0) ? 1 : 0;
    inst.MESm = geom.m;
    inst.MESmGiven = (geom.m != 1.0) ? 1 : 0;

    // Thread onto the shared model's instance list.
    inst.MESnextInstance = shared_card.ucb.MESinstances;
    shared_card.ucb.MESinstances = &inst;

    // Remember the widest real node index for ghost array sizing.
    int32_t widest = -1;
    for (int32_t n : {n_drain, n_gate, n_source}) if (n > widest) widest = n;
    dev->max_neo_node_ = widest;

    return dev;
}

// ---------------------------------------------------------------------------
// declare_internal_nodes
// ---------------------------------------------------------------------------
void MESDevice::declare_internal_nodes(Circuit& ckt) {
    ucb_declare_internal_nodes<Shim::Matrix, Shim::Ckt>(
        ckt, name_,
        [this](Shim::Matrix& m, Shim::Ckt& c) {
            UCB_SPLICE_INSTANCE(MES);
            int states = 0;
            int rc = MESsetup(&m, model_, &c, &states);
            UCB_UNSPLICE_INSTANCE(MES);
            return rc;
        },
        "MESsetup", journal_, max_neo_node_);
}

// ---------------------------------------------------------------------------
// stamp_pattern
// ---------------------------------------------------------------------------
void MESDevice::stamp_pattern(SparsityBuilder& builder) const {
    ucb_stamp_pattern(journal_, builder);
}

// ---------------------------------------------------------------------------
// assign_offsets
// ---------------------------------------------------------------------------
void MESDevice::assign_offsets(const SparsityPattern& pattern) {
    const auto offsets = ucb_compute_offsets(journal_, pattern);

#define RESOLVE(f)                                                       \
    do {                                                                 \
        if (inst_.f >= 0)                                                \
            inst_.f = offsets[inst_.f];                                  \
    } while (0)

    RESOLVE(MESdrainDrainPrimePtr);
    RESOLVE(MESgateDrainPrimePtr);
    RESOLVE(MESgateSourcePrimePtr);
    RESOLVE(MESsourceSourcePrimePtr);
    RESOLVE(MESdrainPrimeDrainPtr);
    RESOLVE(MESdrainPrimeGatePtr);
    RESOLVE(MESdrainPrimeSourcePrimePtr);
    RESOLVE(MESsourcePrimeGatePtr);
    RESOLVE(MESsourcePrimeSourcePtr);
    RESOLVE(MESsourcePrimeDrainPrimePtr);
    RESOLVE(MESdrainDrainPtr);
    RESOLVE(MESgateGatePtr);
    RESOLVE(MESsourceSourcePtr);
    RESOLVE(MESdrainPrimeDrainPrimePtr);
    RESOLVE(MESsourcePrimeSourcePrimePtr);

#undef RESOLVE
}

// ---------------------------------------------------------------------------
// set_state_ptrs
// ---------------------------------------------------------------------------
void MESDevice::set_state_ptrs(double* s0, double* s1, double* s2, double* s3, int32_t base) {
    state0_ = s0;
    state1_ = s1;
    state2_ = s2;
    state3_ = s3;
    state_base_ = base;
    inst_.MESstate = base;
}

// ---------------------------------------------------------------------------
// evaluate
// ---------------------------------------------------------------------------
void MESDevice::evaluate(const std::vector<double>& voltages,
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
        ckt.CKTintegrateMethod = ic->integrate_method;
        ckt.xmu_ratio = ic->xmu_ratio;
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

    // First-call MEStemp.
    if (!temp_done_) {
        int rc = MEStemp(model_, &ckt);
        if (rc != Shim::OK) {
            throw std::runtime_error("MEStemp failed with rc=" + std::to_string(rc));
        }
        temp_done_ = true;
    }

    // Splice this instance as sole member, call load, then restore.
    MESInstance* saved_head      = model_->MESinstances;
    MESInstance* saved_next_inst = inst_.MESnextInstance;
    MESModel*    saved_next_mod  = model_->MESnextModel;
    model_->MESinstances  = &inst_;
    inst_.MESnextInstance = nullptr;
    model_->MESnextModel  = nullptr;
    int rc = MESload(model_, &ckt);
    model_->MESinstances  = saved_head;
    inst_.MESnextInstance = saved_next_inst;
    model_->MESnextModel  = saved_next_mod;
    if (rc != Shim::OK) {
        throw std::runtime_error("MESload failed with rc=" + std::to_string(rc));
    }

    last_noncon_ = ckt.CKTnoncon;

    // Fold ghost rhs contributions back into the real rhs.
    for (int32_t k = 1; k <= max_neo_node_ + 1 && k < n_ghost; ++k) {
        if (k - 1 < n_real) rhs[k - 1] += ghost_rhs_[k];
    }
}

// ---------------------------------------------------------------------------
// ac_stamp — 8 G entries + 3 C entries extracted from ngspice AC load
// ---------------------------------------------------------------------------
void MESDevice::ac_stamp(const std::vector<double>& /*voltages*/,
                           NumericMatrix& G,
                           NumericMatrix& C) {
    auto& here = inst_;
    auto* model = model_;
    const double m = here.MESm;
    if (!state0_) return;

    const int sb = here.MESstate;
    const double gdpr = model->MESdrainConduct * here.MESarea;
    const double gspr = model->MESsourceConduct * here.MESarea;
    const double gm   = state0_[sb + 5];
    const double gds  = state0_[sb + 6];
    const double ggs  = state0_[sb + 7];
    const double ggd  = state0_[sb + 8];
    const double xgs  = state0_[sb + 9];
    const double xgd  = state0_[sb + 11];

    // G matrix (conductance)
    G.add(here.MESdrainDrainPtr,             m * gdpr);
    G.add(here.MESgateGatePtr,               m * (ggd + ggs));
    G.add(here.MESsourceSourcePtr,           m * gspr);
    G.add(here.MESdrainPrimeDrainPrimePtr,   m * (gdpr + gds + ggd));
    G.add(here.MESsourcePrimeSourcePrimePtr, m * (gspr + gds + gm + ggs));
    G.add(here.MESdrainDrainPrimePtr,        m * (-gdpr));
    G.add(here.MESgateDrainPrimePtr,         m * (-ggd));
    G.add(here.MESgateSourcePrimePtr,        m * (-ggs));
    G.add(here.MESsourceSourcePrimePtr,      m * (-gspr));
    G.add(here.MESdrainPrimeDrainPtr,        m * (-gdpr));
    G.add(here.MESdrainPrimeGatePtr,         m * (-ggd + gm));
    G.add(here.MESdrainPrimeSourcePrimePtr,  m * (-gds - gm));
    G.add(here.MESsourcePrimeGatePtr,        m * (-ggs - gm));
    G.add(here.MESsourcePrimeSourcePtr,      m * (-gspr));
    G.add(here.MESsourcePrimeDrainPrimePtr,  m * (-gds));

    // C matrix (capacitance — ngspice stamps cap*omega; neospice multiplies by omega later)
    C.add(here.MESgateGatePtr,               m * (xgd + xgs));
    C.add(here.MESdrainPrimeDrainPrimePtr,   m * xgd);
    C.add(here.MESsourcePrimeSourcePrimePtr, m * xgs);
    C.add(here.MESgateDrainPrimePtr,         m * (-xgd));
    C.add(here.MESgateSourcePrimePtr,        m * (-xgs));
    C.add(here.MESdrainPrimeGatePtr,         m * (-xgd));
    C.add(here.MESsourcePrimeGatePtr,        m * (-xgs));
}

// ---------------------------------------------------------------------------
// compute_trunc
// ---------------------------------------------------------------------------
double MESDevice::compute_trunc(const IntegratorCtx& ctx,
                             const SimOptions& opts) const {
    if (ctx.order < 1 || ctx.delta <= 0.0)
        return 1e30;
    if (!state0_ || !state1_ || !state2_ || !state3_)
        return 1e30;

    const double* states[] = {state0_, state1_, state2_, state3_};
    double dt_min = 1e30;
    ckt_terr(state_base_ + 9, states, ctx, opts, dt_min);   // qgs
    ckt_terr(state_base_ + 11, states, ctx, opts, dt_min);  // qgd
    return dt_min;
}

// ---------------------------------------------------------------------------
// set_ic
// ---------------------------------------------------------------------------
void MESDevice::set_ic(double vds, bool vds_given,
                       double vgs, bool vgs_given) {
    if (vds_given) { inst_.MESicVDS = vds; inst_.MESicVDSGiven = 1; }
    if (vgs_given) { inst_.MESicVGS = vgs; inst_.MESicVGSGiven = 1; }
}

// ---------------------------------------------------------------------------
// device_converged
// ---------------------------------------------------------------------------
bool MESDevice::device_converged() const {
    return last_noncon_ == 0;
}

// ---------------------------------------------------------------------------
// query_param
// ---------------------------------------------------------------------------
std::optional<double> MESDevice::query_param(const std::string& name) const {
    const std::string key = str_tolower(name);
    const double m = inst_.MESm;

    if (state0_ && state_base_ >= 0) {
        int sb = inst_.MESstate;
        if (key == "vgs") return state0_[sb + 0];
        if (key == "vgd") return state0_[sb + 1];
        if (key == "cg")  return m * state0_[sb + 2];
        if (key == "cd")  return m * state0_[sb + 3];
        if (key == "cgd") return m * state0_[sb + 4];
        if (key == "gm")  return m * state0_[sb + 5];
        if (key == "gds") return m * state0_[sb + 6];
        if (key == "ggs") return m * state0_[sb + 7];
        if (key == "ggd") return m * state0_[sb + 8];
        if (key == "qgs") return m * state0_[sb + 9];
        if (key == "cqgs") return m * state0_[sb + 10];
        if (key == "qgd") return m * state0_[sb + 11];
        if (key == "cqgd") return m * state0_[sb + 12];
    }

    // Geometry (not scaled by m)
    if (key == "area") return inst_.MESarea;
    if (key == "m") return inst_.MESm;
    if (key == "icvds") return inst_.MESicVDS;
    if (key == "icvgs") return inst_.MESicVGS;

    return std::nullopt;
}

// ---------------------------------------------------------------------------
// noise_sources — TODO: implement device noise model
// ---------------------------------------------------------------------------
std::vector<Device::NoiseSource> MESDevice::noise_sources(
        double freq, const std::vector<double>& /*dc_solution*/) const {
    if (!state0_ || state_base_ < 0) return {};

    std::vector<NoiseSource> ns;
    const double m = inst_.MESm;
    const double T = sim_temp();

    const int32_t drain_node        = inst_.MESdrainNode       - 1;
    const int32_t source_node       = inst_.MESsourceNode      - 1;
    const int32_t drain_prime_node  = inst_.MESdrainPrimeNode  - 1;
    const int32_t source_prime_node = inst_.MESsourcePrimeNode - 1;

    const int sb = inst_.MESstate;
    const double gm = std::abs(state0_[sb + 5]);
    const double cd = std::abs(state0_[sb + 3]);

    const double gdpr = model_->MESdrainConduct * inst_.MESarea;
    const double gspr = model_->MESsourceConduct * inst_.MESarea;

    if (gdpr > 0.0) {
        ns.push_back({drain_prime_node, drain_node,
                      m * 4.0 * BOLTZMANN * T * gdpr});
    }
    if (gspr > 0.0) {
        ns.push_back({source_prime_node, source_node,
                      m * 4.0 * BOLTZMANN * T * gspr});
    }

    ns.push_back({drain_prime_node, source_prime_node,
                  m * 4.0 * BOLTZMANN * T * (2.0 / 3.0) * gm});

    const double KF = model_->MESfNcoef;
    const double AF = model_->MESfNexp;
    if (KF > 0.0 && freq > 0.0 && cd > 0.0) {
        ns.push_back({drain_prime_node, source_prime_node,
                      m * KF * std::pow(cd, AF) / freq});
    }

    return ns;
}

} // namespace neospice
