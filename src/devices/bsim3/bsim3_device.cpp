#include "devices/bsim3/bsim3_device.hpp"

#include "core/circuit.hpp"        // Circuit::node, tls_integrator_ctx
#include "core/types.hpp"          // SimOptions defaults
#include "devices/ucb_device_init.hpp"
#include "devices/ucb_utils.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <string>

// Forward declarations for translated UCB functions.
namespace neospice::bsim3 {
    int BSIM3setup(Shim::Matrix*, BSIM3Model*, Shim::Ckt*, int*);
    int BSIM3temp(BSIM3Model*, Shim::Ckt*);
    int BSIM3load(BSIM3Model*, Shim::Ckt*);
}

namespace neospice {

using namespace neospice::bsim3;

// ---------------------------------------------------------------------------
// BSIM3ModelCard destructor
// ---------------------------------------------------------------------------
BSIM3ModelCard::~BSIM3ModelCard() {
    auto* p = ucb.pSizeDependParamKnot;
    while (p) {
        auto* next = p->pNext;
        std::free(p);
        p = next;
    }
    ucb.pSizeDependParamKnot = nullptr;
}

// ---------------------------------------------------------------------------
// make
// ---------------------------------------------------------------------------
std::unique_ptr<BSIM3Device>
BSIM3Device::make(std::string name,
        int32_t n_d, int32_t n_g, int32_t n_s, int32_t n_b,
        const Geom& geom, BSIM3ModelCard& shared_card) {
    std::unique_ptr<BSIM3Device> dev(new BSIM3Device(std::move(name)));
    dev->model_ = &shared_card.ucb;
    dev->ext_nodes_ = {n_d, n_g, n_s, n_b};

    if (!shared_card.ucb.BSIM3versionGiven) {
        shared_card.ucb.BSIM3version = "3.3.0";
        shared_card.ucb.BSIM3versionGiven = 1;
    }

    auto& inst = dev->inst_;
    inst.BSIM3name = dev->name().c_str();
    inst.BSIM3modPtr = dev->model_;

    // Node wiring (UCB convention).
    inst.BSIM3dNode = neo_to_ucb(n_d);
    inst.BSIM3gNode = neo_to_ucb(n_g);
    inst.BSIM3sNode = neo_to_ucb(n_s);
    inst.BSIM3bNode = neo_to_ucb(n_b);

    // Geometry.
    inst.BSIM3w = geom.W;
    inst.BSIM3wGiven = 1;
    inst.BSIM3l = geom.L;
    inst.BSIM3lGiven = 1;
    inst.BSIM3drainArea = geom.AD;
    inst.BSIM3drainAreaGiven = (geom.AD != 0.0) ? 1 : 0;
    inst.BSIM3sourceArea = geom.AS;
    inst.BSIM3sourceAreaGiven = (geom.AS != 0.0) ? 1 : 0;
    inst.BSIM3drainPerimeter = geom.PD;
    inst.BSIM3drainPerimeterGiven = (geom.PD != 0.0) ? 1 : 0;
    inst.BSIM3sourcePerimeter = geom.PS;
    inst.BSIM3sourcePerimeterGiven = (geom.PS != 0.0) ? 1 : 0;
    inst.BSIM3drainSquares = geom.NRD;
    inst.BSIM3drainSquaresGiven = (geom.NRD != 0.0) ? 1 : 0;
    inst.BSIM3sourceSquares = geom.NRS;
    inst.BSIM3sourceSquaresGiven = (geom.NRS != 0.0) ? 1 : 0;
    if (geom.M != 1.0) {
        inst.BSIM3m = geom.M;
        inst.BSIM3mGiven = 1;
    }

    // Thread onto the shared model's instance list.
    inst.BSIM3nextInstance = shared_card.ucb.BSIM3instances;
    shared_card.ucb.BSIM3instances = &inst;

    // Remember the widest real node index for ghost array sizing.
    int32_t widest = -1;
    for (int32_t n : {n_d, n_g, n_s, n_b}) if (n > widest) widest = n;
    dev->max_neo_node_ = widest;

    return dev;
}

// ---------------------------------------------------------------------------
// declare_internal_nodes
// ---------------------------------------------------------------------------
void BSIM3Device::declare_internal_nodes(Circuit& ckt) {
    ucb_declare_internal_nodes<Shim::Matrix, Shim::Ckt>(
        ckt, name_,
        [this](Shim::Matrix& m, Shim::Ckt& c) {
            UCB_SPLICE_INSTANCE(BSIM3);
            int states = 0;
            int rc = BSIM3setup(&m, model_, &c, &states);
            UCB_UNSPLICE_INSTANCE(BSIM3);
            return rc;
        },
        "BSIM3setup", journal_, max_neo_node_);
}

// ---------------------------------------------------------------------------
// stamp_pattern
// ---------------------------------------------------------------------------
void BSIM3Device::stamp_pattern(SparsityBuilder& builder) const {
    ucb_stamp_pattern(journal_, builder);
}

// ---------------------------------------------------------------------------
// assign_offsets
// ---------------------------------------------------------------------------
void BSIM3Device::assign_offsets(const SparsityPattern& pattern) {
    const auto offsets = ucb_compute_offsets(journal_, pattern);

#define RESOLVE(f)                                                       \
    do {                                                                 \
        if (inst_.f >= 0)                                                \
            inst_.f = offsets[inst_.f];                                  \
    } while (0)

    RESOLVE(BSIM3DdPtr);
    RESOLVE(BSIM3GgPtr);
    RESOLVE(BSIM3SsPtr);
    RESOLVE(BSIM3BbPtr);
    RESOLVE(BSIM3DPdpPtr);
    RESOLVE(BSIM3SPspPtr);
    RESOLVE(BSIM3DdpPtr);
    RESOLVE(BSIM3GbPtr);
    RESOLVE(BSIM3GdpPtr);
    RESOLVE(BSIM3GspPtr);
    RESOLVE(BSIM3SspPtr);
    RESOLVE(BSIM3BdpPtr);
    RESOLVE(BSIM3BspPtr);
    RESOLVE(BSIM3DPspPtr);
    RESOLVE(BSIM3DPdPtr);
    RESOLVE(BSIM3BgPtr);
    RESOLVE(BSIM3DPgPtr);
    RESOLVE(BSIM3SPgPtr);
    RESOLVE(BSIM3SPsPtr);
    RESOLVE(BSIM3DPbPtr);
    RESOLVE(BSIM3SPbPtr);
    RESOLVE(BSIM3SPdpPtr);
    RESOLVE(BSIM3QqPtr);
    RESOLVE(BSIM3QdpPtr);
    RESOLVE(BSIM3QspPtr);
    RESOLVE(BSIM3QgPtr);
    RESOLVE(BSIM3QbPtr);
    RESOLVE(BSIM3DPqPtr);
    RESOLVE(BSIM3SPqPtr);
    RESOLVE(BSIM3GqPtr);
    RESOLVE(BSIM3BqPtr);

#undef RESOLVE
}

// ---------------------------------------------------------------------------
// set_state_ptrs
// ---------------------------------------------------------------------------
void BSIM3Device::set_state_ptrs(double* s0, double* s1, double* s2, int32_t base) {
    state0_ = s0;
    state1_ = s1;
    state2_ = s2;
    state_base_ = base;
    inst_.BSIM3states = base;
}

// ---------------------------------------------------------------------------
// evaluate
// ---------------------------------------------------------------------------
void BSIM3Device::evaluate(const std::vector<double>& voltages,
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

    // Cache simulation temperature for noise_sources().
    sim_temp_ = sim_opts->temp;

    // State ring.
    ckt.CKTstate0 = state0_;
    ckt.CKTstate1 = state1_;
    ckt.CKTstate2 = state2_;

    // Ghost rhs / old-iterate pointers.
    ckt.CKTrhs    = ghost_rhs_.data();
    ckt.CKTrhsOld = ghost_voltages_.data();
    ckt.mat       = &mat;

    // First-call BSIM3temp.
    if (!temp_done_) {
        int rc = BSIM3temp(model_, &ckt);
        if (rc != Shim::OK) {
            throw std::runtime_error("BSIM3temp failed with rc=" + std::to_string(rc));
        }
        temp_done_ = true;
    }

    // Splice this instance as sole member, call load, then restore.
    BSIM3Instance* saved_head      = model_->BSIM3instances;
    BSIM3Instance* saved_next_inst = inst_.BSIM3nextInstance;
    BSIM3Model*    saved_next_mod  = model_->BSIM3nextModel;
    model_->BSIM3instances  = &inst_;
    inst_.BSIM3nextInstance = nullptr;
    model_->BSIM3nextModel  = nullptr;
    int rc = BSIM3load(model_, &ckt);
    model_->BSIM3instances  = saved_head;
    inst_.BSIM3nextInstance = saved_next_inst;
    model_->BSIM3nextModel  = saved_next_mod;
    if (rc != Shim::OK) {
        throw std::runtime_error("BSIM3load failed with rc=" + std::to_string(rc));
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
bool BSIM3Device::device_converged() const {
    return last_noncon_ == 0;
}

// ---------------------------------------------------------------------------
// set_ic  (Phase 7)
// ---------------------------------------------------------------------------
void BSIM3Device::set_ic(double vds, bool vds_given,
                          double vgs, bool vgs_given,
                          double vbs, bool vbs_given) {
    if (vds_given) { inst_.BSIM3icVDS = vds; inst_.BSIM3icVDSGiven = 1; }
    if (vgs_given) { inst_.BSIM3icVGS = vgs; inst_.BSIM3icVGSGiven = 1; }
    if (vbs_given) { inst_.BSIM3icVBS = vbs; inst_.BSIM3icVBSGiven = 1; }
}

// ---------------------------------------------------------------------------
// ac_stamp — linearized small-signal AC stamp (QS path only)  (Phase 4)
//
// Translates the ngspice BSIM3acLoad() complex-matrix stamping into
// separate G (conductance) and C (capacitance) matrices.  The AC solver
// combines them as (G + jwC) at each frequency point.
//
// BSIM3 is simpler than BSIM4v7 — no rgateMod, no rbodyMod, no igcMod.
// The NQS path (nqsMod != 0 && acnqsMod != 1) adds a Q node with
// partitioned channel charge.  We handle QS mode and the acnqsMod == 1
// NQS approximation; true NQS (frequency-dependent stamps) are omitted.
// ---------------------------------------------------------------------------
void BSIM3Device::ac_stamp(const std::vector<double>& /*voltages*/,
                             NumericMatrix& G, NumericMatrix& C) {
    auto& here = inst_;
    auto* model = model_;

    const double m = here.BSIM3m;

    // Source-side charge conservation capacitances
    const double Csd = -(here.BSIM3cddb + here.BSIM3cgdb + here.BSIM3cbdb);
    const double Csg = -(here.BSIM3cdgb + here.BSIM3cggb + here.BSIM3cbgb);
    const double Css = -(here.BSIM3cdsb + here.BSIM3cgsb + here.BSIM3cbsb);

    // QS mode intrinsic capacitances
    double Cddr, Cdgr, Cdsr;
    double Csdr, Csgr, Cssr;
    double Cgdr, Cggr, Cgsr;
    double gmr, gmbsr, gds_ac;

    if (here.BSIM3acnqsMod) {
        // ACNQS mode: use QS values directly (imaginary conductance terms ~ 0)
        gmr = here.BSIM3gm;
        gmbsr = here.BSIM3gmbs;
        gds_ac = here.BSIM3gds;

        Cddr = here.BSIM3cddb;
        Cdgr = here.BSIM3cdgb;
        Cdsr = here.BSIM3cdsb;
        Csdr = Csd;
        Csgr = Csg;
        Cssr = Css;
        Cgdr = here.BSIM3cgdb;
        Cggr = here.BSIM3cggb;
        Cgsr = here.BSIM3cgsb;
    } else {
        // Pure QS mode
        gmr = here.BSIM3gm;
        gmbsr = here.BSIM3gmbs;
        gds_ac = here.BSIM3gds;

        Cddr = here.BSIM3cddb;
        Cdgr = here.BSIM3cdgb;
        Cdsr = here.BSIM3cdsb;
        Csdr = Csd;
        Csgr = Csg;
        Cssr = Css;
        Cgdr = here.BSIM3cgdb;
        Cggr = here.BSIM3cggb;
        Cgsr = here.BSIM3cgsb;
    }

    // Mode-dependent variables
    double Gmr, Gmbsr, FwdSumr, RevSumr;
    double gbbdp, gbbsp, gbdpg, gbdpdp, gbdpb, gbdpsp;
    double gbspdp, gbspg, gbspb, gbspsp;

    double cggb, cgdb, cgsb, cbgb, cbdb, cbsb, cdgb, cddb, cdsb;

    if (here.BSIM3mode >= 0) {
        // Forward mode
        Gmr = gmr;
        Gmbsr = gmbsr;
        FwdSumr = Gmr + Gmbsr;
        RevSumr = 0.0;

        gbbdp = -(here.BSIM3gbds);
        gbbsp = here.BSIM3gbds + here.BSIM3gbgs + here.BSIM3gbbs;
        gbdpg = here.BSIM3gbgs;
        gbdpb = here.BSIM3gbbs;
        gbdpdp = here.BSIM3gbds;
        gbdpsp = -(gbdpg + gbdpb + gbdpdp);

        gbspdp = 0.0;
        gbspg = 0.0;
        gbspb = 0.0;
        gbspsp = 0.0;

        cggb = Cggr;
        cgsb = Cgsr;
        cgdb = Cgdr;
        cbgb = here.BSIM3cbgb;
        cbsb = here.BSIM3cbsb;
        cbdb = here.BSIM3cbdb;
        cdgb = Cdgr;
        cdsb = Cdsr;
        cddb = Cddr;
    } else {
        // Reverse mode
        Gmr = -gmr;
        Gmbsr = -gmbsr;
        FwdSumr = 0.0;
        RevSumr = -(Gmr + Gmbsr);

        gbbsp = -(here.BSIM3gbds);
        gbbdp = here.BSIM3gbds + here.BSIM3gbgs + here.BSIM3gbbs;
        gbdpg = 0.0;
        gbdpsp = 0.0;
        gbdpb = 0.0;
        gbdpdp = 0.0;
        gbspg = here.BSIM3gbgs;
        gbspsp = here.BSIM3gbds;
        gbspb = here.BSIM3gbbs;
        gbspdp = -(gbspg + gbspsp + gbspb);

        cggb = Cggr;
        cgsb = Cgdr;     // swapped
        cgdb = Cgsr;
        cbgb = here.BSIM3cbgb;
        cbsb = here.BSIM3cbdb;  // swapped
        cbdb = here.BSIM3cbsb;
        cdgb = -(Cdgr + cggb + cbgb);
        cdsb = -(Cddr + cgsb + cbsb);
        cddb = -(Cdsr + cgdb + cbdb);
    }

    const double gdpr  = here.BSIM3drainConductance;
    const double gspr  = here.BSIM3sourceConductance;
    const double gbd   = here.BSIM3gbd;
    const double gbs   = here.BSIM3gbs;
    const double capbd = here.BSIM3capbd;
    const double capbs = here.BSIM3capbs;

    const double GSoverlapCap = here.BSIM3cgso;
    const double GDoverlapCap = here.BSIM3cgdo;
    const double GBoverlapCap = here.pParam->BSIM3cgbo;

    // Capacitance stamps (into C matrix)
    // xcdgb = (cdgb - GDoverlapCap) * omega => stamp cdgb - GDoverlapCap into C
    double C_DPg = cdgb - GDoverlapCap;
    double C_DPdp = cddb + capbd + GDoverlapCap;
    double C_DPs = cdsb;
    double C_SPg = -(cggb + cbgb + cdgb + GSoverlapCap);
    double C_SPd = -(cgdb + cbdb + cddb);
    double C_SPsp = capbs + GSoverlapCap - (cgsb + cbsb + cdsb);
    double C_Gg = cggb + GDoverlapCap + GSoverlapCap + GBoverlapCap;
    double C_Gd = cgdb - GDoverlapCap;
    double C_Gs = cgsb - GSoverlapCap;
    double C_Bg = cbgb - GBoverlapCap;
    double C_Bd = cbdb - capbd;
    double C_Bs = cbsb - capbs;

    // ---- Stamp G matrix (conductance) ----

    // D-D
    G.add(here.BSIM3DdPtr, m * gdpr);
    // S-S
    G.add(here.BSIM3SsPtr, m * gspr);
    // B-B
    G.add(here.BSIM3BbPtr, m * (gbd + gbs - here.BSIM3gbbs));

    // DP-DP
    G.add(here.BSIM3DPdpPtr, m * (gdpr + gds_ac + gbd + RevSumr + gbdpdp));
    // SP-SP
    G.add(here.BSIM3SPspPtr, m * (gspr + gds_ac + gbs + FwdSumr + gbspsp));

    // D-DP
    G.add(here.BSIM3DdpPtr, m * (-gdpr));
    // S-SP
    G.add(here.BSIM3SspPtr, m * (-gspr));

    // B-G
    G.add(here.BSIM3BgPtr, m * (-here.BSIM3gbgs));
    // B-DP
    G.add(here.BSIM3BdpPtr, m * (-(gbd - gbbdp)));
    // B-SP
    G.add(here.BSIM3BspPtr, m * (-(gbs - gbbsp)));

    // DP-D
    G.add(here.BSIM3DPdPtr, m * (-gdpr));
    // DP-G
    G.add(here.BSIM3DPgPtr, m * (Gmr + gbdpg));
    // DP-B
    G.add(here.BSIM3DPbPtr, m * (-(gbd - Gmbsr - gbdpb)));
    // DP-SP
    G.add(here.BSIM3DPspPtr, m * (-(gds_ac + FwdSumr - gbdpsp)));

    // SP-G
    G.add(here.BSIM3SPgPtr, m * (-(Gmr - gbspg)));
    // SP-S
    G.add(here.BSIM3SPsPtr, m * (-gspr));
    // SP-B
    G.add(here.BSIM3SPbPtr, m * (-(gbs + Gmbsr - gbspb)));
    // SP-DP
    G.add(here.BSIM3SPdpPtr, m * (-(gds_ac + RevSumr - gbspdp)));

    // G-G (just the gate leakage/NQS terms, empty for QS)
    // No xgtg terms for QS mode

    // ---- Stamp C matrix (capacitance) ----

    // G row
    C.add(here.BSIM3GgPtr, m * C_Gg);
    C.add(here.BSIM3GdpPtr, m * C_Gd);
    C.add(here.BSIM3GspPtr, m * C_Gs);
    C.add(here.BSIM3GbPtr, m * (-(C_Gg + C_Gd + C_Gs)));

    // B row
    C.add(here.BSIM3BgPtr, m * C_Bg);
    C.add(here.BSIM3BdpPtr, m * C_Bd);
    C.add(here.BSIM3BspPtr, m * C_Bs);
    C.add(here.BSIM3BbPtr, m * (-(C_Bg + C_Bd + C_Bs)));

    // DP row
    C.add(here.BSIM3DPgPtr, m * C_DPg);
    C.add(here.BSIM3DPdpPtr, m * C_DPdp);
    C.add(here.BSIM3DPspPtr, m * C_DPs);
    C.add(here.BSIM3DPbPtr, m * (-(C_DPg + C_DPdp + C_DPs)));

    // SP row
    C.add(here.BSIM3SPgPtr, m * C_SPg);
    C.add(here.BSIM3SPdpPtr, m * C_SPd);
    C.add(here.BSIM3SPspPtr, m * C_SPsp);
    C.add(here.BSIM3SPbPtr, m * (-(C_SPg + C_SPd + C_SPsp)));

    // NQS Q node — keep it non-singular
    if (here.BSIM3nqsMod) {
        G.add(here.BSIM3QqPtr, m * 1.0);
    }
}

// ---------------------------------------------------------------------------
// compute_trunc — device-specific local truncation error  (Phase 5)
//
// BSIM3 charge state variables:
//   qb  at offset 4  (cqb at 5)
//   qg  at offset 6  (cqg at 7)
//   qd  at offset 8  (cqd at 9)
// ---------------------------------------------------------------------------
double BSIM3Device::compute_trunc(const IntegratorCtx& ctx,
                                   const SimOptions& opts) const {
    if (ctx.order < 2 || ctx.delta <= 0.0)
        return 1e30;

    if (!state0_ || !state1_ || !state2_)
        return 1e30;

    const double lte_coeff = ctx.lte_coefficient();

    const double h  = ctx.delta;
    const double h1 = ctx.delta_old[1];
    if (h1 <= 0.0)
        return 1e30;

    // Charge offsets relative to instance base
    static constexpr int charge_offsets[] = { 4, 6, 8 };  // qb, qg, qd
    static constexpr int ncharges = 3;

    double dt_min = 1e30;

    for (int i = 0; i < ncharges; ++i) {
        const int qcap = state_base_ + charge_offsets[i];
        const int ccap = qcap + 1;

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

        const double qcap2 = state2_[qcap];
        double dd1_0 = (qcap0 - qcap1) / h;
        double dd1_1 = (qcap1 - qcap2) / h1;
        double dd2 = (dd1_0 - dd1_1) / (h + h1);

        double lte_est = lte_coeff * std::abs(dd2);
        if (lte_est <= opts.abstol)
            continue;

        double del = opts.trtol * tol / lte_est;
        del = std::sqrt(del);

        if (del < dt_min)
            dt_min = del;
    }

    return dt_min;
}

// ---------------------------------------------------------------------------
// query_param — post-simulation parameter query  (Phase 8)
// ---------------------------------------------------------------------------
std::optional<double>
BSIM3Device::query_param(const std::string& name) const {
    const std::string key = str_tolower(name);
    const double m = inst_.BSIM3m;

    // --- Operating-point parameters (scaled by multiplier m) ---
    if (key == "gm")                        return inst_.BSIM3gm * m;
    if (key == "gds")                       return inst_.BSIM3gds * m;
    if (key == "gmbs")                      return inst_.BSIM3gmbs * m;
    if (key == "vth" || key == "von")       return inst_.BSIM3von;
    if (key == "vdsat")                     return inst_.BSIM3vdsat;
    if (key == "id" || key == "cd")         return inst_.BSIM3cd * m;
    if (key == "ibs" || key == "cbs")       return inst_.BSIM3cbs * m;
    if (key == "ibd" || key == "cbd")       return inst_.BSIM3cbd * m;
    if (key == "gbd")                       return inst_.BSIM3gbd * m;
    if (key == "gbs")                       return inst_.BSIM3gbs * m;

    // --- Capacitances ---
    if (key == "cgg")                       return inst_.BSIM3cggb * m;
    if (key == "cgd")                       return inst_.BSIM3cgdb * m;
    if (key == "cgs")                       return inst_.BSIM3cgsb * m;
    if (key == "cdg")                       return inst_.BSIM3cdgb * m;
    if (key == "cdd")                       return inst_.BSIM3cddb * m;
    if (key == "cds")                       return inst_.BSIM3cdsb * m;
    if (key == "cbg")                       return inst_.BSIM3cbgb * m;
    if (key == "cbd_cap" || key == "cbdb")  return inst_.BSIM3cbdb * m;
    if (key == "cbs_cap" || key == "cbsb")  return inst_.BSIM3cbsb * m;

    // --- Charges ---
    if (key == "qg")                        return inst_.BSIM3qgate * m;
    if (key == "qd")                        return inst_.BSIM3qdrn * m;
    if (key == "qb")                        return inst_.BSIM3qbulk * m;

    // --- Junction capacitances ---
    if (key == "capbd")                     return inst_.BSIM3capbd * m;
    if (key == "capbs")                     return inst_.BSIM3capbs * m;

    // --- Terminal voltages from state vector ---
    if (state0_ && state_base_ >= 0) {
        if (key == "vbd")                   return state0_[state_base_ + 0];
        if (key == "vbs")                   return state0_[state_base_ + 1];
        if (key == "vgs")                   return state0_[state_base_ + 2];
        if (key == "vds")                   return state0_[state_base_ + 3];
    }

    // --- Geometry (no multiplier) ---
    if (key == "w")                         return inst_.BSIM3w;
    if (key == "l")                         return inst_.BSIM3l;
    if (key == "m")                         return inst_.BSIM3m;

    return std::nullopt;  // unrecognized parameter
}

// ---------------------------------------------------------------------------
// noise_sources — BSIM3 noise model  (Phase 4b)
//
// Implements noise sources from b3noi.c:
//   1. Drain/source series resistance thermal noise
//   2. Channel thermal noise (noiMod 1-6)
//   3. Flicker (1/f) noise
// ---------------------------------------------------------------------------
static constexpr double NOISE_MINLOG = 1e-38;

std::vector<Device::NoiseSource>
BSIM3Device::noise_sources(double freq,
                            const std::vector<double>& /*dc_solution*/) const {
    const auto* model = model_;
    const auto& inst  = inst_;
    const auto* pParam = inst.pParam;

    // Guard: if setup hasn't run yet pParam is null.
    if (!pParam) return {};

    const double m     = inst.BSIM3m;
    const double kT    = BOLTZMANN * sim_temp_;
    const double fourKT = 4.0 * kT;

    // Node indices (neospice convention)
    const int32_t dp_neo = ucb_to_neo(inst.BSIM3dNodePrime);
    const int32_t sp_neo = ucb_to_neo(inst.BSIM3sNodePrime);
    const int32_t d_neo  = ucb_to_neo(inst.BSIM3dNode);
    const int32_t s_neo  = ucb_to_neo(inst.BSIM3sNode);

    std::vector<NoiseSource> sources;
    sources.reserve(4);

    // -----------------------------------------------------------------------
    // 1. Drain / Source series resistance thermal noise
    // -----------------------------------------------------------------------
    const double gdpr = inst.BSIM3drainConductance;
    const double gspr = inst.BSIM3sourceConductance;

    if (gdpr > 0.0)
        sources.push_back({dp_neo, d_neo, fourKT * gdpr * m});
    if (gspr > 0.0)
        sources.push_back({sp_neo, s_neo, fourKT * gspr * m});

    // -----------------------------------------------------------------------
    // 2. Channel thermal noise (noiMod dependent)
    // -----------------------------------------------------------------------
    double channel_noise = 0.0;

    switch (model->BSIM3noiMod) {
      case 1:
      case 3: {
        // SPICE2 thermal: 2/3 * |gm + gds + gmbs|
        const double G_ch = 2.0 / 3.0 * std::abs(inst.BSIM3gm + inst.BSIM3gds
                                                    + inst.BSIM3gmbs);
        channel_noise = fourKT * G_ch * m;
        break;
      }
      case 5:
      case 6: {
        // SPICE2 with linear/sat fix
        double vds_eff = 0.0;
        if (state0_ && state_base_ >= 0)
            vds_eff = std::min(state0_[state_base_ + 3], inst.BSIM3vdsat);
        double factor = (inst.BSIM3vdsat > 0.0)
                       ? (3.0 - vds_eff / inst.BSIM3vdsat) / 3.0
                       : 2.0 / 3.0;
        const double G_ch = factor * std::abs(inst.BSIM3gm + inst.BSIM3gds
                                               + inst.BSIM3gmbs);
        channel_noise = fourKT * G_ch * m;
        break;
      }
      case 2:
      case 4:
      default: {
        // BSIM3 thermal noise: ueff * |qinv| / (Leff^2 + ueff*|qinv|*rds)
        const double Leff = pParam->BSIM3leff;
        const double Leff2 = Leff * Leff;
        const double ueff = inst.BSIM3ueff;
        const double qinv_abs = std::abs(inst.BSIM3qinv);
        const double rds = inst.BSIM3rds;
        double G_ch = 0.0;
        double denom = Leff2 + ueff * qinv_abs * rds;
        if (denom > 0.0)
            G_ch = ueff * qinv_abs / denom;
        channel_noise = fourKT * G_ch * m;
        break;
      }
    }

    if (channel_noise > 0.0)
        sources.push_back({dp_neo, sp_neo, channel_noise});

    // -----------------------------------------------------------------------
    // 3. Flicker (1/f) noise
    // -----------------------------------------------------------------------
    if (freq > 0.0) {
        double flicker_noise = 0.0;
        const double cd     = std::abs(inst.BSIM3cd);
        const double Leff   = pParam->BSIM3leff;

        switch (model->BSIM3noiMod) {
          case 1:
          case 4:
          case 5: {
            // SPICE2 1/f noise: kf * |Id|^af / (f^ef * Leff^2 * cox)
            if (model->BSIM3kf > 0.0 && model->BSIM3cox > 0.0 && Leff > 0.0) {
                flicker_noise = m * model->BSIM3kf
                    * std::exp(model->BSIM3af * std::log(std::max(cd, NOISE_MINLOG)))
                    / (std::pow(freq, model->BSIM3ef) * Leff * Leff * model->BSIM3cox);
            }
            break;
          }
          case 2:
          case 3:
          case 6: {
            // BSIM3 1/f noise: StrongInversionNoiseEval + weak-inversion term
            double vds = 0.0;
            if (state0_ && state_base_ >= 0)
                vds = std::abs(state0_[state_base_ + 3]);

            // StrongInversionNoiseEval
            double Lnoi = Leff - 2.0 * model->BSIM3lintnoi;
            double Lnoi2 = Lnoi * Lnoi;
            double esat = (pParam->BSIM3vsattemp > 0.0 && inst.BSIM3ueff > 0.0)
                         ? 2.0 * pParam->BSIM3vsattemp / inst.BSIM3ueff : 1e10;

            double DelClm = 0.0;
            if (model->BSIM3em > 0.0 && pParam->BSIM3litl > 0.0 && esat > 0.0) {
                double T0 = ((vds - inst.BSIM3Vdseff) / pParam->BSIM3litl
                              + model->BSIM3em) / esat;
                DelClm = pParam->BSIM3litl * std::log(std::max(T0, NOISE_MINLOG));
                if (DelClm < 0.0) DelClm = 0.0;
            }

            double EffFreq = std::pow(freq, model->BSIM3ef);
            const double CHARGE_Q = 1.60217663e-19;
            double T1 = CHARGE_Q * CHARGE_Q * 8.62e-5 * cd * sim_temp_ * inst.BSIM3ueff;
            double T2 = 1.0e8 * EffFreq * inst.BSIM3Abulk * model->BSIM3cox * Lnoi2;
            double N0 = model->BSIM3cox * inst.BSIM3Vgsteff / CHARGE_Q;
            double Nl = model->BSIM3cox * inst.BSIM3Vgsteff
                       * (1.0 - inst.BSIM3AbovVgst2Vtm * inst.BSIM3Vdseff) / CHARGE_Q;

            double T3 = model->BSIM3oxideTrapDensityA
                       * std::log(std::max((N0 + 2.0e14) / (Nl + 2.0e14), NOISE_MINLOG));
            double T4 = model->BSIM3oxideTrapDensityB * (N0 - Nl);
            double T5 = model->BSIM3oxideTrapDensityC * 0.5 * (N0 * N0 - Nl * Nl);

            double T6 = 8.62e-5 * sim_temp_ * cd * cd;
            double T7 = 1.0e8 * EffFreq * Lnoi2 * pParam->BSIM3weff;
            double T8 = model->BSIM3oxideTrapDensityA
                       + model->BSIM3oxideTrapDensityB * Nl
                       + model->BSIM3oxideTrapDensityC * Nl * Nl;
            double T9 = (Nl + 2.0e14) * (Nl + 2.0e14);

            double Ssi = 0.0;
            if (T2 > 0.0)
                Ssi = T1 / T2 * (T3 + T4 + T5);
            if (T7 > 0.0 && T9 > 0.0)
                Ssi += T6 / T7 * DelClm * T8 / T9;

            // Weak-inversion scattering term
            double T10 = model->BSIM3oxideTrapDensityA * 8.62e-5 * sim_temp_;
            double T11 = pParam->BSIM3weff * pParam->BSIM3leff
                        * std::pow(freq, model->BSIM3ef) * 4.0e36;
            double Swi = 0.0;
            if (T11 > 0.0)
                Swi = T10 / T11 * cd * cd;

            double T1_tot = Swi + Ssi;
            if (T1_tot > 0.0)
                flicker_noise = m * (Ssi * Swi) / T1_tot;
            break;
          }
        }

        if (flicker_noise > 0.0)
            sources.push_back({dp_neo, sp_neo, flicker_noise});
    }

    return sources;
}

} // namespace neospice
