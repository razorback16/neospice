#include "devices/bjt/bjt_device.hpp"

#include "core/circuit.hpp"        // Circuit::node, tls_integrator_ctx
#include "core/types.hpp"          // SimOptions defaults
#include "devices/ucb_device_init.hpp"
#include "devices/ucb_utils.hpp"

#include <cmath>
#include <stdexcept>
#include <string>
#include "devices/ckt_terr.hpp"

// Forward declarations for translated UCB functions.
namespace neospice::bjt {
    int BJTsetup(Shim::Matrix*, BJTModel*, Shim::Ckt*, int*);
    int BJTtemp(BJTModel*, Shim::Ckt*);
    int BJTload(BJTModel*, Shim::Ckt*);
}

namespace neospice {

using namespace neospice::bjt;

// ---------------------------------------------------------------------------
// BJTModelCard destructor
// ---------------------------------------------------------------------------
BJTModelCard::~BJTModelCard() = default;

// ---------------------------------------------------------------------------
// make
// ---------------------------------------------------------------------------
std::unique_ptr<BJTDevice>
BJTDevice::make(std::string name,
        int32_t n_col, int32_t n_base, int32_t n_emit, int32_t n_subst,
        const Geom& geom, BJTModelCard& shared_card) {
    std::unique_ptr<BJTDevice> dev(new BJTDevice(std::move(name)));
    dev->model_ = &shared_card.ucb;

    auto& inst = dev->inst_;
    inst.BJTname = dev->name().c_str();
    inst.BJTmodPtr = dev->model_;

    dev->ext_nodes_ = {n_col, n_base, n_emit, n_subst};

    // Node wiring (UCB convention).
    inst.BJTcolNode = neo_to_ucb(n_col);
    inst.BJTbaseNode = neo_to_ucb(n_base);
    inst.BJTemitNode = neo_to_ucb(n_emit);
    inst.BJTsubstNode = neo_to_ucb(n_subst);

    // Geometry.
    inst.BJTarea = geom.area;
    inst.BJTareaGiven = geom.area_given ? 1 : 0;
    inst.BJTareab = geom.areab;
    inst.BJTareabGiven = geom.areab_given ? 1 : 0;
    inst.BJTareac = geom.areac;
    inst.BJTareacGiven = geom.areac_given ? 1 : 0;
    inst.BJTm = geom.m;
    inst.BJTmGiven = geom.m_given ? 1 : 0;

    // Thread onto the shared model's instance list.
    inst.BJTnextInstance = shared_card.ucb.BJTinstances;
    shared_card.ucb.BJTinstances = &inst;

    // Remember the widest real node index for ghost array sizing.
    int32_t widest = -1;
    for (int32_t n : {n_col, n_base, n_emit, n_subst}) if (n > widest) widest = n;
    dev->max_neo_node_ = widest;

    return dev;
}

// ---------------------------------------------------------------------------
// declare_internal_nodes
// ---------------------------------------------------------------------------
void BJTDevice::declare_internal_nodes(Circuit& ckt) {
    ucb_declare_internal_nodes<Shim::Matrix, Shim::Ckt>(
        ckt, name_,
        [this](Shim::Matrix& m, Shim::Ckt& c) {
            UCB_SPLICE_INSTANCE(BJT);
            int states = 0;
            int rc = BJTsetup(&m, model_, &c, &states);
            UCB_UNSPLICE_INSTANCE(BJT);
            return rc;
        },
        "BJTsetup", journal_, max_neo_node_);
}

// ---------------------------------------------------------------------------
// stamp_pattern
// ---------------------------------------------------------------------------
void BJTDevice::stamp_pattern(SparsityBuilder& builder) const {
    ucb_stamp_pattern(journal_, builder);
}

// ---------------------------------------------------------------------------
// assign_offsets
// ---------------------------------------------------------------------------
void BJTDevice::assign_offsets(const SparsityPattern& pattern) {
    const auto offsets = ucb_compute_offsets(journal_, pattern);

#define RESOLVE(f)                                                       \
    do {                                                                 \
        if (inst_.f >= 0)                                                \
            inst_.f = offsets[inst_.f];                                  \
    } while (0)

    RESOLVE(BJTcolColPrimePtr);
    RESOLVE(BJTbaseBasePrimePtr);
    RESOLVE(BJTemitEmitPrimePtr);
    RESOLVE(BJTcolPrimeColPtr);
    RESOLVE(BJTcolPrimeBasePrimePtr);
    RESOLVE(BJTcolPrimeEmitPrimePtr);
    RESOLVE(BJTbasePrimeBasePtr);
    RESOLVE(BJTbasePrimeColPrimePtr);
    RESOLVE(BJTbasePrimeEmitPrimePtr);
    RESOLVE(BJTemitPrimeEmitPtr);
    RESOLVE(BJTemitPrimeColPrimePtr);
    RESOLVE(BJTemitPrimeBasePrimePtr);
    RESOLVE(BJTcolColPtr);
    RESOLVE(BJTbaseBasePtr);
    RESOLVE(BJTemitEmitPtr);
    RESOLVE(BJTcolPrimeColPrimePtr);
    RESOLVE(BJTbasePrimeBasePrimePtr);
    RESOLVE(BJTemitPrimeEmitPrimePtr);
    RESOLVE(BJTsubstSubstPtr);
    RESOLVE(BJTsubstConSubstPtr);
    RESOLVE(BJTsubstSubstConPtr);
    RESOLVE(BJTbaseColPrimePtr);
    RESOLVE(BJTcolPrimeBasePtr);
    RESOLVE(BJTsubstConSubstConPtr);

#undef RESOLVE
}

// ---------------------------------------------------------------------------
// set_state_ptrs
// ---------------------------------------------------------------------------
void BJTDevice::set_state_ptrs(double* s0, double* s1, double* s2, double* s3, int32_t base) {
    state0_ = s0;
    state1_ = s1;
    state2_ = s2;
    state3_ = s3;
    state_base_ = base;
    inst_.BJTstate = base;
}

// ---------------------------------------------------------------------------
// evaluate
// ---------------------------------------------------------------------------
void BJTDevice::evaluate(const std::vector<double>& voltages,
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

    // First-call BJTtemp.
    if (!temp_done_) {
        int rc = BJTtemp(model_, &ckt);
        if (rc != Shim::OK) {
            throw std::runtime_error("BJTtemp failed with rc=" + std::to_string(rc));
        }
        temp_done_ = true;
    }

    // Splice this instance as sole member, call load, then restore.
    BJTInstance* saved_head      = model_->BJTinstances;
    BJTInstance* saved_next_inst = inst_.BJTnextInstance;
    BJTModel*    saved_next_mod  = model_->BJTnextModel;
    model_->BJTinstances  = &inst_;
    inst_.BJTnextInstance = nullptr;
    model_->BJTnextModel  = nullptr;
    int rc = BJTload(model_, &ckt);
    model_->BJTinstances  = saved_head;
    inst_.BJTnextInstance = saved_next_inst;
    model_->BJTnextModel  = saved_next_mod;
    if (rc != Shim::OK) {
        throw std::runtime_error("BJTload failed with rc=" + std::to_string(rc));
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
bool BJTDevice::device_converged() const {
    return last_noncon_ == 0;
}

// ---------------------------------------------------------------------------
// set_ic — Initial conditions for VBE and VCE
// ---------------------------------------------------------------------------
void BJTDevice::set_ic(double vbe, bool vbe_given,
                       double vce, bool vce_given) {
    if (vbe_given) { inst_.BJTicVBE = vbe; inst_.BJTicVBEGiven = 1; }
    if (vce_given) { inst_.BJTicVCE = vce; inst_.BJTicVCEGiven = 1; }
}

// ---------------------------------------------------------------------------
// ac_stamp — linearized small-signal AC stamp (G/C matrix split)
//
// Translates ngspice bjtacld.c complex-matrix stamping into separate
// G (conductance) and C (capacitance) matrices.  The AC solver combines
// them as (G + jwC) at each frequency point.
//
// NOTE: Excess phase (PTF) is not supported in the static G/C split.
// When model.BJTexcessPhaseFactor != 0, gm becomes frequency-dependent
// involving sin/cos(omega*td).  We ignore it here (xgm = 0 always).
// ---------------------------------------------------------------------------
void BJTDevice::ac_stamp(const std::vector<double>& /*voltages*/,
                         NumericMatrix& G, NumericMatrix& C) {
    auto& inst = inst_;
    auto* model = model_;
    const double m = inst.BJTm;

    // --- Conductance values from state vector ---
    const double gcpr = inst.BJTtcollectorConduct * inst.BJTarea;
    const double gepr = inst.BJTtemitterConduct * inst.BJTarea;
    const double gpi  = state0_[inst.BJTstate + 4];   // BJTgpi
    const double gmu  = state0_[inst.BJTstate + 5];   // BJTgmu
    const double gm   = state0_[inst.BJTstate + 6];   // BJTgm
    const double go   = state0_[inst.BJTstate + 7];   // BJTgo
    const double gx   = state0_[inst.BJTstate + 16];  // BJTgx

    // --- Capacitance values from state vector ---
    // These are dQ/dV (capacitance), NOT C*omega.
    const double cpi  = state0_[inst.BJTstate + 9];   // BJTcqbe
    const double cmu  = state0_[inst.BJTstate + 11];  // BJTcqbc
    const double csub = state0_[inst.BJTstate + 13];  // BJTcqsub
    const double cbx  = state0_[inst.BJTstate + 15];  // BJTcqbx
    const double cmcb = state0_[inst.BJTstate + 17];  // BJTcexbc

    // --- G matrix stamps ---
    G.add(inst.BJTcolColPtr,               m * gcpr);
    G.add(inst.BJTbaseBasePtr,             m * gx);
    G.add(inst.BJTemitEmitPtr,             m * gepr);
    G.add(inst.BJTcolPrimeColPrimePtr,     m * (gmu + go + gcpr));
    G.add(inst.BJTbasePrimeBasePrimePtr,   m * (gx + gpi + gmu));
    G.add(inst.BJTemitPrimeEmitPrimePtr,   m * (gpi + gepr + gm + go));
    G.add(inst.BJTcolColPrimePtr,          m * (-gcpr));
    G.add(inst.BJTbaseBasePrimePtr,        m * (-gx));
    G.add(inst.BJTemitEmitPrimePtr,        m * (-gepr));
    G.add(inst.BJTcolPrimeColPtr,          m * (-gcpr));
    G.add(inst.BJTcolPrimeBasePrimePtr,    m * (-gmu + gm));
    G.add(inst.BJTcolPrimeEmitPrimePtr,    m * (-gm - go));
    G.add(inst.BJTbasePrimeBasePtr,        m * (-gx));
    G.add(inst.BJTbasePrimeColPrimePtr,    m * (-gmu));
    G.add(inst.BJTbasePrimeEmitPrimePtr,   m * (-gpi));
    G.add(inst.BJTemitPrimeEmitPtr,        m * (-gepr));
    G.add(inst.BJTemitPrimeColPrimePtr,    m * (-go));
    G.add(inst.BJTemitPrimeBasePrimePtr,   m * (-gpi - gm));

    // --- C matrix stamps ---
    C.add(inst.BJTbaseBasePtr,             m * cbx);
    C.add(inst.BJTcolPrimeColPrimePtr,     m * (cmu + cbx));
    C.add(inst.BJTsubstConSubstConPtr,     m * csub);
    C.add(inst.BJTbasePrimeBasePrimePtr,   m * (cpi + cmu + cmcb));
    C.add(inst.BJTemitPrimeEmitPrimePtr,   m * cpi);
    C.add(inst.BJTcolPrimeBasePrimePtr,    m * (-cmu));
    C.add(inst.BJTbasePrimeColPrimePtr,    m * (-cmu - cmcb));
    C.add(inst.BJTbasePrimeEmitPrimePtr,   m * (-cpi));
    C.add(inst.BJTemitPrimeColPrimePtr,    m * cmcb);
    C.add(inst.BJTemitPrimeBasePrimePtr,   m * (-cpi - cmcb));
    C.add(inst.BJTsubstSubstPtr,           m * csub);
    C.add(inst.BJTsubstConSubstPtr,        m * (-csub));
    C.add(inst.BJTsubstSubstConPtr,        m * (-csub));
    C.add(inst.BJTbaseColPrimePtr,         m * (-cbx));
    C.add(inst.BJTcolPrimeBasePtr,         m * (-cbx));
}

// ---------------------------------------------------------------------------
// compute_trunc — device-specific local truncation error for time stepping
// ---------------------------------------------------------------------------
double BJTDevice::compute_trunc(const IntegratorCtx& ctx,
                                const SimOptions& opts) const {
    if (ctx.order < 1 || ctx.delta <= 0.0)
        return 1e30;
    if (!state0_ || !state1_ || !state2_ || !state3_)
        return 1e30;

    const double* states[] = {state0_, state1_, state2_, state3_};
    double dt_min = 1e30;
    static const int charge_offsets[] = {8, 10, 12, 14};  // qbe, qbc, qsub, qbx
    for (int rel : charge_offsets)
        ckt_terr(state_base_ + rel, states, ctx, opts, dt_min);
    return dt_min;
}

// ---------------------------------------------------------------------------
// noise_sources — BJT noise contributions (shot + thermal + flicker)
//
// Sources (following ngspice bjtnoise.c):
//   1. Collector shot noise:     S_ic = 2*q*|Ic|      (col_prime <-> emit_prime)
//   2. Base shot + flicker:      S_ib = 2*q*|Ib| + KF*|Ib|^AF/f  (base_prime <-> emit_prime)
//   3. Base resistance thermal:  S_rb = 4kT/Rb        (base_ext <-> base_prime), if Rb>0
//   4. Collector series thermal: S_rc = 4kT/Rc        (col_ext <-> col_prime),   if Rc>0
//   5. Emitter series thermal:   S_re = 4kT/Re        (emit_ext <-> emit_prime), if Re>0
//
// UCB nodes are 1-based (0=ground); neospice nodes are 0-based (-1=ground).
// Convert: neo = ucb - 1.
// ---------------------------------------------------------------------------
std::vector<Device::NoiseSource> BJTDevice::noise_sources(
        double freq, const std::vector<double>& /*dc_solution*/) const {
    if (!state0_ || state_base_ < 0)
        return {};

    std::vector<NoiseSource> sources;

    // Neospice node indices (ucb-1): -1 is ground.
    const int32_t col_node       = inst_.BJTcolNode       - 1;
    const int32_t base_node      = inst_.BJTbaseNode      - 1;
    const int32_t emit_node      = inst_.BJTemitNode      - 1;
    const int32_t col_prime_node = inst_.BJTcolPrimeNode  - 1;
    const int32_t base_prime_node= inst_.BJTbasePrimeNode - 1;
    const int32_t emit_prime_node= inst_.BJTemitPrimeNode - 1;

    const double T  = T_NOMINAL;   // device temperature
    const double m  = inst_.BJTm;  // parallel multiplier

    // DC operating point currents (from state vector).
    const double Ic = std::abs(state0_[state_base_ + 2]);  // BJTcc = BJTstate+2
    const double Ib = std::abs(state0_[state_base_ + 3]);  // BJTcb = BJTstate+3

    // --- 1. Collector shot noise ---
    sources.push_back({col_prime_node, emit_prime_node,
                       m * 2.0 * CHARGE_Q * Ic});

    // --- 2. Base shot noise + flicker noise ---
    double S_ib = 2.0 * CHARGE_Q * Ib;
    const double KF = model_->BJTfNcoef;   // flicker noise coefficient (default 0)
    const double AF = model_->BJTfNexp;    // flicker noise exponent (default 1)
    if (KF > 0.0 && freq > 0.0 && Ib > 0.0) {
        S_ib += KF * std::pow(Ib, AF) / freq;
    }
    sources.push_back({base_prime_node, emit_prime_node, m * S_ib});

    // --- 3. Base resistance thermal noise (external to internal base) ---
    // BJTtbaseResist is the temperature-adjusted base resistance.
    const double Rb = inst_.BJTtbaseResist;
    if (Rb > 0.0) {
        sources.push_back({base_node, base_prime_node,
                           m * 4.0 * BOLTZMANN * T / Rb});
    }

    // --- 4. Collector series resistance thermal noise ---
    // BJTtcollectorConduct = 1/Rc (temperature-adjusted).
    const double Gc = inst_.BJTtcollectorConduct * inst_.BJTareac;
    if (Gc > 0.0) {
        sources.push_back({col_node, col_prime_node,
                           m * 4.0 * BOLTZMANN * T * Gc});
    }

    // --- 5. Emitter series resistance thermal noise ---
    const double Ge = inst_.BJTtemitterConduct * inst_.BJTarea;
    if (Ge > 0.0) {
        sources.push_back({emit_node, emit_prime_node,
                           m * 4.0 * BOLTZMANN * T * Ge});
    }

    return sources;
}

// ---------------------------------------------------------------------------
// query_param — post-simulation parameter query
// ---------------------------------------------------------------------------
std::optional<double>
BJTDevice::query_param(const std::string& name) const {
    const std::string key = str_tolower(name);
    const double m = inst_.BJTm;

    // --- Operating-point parameters from state vector ---
    if (state0_ && state_base_ >= 0) {
        if (key == "vbe")   return state0_[state_base_ + 0];
        if (key == "vbc")   return state0_[state_base_ + 1];
        if (key == "ic" || key == "cc")  return state0_[state_base_ + 2] * m;
        if (key == "ib" || key == "cb")  return state0_[state_base_ + 3] * m;
        if (key == "gpi")   return state0_[state_base_ + 4] * m;
        if (key == "gmu")   return state0_[state_base_ + 5] * m;
        if (key == "gm")    return state0_[state_base_ + 6] * m;
        if (key == "go")    return state0_[state_base_ + 7] * m;
        if (key == "gx")    return state0_[state_base_ + 16] * m;
    }

    // --- Geometry (no multiplier) ---
    if (key == "area")  return inst_.BJTarea;
    if (key == "areab") return inst_.BJTareab;
    if (key == "areac") return inst_.BJTareac;
    if (key == "m")     return inst_.BJTm;

    return std::nullopt;
}

// ---------------------------------------------------------------------------
// check_soa — Safe Operating Area check (informational)
// ---------------------------------------------------------------------------
Device::SoaResult
BJTDevice::check_soa(const std::vector<double>& solution) const {
    if (!model_ || !state0_ || state_base_ < 0)
        return {};

    double vbe = state0_[state_base_ + 0];
    double vbc = state0_[state_base_ + 1];
    double vce = vbe - vbc;

    if (model_->BJTvbeMaxGiven && model_->BJTvbeMax > 0.0) {
        if (std::abs(vbe) > model_->BJTvbeMax)
            return {false, "Vbe", vbe, model_->BJTvbeMax};
    }
    if (model_->BJTvbcMaxGiven && model_->BJTvbcMax > 0.0) {
        if (std::abs(vbc) > model_->BJTvbcMax)
            return {false, "Vbc", vbc, model_->BJTvbcMax};
    }
    if (model_->BJTvceMaxGiven && model_->BJTvceMax > 0.0) {
        if (std::abs(vce) > model_->BJTvceMax)
            return {false, "Vce", vce, model_->BJTvceMax};
    }

    return {};
}

} // namespace neospice
