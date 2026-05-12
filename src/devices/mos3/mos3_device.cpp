#include "devices/mos3/mos3_device.hpp"

#include "core/circuit.hpp"        // Circuit::node, tls_integrator_ctx
#include "core/types.hpp"          // SimOptions defaults
#include "devices/ucb_device_init.hpp"
#include "devices/ucb_utils.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>

// Forward declarations for translated UCB functions.
namespace neospice::mos3 {
    int MOS3setup(Shim::Matrix*, MOS3Model*, Shim::Ckt*, int*);
    int MOS3temp(MOS3Model*, Shim::Ckt*);
    int MOS3load(MOS3Model*, Shim::Ckt*);
}

namespace neospice {

using namespace neospice::mos3;

// ---------------------------------------------------------------------------
// MOS3ModelCard destructor
// ---------------------------------------------------------------------------
MOS3ModelCard::~MOS3ModelCard() = default;

// ---------------------------------------------------------------------------
// make
// ---------------------------------------------------------------------------
std::unique_ptr<MOS3Device>
MOS3Device::make(std::string name,
        int32_t n_d, int32_t n_g, int32_t n_s, int32_t n_b,
        const Geom& geom, MOS3ModelCard& shared_card) {
    std::unique_ptr<MOS3Device> dev(new MOS3Device(std::move(name)));
    dev->model_ = &shared_card.ucb;

    auto& inst = dev->inst_;
    inst.MOS3name = dev->name().c_str();
    inst.MOS3modPtr = dev->model_;

    // Node wiring (UCB convention).
    inst.MOS3dNode = neo_to_ucb(n_d);
    inst.MOS3gNode = neo_to_ucb(n_g);
    inst.MOS3sNode = neo_to_ucb(n_s);
    inst.MOS3bNode = neo_to_ucb(n_b);

    // Geometry.
    inst.MOS3w = geom.W;
    inst.MOS3wGiven = geom.wGiven ? 1 : 0;
    inst.MOS3l = geom.L;
    inst.MOS3lGiven = geom.lGiven ? 1 : 0;
    inst.MOS3drainArea = geom.AD;
    inst.MOS3drainAreaGiven = (geom.AD != 0.0) ? 1 : 0;
    inst.MOS3sourceArea = geom.AS;
    inst.MOS3sourceAreaGiven = (geom.AS != 0.0) ? 1 : 0;
    inst.MOS3drainPerimiter = geom.PD;
    inst.MOS3drainPerimiterGiven = (geom.PD != 0.0) ? 1 : 0;
    inst.MOS3sourcePerimiter = geom.PS;
    inst.MOS3sourcePerimiterGiven = (geom.PS != 0.0) ? 1 : 0;
    inst.MOS3drainSquares = geom.NRD;
    inst.MOS3drainSquaresGiven = (geom.NRD != 0.0) ? 1 : 0;
    inst.MOS3sourceSquares = geom.NRS;
    inst.MOS3sourceSquaresGiven = (geom.NRS != 0.0) ? 1 : 0;
    inst.MOS3m = geom.M;
    inst.MOS3mGiven = (geom.M != 1.0) ? 1 : 0;

    // Thread onto the shared model's instance list.
    inst.MOS3nextInstance = shared_card.ucb.MOS3instances;
    shared_card.ucb.MOS3instances = &inst;

    // Remember the widest real node index for ghost array sizing.
    int32_t widest = -1;
    for (int32_t n : {n_d, n_g, n_s, n_b}) if (n > widest) widest = n;
    dev->max_neo_node_ = widest;

    return dev;
}

// ---------------------------------------------------------------------------
// declare_internal_nodes
// ---------------------------------------------------------------------------
void MOS3Device::declare_internal_nodes(Circuit& ckt) {
    ucb_declare_internal_nodes<Shim::Matrix, Shim::Ckt>(
        ckt, name_,
        [this](Shim::Matrix& m, Shim::Ckt& c) {
            UCB_SPLICE_INSTANCE(MOS3);
            int states = 0;
            int rc = MOS3setup(&m, model_, &c, &states);
            UCB_UNSPLICE_INSTANCE(MOS3);
            return rc;
        },
        "MOS3setup", journal_, max_neo_node_);
}

// ---------------------------------------------------------------------------
// stamp_pattern
// ---------------------------------------------------------------------------
void MOS3Device::stamp_pattern(SparsityBuilder& builder) const {
    ucb_stamp_pattern(journal_, builder);
}

// ---------------------------------------------------------------------------
// assign_offsets
// ---------------------------------------------------------------------------
void MOS3Device::assign_offsets(const SparsityPattern& pattern) {
    const auto offsets = ucb_compute_offsets(journal_, pattern);

#define RESOLVE(f)                                                       \
    do {                                                                 \
        if (inst_.f >= 0)                                                \
            inst_.f = offsets[inst_.f];                                  \
    } while (0)

    RESOLVE(MOS3DdPtr);
    RESOLVE(MOS3GgPtr);
    RESOLVE(MOS3SsPtr);
    RESOLVE(MOS3BbPtr);
    RESOLVE(MOS3DPdpPtr);
    RESOLVE(MOS3SPspPtr);
    RESOLVE(MOS3DdpPtr);
    RESOLVE(MOS3GbPtr);
    RESOLVE(MOS3GdpPtr);
    RESOLVE(MOS3GspPtr);
    RESOLVE(MOS3SspPtr);
    RESOLVE(MOS3BdpPtr);
    RESOLVE(MOS3BspPtr);
    RESOLVE(MOS3DPspPtr);
    RESOLVE(MOS3DPdPtr);
    RESOLVE(MOS3BgPtr);
    RESOLVE(MOS3DPgPtr);
    RESOLVE(MOS3SPgPtr);
    RESOLVE(MOS3SPsPtr);
    RESOLVE(MOS3DPbPtr);
    RESOLVE(MOS3SPbPtr);
    RESOLVE(MOS3SPdpPtr);

#undef RESOLVE
}

// ---------------------------------------------------------------------------
// set_state_ptrs
// ---------------------------------------------------------------------------
void MOS3Device::set_state_ptrs(double* s0, double* s1, double* s2, int32_t base) {
    state0_ = s0;
    state1_ = s1;
    state2_ = s2;
    state_base_ = base;
    inst_.MOS3states = base;
}

// ---------------------------------------------------------------------------
// evaluate
// ---------------------------------------------------------------------------
void MOS3Device::evaluate(const std::vector<double>& voltages,
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

    // First-call MOS3temp.
    if (!temp_done_) {
        int rc = MOS3temp(model_, &ckt);
        if (rc != Shim::OK) {
            throw std::runtime_error("MOS3temp failed with rc=" + std::to_string(rc));
        }
        temp_done_ = true;
    }

    // Splice this instance as sole member, call load, then restore.
    MOS3Instance* saved_head      = model_->MOS3instances;
    MOS3Instance* saved_next_inst = inst_.MOS3nextInstance;
    MOS3Model*    saved_next_mod  = model_->MOS3nextModel;
    model_->MOS3instances  = &inst_;
    inst_.MOS3nextInstance = nullptr;
    model_->MOS3nextModel  = nullptr;
    int rc = MOS3load(model_, &ckt);
    model_->MOS3instances  = saved_head;
    inst_.MOS3nextInstance = saved_next_inst;
    model_->MOS3nextModel  = saved_next_mod;
    if (rc != Shim::OK) {
        throw std::runtime_error("MOS3load failed with rc=" + std::to_string(rc));
    }

    last_noncon_ = ckt.CKTnoncon;

    // Fold ghost rhs contributions back into the real rhs.
    for (int32_t k = 1; k <= max_neo_node_ + 1 && k < n_ghost; ++k) {
        if (k - 1 < n_real) rhs[k - 1] += ghost_rhs_[k];
    }
}

// ---------------------------------------------------------------------------
// ac_stamp -- linearized small-signal AC stamp (G/C matrix split)
//
// Ported from ngspice mos3acld.c.  The AC solver combines the matrices
// as (G + jwC) at each frequency point.
// ---------------------------------------------------------------------------
void MOS3Device::ac_stamp(const std::vector<double>& /*voltages*/,
                            NumericMatrix& G,
                            NumericMatrix& C) {
    auto& inst = inst_;

    int xnrm, xrev;
    if (inst.MOS3mode < 0) {
        xnrm = 0;
        xrev = 1;
    } else {
        xnrm = 1;
        xrev = 0;
    }

    // Meyer's capacitor model (MOS3 uses EffectiveWidth and EffectiveLength)
    double EffectiveWidth = inst.MOS3w - 2 * model_->MOS3widthNarrow +
                            model_->MOS3widthAdjust;
    double EffectiveLength = inst.MOS3l - 2 * model_->MOS3latDiff +
                             model_->MOS3lengthAdjust;
    double GateSourceOverlapCap = model_->MOS3gateSourceOverlapCapFactor *
                                  inst.MOS3m * EffectiveWidth;
    double GateDrainOverlapCap  = model_->MOS3gateDrainOverlapCapFactor *
                                  inst.MOS3m * EffectiveWidth;
    double GateBulkOverlapCap   = model_->MOS3gateBulkOverlapCapFactor *
                                  inst.MOS3m * EffectiveLength;

    // capgs/capgd/capgb from state -- note the doubling (2x) per Meyer model
    double capgs = (state0_[inst.MOS3states + 4] +
                    state0_[inst.MOS3states + 4] +
                    GateSourceOverlapCap);
    double capgd = (state0_[inst.MOS3states + 7] +
                    state0_[inst.MOS3states + 7] +
                    GateDrainOverlapCap);
    double capgb = (state0_[inst.MOS3states + 10] +
                    state0_[inst.MOS3states + 10] +
                    GateBulkOverlapCap);
    double capbd = inst.MOS3capbd;
    double capbs = inst.MOS3capbs;

    // --- C matrix stamps (capacitances) ---
    C.add(inst.MOS3GgPtr,    capgd + capgs + capgb);
    C.add(inst.MOS3BbPtr,    capgb + capbd + capbs);
    C.add(inst.MOS3DPdpPtr,  capgd + capbd);
    C.add(inst.MOS3SPspPtr,  capgs + capbs);
    C.add(inst.MOS3GbPtr,   -capgb);
    C.add(inst.MOS3GdpPtr,  -capgd);
    C.add(inst.MOS3GspPtr,  -capgs);
    C.add(inst.MOS3BgPtr,   -capgb);
    C.add(inst.MOS3BdpPtr,  -capbd);
    C.add(inst.MOS3BspPtr,  -capbs);
    C.add(inst.MOS3DPgPtr,  -capgd);
    C.add(inst.MOS3DPbPtr,  -capbd);
    C.add(inst.MOS3SPgPtr,  -capgs);
    C.add(inst.MOS3SPbPtr,  -capbs);

    // --- G matrix stamps (conductances) ---
    G.add(inst.MOS3DdPtr,    inst.MOS3drainConductance);
    G.add(inst.MOS3SsPtr,    inst.MOS3sourceConductance);
    G.add(inst.MOS3BbPtr,    inst.MOS3gbd + inst.MOS3gbs);
    G.add(inst.MOS3DPdpPtr,  inst.MOS3drainConductance +
                              inst.MOS3gds + inst.MOS3gbd +
                              xrev * (inst.MOS3gm + inst.MOS3gmbs));
    G.add(inst.MOS3SPspPtr,  inst.MOS3sourceConductance +
                              inst.MOS3gds + inst.MOS3gbs +
                              xnrm * (inst.MOS3gm + inst.MOS3gmbs));
    G.add(inst.MOS3DdpPtr,  -inst.MOS3drainConductance);
    G.add(inst.MOS3SspPtr,  -inst.MOS3sourceConductance);
    G.add(inst.MOS3BdpPtr,  -inst.MOS3gbd);
    G.add(inst.MOS3BspPtr,  -inst.MOS3gbs);
    G.add(inst.MOS3DPdPtr,  -inst.MOS3drainConductance);
    G.add(inst.MOS3DPgPtr,   (xnrm - xrev) * inst.MOS3gm);
    G.add(inst.MOS3DPbPtr,  -inst.MOS3gbd + (xnrm - xrev) * inst.MOS3gmbs);
    G.add(inst.MOS3DPspPtr, -inst.MOS3gds -
                              xnrm * (inst.MOS3gm + inst.MOS3gmbs));
    G.add(inst.MOS3SPgPtr,  -(xnrm - xrev) * inst.MOS3gm);
    G.add(inst.MOS3SPsPtr,  -inst.MOS3sourceConductance);
    G.add(inst.MOS3SPbPtr,  -inst.MOS3gbs - (xnrm - xrev) * inst.MOS3gmbs);
    G.add(inst.MOS3SPdpPtr, -inst.MOS3gds -
                              xrev * (inst.MOS3gm + inst.MOS3gmbs));
}

// ---------------------------------------------------------------------------
// compute_trunc -- device-specific local truncation error for time stepping
//
// Charge offsets: qgs=5, qgd=8, qgb=11, qbd=13, qbs=15
// ---------------------------------------------------------------------------
double MOS3Device::compute_trunc(const IntegratorCtx& ctx,
                              const SimOptions& opts) const {
    if (ctx.order < 2 || ctx.delta <= 0.0)
        return 1e30;

    if (!state0_ || !state1_ || !state2_)
        return 1e30;

    const double lte_coeff = ctx.lte_coefficient();
    const double h0 = ctx.delta;
    const double h1 = ctx.delta_old[1];
    if (h1 <= 0.0) return 1e30;

    // Charge state variable offsets: qgs=5, qgd=8, qgb=11, qbd=13, qbs=15
    static const int charge_offsets[] = {5, 8, 11, 13, 15};
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
// device_converged
// ---------------------------------------------------------------------------
bool MOS3Device::device_converged() const {
    return last_noncon_ == 0;
}

// ---------------------------------------------------------------------------
// set_ic -- Initial conditions for VDS, VGS, VBS
// ---------------------------------------------------------------------------
void MOS3Device::set_ic(double vds, bool vds_given,
                        double vgs, bool vgs_given,
                        double vbs, bool vbs_given) {
    if (vds_given) { inst_.MOS3icVDS = vds; inst_.MOS3icVDSGiven = 1; }
    if (vgs_given) { inst_.MOS3icVGS = vgs; inst_.MOS3icVGSGiven = 1; }
    if (vbs_given) { inst_.MOS3icVBS = vbs; inst_.MOS3icVBSGiven = 1; }
}

// ---------------------------------------------------------------------------
// query_param -- post-simulation parameter query
// ---------------------------------------------------------------------------
std::optional<double>
MOS3Device::query_param(const std::string& name) const {
    const std::string key = str_tolower(name);
    const double m = inst_.MOS3m;

    // --- Operating-point voltages from state vector ---
    if (state0_ && state_base_ >= 0) {
        if (key == "vbd") return state0_[inst_.MOS3states + 0];
        if (key == "vbs") return state0_[inst_.MOS3states + 1];
        if (key == "vgs") return state0_[inst_.MOS3states + 2];
        if (key == "vds") return state0_[inst_.MOS3states + 3];
    }

    // --- Small-signal parameters (from instance, scaled by m) ---
    if (key == "id" || key == "cd") return inst_.MOS3cd * m;
    if (key == "gm")   return inst_.MOS3gm * m;
    if (key == "gds")  return inst_.MOS3gds * m;
    if (key == "gmb" || key == "gmbs") return inst_.MOS3gmbs * m;
    if (key == "gbd")  return inst_.MOS3gbd * m;
    if (key == "gbs")  return inst_.MOS3gbs * m;
    if (key == "vth" || key == "von") return inst_.MOS3von;
    if (key == "vdsat") return inst_.MOS3vdsat;

    // --- Capacitances ---
    if (key == "cbd" || key == "capbd") return inst_.MOS3capbd * m;
    if (key == "cbs" || key == "capbs") return inst_.MOS3capbs * m;
    if (key == "cgs" || key == "capgs") return inst_.MOS3cgs * m;
    if (key == "cgd" || key == "capgd") return inst_.MOS3cgd * m;
    if (key == "cgb" || key == "capgb") return inst_.MOS3cgb * m;

    // --- Geometry ---
    if (key == "w") return inst_.MOS3w;
    if (key == "l") return inst_.MOS3l;
    if (key == "m") return inst_.MOS3m;

    return std::nullopt;
}

// ---------------------------------------------------------------------------
// noise_sources -- MOS3 noise contributions
//
// Sources (following ngspice mos3noi.c):
//   1. Drain resistance thermal noise:  S = 4kT * gd   (d <-> dp)
//   2. Source resistance thermal noise:  S = 4kT * gs   (s <-> sp)
//   3. Channel thermal noise:            S = 4kT * (2/3) * |gm|   (dp <-> sp)
//   4. Flicker noise:  S = KF * |Id|^AF / (f * EffW * EffL * Cox^2)
// ---------------------------------------------------------------------------
std::vector<Device::NoiseSource> MOS3Device::noise_sources(
        double freq, const std::vector<double>& /*dc_solution*/) const {
    if (!state0_ || state_base_ < 0)
        return {};

    std::vector<NoiseSource> sources;

    // Neospice node indices (ucb-1): -1 is ground.
    const int32_t d_node  = inst_.MOS3dNode - 1;
    const int32_t s_node  = inst_.MOS3sNode - 1;
    const int32_t dp_node = inst_.MOS3dNodePrime - 1;
    const int32_t sp_node = inst_.MOS3sNodePrime - 1;

    const double T = T_NOMINAL;
    const double m = inst_.MOS3m;

    // 1. Drain series resistance thermal noise
    if (inst_.MOS3drainConductance > 0.0) {
        double S_rd = 4.0 * BOLTZMANN * T * inst_.MOS3drainConductance;
        sources.push_back({d_node, dp_node, m * S_rd});
    }

    // 2. Source series resistance thermal noise
    if (inst_.MOS3sourceConductance > 0.0) {
        double S_rs = 4.0 * BOLTZMANN * T * inst_.MOS3sourceConductance;
        sources.push_back({s_node, sp_node, m * S_rs});
    }

    // 3. Channel thermal noise
    double gm = std::abs(inst_.MOS3gm);
    double S_ch = 4.0 * BOLTZMANN * T * (2.0 / 3.0) * gm;
    sources.push_back({dp_node, sp_node, m * S_ch});

    // 4. Flicker noise
    double KF = model_->MOS3fNcoef;
    double AF = model_->MOS3fNexp;
    double Id = std::abs(inst_.MOS3cd);
    if (KF > 0.0 && freq > 0.0 && Id > 0.0) {
        double EffectiveWidth = inst_.MOS3w - 2 * model_->MOS3widthNarrow;
        double EffectiveLength = inst_.MOS3l - 2 * model_->MOS3latDiff;
        double coxSquared;
        if (model_->MOS3oxideCapFactor == 0.0) {
            // Assume tox = 1e-7 (same as ngspice default)
            coxSquared = 3.9 * 8.854214871e-12 / 1e-7;
        } else {
            coxSquared = model_->MOS3oxideCapFactor;
        }
        coxSquared *= coxSquared;
        double S_fl = KF * std::pow(Id, AF) /
                      (freq * EffectiveWidth * EffectiveLength * coxSquared);
        sources.push_back({dp_node, sp_node, m * S_fl});
    }

    return sources;
}

} // namespace neospice
