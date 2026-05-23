#include "devices/dio/dio_device.hpp"

#include "core/circuit.hpp"        // Circuit::node, tls_integrator_ctx
#include "core/types.hpp"          // SimOptions defaults
#include "devices/ucb_device_init.hpp"
#include "devices/ucb_utils.hpp"
#include <cmath>
#include <optional>
#include <stdexcept>
#include <string>

// Forward declarations for translated UCB functions.
namespace neospice::dio {
    int DIOsetup(Shim::Matrix*, DIOModel*, Shim::Ckt*, int*);
    int DIOtemp(DIOModel*, Shim::Ckt*);
    int DIOload(DIOModel*, Shim::Ckt*);
}

namespace neospice {

using namespace neospice::dio;

// ---------------------------------------------------------------------------
// DIOModelCard destructor
// ---------------------------------------------------------------------------
DIOModelCard::~DIOModelCard() = default;

// ---------------------------------------------------------------------------
// make
// ---------------------------------------------------------------------------
std::unique_ptr<DIODevice>
DIODevice::make(std::string name,
        int32_t n_pos, int32_t n_neg,
        const Geom& geom, DIOModelCard& shared_card) {
    std::unique_ptr<DIODevice> dev(new DIODevice(std::move(name)));
    dev->model_ = &shared_card.ucb;

    auto& inst = dev->inst_;
    inst.DIOname = dev->name().c_str();
    inst.DIOmodPtr = dev->model_;

    // Node wiring (UCB convention).
    inst.DIOposNode = neo_to_ucb(n_pos);
    inst.DIOnegNode = neo_to_ucb(n_neg);

    // Geometry.
    inst.DIOarea = geom.area;
    inst.DIOareaGiven = (geom.area != 1.0) ? 1 : 0;
    inst.DIOpj = geom.pj;
    inst.DIOpjGiven = (geom.pj != 0.0) ? 1 : 0;
    inst.DIOw = geom.w;
    inst.DIOwGiven = (geom.w != 0.0) ? 1 : 0;
    inst.DIOl = geom.l;
    inst.DIOlGiven = (geom.l != 0.0) ? 1 : 0;
    inst.DIOm = geom.m;
    inst.DIOmGiven = (geom.m != 1.0) ? 1 : 0;

    // Thread onto the shared model's instance list.
    inst.DIOnextInstance = shared_card.ucb.DIOinstances;
    shared_card.ucb.DIOinstances = &inst;

    // Remember the widest real node index for ghost array sizing.
    int32_t widest = -1;
    for (int32_t n : {n_pos, n_neg}) if (n > widest) widest = n;
    dev->max_neo_node_ = widest;

    return dev;
}

// ---------------------------------------------------------------------------
// declare_internal_nodes
// ---------------------------------------------------------------------------
void DIODevice::declare_internal_nodes(Circuit& ckt) {
    ucb_declare_internal_nodes<Shim::Matrix, Shim::Ckt>(
        ckt, name_,
        [this](Shim::Matrix& m, Shim::Ckt& c) {
            UCB_SPLICE_INSTANCE(DIO);
            int states = 0;
            int rc = DIOsetup(&m, model_, &c, &states);
            UCB_UNSPLICE_INSTANCE(DIO);
            return rc;
        },
        "DIOsetup", journal_, max_neo_node_);
}

// ---------------------------------------------------------------------------
// stamp_pattern
// ---------------------------------------------------------------------------
void DIODevice::stamp_pattern(SparsityBuilder& builder) const {
    ucb_stamp_pattern(journal_, builder);
}

// ---------------------------------------------------------------------------
// assign_offsets
// ---------------------------------------------------------------------------
void DIODevice::assign_offsets(const SparsityPattern& pattern) {
    const auto offsets = ucb_compute_offsets(journal_, pattern);

#define RESOLVE(f)                                                       \
    do {                                                                 \
        if (inst_.f >= 0)                                                \
            inst_.f = offsets[inst_.f];                                  \
    } while (0)

    RESOLVE(DIOposPosPrimePtr);
    RESOLVE(DIOnegPosPrimePtr);
    RESOLVE(DIOposPrimePosPtr);
    RESOLVE(DIOposPrimeNegPtr);
    RESOLVE(DIOposPosPtr);
    RESOLVE(DIOnegNegPtr);
    RESOLVE(DIOposPrimePosPrimePtr);

#undef RESOLVE
}

// ---------------------------------------------------------------------------
// set_state_ptrs
// ---------------------------------------------------------------------------
void DIODevice::set_state_ptrs(double* s0, double* s1, double* s2, int32_t base) {
    state0_ = s0;
    state1_ = s1;
    state2_ = s2;
    state_base_ = base;
    inst_.DIOstate = base;
}

// ---------------------------------------------------------------------------
// evaluate
// ---------------------------------------------------------------------------
void DIODevice::evaluate(const std::vector<double>& voltages,
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

    // First-call DIOtemp.
    if (!temp_done_) {
        int rc = DIOtemp(model_, &ckt);
        if (rc != Shim::OK) {
            throw std::runtime_error("DIOtemp failed with rc=" + std::to_string(rc));
        }
        temp_done_ = true;
    }

    // Splice this instance as sole member, call load, then restore.
    DIOInstance* saved_head      = model_->DIOinstances;
    DIOInstance* saved_next_inst = inst_.DIOnextInstance;
    DIOModel*    saved_next_mod  = model_->DIOnextModel;
    model_->DIOinstances  = &inst_;
    inst_.DIOnextInstance = nullptr;
    model_->DIOnextModel  = nullptr;
    int rc = DIOload(model_, &ckt);
    model_->DIOinstances  = saved_head;
    inst_.DIOnextInstance = saved_next_inst;
    model_->DIOnextModel  = saved_next_mod;
    if (rc != Shim::OK) {
        throw std::runtime_error("DIOload failed with rc=" + std::to_string(rc));
    }

    last_noncon_ = ckt.CKTnoncon;

    // Fold ghost rhs contributions back into the real rhs.
    for (int32_t k = 1; k <= max_neo_node_ + 1 && k < n_ghost; ++k) {
        if (k - 1 < n_real) rhs[k - 1] += ghost_rhs_[k];
    }
}

// ---------------------------------------------------------------------------
// ac_stamp — G/C matrix split from ngspice dioacld.c
//
// Diode AC model:
//   pos ─── gspr ─── posPrime ─── geq ─── neg
//                                 capd
// G matrix: gspr (series conductance), geq (junction small-signal conductance)
// C matrix: capd (junction + diffusion capacitance, stored in DIOcapCurrent)
// ---------------------------------------------------------------------------
void DIODevice::ac_stamp(const std::vector<double>& /*voltages*/,
                           NumericMatrix& G, NumericMatrix& C) {
    const double gspr = inst_.DIOtConductance * inst_.DIOarea;
    const double geq  = state0_[state_base_ + 2];   // DIOconduct
    const double capd = state0_[state_base_ + 4];   // DIOcapCurrent (stores cap after SMSIG)

    auto add_G = [&](int off, double val) { if (off >= 0) G.add(off, val); };
    auto add_C = [&](int off, double val) { if (off >= 0) C.add(off, val); };

    add_G(inst_.DIOposPosPtr,               gspr);
    add_G(inst_.DIOnegNegPtr,               geq);
    add_G(inst_.DIOposPrimePosPrimePtr,     geq + gspr);
    add_G(inst_.DIOposPosPrimePtr,          -gspr);
    add_G(inst_.DIOnegPosPrimePtr,          -geq);
    add_G(inst_.DIOposPrimePosPtr,          -gspr);
    add_G(inst_.DIOposPrimeNegPtr,          -geq);

    add_C(inst_.DIOnegNegPtr,               capd);
    add_C(inst_.DIOposPrimePosPrimePtr,     capd);
    add_C(inst_.DIOnegPosPrimePtr,          -capd);
    add_C(inst_.DIOposPrimeNegPtr,          -capd);
}

// ---------------------------------------------------------------------------
// compute_trunc — LTE on DIOcapCharge (offset 3)
// ---------------------------------------------------------------------------
double DIODevice::compute_trunc(const IntegratorCtx& ctx,
                             const SimOptions& opts) const {
    if (ctx.order < 2 || ctx.delta <= 0.0)
        return 1e30;
    if (!state0_ || !state1_ || !state2_)
        return 1e30;

    const double h0 = ctx.delta;
    const double h1 = ctx.delta_old[1];
    if (h1 <= 0.0) return 1e30;

    const int qcap = state_base_ + 3;  // DIOcapCharge
    const double q0 = state0_[qcap], q1 = state1_[qcap], q2 = state2_[qcap];
    const double dd2 = ((q0 - q1) / h0 - (q1 - q2) / h1) / (h0 + h1);
    const double tol = opts.chgtol + opts.reltol * std::max(std::abs(q0), std::abs(q1));
    if (tol <= 0.0 || std::abs(dd2) <= 1e-30)
        return 1e30;

    const double lte_coeff = ctx.lte_coefficient();
    return std::sqrt(opts.trtol * tol / (lte_coeff * std::abs(dd2)));
}

// ---------------------------------------------------------------------------
// device_converged
// ---------------------------------------------------------------------------
bool DIODevice::device_converged() const {
    return last_noncon_ == 0;
}

// ---------------------------------------------------------------------------
// query_param — operating-point parameter query
// ---------------------------------------------------------------------------
std::optional<double> DIODevice::query_param(const std::string& name) const {
    const std::string key = str_tolower(name);
    const double m = inst_.DIOm;

    if (state0_ && state_base_ >= 0) {
        if (key == "vd" || key == "v")     return state0_[state_base_ + 0];
        if (key == "id" || key == "c")     return state0_[state_base_ + 1] * m;
        if (key == "gd" || key == "conduct") return state0_[state_base_ + 2] * m;
        if (key == "cd" || key == "charge")  return state0_[state_base_ + 3] * m;
        if (key == "cap" || key == "capcur") return state0_[state_base_ + 4] * m;
    }

    if (key == "area") return inst_.DIOarea;
    if (key == "pj")   return inst_.DIOpj;
    if (key == "m")    return inst_.DIOm;

    return std::nullopt;
}

// ---------------------------------------------------------------------------
// noise_sources — diode noise (thermal + shot + flicker)
//
// From ngspice dionoise.c:
//   1. Series resistance thermal: 4kT * Gconductance * area * m
//      between pos and posPrime
//   2. Junction shot noise: 2*q*|Id|  between posPrime and neg
//   3. Flicker (1/f) noise: KF * |Id/m|^AF / f * m  between posPrime and neg
// ---------------------------------------------------------------------------
std::vector<Device::NoiseSource> DIODevice::noise_sources(
        double freq, const std::vector<double>& /*dc_solution*/) const {
    if (!state0_ || state_base_ < 0)
        return {};

    std::vector<NoiseSource> sources;

    const int32_t pos_node       = inst_.DIOposNode      - 1;
    const int32_t neg_node       = inst_.DIOnegNode       - 1;
    const int32_t pos_prime_node = inst_.DIOposPrimeNode  - 1;

    const double T = sim_temp();
    const double m = inst_.DIOm;

    // Series resistance thermal noise: S = 4kT * G_series * area * m
    const double Gs = inst_.DIOtConductance * inst_.DIOarea * m;
    if (Gs > 0.0) {
        sources.push_back({pos_prime_node, pos_node,
                           4.0 * BOLTZMANN * T * Gs});
    }

    // Junction current (from state: DIOcurrent = state_base_ + 1)
    const double Id = state0_[state_base_ + 1];

    // Shot noise: S = 2*q*|Id|
    sources.push_back({pos_prime_node, neg_node,
                       2.0 * CHARGE_Q * std::abs(Id)});

    // Flicker noise: S = KF * |Id/m|^AF / f^EF * m
    const double KF = model_->DIOfNcoef;
    const double AF = model_->DIOfNexp;
    const double EF = model_->DIOfNfreqExp;
    if (KF > 0.0 && freq > 0.0) {
        const double Iabs = std::abs(Id / m);
        double S_flicker = KF * std::pow(std::max(Iabs, 1e-38), AF)
                         / std::pow(freq, EF) * m;
        sources.push_back({pos_prime_node, neg_node, S_flicker});
    }

    return sources;
}

// ---------------------------------------------------------------------------
// set_ic — initial condition for diode voltage
// ---------------------------------------------------------------------------
void DIODevice::set_ic(double vd, bool vd_given) {
    if (vd_given) {
        inst_.DIOinitCond = vd;
        inst_.DIOinitCondGiven = 1;
    }
}

} // namespace neospice
