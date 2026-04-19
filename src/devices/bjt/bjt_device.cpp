#include "devices/bjt/bjt_device.hpp"

#include "core/circuit.hpp"        // Circuit::node, tls_integrator_ctx
#include "core/types.hpp"          // SimOptions defaults

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

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
std::unique_ptr<BJTDevice>
BJTDevice::make(std::string name,
        int32_t n_col, int32_t n_base, int32_t n_emit, int32_t n_subst,
        const Geom& geom, BJTModelCard& shared_card) {
    std::unique_ptr<BJTDevice> dev(new BJTDevice(std::move(name)));
    dev->model_ = &shared_card.ucb;

    auto& inst = dev->inst_;
    inst.BJTname = dev->name().c_str();
    inst.BJTmodPtr = dev->model_;

    // Node wiring (UCB convention).
    inst.BJTcolNode = neo_to_ucb(n_col);
    inst.BJTbaseNode = neo_to_ucb(n_base);
    inst.BJTemitNode = neo_to_ucb(n_emit);
    inst.BJTsubstNode = neo_to_ucb(n_subst);

    // Geometry.
    inst.BJTarea = geom.area;
    inst.BJTareaGiven = (geom.area != 1.0) ? 1 : 0;
    inst.BJTareab = geom.areab;
    inst.BJTareabGiven = (geom.areab != 1.0) ? 1 : 0;
    inst.BJTareac = geom.areac;
    inst.BJTareacGiven = (geom.areac != 1.0) ? 1 : 0;
    inst.BJTm = geom.m;
    inst.BJTmGiven = (geom.m != 1.0) ? 1 : 0;

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
    int rc = BJTsetup(&shim_matrix, model_, &setup_ckt, &states);
    if (rc != Shim::OK) {
        throw std::runtime_error("BJTsetup failed with rc=" + std::to_string(rc));
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
void BJTDevice::stamp_pattern(SparsityBuilder& builder) const {
    for (auto [r, c] : journal_) {
        if (r <= 0 || c <= 0) continue;
        builder.add(r - 1, c - 1);
    }
}

// ---------------------------------------------------------------------------
// assign_offsets
// ---------------------------------------------------------------------------
void BJTDevice::assign_offsets(const SparsityPattern& pattern) {
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

#undef RESOLVE
}

// ---------------------------------------------------------------------------
// set_state_ptrs
// ---------------------------------------------------------------------------
void BJTDevice::set_state_ptrs(double* s0, double* s1, double* s2, int32_t base) {
    state0_ = s0;
    state1_ = s1;
    state2_ = s2;
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
    if (ctx.order < 2 || ctx.delta <= 0.0)
        return 1e30;

    if (!state0_ || !state1_ || !state2_)
        return 1e30;

    const double lte_coeff = 2.0 / 9.0;
    const double h0 = ctx.delta;
    const double h1 = ctx.delta_old[1];
    if (h1 <= 0.0) return 1e30;

    // Charge offsets: qbe=8, qbc=10, qsub=12, qbx=14
    static const int charge_offsets[] = {8, 10, 12, 14};
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
// query_param — post-simulation parameter query
// ---------------------------------------------------------------------------
static std::string str_tolower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

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

} // namespace neospice
