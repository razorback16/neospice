#include "devices/bsim3v32/bsim3v32_device.hpp"

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
#include "devices/ckt_terr.hpp"

// Forward declarations for translated UCB functions.
namespace neospice::bsim3v32 {
    int BSIM3v32setup(Shim::Matrix*, BSIM3v32Model*, Shim::Ckt*, int*);
    int BSIM3v32temp(BSIM3v32Model*, Shim::Ckt*);
    int BSIM3v32load(BSIM3v32Model*, Shim::Ckt*);
}

namespace neospice {

using namespace neospice::bsim3v32;

// ---------------------------------------------------------------------------
// BSIM3v32ModelCard destructor
// ---------------------------------------------------------------------------
BSIM3v32ModelCard::~BSIM3v32ModelCard() {
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
std::unique_ptr<BSIM3v32Device>
BSIM3v32Device::make(std::string name,
        int32_t n_d, int32_t n_g, int32_t n_s, int32_t n_b,
        const Geom& geom, BSIM3v32ModelCard& shared_card) {
    std::unique_ptr<BSIM3v32Device> dev(new BSIM3v32Device(std::move(name)));
    dev->ext_nodes_ = {n_d, n_g, n_s, n_b};
    dev->model_ = &shared_card.ucb;

    if (!shared_card.ucb.BSIM3v32versionGiven) {
        shared_card.ucb.BSIM3v32version = "3.24";
        shared_card.ucb.BSIM3v32versionGiven = 1;
    }

    auto& inst = dev->inst_;
    inst.BSIM3v32name = dev->name().c_str();
    inst.BSIM3v32modPtr = dev->model_;

    // Node wiring (UCB convention).
    inst.BSIM3v32dNode = neo_to_ucb(n_d);
    inst.BSIM3v32gNode = neo_to_ucb(n_g);
    inst.BSIM3v32sNode = neo_to_ucb(n_s);
    inst.BSIM3v32bNode = neo_to_ucb(n_b);

    // Geometry.
    inst.BSIM3v32w = geom.W;
    inst.BSIM3v32wGiven = 1;
    inst.BSIM3v32l = geom.L;
    inst.BSIM3v32lGiven = 1;
    inst.BSIM3v32drainArea = geom.AD;
    inst.BSIM3v32drainAreaGiven = (geom.AD != 0.0) ? 1 : 0;
    inst.BSIM3v32sourceArea = geom.AS;
    inst.BSIM3v32sourceAreaGiven = (geom.AS != 0.0) ? 1 : 0;
    inst.BSIM3v32drainPerimeter = geom.PD;
    inst.BSIM3v32drainPerimeterGiven = (geom.PD != 0.0) ? 1 : 0;
    inst.BSIM3v32sourcePerimeter = geom.PS;
    inst.BSIM3v32sourcePerimeterGiven = (geom.PS != 0.0) ? 1 : 0;
    inst.BSIM3v32drainSquares = geom.NRD;
    inst.BSIM3v32drainSquaresGiven = (geom.NRD != 0.0) ? 1 : 0;
    inst.BSIM3v32sourceSquares = geom.NRS;
    inst.BSIM3v32sourceSquaresGiven = (geom.NRS != 0.0) ? 1 : 0;
    if (geom.M != 1.0) {
        inst.BSIM3v32m = geom.M;
        inst.BSIM3v32mGiven = 1;
    }

    // Thread onto the shared model's instance list.
    inst.BSIM3v32nextInstance = shared_card.ucb.BSIM3v32instances;
    shared_card.ucb.BSIM3v32instances = &inst;

    // Remember the widest real node index for ghost array sizing.
    int32_t widest = -1;
    for (int32_t n : {n_d, n_g, n_s, n_b}) if (n > widest) widest = n;
    dev->max_neo_node_ = widest;

    return dev;
}

// ---------------------------------------------------------------------------
// declare_internal_nodes
// ---------------------------------------------------------------------------
void BSIM3v32Device::declare_internal_nodes(Circuit& ckt) {
    ucb_declare_internal_nodes<Shim::Matrix, Shim::Ckt>(
        ckt, name_,
        [this](Shim::Matrix& m, Shim::Ckt& c) {
            UCB_SPLICE_INSTANCE(BSIM3v32);
            int states = 0;
            int rc = BSIM3v32setup(&m, model_, &c, &states);
            UCB_UNSPLICE_INSTANCE(BSIM3v32);
            return rc;
        },
        "BSIM3v32setup", journal_, max_neo_node_);
}

// ---------------------------------------------------------------------------
// stamp_pattern
// ---------------------------------------------------------------------------
void BSIM3v32Device::stamp_pattern(SparsityBuilder& builder) const {
    ucb_stamp_pattern(journal_, builder);
}

// ---------------------------------------------------------------------------
// assign_offsets
// ---------------------------------------------------------------------------
void BSIM3v32Device::assign_offsets(const SparsityPattern& pattern) {
    const auto offsets = ucb_compute_offsets(journal_, pattern);

#define RESOLVE(f)                                                       \
    do {                                                                 \
        if (inst_.f >= 0)                                                \
            inst_.f = offsets[inst_.f];                                  \
    } while (0)

    RESOLVE(BSIM3v32DdPtr);
    RESOLVE(BSIM3v32GgPtr);
    RESOLVE(BSIM3v32SsPtr);
    RESOLVE(BSIM3v32BbPtr);
    RESOLVE(BSIM3v32DPdpPtr);
    RESOLVE(BSIM3v32SPspPtr);
    RESOLVE(BSIM3v32DdpPtr);
    RESOLVE(BSIM3v32GbPtr);
    RESOLVE(BSIM3v32GdpPtr);
    RESOLVE(BSIM3v32GspPtr);
    RESOLVE(BSIM3v32SspPtr);
    RESOLVE(BSIM3v32BdpPtr);
    RESOLVE(BSIM3v32BspPtr);
    RESOLVE(BSIM3v32DPspPtr);
    RESOLVE(BSIM3v32DPdPtr);
    RESOLVE(BSIM3v32BgPtr);
    RESOLVE(BSIM3v32DPgPtr);
    RESOLVE(BSIM3v32SPgPtr);
    RESOLVE(BSIM3v32SPsPtr);
    RESOLVE(BSIM3v32DPbPtr);
    RESOLVE(BSIM3v32SPbPtr);
    RESOLVE(BSIM3v32SPdpPtr);
    RESOLVE(BSIM3v32QqPtr);
    RESOLVE(BSIM3v32QdpPtr);
    RESOLVE(BSIM3v32QgPtr);
    RESOLVE(BSIM3v32QspPtr);
    RESOLVE(BSIM3v32QbPtr);
    RESOLVE(BSIM3v32DPqPtr);
    RESOLVE(BSIM3v32GqPtr);
    RESOLVE(BSIM3v32SPqPtr);
    RESOLVE(BSIM3v32BqPtr);

#undef RESOLVE
}

// ---------------------------------------------------------------------------
// set_state_ptrs
// ---------------------------------------------------------------------------
void BSIM3v32Device::set_state_ptrs(double* s0, double* s1, double* s2, double* s3, int32_t base) {
    state0_ = s0;
    state1_ = s1;
    state2_ = s2;
    state3_ = s3;
    state_base_ = base;
    inst_.BSIM3v32states = base;
}

// ---------------------------------------------------------------------------
// evaluate
// ---------------------------------------------------------------------------
void BSIM3v32Device::evaluate(const std::vector<double>& voltages,
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

    // First-call BSIM3v32temp.
    if (!temp_done_) {
        int rc = BSIM3v32temp(model_, &ckt);
        if (rc != Shim::OK) {
            throw std::runtime_error("BSIM3v32temp failed with rc=" + std::to_string(rc));
        }
        temp_done_ = true;
    }

    // Splice this instance as sole member, call load, then restore.
    BSIM3v32Instance* saved_head      = model_->BSIM3v32instances;
    BSIM3v32Instance* saved_next_inst = inst_.BSIM3v32nextInstance;
    BSIM3v32Model*    saved_next_mod  = model_->BSIM3v32nextModel;
    model_->BSIM3v32instances  = &inst_;
    inst_.BSIM3v32nextInstance = nullptr;
    model_->BSIM3v32nextModel  = nullptr;
    int rc = BSIM3v32load(model_, &ckt);
    model_->BSIM3v32instances  = saved_head;
    inst_.BSIM3v32nextInstance = saved_next_inst;
    model_->BSIM3v32nextModel  = saved_next_mod;
    if (rc != Shim::OK) {
        throw std::runtime_error("BSIM3v32load failed with rc=" + std::to_string(rc));
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
bool BSIM3v32Device::device_converged() const {
    return last_noncon_ == 0;
}

// ---------------------------------------------------------------------------
// set_ic
// ---------------------------------------------------------------------------
void BSIM3v32Device::set_ic(double vds, bool vds_given,
                              double vgs, bool vgs_given,
                              double vbs, bool vbs_given) {
    if (vds_given) { inst_.BSIM3v32icVDS = vds; inst_.BSIM3v32icVDSGiven = 1; }
    if (vgs_given) { inst_.BSIM3v32icVGS = vgs; inst_.BSIM3v32icVGSGiven = 1; }
    if (vbs_given) { inst_.BSIM3v32icVBS = vbs; inst_.BSIM3v32icVBSGiven = 1; }
}

// ---------------------------------------------------------------------------
// ac_stamp -- linearized small-signal AC stamp
//
// Translates the ngspice BSIM3v32acLoad() complex-matrix stamping into
// separate G (conductance) and C (capacitance) matrices.
//
// For QS mode (nqsMod == 0), this is a direct translation.
// For NQS mode (nqsMod != 0), we include Q-node stamps.
// ---------------------------------------------------------------------------
void BSIM3v32Device::ac_stamp(const std::vector<double>& /*voltages*/,
                                NumericMatrix& G, NumericMatrix& C) {
    auto& here = inst_;
    auto* model = model_;

    const double m = here.BSIM3v32m;

    // Source-side charge conservation capacitances
    const double Csd = -(here.BSIM3v32cddb + here.BSIM3v32cgdb + here.BSIM3v32cbdb);
    const double Csg = -(here.BSIM3v32cdgb + here.BSIM3v32cggb + here.BSIM3v32cbgb);
    const double Css = -(here.BSIM3v32cdsb + here.BSIM3v32cgsb + here.BSIM3v32cbsb);

    double Gm, Gmbs, FwdSum, RevSum;
    double gbbdp, gbbsp, gbdpg, gbdpdp, gbdpb, gbdpsp;
    double gbspdp, gbspg, gbspb, gbspsp;
    double cggb, cgdb, cgsb, cbgb, cbdb, cbsb, cdgb, cddb, cdsb;
    double xgtg = 0.0, xgtd = 0.0, xgts = 0.0, xgtb = 0.0;
    double dxpart = 0.0, sxpart = 0.0;
    double ddxpart_dVd = 0.0, ddxpart_dVg = 0.0, ddxpart_dVb = 0.0, ddxpart_dVs = 0.0;
    double dsxpart_dVd = 0.0, dsxpart_dVg = 0.0, dsxpart_dVb = 0.0, dsxpart_dVs = 0.0;
    double xcqgb = 0.0, xcqdb = 0.0, xcqsb = 0.0, xcqbb = 0.0;

    if (here.BSIM3v32mode >= 0) {
        // Forward mode
        Gm = here.BSIM3v32gm;
        Gmbs = here.BSIM3v32gmbs;
        FwdSum = Gm + Gmbs;
        RevSum = 0.0;

        gbbdp = -(here.BSIM3v32gbds);
        gbbsp = here.BSIM3v32gbds + here.BSIM3v32gbgs + here.BSIM3v32gbbs;
        gbdpg = here.BSIM3v32gbgs;
        gbdpb = here.BSIM3v32gbbs;
        gbdpdp = here.BSIM3v32gbds;
        gbdpsp = -(gbdpg + gbdpb + gbdpdp);

        gbspdp = 0.0;
        gbspg = 0.0;
        gbspb = 0.0;
        gbspsp = 0.0;

        if (here.BSIM3v32nqsMod == 0) {
            cggb = here.BSIM3v32cggb;
            cgsb = here.BSIM3v32cgsb;
            cgdb = here.BSIM3v32cgdb;
            cbgb = here.BSIM3v32cbgb;
            cbsb = here.BSIM3v32cbsb;
            cbdb = here.BSIM3v32cbdb;
            cdgb = here.BSIM3v32cdgb;
            cdsb = here.BSIM3v32cdsb;
            cddb = here.BSIM3v32cddb;
            xgtg = xgtd = xgts = xgtb = 0.0;
            sxpart = 0.6;
            dxpart = 0.4;
        } else {
            // NQS mode
            cggb = cgdb = cgsb = 0.0;
            cbgb = cbdb = cbsb = 0.0;
            cdgb = cddb = cdsb = 0.0;
            xgtg = here.BSIM3v32gtg;
            xgtd = here.BSIM3v32gtd;
            xgts = here.BSIM3v32gts;
            xgtb = here.BSIM3v32gtb;
            xcqgb = here.BSIM3v32cqgb;
            xcqdb = here.BSIM3v32cqdb;
            xcqsb = here.BSIM3v32cqsb;
            xcqbb = here.BSIM3v32cqbb;

            double CoxWL = model->BSIM3v32cox * here.pParam->BSIM3v32weffCV
                         * here.pParam->BSIM3v32leffCV;
            double qcheq = -(here.BSIM3v32qgate + here.BSIM3v32qbulk);
            if (std::abs(qcheq) <= 1.0e-5 * CoxWL) {
                if (model->BSIM3v32xpart < 0.5) dxpart = 0.4;
                else if (model->BSIM3v32xpart > 0.5) dxpart = 0.0;
                else dxpart = 0.5;
            } else {
                dxpart = here.BSIM3v32qdrn / qcheq;
                double Cdd_nqs = here.BSIM3v32cddb;
                double Csd_nqs = -(here.BSIM3v32cgdb + here.BSIM3v32cddb + here.BSIM3v32cbdb);
                ddxpart_dVd = (Cdd_nqs - dxpart * (Cdd_nqs + Csd_nqs)) / qcheq;
                double Cdg_nqs = here.BSIM3v32cdgb;
                double Csg_nqs = -(here.BSIM3v32cggb + here.BSIM3v32cdgb + here.BSIM3v32cbgb);
                ddxpart_dVg = (Cdg_nqs - dxpart * (Cdg_nqs + Csg_nqs)) / qcheq;
                double Cds_nqs = here.BSIM3v32cdsb;
                double Css_nqs = -(here.BSIM3v32cgsb + here.BSIM3v32cdsb + here.BSIM3v32cbsb);
                ddxpart_dVs = (Cds_nqs - dxpart * (Cds_nqs + Css_nqs)) / qcheq;
                ddxpart_dVb = -(ddxpart_dVd + ddxpart_dVg + ddxpart_dVs);
            }
            sxpart = 1.0 - dxpart;
            dsxpart_dVd = -ddxpart_dVd;
            dsxpart_dVg = -ddxpart_dVg;
            dsxpart_dVs = -ddxpart_dVs;
            dsxpart_dVb = -(dsxpart_dVd + dsxpart_dVg + dsxpart_dVs);
        }
    } else {
        // Reverse mode
        Gm = -here.BSIM3v32gm;
        Gmbs = -here.BSIM3v32gmbs;
        FwdSum = 0.0;
        RevSum = -(Gm + Gmbs);

        gbbsp = -(here.BSIM3v32gbds);
        gbbdp = here.BSIM3v32gbds + here.BSIM3v32gbgs + here.BSIM3v32gbbs;
        gbdpg = 0.0;
        gbdpsp = 0.0;
        gbdpb = 0.0;
        gbdpdp = 0.0;
        gbspg = here.BSIM3v32gbgs;
        gbspsp = here.BSIM3v32gbds;
        gbspb = here.BSIM3v32gbbs;
        gbspdp = -(gbspg + gbspsp + gbspb);

        if (here.BSIM3v32nqsMod == 0) {
            cggb = here.BSIM3v32cggb;
            cgsb = here.BSIM3v32cgdb;     // swapped
            cgdb = here.BSIM3v32cgsb;
            cbgb = here.BSIM3v32cbgb;
            cbsb = here.BSIM3v32cbdb;     // swapped
            cbdb = here.BSIM3v32cbsb;
            cdgb = -(here.BSIM3v32cdgb + cggb + cbgb);
            cdsb = -(here.BSIM3v32cddb + cgsb + cbsb);
            cddb = -(here.BSIM3v32cdsb + cgdb + cbdb);
            xgtg = xgtd = xgts = xgtb = 0.0;
            sxpart = 0.4;
            dxpart = 0.6;
        } else {
            // NQS reverse mode
            cggb = cgdb = cgsb = 0.0;
            cbgb = cbdb = cbsb = 0.0;
            cdgb = cddb = cdsb = 0.0;
            xgtg = here.BSIM3v32gtg;
            xgtd = here.BSIM3v32gts;  // swapped
            xgts = here.BSIM3v32gtd;
            xgtb = here.BSIM3v32gtb;
            xcqgb = here.BSIM3v32cqgb;
            xcqdb = here.BSIM3v32cqsb;  // swapped
            xcqsb = here.BSIM3v32cqdb;
            xcqbb = here.BSIM3v32cqbb;

            double CoxWL = model->BSIM3v32cox * here.pParam->BSIM3v32weffCV
                         * here.pParam->BSIM3v32leffCV;
            double qcheq = -(here.BSIM3v32qgate + here.BSIM3v32qbulk);
            if (std::abs(qcheq) <= 1.0e-5 * CoxWL) {
                if (model->BSIM3v32xpart < 0.5) sxpart = 0.4;
                else if (model->BSIM3v32xpart > 0.5) sxpart = 0.0;
                else sxpart = 0.5;
            } else {
                sxpart = here.BSIM3v32qdrn / qcheq;
                double Css_nqs = here.BSIM3v32cddb;
                double Cds_nqs = -(here.BSIM3v32cgdb + here.BSIM3v32cddb + here.BSIM3v32cbdb);
                dsxpart_dVs = (Css_nqs - sxpart * (Css_nqs + Cds_nqs)) / qcheq;
                double Csg_nqs = here.BSIM3v32cdgb;
                double Cdg_nqs = -(here.BSIM3v32cggb + here.BSIM3v32cdgb + here.BSIM3v32cbgb);
                dsxpart_dVg = (Csg_nqs - sxpart * (Csg_nqs + Cdg_nqs)) / qcheq;
                double Csd_nqs = here.BSIM3v32cdsb;
                double Cdd_nqs = -(here.BSIM3v32cgsb + here.BSIM3v32cdsb + here.BSIM3v32cbsb);
                dsxpart_dVd = (Csd_nqs - sxpart * (Csd_nqs + Cdd_nqs)) / qcheq;
                dsxpart_dVb = -(dsxpart_dVd + dsxpart_dVg + dsxpart_dVs);
            }
            dxpart = 1.0 - sxpart;
            ddxpart_dVd = -dsxpart_dVd;
            ddxpart_dVg = -dsxpart_dVg;
            ddxpart_dVs = -dsxpart_dVs;
            ddxpart_dVb = -(ddxpart_dVd + ddxpart_dVg + ddxpart_dVs);
        }
    }

    double T1 = 0.0;
    if (state0_ && state_base_ >= 0)
        T1 = state0_[state_base_ + 16] * here.BSIM3v32gtau;  // qdef * gtau

    const double gdpr  = here.BSIM3v32drainConductance;
    const double gspr  = here.BSIM3v32sourceConductance;
    const double gds   = here.BSIM3v32gds;
    const double gbd   = here.BSIM3v32gbd;
    const double gbs   = here.BSIM3v32gbs;
    const double capbd = here.BSIM3v32capbd;
    const double capbs = here.BSIM3v32capbs;

    const double GSoverlapCap = here.BSIM3v32cgso;
    const double GDoverlapCap = here.BSIM3v32cgdo;
    const double GBoverlapCap = here.pParam->BSIM3v32cgbo;

    // Capacitance intermediate values
    double C_DPg  = cdgb - GDoverlapCap;
    double C_DPdp = cddb + capbd + GDoverlapCap;
    double C_DPs  = cdsb;
    double C_SPg  = -(cggb + cbgb + cdgb + GSoverlapCap);
    double C_SPd  = -(cgdb + cbdb + cddb);
    double C_SPsp = capbs + GSoverlapCap - (cgsb + cbsb + cdsb);
    double C_Gg   = cggb + GDoverlapCap + GSoverlapCap + GBoverlapCap;
    double C_Gd   = cgdb - GDoverlapCap;
    double C_Gs   = cgsb - GSoverlapCap;
    double C_Bg   = cbgb - GBoverlapCap;
    double C_Bd   = cbdb - capbd;
    double C_Bs   = cbsb - capbs;

    // ---- Stamp G matrix (conductance) ----

    // D-D
    G.add(here.BSIM3v32DdPtr, m * gdpr);
    // S-S
    G.add(here.BSIM3v32SsPtr, m * gspr);
    // B-B
    G.add(here.BSIM3v32BbPtr, m * (gbd + gbs - here.BSIM3v32gbbs));

    // DP-DP
    G.add(here.BSIM3v32DPdpPtr, m * (gdpr + gds + gbd + RevSum
          + dxpart * xgtd + T1 * ddxpart_dVd + gbdpdp));
    // SP-SP
    G.add(here.BSIM3v32SPspPtr, m * (gspr + gds + gbs + FwdSum
          + sxpart * xgts + T1 * dsxpart_dVs + gbspsp));

    // D-DP
    G.add(here.BSIM3v32DdpPtr, m * (-gdpr));
    // S-SP
    G.add(here.BSIM3v32SspPtr, m * (-gspr));

    // B-G
    G.add(here.BSIM3v32BgPtr, m * (-here.BSIM3v32gbgs));
    // B-DP
    G.add(here.BSIM3v32BdpPtr, m * (-(gbd - gbbdp)));
    // B-SP
    G.add(here.BSIM3v32BspPtr, m * (-(gbs - gbbsp)));

    // DP-D
    G.add(here.BSIM3v32DPdPtr, m * (-gdpr));
    // DP-G
    G.add(here.BSIM3v32DPgPtr, m * (Gm + dxpart * xgtg + T1 * ddxpart_dVg + gbdpg));
    // DP-B
    G.add(here.BSIM3v32DPbPtr, m * (-(gbd - Gmbs - dxpart * xgtb - T1 * ddxpart_dVb - gbdpb)));
    // DP-SP
    G.add(here.BSIM3v32DPspPtr, m * (-(gds + FwdSum - dxpart * xgts - T1 * ddxpart_dVs - gbdpsp)));

    // SP-G
    G.add(here.BSIM3v32SPgPtr, m * (-(Gm - sxpart * xgtg - T1 * dsxpart_dVg - gbspg)));
    // SP-S
    G.add(here.BSIM3v32SPsPtr, m * (-gspr));
    // SP-B
    G.add(here.BSIM3v32SPbPtr, m * (-(gbs + Gmbs - sxpart * xgtb - T1 * dsxpart_dVb - gbspb)));
    // SP-DP
    G.add(here.BSIM3v32SPdpPtr, m * (-(gds + RevSum - sxpart * xgtd - T1 * dsxpart_dVd - gbspdp)));

    // G-G, G-B, G-DP, G-SP (NQS xgt terms)
    G.add(here.BSIM3v32GgPtr, m * (-xgtg));
    G.add(here.BSIM3v32GbPtr, m * (-xgtb));
    G.add(here.BSIM3v32GdpPtr, m * (-xgtd));
    G.add(here.BSIM3v32GspPtr, m * (-xgts));

    // ---- Stamp C matrix (capacitance) ----

    // G row
    C.add(here.BSIM3v32GgPtr, m * C_Gg);
    C.add(here.BSIM3v32GdpPtr, m * C_Gd);
    C.add(here.BSIM3v32GspPtr, m * C_Gs);
    C.add(here.BSIM3v32GbPtr, m * (-(C_Gg + C_Gd + C_Gs)));

    // B row
    C.add(here.BSIM3v32BgPtr, m * C_Bg);
    C.add(here.BSIM3v32BdpPtr, m * C_Bd);
    C.add(here.BSIM3v32BspPtr, m * C_Bs);
    C.add(here.BSIM3v32BbPtr, m * (-(C_Bg + C_Bd + C_Bs)));

    // DP row
    C.add(here.BSIM3v32DPgPtr, m * C_DPg);
    C.add(here.BSIM3v32DPdpPtr, m * C_DPdp);
    C.add(here.BSIM3v32DPspPtr, m * C_DPs);
    C.add(here.BSIM3v32DPbPtr, m * (-(C_DPg + C_DPdp + C_DPs)));

    // SP row
    C.add(here.BSIM3v32SPgPtr, m * C_SPg);
    C.add(here.BSIM3v32SPdpPtr, m * C_SPd);
    C.add(here.BSIM3v32SPspPtr, m * C_SPsp);
    C.add(here.BSIM3v32SPbPtr, m * (-(C_SPg + C_SPd + C_SPsp)));

    // NQS Q node stamps
    if (here.BSIM3v32nqsMod) {
        constexpr double ScalingFactor = 1.0e-9;

        // C: Q-Q
        C.add(here.BSIM3v32QqPtr, m * ScalingFactor);
        // C: Q-g, Q-dp, Q-sp, Q-b
        C.add(here.BSIM3v32QgPtr, m * (-xcqgb));
        C.add(here.BSIM3v32QdpPtr, m * (-xcqdb));
        C.add(here.BSIM3v32QspPtr, m * (-xcqsb));
        C.add(here.BSIM3v32QbPtr, m * (-xcqbb));

        // G: Q-Q
        G.add(here.BSIM3v32QqPtr, m * here.BSIM3v32gtau);
        // G: DP-Q, SP-Q, G-Q
        G.add(here.BSIM3v32DPqPtr, m * (dxpart * here.BSIM3v32gtau));
        G.add(here.BSIM3v32SPqPtr, m * (sxpart * here.BSIM3v32gtau));
        G.add(here.BSIM3v32GqPtr, m * (-here.BSIM3v32gtau));
        // G: Q-g, Q-dp, Q-sp, Q-b
        G.add(here.BSIM3v32QgPtr, m * xgtg);
        G.add(here.BSIM3v32QdpPtr, m * xgtd);
        G.add(here.BSIM3v32QspPtr, m * xgts);
        G.add(here.BSIM3v32QbPtr, m * xgtb);
    }
}

// ---------------------------------------------------------------------------
// compute_trunc -- device-specific local truncation error
//
// BSIM3v32 charge state variables:
//   qb  at offset 4  (cqb at 5)
//   qg  at offset 6  (cqg at 7)
//   qd  at offset 8  (cqd at 9)
// ---------------------------------------------------------------------------
double BSIM3v32Device::compute_trunc(const IntegratorCtx& ctx,
                                      const SimOptions& opts) const {
    if (ctx.order < 1 || ctx.delta <= 0.0)
        return 1e30;
    if (!state0_ || !state1_ || !state2_ || !state3_)
        return 1e30;

    const double* states[] = {state0_, state1_, state2_, state3_};
    double dt_min = 1e30;
    static const int charge_offsets[] = {4, 6, 8};  // qb, qg, qd
    for (int rel : charge_offsets)
        ckt_terr(state_base_ + rel, states, ctx, opts, dt_min);
    return dt_min;
}

// ---------------------------------------------------------------------------
// query_param -- post-simulation parameter query
// ---------------------------------------------------------------------------
std::optional<double>
BSIM3v32Device::query_param(const std::string& name) const {
    const std::string key = str_tolower(name);
    const double m = inst_.BSIM3v32m;

    // --- Operating-point parameters (scaled by multiplier m) ---
    if (key == "gm")                        return inst_.BSIM3v32gm * m;
    if (key == "gds")                       return inst_.BSIM3v32gds * m;
    if (key == "gmbs")                      return inst_.BSIM3v32gmbs * m;
    if (key == "vth" || key == "von")       return inst_.BSIM3v32von;
    if (key == "vdsat")                     return inst_.BSIM3v32vdsat;
    if (key == "id" || key == "cd")         return inst_.BSIM3v32cd * m;
    if (key == "ibs" || key == "cbs")       return inst_.BSIM3v32cbs * m;
    if (key == "ibd" || key == "cbd")       return inst_.BSIM3v32cbd * m;
    if (key == "gbd")                       return inst_.BSIM3v32gbd * m;
    if (key == "gbs")                       return inst_.BSIM3v32gbs * m;

    // --- Capacitances ---
    if (key == "cgg")                       return inst_.BSIM3v32cggb * m;
    if (key == "cgd")                       return inst_.BSIM3v32cgdb * m;
    if (key == "cgs")                       return inst_.BSIM3v32cgsb * m;
    if (key == "cdg")                       return inst_.BSIM3v32cdgb * m;
    if (key == "cdd")                       return inst_.BSIM3v32cddb * m;
    if (key == "cds")                       return inst_.BSIM3v32cdsb * m;
    if (key == "cbg")                       return inst_.BSIM3v32cbgb * m;
    if (key == "cbd_cap" || key == "cbdb")  return inst_.BSIM3v32cbdb * m;
    if (key == "cbs_cap" || key == "cbsb")  return inst_.BSIM3v32cbsb * m;

    // --- Charges ---
    if (key == "qg")                        return inst_.BSIM3v32qgate * m;
    if (key == "qd")                        return inst_.BSIM3v32qdrn * m;
    if (key == "qb")                        return inst_.BSIM3v32qbulk * m;

    // --- Junction capacitances ---
    if (key == "capbd")                     return inst_.BSIM3v32capbd * m;
    if (key == "capbs")                     return inst_.BSIM3v32capbs * m;

    // --- Terminal voltages from state vector ---
    if (state0_ && state_base_ >= 0) {
        if (key == "vbd")                   return state0_[state_base_ + 0];
        if (key == "vbs")                   return state0_[state_base_ + 1];
        if (key == "vgs")                   return state0_[state_base_ + 2];
        if (key == "vds")                   return state0_[state_base_ + 3];
    }

    // --- Geometry (no multiplier) ---
    if (key == "w")                         return inst_.BSIM3v32w;
    if (key == "l")                         return inst_.BSIM3v32l;
    if (key == "m")                         return inst_.BSIM3v32m;

    return std::nullopt;  // unrecognized parameter
}

// ---------------------------------------------------------------------------
// noise_sources -- BSIM3v32 noise model
//
// Implements noise sources from b3v32noi.c:
//   1. Drain/source series resistance thermal noise
//   2. Channel thermal noise (noiMod 1-4)
//   3. Flicker (1/f) noise
// ---------------------------------------------------------------------------
static constexpr double NOISE_MINLOG = 1e-38;

std::vector<Device::NoiseSource>
BSIM3v32Device::noise_sources(double freq,
                               const std::vector<double>& /*dc_solution*/) const {
    const auto* model = model_;
    const auto& inst  = inst_;
    const auto* pParam = inst.pParam;

    // Guard: if setup hasn't run yet pParam is null.
    if (!pParam) return {};

    const double m     = inst.BSIM3v32m;
    const double kT    = BOLTZMANN * sim_temp_;
    const double fourKT = 4.0 * kT;

    // Node indices (neospice convention)
    const int32_t dp_neo = ucb_to_neo(inst.BSIM3v32dNodePrime);
    const int32_t sp_neo = ucb_to_neo(inst.BSIM3v32sNodePrime);
    const int32_t d_neo  = ucb_to_neo(inst.BSIM3v32dNode);
    const int32_t s_neo  = ucb_to_neo(inst.BSIM3v32sNode);

    std::vector<NoiseSource> sources;
    sources.reserve(4);

    // -----------------------------------------------------------------------
    // 1. Drain / Source series resistance thermal noise
    // -----------------------------------------------------------------------
    const double gdpr = inst.BSIM3v32drainConductance;
    const double gspr = inst.BSIM3v32sourceConductance;

    if (gdpr > 0.0)
        sources.push_back({dp_neo, d_neo, fourKT * gdpr * m});
    if (gspr > 0.0)
        sources.push_back({sp_neo, s_neo, fourKT * gspr * m});

    // -----------------------------------------------------------------------
    // 2. Channel thermal noise (noiMod dependent)
    // -----------------------------------------------------------------------
    double channel_noise = 0.0;

    switch (model->BSIM3v32noiMod) {
      case 1:
      case 3: {
        // SPICE2 thermal: 2/3 * |gm + gds + gmbs|
        const double G_ch = 2.0 / 3.0 * std::abs(inst.BSIM3v32gm + inst.BSIM3v32gds
                                                    + inst.BSIM3v32gmbs);
        channel_noise = fourKT * G_ch * m;
        break;
      }
      case 2:
      case 4:
      default: {
        // BSIM3v32 thermal noise (v3.2.4 form):
        // ueff * |qinv| / (Leff^2 + ueff*|qinv|*rds)
        const double Leff = pParam->BSIM3v32leff;
        const double Leff2 = Leff * Leff;
        const double ueff = inst.BSIM3v32ueff;
        const double qinv_abs = std::abs(inst.BSIM3v32qinv);
        const double rds = inst.BSIM3v32rds;
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
        const double cd     = std::abs(inst.BSIM3v32cd);
        const double Leff   = pParam->BSIM3v32leff;

        switch (model->BSIM3v32noiMod) {
          case 1:
          case 4: {
            // SPICE2 1/f noise: kf * |Id|^af / (f^ef * Leff^2 * cox)
            if (model->BSIM3v32kf > 0.0 && model->BSIM3v32cox > 0.0 && Leff > 0.0) {
                flicker_noise = m * model->BSIM3v32kf
                    * std::exp(model->BSIM3v32af * std::log(std::max(cd, NOISE_MINLOG)))
                    / (std::pow(freq, model->BSIM3v32ef) * Leff * Leff * model->BSIM3v32cox);
            }
            break;
          }
          case 2:
          case 3: {
            // BSIM3v32 1/f noise: StrongInversionNoiseEval + weak-inversion term
            double vds = 0.0;
            if (state0_ && state_base_ >= 0)
                vds = std::abs(state0_[state_base_ + 3]);

            // StrongInversionNoiseEvalNew (v3.2.4)
            double esat = (pParam->BSIM3v32vsattemp > 0.0 && inst.BSIM3v32ueff > 0.0)
                         ? 2.0 * pParam->BSIM3v32vsattemp / inst.BSIM3v32ueff : 1e10;

            double DelClm = 0.0;
            if (model->BSIM3v32em > 0.0 && pParam->BSIM3v32litl > 0.0 && esat > 0.0) {
                double T0 = ((vds - inst.BSIM3v32Vdseff) / pParam->BSIM3v32litl
                              + model->BSIM3v32em) / esat;
                DelClm = pParam->BSIM3v32litl * std::log(std::max(T0, NOISE_MINLOG));
                if (DelClm < 0.0) DelClm = 0.0;
            }

            double EffFreq = std::pow(freq, model->BSIM3v32ef);
            const double CHARGE_Q = 1.60217663e-19;
            double T1 = CHARGE_Q * CHARGE_Q * 8.62e-5 * cd * sim_temp_ * inst.BSIM3v32ueff;
            double T2 = 1.0e8 * EffFreq * inst.BSIM3v32Abulk * model->BSIM3v32cox
                       * pParam->BSIM3v32leff * pParam->BSIM3v32leff;
            double N0 = model->BSIM3v32cox * inst.BSIM3v32Vgsteff / CHARGE_Q;
            double Nl = model->BSIM3v32cox * inst.BSIM3v32Vgsteff
                       * (1.0 - inst.BSIM3v32AbovVgst2Vtm * inst.BSIM3v32Vdseff) / CHARGE_Q;

            double T3 = model->BSIM3v32oxideTrapDensityA
                       * std::log(std::max((N0 + 2.0e14) / (Nl + 2.0e14), NOISE_MINLOG));
            double T4 = model->BSIM3v32oxideTrapDensityB * (N0 - Nl);
            double T5 = model->BSIM3v32oxideTrapDensityC * 0.5 * (N0 * N0 - Nl * Nl);

            double T6 = 8.62e-5 * sim_temp_ * cd * cd;
            double T7 = 1.0e8 * EffFreq * pParam->BSIM3v32leff
                       * pParam->BSIM3v32leff * pParam->BSIM3v32weff;
            double T8 = model->BSIM3v32oxideTrapDensityA
                       + model->BSIM3v32oxideTrapDensityB * Nl
                       + model->BSIM3v32oxideTrapDensityC * Nl * Nl;
            double T9 = (Nl + 2.0e14) * (Nl + 2.0e14);

            double Ssi = 0.0;
            if (T2 > 0.0)
                Ssi = T1 / T2 * (T3 + T4 + T5);
            if (T7 > 0.0 && T9 > 0.0)
                Ssi += T6 / T7 * DelClm * T8 / T9;

            // Weak-inversion scattering term
            double T10 = model->BSIM3v32oxideTrapDensityA * 8.62e-5 * sim_temp_;
            double T11 = pParam->BSIM3v32weff * pParam->BSIM3v32leff
                        * std::pow(freq, model->BSIM3v32ef) * 4.0e36;
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
