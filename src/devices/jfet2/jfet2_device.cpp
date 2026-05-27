#include "devices/jfet2/jfet2_device.hpp"
#include "devices/jfet2/jfet2_psmodel.hpp"

#include "core/circuit.hpp"        // Circuit::node, tls_integrator_ctx
#include "core/types.hpp"          // SimOptions defaults
#include "devices/ucb_device_init.hpp"
#include "devices/ucb_utils.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>
#include "devices/ckt_terr.hpp"

// Forward declarations for translated UCB functions.
namespace neospice::jfet2 {
    int JFET2setup(Shim::Matrix*, JFET2Model*, Shim::Ckt*, int*);
    int JFET2temp(JFET2Model*, Shim::Ckt*);
    int JFET2load(JFET2Model*, Shim::Ckt*);
}

namespace neospice {

using namespace neospice::jfet2;

// ---------------------------------------------------------------------------
// JFET2ModelCard destructor
// ---------------------------------------------------------------------------
JFET2ModelCard::~JFET2ModelCard() = default;

// ---------------------------------------------------------------------------
// make
// ---------------------------------------------------------------------------
std::unique_ptr<JFET2Device>
JFET2Device::make(std::string name,
        int32_t n_drain, int32_t n_gate, int32_t n_source,
        const Geom& geom, JFET2ModelCard& shared_card) {
    std::unique_ptr<JFET2Device> dev(new JFET2Device(std::move(name)));
    dev->model_ = &shared_card.ucb;
    dev->ext_nodes_ = {n_drain, n_gate, n_source};

    auto& inst = dev->inst_;
    inst.JFET2name = dev->name().c_str();
    inst.JFET2modPtr = dev->model_;

    // Node wiring (UCB convention).
    inst.JFET2drainNode = neo_to_ucb(n_drain);
    inst.JFET2gateNode = neo_to_ucb(n_gate);
    inst.JFET2sourceNode = neo_to_ucb(n_source);

    // Geometry.
    inst.JFET2area = geom.area;
    inst.JFET2areaGiven = geom.area_given ? 1 : 0;
    inst.JFET2m = geom.m;
    inst.JFET2mGiven = geom.m_given ? 1 : 0;

    // Thread onto the shared model's instance list.
    inst.JFET2nextInstance = shared_card.ucb.JFET2instances;
    shared_card.ucb.JFET2instances = &inst;

    // Remember the widest real node index for ghost array sizing.
    int32_t widest = -1;
    for (int32_t n : {n_drain, n_gate, n_source}) if (n > widest) widest = n;
    dev->max_neo_node_ = widest;

    return dev;
}

// ---------------------------------------------------------------------------
// declare_internal_nodes
// ---------------------------------------------------------------------------
void JFET2Device::declare_internal_nodes(Circuit& ckt) {
    ucb_declare_internal_nodes<Shim::Matrix, Shim::Ckt>(
        ckt, name_,
        [this](Shim::Matrix& m, Shim::Ckt& c) {
            UCB_SPLICE_INSTANCE(JFET2);
            int states = 0;
            int rc = JFET2setup(&m, model_, &c, &states);
            UCB_UNSPLICE_INSTANCE(JFET2);
            return rc;
        },
        "JFET2setup", journal_, max_neo_node_);
}

// ---------------------------------------------------------------------------
// stamp_pattern
// ---------------------------------------------------------------------------
void JFET2Device::stamp_pattern(SparsityBuilder& builder) const {
    ucb_stamp_pattern(journal_, builder);
}

// ---------------------------------------------------------------------------
// assign_offsets
// ---------------------------------------------------------------------------
void JFET2Device::assign_offsets(const SparsityPattern& pattern) {
    const auto offsets = ucb_compute_offsets(journal_, pattern);

#define RESOLVE(f)                                                       \
    do {                                                                 \
        if (inst_.f >= 0)                                                \
            inst_.f = offsets[inst_.f];                                  \
    } while (0)

    RESOLVE(JFET2drainDrainPrimePtr);
    RESOLVE(JFET2gateDrainPrimePtr);
    RESOLVE(JFET2gateSourcePrimePtr);
    RESOLVE(JFET2sourceSourcePrimePtr);
    RESOLVE(JFET2drainPrimeDrainPtr);
    RESOLVE(JFET2drainPrimeGatePtr);
    RESOLVE(JFET2drainPrimeSourcePrimePtr);
    RESOLVE(JFET2sourcePrimeGatePtr);
    RESOLVE(JFET2sourcePrimeSourcePtr);
    RESOLVE(JFET2sourcePrimeDrainPrimePtr);
    RESOLVE(JFET2drainDrainPtr);
    RESOLVE(JFET2gateGatePtr);
    RESOLVE(JFET2sourceSourcePtr);
    RESOLVE(JFET2drainPrimeDrainPrimePtr);
    RESOLVE(JFET2sourcePrimeSourcePrimePtr);

#undef RESOLVE
}

// ---------------------------------------------------------------------------
// set_state_ptrs
// ---------------------------------------------------------------------------
void JFET2Device::set_state_ptrs(double* s0, double* s1, double* s2, double* s3, int32_t base) {
    state0_ = s0;
    state1_ = s1;
    state2_ = s2;
    state3_ = s3;
    state_base_ = base;
    inst_.JFET2state = base;
}

// ---------------------------------------------------------------------------
// evaluate
// ---------------------------------------------------------------------------
void JFET2Device::evaluate(const std::vector<double>& voltages,
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

    // First-call JFET2temp.
    if (!temp_done_) {
        int rc = JFET2temp(model_, &ckt);
        if (rc != Shim::OK) {
            throw std::runtime_error("JFET2temp failed with rc=" + std::to_string(rc));
        }
        temp_done_ = true;
    }

    // Splice this instance as sole member, call load, then restore.
    JFET2Instance* saved_head      = model_->JFET2instances;
    JFET2Instance* saved_next_inst = inst_.JFET2nextInstance;
    JFET2Model*    saved_next_mod  = model_->JFET2nextModel;
    model_->JFET2instances  = &inst_;
    inst_.JFET2nextInstance = nullptr;
    model_->JFET2nextModel  = nullptr;
    int rc = JFET2load(model_, &ckt);
    model_->JFET2instances  = saved_head;
    inst_.JFET2nextInstance = saved_next_inst;
    model_->JFET2nextModel  = saved_next_mod;
    if (rc != Shim::OK) {
        throw std::runtime_error("JFET2load failed with rc=" + std::to_string(rc));
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
bool JFET2Device::device_converged() const {
    return last_noncon_ == 0;
}

// ---------------------------------------------------------------------------
// set_ic
// ---------------------------------------------------------------------------
void JFET2Device::set_ic(double vds, bool vds_given,
                          double vgs, bool vgs_given) {
    if (vds_given) { inst_.JFET2icVDS = vds; inst_.JFET2icVDSGiven = 1; }
    if (vgs_given) { inst_.JFET2icVGS = vgs; inst_.JFET2icVGSGiven = 1; }
}

// ---------------------------------------------------------------------------
// ac_stamp — linearized small-signal AC stamp (G/C matrix split)
//
// Translates ngspice jfet2acld.c complex-matrix stamping into separate
// G (conductance) and C (capacitance) matrices.
//
// The JFET2 AC model uses PSacload() which produces frequency-dependent
// small-signal parameters (gm, xgm, gds, xgds) due to the rate-dependent
// feedback time constants (taug, taud). The real parts go into G and
// imaginary parts go into C (divided by omega).
// ---------------------------------------------------------------------------
void JFET2Device::ac_stamp(const std::vector<double>& /*voltages*/,
                            NumericMatrix& G, NumericMatrix& C) {
    auto& inst = inst_;
    const double m = inst.JFET2m;

    // --- Conductance values ---
    const double gdpr = model_->JFET2drainConduct * inst.JFET2area;
    const double gspr = model_->JFET2sourceConduct * inst.JFET2area;
    double gm   = state0_[inst.JFET2state + 5];   // JFET2gm
    double gds  = state0_[inst.JFET2state + 6];   // JFET2gds
    const double ggs  = state0_[inst.JFET2state + 7];   // JFET2ggs
    const double ggd  = state0_[inst.JFET2state + 8];   // JFET2ggd

    // --- Capacitance values (stored by MODEINITSMSIG) ---
    const double capgs = state0_[inst.JFET2state + 9];   // JFET2qgs (cap in smsig mode)
    const double capgd = state0_[inst.JFET2state + 11];  // JFET2qgd (cap in smsig mode)
    const double capds = state0_[inst.JFET2state + 13];  // JFET2qds (cap in smsig mode)

    // Note: For the JFET2 Parker-Skellern model, PSacload produces
    // frequency-dependent gm and gds due to taug and taud time constants.
    // In the G/C split approach, we put the DC values into G and the
    // capacitance contributions into C. The frequency-dependent corrections
    // from PSacload are omitted since they can't be represented in the G/C
    // framework (they would require complex frequency-dependent conductances).
    // This is accurate when taug=0 and taud=0 (no rate-dependent feedback).

    // --- G matrix stamps (same topology as DC stamps) ---
    G.add(inst.JFET2drainDrainPtr,               m * gdpr);
    G.add(inst.JFET2gateGatePtr,                 m * (ggd + ggs));
    G.add(inst.JFET2sourceSourcePtr,             m * gspr);
    G.add(inst.JFET2drainPrimeDrainPrimePtr,     m * (gdpr + gds + ggd));
    G.add(inst.JFET2sourcePrimeSourcePrimePtr,   m * (gspr + gds + gm + ggs));
    G.add(inst.JFET2drainDrainPrimePtr,          m * (-gdpr));
    G.add(inst.JFET2gateDrainPrimePtr,           m * (-ggd));
    G.add(inst.JFET2gateSourcePrimePtr,          m * (-ggs));
    G.add(inst.JFET2sourceSourcePrimePtr,        m * (-gspr));
    G.add(inst.JFET2drainPrimeDrainPtr,          m * (-gdpr));
    G.add(inst.JFET2drainPrimeGatePtr,           m * (-ggd + gm));
    G.add(inst.JFET2drainPrimeSourcePrimePtr,    m * (-gds - gm));
    G.add(inst.JFET2sourcePrimeGatePtr,          m * (-ggs - gm));
    G.add(inst.JFET2sourcePrimeSourcePtr,        m * (-gspr));
    G.add(inst.JFET2sourcePrimeDrainPrimePtr,    m * (-gds));

    // --- C matrix stamps (capacitance contributions) ---
    C.add(inst.JFET2gateGatePtr,                 m * (capgd + capgs));
    C.add(inst.JFET2drainPrimeDrainPrimePtr,     m * (capgd + capds));
    C.add(inst.JFET2sourcePrimeSourcePrimePtr,   m * (capgs + capds));
    C.add(inst.JFET2gateDrainPrimePtr,           m * (-capgd));
    C.add(inst.JFET2gateSourcePrimePtr,          m * (-capgs));
    C.add(inst.JFET2drainPrimeGatePtr,           m * (-capgd));
    C.add(inst.JFET2sourcePrimeGatePtr,          m * (-capgs));
    C.add(inst.JFET2drainPrimeSourcePrimePtr,    m * (-capds));
    C.add(inst.JFET2sourcePrimeDrainPrimePtr,    m * (-capds));
}

// ---------------------------------------------------------------------------
// compute_trunc — device-specific local truncation error
// From jfet2trun.c: CKTterr on qgs and qgd charges.
// ---------------------------------------------------------------------------
double JFET2Device::compute_trunc(const IntegratorCtx& ctx,
                                   const SimOptions& opts) const {
    if (ctx.order < 1 || ctx.delta <= 0.0)
        return 1e30;
    if (!state0_ || !state1_ || !state2_ || !state3_)
        return 1e30;

    const double* states[] = {state0_, state1_, state2_, state3_};
    double dt_min = 1e30;
    static const int charge_offsets[] = {9, 11};  // qgs, qgd
    for (int rel : charge_offsets)
        ckt_terr(state_base_ + rel, states, ctx, opts, dt_min);
    return dt_min;
}

// ---------------------------------------------------------------------------
// noise_sources — JFET2 noise contributions
//
// From jfet2noi.c:
//   1. Rd thermal noise: drain_prime <-> drain
//   2. Rs thermal noise: source_prime <-> source
//   3. Channel thermal noise: 4kT*(2/3)*gm (drain_prime <-> source_prime)
//   4. Flicker noise: KF*|Id|^AF/f (drain_prime <-> source_prime)
// ---------------------------------------------------------------------------
std::vector<Device::NoiseSource> JFET2Device::noise_sources(
        double freq, const std::vector<double>& /*dc_solution*/) const {
    if (!state0_ || state_base_ < 0)
        return {};

    std::vector<NoiseSource> sources;

    const double T = sim_temp();
    const double m = inst_.JFET2m;

    // Neospice node indices (ucb-1): -1 is ground.
    const int32_t drain_node         = inst_.JFET2drainNode       - 1;
    const int32_t source_node        = inst_.JFET2sourceNode      - 1;
    const int32_t drain_prime_node   = inst_.JFET2drainPrimeNode  - 1;
    const int32_t source_prime_node  = inst_.JFET2sourcePrimeNode - 1;

    // DC operating point from state vector.
    const double gm = std::abs(state0_[state_base_ + 5]);  // JFET2gm
    const double Id = std::abs(state0_[state_base_ + 3]);  // JFET2cd

    // 1. Rd thermal noise
    const double Gd = model_->JFET2drainConduct * inst_.JFET2area;
    if (Gd > 0.0) {
        sources.push_back({drain_prime_node, drain_node,
                           m * 4.0 * BOLTZMANN * T * Gd});
    }

    // 2. Rs thermal noise
    const double Gs = model_->JFET2sourceConduct * inst_.JFET2area;
    if (Gs > 0.0) {
        sources.push_back({source_prime_node, source_node,
                           m * 4.0 * BOLTZMANN * T * Gs});
    }

    // 3. Channel thermal noise: 4kT*(2/3)*|gm|
    sources.push_back({drain_prime_node, source_prime_node,
                       m * 4.0 * BOLTZMANN * T * (2.0 / 3.0) * gm});

    // 4. Flicker noise: KF * |Id|^AF / f
    const double KF = model_->JFET2fNcoef;
    const double AF = model_->JFET2fNexp;
    if (KF > 0.0 && freq > 0.0 && Id > 0.0) {
        double S_flicker = KF * std::pow(Id, AF) / freq;
        sources.push_back({drain_prime_node, source_prime_node, m * S_flicker});
    }

    return sources;
}

// ---------------------------------------------------------------------------
// query_param — post-simulation parameter query
// ---------------------------------------------------------------------------
std::optional<double>
JFET2Device::query_param(const std::string& name) const {
    const std::string key = str_tolower(name);
    const double m = inst_.JFET2m;

    // --- Operating-point parameters from state vector ---
    if (state0_ && state_base_ >= 0) {
        if (key == "vgs")  return state0_[inst_.JFET2state + 0];
        if (key == "vgd")  return state0_[inst_.JFET2state + 1];
        if (key == "vds")  return state0_[inst_.JFET2state + 0] - state0_[inst_.JFET2state + 1];
        if (key == "ig")   return state0_[inst_.JFET2state + 2] * m;
        if (key == "id")   return state0_[inst_.JFET2state + 3] * m;
        if (key == "igd")  return state0_[inst_.JFET2state + 4] * m;
        if (key == "gm")   return state0_[inst_.JFET2state + 5] * m;
        if (key == "gds")  return state0_[inst_.JFET2state + 6] * m;
        if (key == "ggs")  return state0_[inst_.JFET2state + 7] * m;
        if (key == "ggd")  return state0_[inst_.JFET2state + 8] * m;
    }

    // --- Geometry (not scaled by m) ---
    if (key == "area") return inst_.JFET2area;
    if (key == "m")    return inst_.JFET2m;

    return std::nullopt;
}

} // namespace neospice
