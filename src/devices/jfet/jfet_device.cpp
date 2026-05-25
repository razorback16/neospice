#include "devices/jfet/jfet_device.hpp"

#include "core/circuit.hpp"        // Circuit::node, tls_integrator_ctx
#include "core/types.hpp"          // SimOptions defaults
#include "devices/ucb_device_init.hpp"
#include "devices/ucb_utils.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>

// Forward declarations for translated UCB functions.
namespace neospice::jfet {
    int JFETsetup(Shim::Matrix*, JFETModel*, Shim::Ckt*, int*);
    int JFETtemp(JFETModel*, Shim::Ckt*);
    int JFETload(JFETModel*, Shim::Ckt*);
}

namespace neospice {

using namespace neospice::jfet;

// ---------------------------------------------------------------------------
// JFETModelCard destructor
// ---------------------------------------------------------------------------
JFETModelCard::~JFETModelCard() = default;

// ---------------------------------------------------------------------------
// make
// ---------------------------------------------------------------------------
std::unique_ptr<JFETDevice>
JFETDevice::make(std::string name,
        int32_t n_drain, int32_t n_gate, int32_t n_source,
        const Geom& geom, JFETModelCard& shared_card) {
    std::unique_ptr<JFETDevice> dev(new JFETDevice(std::move(name)));
    dev->model_ = &shared_card.ucb;
    dev->ext_nodes_ = {n_drain, n_gate, n_source};

    auto& inst = dev->inst_;
    inst.JFETname = dev->name().c_str();
    inst.JFETmodPtr = dev->model_;

    // Node wiring (UCB convention).
    inst.JFETdrainNode = neo_to_ucb(n_drain);
    inst.JFETgateNode = neo_to_ucb(n_gate);
    inst.JFETsourceNode = neo_to_ucb(n_source);

    // Geometry.
    inst.JFETarea = geom.area;
    inst.JFETareaGiven = geom.area_given ? 1 : 0;
    inst.JFETm = geom.m;
    inst.JFETmGiven = geom.m_given ? 1 : 0;

    // Thread onto the shared model's instance list.
    inst.JFETnextInstance = shared_card.ucb.JFETinstances;
    shared_card.ucb.JFETinstances = &inst;

    // Remember the widest real node index for ghost array sizing.
    int32_t widest = -1;
    for (int32_t n : {n_drain, n_gate, n_source}) if (n > widest) widest = n;
    dev->max_neo_node_ = widest;

    return dev;
}

// ---------------------------------------------------------------------------
// declare_internal_nodes
// ---------------------------------------------------------------------------
void JFETDevice::declare_internal_nodes(Circuit& ckt) {
    ucb_declare_internal_nodes<Shim::Matrix, Shim::Ckt>(
        ckt, name_,
        [this](Shim::Matrix& m, Shim::Ckt& c) {
            UCB_SPLICE_INSTANCE(JFET);
            int states = 0;
            int rc = JFETsetup(&m, model_, &c, &states);
            UCB_UNSPLICE_INSTANCE(JFET);
            return rc;
        },
        "JFETsetup", journal_, max_neo_node_);
}

// ---------------------------------------------------------------------------
// stamp_pattern
// ---------------------------------------------------------------------------
void JFETDevice::stamp_pattern(SparsityBuilder& builder) const {
    ucb_stamp_pattern(journal_, builder);
}

// ---------------------------------------------------------------------------
// assign_offsets
// ---------------------------------------------------------------------------
void JFETDevice::assign_offsets(const SparsityPattern& pattern) {
    const auto offsets = ucb_compute_offsets(journal_, pattern);

#define RESOLVE(f)                                                       \
    do {                                                                 \
        if (inst_.f >= 0)                                                \
            inst_.f = offsets[inst_.f];                                  \
    } while (0)

    RESOLVE(JFETdrainDrainPrimePtr);
    RESOLVE(JFETgateDrainPrimePtr);
    RESOLVE(JFETgateSourcePrimePtr);
    RESOLVE(JFETsourceSourcePrimePtr);
    RESOLVE(JFETdrainPrimeDrainPtr);
    RESOLVE(JFETdrainPrimeGatePtr);
    RESOLVE(JFETdrainPrimeSourcePrimePtr);
    RESOLVE(JFETsourcePrimeGatePtr);
    RESOLVE(JFETsourcePrimeSourcePtr);
    RESOLVE(JFETsourcePrimeDrainPrimePtr);
    RESOLVE(JFETdrainDrainPtr);
    RESOLVE(JFETgateGatePtr);
    RESOLVE(JFETsourceSourcePtr);
    RESOLVE(JFETdrainPrimeDrainPrimePtr);
    RESOLVE(JFETsourcePrimeSourcePrimePtr);

#undef RESOLVE
}

// ---------------------------------------------------------------------------
// set_state_ptrs
// ---------------------------------------------------------------------------
void JFETDevice::set_state_ptrs(double* s0, double* s1, double* s2, int32_t base) {
    state0_ = s0;
    state1_ = s1;
    state2_ = s2;
    state_base_ = base;
    inst_.JFETstate = base;
}

// ---------------------------------------------------------------------------
// evaluate
// ---------------------------------------------------------------------------
void JFETDevice::evaluate(const std::vector<double>& voltages,
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
        ckt.CKTintegrateMethod = ic->integrate_method;
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

    // First-call JFETtemp.
    if (!temp_done_) {
        int rc = JFETtemp(model_, &ckt);
        if (rc != Shim::OK) {
            throw std::runtime_error("JFETtemp failed with rc=" + std::to_string(rc));
        }
        temp_done_ = true;
    }

    // Splice this instance as sole member, call load, then restore.
    JFETInstance* saved_head      = model_->JFETinstances;
    JFETInstance* saved_next_inst = inst_.JFETnextInstance;
    JFETModel*    saved_next_mod  = model_->JFETnextModel;
    model_->JFETinstances  = &inst_;
    inst_.JFETnextInstance = nullptr;
    model_->JFETnextModel  = nullptr;
    int rc = JFETload(model_, &ckt);
    model_->JFETinstances  = saved_head;
    inst_.JFETnextInstance = saved_next_inst;
    model_->JFETnextModel  = saved_next_mod;
    if (rc != Shim::OK) {
        throw std::runtime_error("JFETload failed with rc=" + std::to_string(rc));
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
bool JFETDevice::device_converged() const {
    return last_noncon_ == 0;
}

// ---------------------------------------------------------------------------
// set_ic — Initial conditions for VDS and VGS
// ---------------------------------------------------------------------------
void JFETDevice::set_ic(double vds, bool vds_given,
                        double vgs, bool vgs_given) {
    if (vds_given) { inst_.JFETicVDS = vds; inst_.JFETicVDSGiven = 1; }
    if (vgs_given) { inst_.JFETicVGS = vgs; inst_.JFETicVGSGiven = 1; }
}

// ---------------------------------------------------------------------------
// ac_stamp — linearized small-signal AC stamp (G/C matrix split)
//
// Translates ngspice jfetacld.c complex-matrix stamping into separate
// G (conductance) and C (capacitance) matrices.  The AC solver combines
// them as (G + jwC) at each frequency point.
// ---------------------------------------------------------------------------
void JFETDevice::ac_stamp(const std::vector<double>& /*voltages*/,
                          NumericMatrix& G, NumericMatrix& C) {
    auto& inst = inst_;
    const double m = inst.JFETm;

    // --- Conductance values ---
    const double gdpr = model_->JFETdrainConduct * inst.JFETarea;
    const double gspr = model_->JFETsourceConduct * inst.JFETarea;
    const double gm   = state0_[inst.JFETstate + 5];   // JFETgm
    const double gds  = state0_[inst.JFETstate + 6];   // JFETgds
    const double ggs  = state0_[inst.JFETstate + 7];   // JFETggs
    const double ggd  = state0_[inst.JFETstate + 8];   // JFETggd

    // --- Capacitance values (dQ/dV stored by NIintegrate) ---
    const double xgs  = state0_[inst.JFETstate + 9];   // JFETqgs (capacitance)
    const double xgd  = state0_[inst.JFETstate + 11];  // JFETqgd (capacitance)

    // --- G matrix stamps ---
    G.add(inst.JFETdrainDrainPtr,               m * gdpr);
    G.add(inst.JFETgateGatePtr,                 m * (ggd + ggs));
    G.add(inst.JFETsourceSourcePtr,             m * gspr);
    G.add(inst.JFETdrainPrimeDrainPrimePtr,     m * (gdpr + gds + ggd));
    G.add(inst.JFETsourcePrimeSourcePrimePtr,   m * (gspr + gds + gm + ggs));
    G.add(inst.JFETdrainDrainPrimePtr,          m * (-gdpr));
    G.add(inst.JFETgateDrainPrimePtr,           m * (-ggd));
    G.add(inst.JFETgateSourcePrimePtr,          m * (-ggs));
    G.add(inst.JFETsourceSourcePrimePtr,        m * (-gspr));
    G.add(inst.JFETdrainPrimeDrainPtr,          m * (-gdpr));
    G.add(inst.JFETdrainPrimeGatePtr,           m * (-ggd + gm));
    G.add(inst.JFETdrainPrimeSourcePrimePtr,    m * (-gds - gm));
    G.add(inst.JFETsourcePrimeGatePtr,          m * (-ggs - gm));
    G.add(inst.JFETsourcePrimeSourcePtr,        m * (-gspr));
    G.add(inst.JFETsourcePrimeDrainPrimePtr,    m * (-gds));

    // --- C matrix stamps ---
    C.add(inst.JFETgateGatePtr,                 m * (xgd + xgs));
    C.add(inst.JFETdrainPrimeDrainPrimePtr,     m * xgd);
    C.add(inst.JFETsourcePrimeSourcePrimePtr,   m * xgs);
    C.add(inst.JFETgateDrainPrimePtr,           m * (-xgd));
    C.add(inst.JFETgateSourcePrimePtr,          m * (-xgs));
    C.add(inst.JFETdrainPrimeGatePtr,           m * (-xgd));
    C.add(inst.JFETsourcePrimeGatePtr,          m * (-xgs));
}

// ---------------------------------------------------------------------------
// compute_trunc — device-specific local truncation error for time stepping
// ---------------------------------------------------------------------------
double JFETDevice::compute_trunc(const IntegratorCtx& ctx,
                                 const SimOptions& opts) const {
    if (ctx.order < 2 || ctx.delta <= 0.0)
        return 1e30;

    if (!state0_ || !state1_ || !state2_)
        return 1e30;

    const double lte_coeff = ctx.lte_coefficient();
    const double h0 = ctx.delta;
    const double h1 = ctx.delta_old[1];
    if (h1 <= 0.0) return 1e30;

    // Charge offsets: qgs=9, qgd=11
    static const int charge_offsets[] = {9, 11};
    double dt_min = 1e30;

    for (int rel : charge_offsets) {
        int qcap = state_base_ + rel;
        double q0 = state0_[qcap], q1 = state1_[qcap], q2 = state2_[qcap];
        double dd1 = (q0 - q1) / h0;
        double dd2 = ((q0 - q1) / h0 - (q1 - q2) / h1) / (h0 + h1);
        double tol = opts.chgtol + opts.reltol * std::max(std::abs(q0), std::abs(q1));
        if (tol <= 0.0) continue;
        if (std::abs(dd2) > 1e-30) {
            double del = opts.trtol * tol / (lte_coeff * std::abs(dd2));
            del = std::sqrt(del);
            dt_min = std::min(dt_min, del);
        }
    }
    return dt_min;
}

// ---------------------------------------------------------------------------
// noise_sources — JFET noise contributions (channel thermal + flicker)
//
// Sources (following ngspice jfetnoi.c):
//   1. Channel thermal + flicker:  S_id = 4kT*(2/3)*gm + KF*|Id|^AF/f
//                                  (drain_prime <-> source_prime)
//   2. Gate shot noise:            S_ig = 2*q*|Ig|   (gate <-> source_prime)
//
// UCB nodes are 1-based (0=ground); neospice nodes are 0-based (-1=ground).
// ---------------------------------------------------------------------------
std::vector<Device::NoiseSource> JFETDevice::noise_sources(
        double freq, const std::vector<double>& /*dc_solution*/) const {
    if (!state0_ || state_base_ < 0)
        return {};

    std::vector<NoiseSource> sources;

    // Neospice node indices (ucb-1): -1 is ground.
    const int32_t gate_node         = inst_.JFETgateNode         - 1;
    const int32_t drain_prime_node  = inst_.JFETdrainPrimeNode   - 1;
    const int32_t source_prime_node = inst_.JFETsourcePrimeNode  - 1;

    const double T = T_NOMINAL;   // device temperature
    const double m = inst_.JFETm; // parallel multiplier

    // DC operating point from state vector.
    const double gm = std::abs(state0_[state_base_ + 5]);  // JFETgm  = JFETstate+5
    const double Id = std::abs(state0_[state_base_ + 3]);  // JFETcd  = JFETstate+3
    const double Ig = std::abs(state0_[state_base_ + 2]);  // JFETcg  = JFETstate+2

    // --- 1. Channel thermal noise + flicker noise ---
    double S_ch = 4.0 * BOLTZMANN * T * (2.0 / 3.0) * gm;
    const double KF = model_->JFETfNcoef;   // flicker noise coefficient (default 0)
    const double AF = model_->JFETfNexp;    // flicker noise exponent (default 1)
    if (KF > 0.0 && freq > 0.0 && Id > 0.0) {
        S_ch += KF * std::pow(Id, AF) / freq;
    }
    sources.push_back({drain_prime_node, source_prime_node, m * S_ch});

    // --- 2. Gate shot noise (gate leakage) ---
    if (Ig > 0.0) {
        sources.push_back({gate_node, source_prime_node,
                           m * 2.0 * CHARGE_Q * Ig});
    }

    return sources;
}

// ---------------------------------------------------------------------------
// query_param — post-simulation parameter query
// ---------------------------------------------------------------------------
std::optional<double>
JFETDevice::query_param(const std::string& name) const {
    const std::string key = str_tolower(name);
    const double m = inst_.JFETm;

    // --- Operating-point parameters from state vector ---
    if (state0_ && state_base_ >= 0) {
        if (key == "vgs")  return state0_[inst_.JFETstate + 0];
        if (key == "vgd")  return state0_[inst_.JFETstate + 1];
        if (key == "id")   return state0_[inst_.JFETstate + 3] * m;
        if (key == "ig")   return state0_[inst_.JFETstate + 2] * m;
        if (key == "gm")   return state0_[inst_.JFETstate + 5] * m;
        if (key == "gds")  return state0_[inst_.JFETstate + 6] * m;
    }

    // --- Geometry (no multiplier) ---
    if (key == "area") return inst_.JFETarea;
    if (key == "m")    return inst_.JFETm;

    return std::nullopt;
}

} // namespace neospice
