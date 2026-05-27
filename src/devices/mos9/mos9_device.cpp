#include "devices/mos9/mos9_device.hpp"

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
#include "devices/ckt_terr.hpp"

// Forward declarations for translated UCB functions.
namespace neospice::mos9 {
    int MOS9setup(Shim::Matrix*, MOS9Model*, Shim::Ckt*, int*);
    int MOS9temp(MOS9Model*, Shim::Ckt*);
    int MOS9load(MOS9Model*, Shim::Ckt*);
}

namespace neospice {

using namespace neospice::mos9;

// ---------------------------------------------------------------------------
// MOS9ModelCard destructor
// ---------------------------------------------------------------------------
MOS9ModelCard::~MOS9ModelCard() = default;

// ---------------------------------------------------------------------------
// make
// ---------------------------------------------------------------------------
std::unique_ptr<MOS9Device>
MOS9Device::make(std::string name,
        int32_t n_d, int32_t n_g, int32_t n_s, int32_t n_b,
        const Geom& geom, MOS9ModelCard& shared_card) {
    std::unique_ptr<MOS9Device> dev(new MOS9Device(std::move(name)));
    dev->model_ = &shared_card.ucb;
    dev->ext_nodes_ = {n_d, n_g, n_s, n_b};

    auto& inst = dev->inst_;
    inst.MOS9name = dev->name().c_str();
    inst.MOS9modPtr = dev->model_;

    // Node wiring (UCB convention).
    inst.MOS9dNode = neo_to_ucb(n_d);
    inst.MOS9gNode = neo_to_ucb(n_g);
    inst.MOS9sNode = neo_to_ucb(n_s);
    inst.MOS9bNode = neo_to_ucb(n_b);

    // Geometry.
    inst.MOS9w = geom.W;
    inst.MOS9wGiven = geom.wGiven ? 1 : 0;
    inst.MOS9l = geom.L;
    inst.MOS9lGiven = geom.lGiven ? 1 : 0;
    inst.MOS9drainArea = geom.AD;
    inst.MOS9drainAreaGiven = (geom.AD != 0.0) ? 1 : 0;
    inst.MOS9sourceArea = geom.AS;
    inst.MOS9sourceAreaGiven = (geom.AS != 0.0) ? 1 : 0;
    inst.MOS9drainPerimiter = geom.PD;
    inst.MOS9drainPerimiterGiven = (geom.PD != 0.0) ? 1 : 0;
    inst.MOS9sourcePerimiter = geom.PS;
    inst.MOS9sourcePerimiterGiven = (geom.PS != 0.0) ? 1 : 0;
    inst.MOS9drainSquares = geom.NRD;
    inst.MOS9drainSquaresGiven = (geom.NRD != 0.0) ? 1 : 0;
    inst.MOS9sourceSquares = geom.NRS;
    inst.MOS9sourceSquaresGiven = (geom.NRS != 0.0) ? 1 : 0;
    inst.MOS9m = geom.M;
    inst.MOS9mGiven = (geom.M != 1.0) ? 1 : 0;

    // Thread onto the shared model's instance list.
    inst.MOS9nextInstance = shared_card.ucb.MOS9instances;
    shared_card.ucb.MOS9instances = &inst;

    // Remember the widest real node index for ghost array sizing.
    int32_t widest = -1;
    for (int32_t n : {n_d, n_g, n_s, n_b}) if (n > widest) widest = n;
    dev->max_neo_node_ = widest;

    return dev;
}

// ---------------------------------------------------------------------------
// declare_internal_nodes
// ---------------------------------------------------------------------------
void MOS9Device::declare_internal_nodes(Circuit& ckt) {
    ucb_declare_internal_nodes<Shim::Matrix, Shim::Ckt>(
        ckt, name_,
        [this](Shim::Matrix& m, Shim::Ckt& c) {
            UCB_SPLICE_INSTANCE(MOS9);
            int states = 0;
            int rc = MOS9setup(&m, model_, &c, &states);
            UCB_UNSPLICE_INSTANCE(MOS9);
            return rc;
        },
        "MOS9setup", journal_, max_neo_node_);
}

// ---------------------------------------------------------------------------
// stamp_pattern
// ---------------------------------------------------------------------------
void MOS9Device::stamp_pattern(SparsityBuilder& builder) const {
    ucb_stamp_pattern(journal_, builder);
}

// ---------------------------------------------------------------------------
// assign_offsets
// ---------------------------------------------------------------------------
void MOS9Device::assign_offsets(const SparsityPattern& pattern) {
    const auto offsets = ucb_compute_offsets(journal_, pattern);

#define RESOLVE(f)                                                       \
    do {                                                                 \
        if (inst_.f >= 0)                                                \
            inst_.f = offsets[inst_.f];                                  \
    } while (0)

    RESOLVE(MOS9DdPtr);
    RESOLVE(MOS9GgPtr);
    RESOLVE(MOS9SsPtr);
    RESOLVE(MOS9BbPtr);
    RESOLVE(MOS9DPdpPtr);
    RESOLVE(MOS9SPspPtr);
    RESOLVE(MOS9DdpPtr);
    RESOLVE(MOS9GbPtr);
    RESOLVE(MOS9GdpPtr);
    RESOLVE(MOS9GspPtr);
    RESOLVE(MOS9SspPtr);
    RESOLVE(MOS9BdpPtr);
    RESOLVE(MOS9BspPtr);
    RESOLVE(MOS9DPspPtr);
    RESOLVE(MOS9DPdPtr);
    RESOLVE(MOS9BgPtr);
    RESOLVE(MOS9DPgPtr);
    RESOLVE(MOS9SPgPtr);
    RESOLVE(MOS9SPsPtr);
    RESOLVE(MOS9DPbPtr);
    RESOLVE(MOS9SPbPtr);
    RESOLVE(MOS9SPdpPtr);

#undef RESOLVE
}

// ---------------------------------------------------------------------------
// set_state_ptrs
// ---------------------------------------------------------------------------
void MOS9Device::set_state_ptrs(double* s0, double* s1, double* s2, double* s3, int32_t base) {
    state0_ = s0;
    state1_ = s1;
    state2_ = s2;
    state3_ = s3;
    state_base_ = base;
    inst_.MOS9states = base;
}

// ---------------------------------------------------------------------------
// evaluate
// ---------------------------------------------------------------------------
void MOS9Device::evaluate(const std::vector<double>& voltages,
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

    // First-call MOS9temp.
    if (!temp_done_) {
        int rc = MOS9temp(model_, &ckt);
        if (rc != Shim::OK) {
            throw std::runtime_error("MOS9temp failed with rc=" + std::to_string(rc));
        }
        temp_done_ = true;
    }

    // Splice this instance as sole member, call load, then restore.
    MOS9Instance* saved_head      = model_->MOS9instances;
    MOS9Instance* saved_next_inst = inst_.MOS9nextInstance;
    MOS9Model*    saved_next_mod  = model_->MOS9nextModel;
    model_->MOS9instances  = &inst_;
    inst_.MOS9nextInstance = nullptr;
    model_->MOS9nextModel  = nullptr;
    int rc = MOS9load(model_, &ckt);
    model_->MOS9instances  = saved_head;
    inst_.MOS9nextInstance = saved_next_inst;
    model_->MOS9nextModel  = saved_next_mod;
    if (rc != Shim::OK) {
        throw std::runtime_error("MOS9load failed with rc=" + std::to_string(rc));
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
// Ported from ngspice mos9acld.c.  The AC solver combines the matrices
// as (G + jwC) at each frequency point.
// ---------------------------------------------------------------------------
void MOS9Device::ac_stamp(const std::vector<double>& /*voltages*/,
                            NumericMatrix& G,
                            NumericMatrix& C) {
    auto& inst = inst_;

    int xnrm, xrev;
    if (inst.MOS9mode < 0) {
        xnrm = 0;
        xrev = 1;
    } else {
        xnrm = 1;
        xrev = 0;
    }

    // Meyer's capacitor model -- MOS9 uses width/length adjustments
    double EffectiveWidth = inst.MOS9w - 2 * model_->MOS9widthNarrow +
                            model_->MOS9widthAdjust;
    double EffectiveLength = inst.MOS9l - 2 * model_->MOS9latDiff +
                             model_->MOS9lengthAdjust;
    double GateSourceOverlapCap = model_->MOS9gateSourceOverlapCapFactor *
                                  inst.MOS9m * EffectiveWidth;
    double GateDrainOverlapCap  = model_->MOS9gateDrainOverlapCapFactor *
                                  inst.MOS9m * EffectiveWidth;
    double GateBulkOverlapCap   = model_->MOS9gateBulkOverlapCapFactor *
                                  inst.MOS9m * EffectiveLength;

    // capgs/capgd/capgb from state -- note the doubling (2x) per Meyer model
    double capgs = (state0_[inst.MOS9states + 4] +
                    state0_[inst.MOS9states + 4] +
                    GateSourceOverlapCap);
    double capgd = (state0_[inst.MOS9states + 7] +
                    state0_[inst.MOS9states + 7] +
                    GateDrainOverlapCap);
    double capgb = (state0_[inst.MOS9states + 10] +
                    state0_[inst.MOS9states + 10] +
                    GateBulkOverlapCap);
    double capbd = inst.MOS9capbd;
    double capbs = inst.MOS9capbs;

    // --- C matrix stamps (capacitances) ---
    C.add(inst.MOS9GgPtr,    capgd + capgs + capgb);
    C.add(inst.MOS9BbPtr,    capgb + capbd + capbs);
    C.add(inst.MOS9DPdpPtr,  capgd + capbd);
    C.add(inst.MOS9SPspPtr,  capgs + capbs);
    C.add(inst.MOS9GbPtr,   -capgb);
    C.add(inst.MOS9GdpPtr,  -capgd);
    C.add(inst.MOS9GspPtr,  -capgs);
    C.add(inst.MOS9BgPtr,   -capgb);
    C.add(inst.MOS9BdpPtr,  -capbd);
    C.add(inst.MOS9BspPtr,  -capbs);
    C.add(inst.MOS9DPgPtr,  -capgd);
    C.add(inst.MOS9DPbPtr,  -capbd);
    C.add(inst.MOS9SPgPtr,  -capgs);
    C.add(inst.MOS9SPbPtr,  -capbs);

    // --- G matrix stamps (conductances) ---
    G.add(inst.MOS9DdPtr,    inst.MOS9drainConductance);
    G.add(inst.MOS9SsPtr,    inst.MOS9sourceConductance);
    G.add(inst.MOS9BbPtr,    inst.MOS9gbd + inst.MOS9gbs);
    G.add(inst.MOS9DPdpPtr,  inst.MOS9drainConductance +
                              inst.MOS9gds + inst.MOS9gbd +
                              xrev * (inst.MOS9gm + inst.MOS9gmbs));
    G.add(inst.MOS9SPspPtr,  inst.MOS9sourceConductance +
                              inst.MOS9gds + inst.MOS9gbs +
                              xnrm * (inst.MOS9gm + inst.MOS9gmbs));
    G.add(inst.MOS9DdpPtr,  -inst.MOS9drainConductance);
    G.add(inst.MOS9SspPtr,  -inst.MOS9sourceConductance);
    G.add(inst.MOS9BdpPtr,  -inst.MOS9gbd);
    G.add(inst.MOS9BspPtr,  -inst.MOS9gbs);
    G.add(inst.MOS9DPdPtr,  -inst.MOS9drainConductance);
    G.add(inst.MOS9DPgPtr,   (xnrm - xrev) * inst.MOS9gm);
    G.add(inst.MOS9DPbPtr,  -inst.MOS9gbd + (xnrm - xrev) * inst.MOS9gmbs);
    G.add(inst.MOS9DPspPtr, -inst.MOS9gds -
                              xnrm * (inst.MOS9gm + inst.MOS9gmbs));
    G.add(inst.MOS9SPgPtr,  -(xnrm - xrev) * inst.MOS9gm);
    G.add(inst.MOS9SPsPtr,  -inst.MOS9sourceConductance);
    G.add(inst.MOS9SPbPtr,  -inst.MOS9gbs - (xnrm - xrev) * inst.MOS9gmbs);
    G.add(inst.MOS9SPdpPtr, -inst.MOS9gds -
                              xrev * (inst.MOS9gm + inst.MOS9gmbs));
}

// ---------------------------------------------------------------------------
// compute_trunc -- device-specific local truncation error for time stepping
//
// Charge offsets: qgs=5, qgd=8, qgb=11, qbd=13, qbs=15
// ---------------------------------------------------------------------------
double MOS9Device::compute_trunc(const IntegratorCtx& ctx,
                              const SimOptions& opts) const {
    if (ctx.order < 1 || ctx.delta <= 0.0)
        return 1e30;
    if (!state0_ || !state1_ || !state2_ || !state3_)
        return 1e30;

    const double* states[] = {state0_, state1_, state2_, state3_};
    double dt_min = 1e30;
    static const int charge_offsets[] = {5, 8, 11, 13, 15};  // qgs, qgd, qgb, qbd, qbs
    for (int rel : charge_offsets)
        ckt_terr(state_base_ + rel, states, ctx, opts, dt_min);
    return dt_min;
}

// ---------------------------------------------------------------------------
// device_converged
// ---------------------------------------------------------------------------
bool MOS9Device::device_converged() const {
    return last_noncon_ == 0;
}

// ---------------------------------------------------------------------------
// set_ic -- Initial conditions for VDS, VGS, VBS
// ---------------------------------------------------------------------------
void MOS9Device::set_ic(double vds, bool vds_given,
                        double vgs, bool vgs_given,
                        double vbs, bool vbs_given) {
    if (vds_given) { inst_.MOS9icVDS = vds; inst_.MOS9icVDSGiven = 1; }
    if (vgs_given) { inst_.MOS9icVGS = vgs; inst_.MOS9icVGSGiven = 1; }
    if (vbs_given) { inst_.MOS9icVBS = vbs; inst_.MOS9icVBSGiven = 1; }
}

// ---------------------------------------------------------------------------
// query_param -- post-simulation parameter query
// ---------------------------------------------------------------------------
std::optional<double>
MOS9Device::query_param(const std::string& name) const {
    const std::string key = str_tolower(name);
    const double m = inst_.MOS9m;

    // --- Operating-point voltages from state vector ---
    if (state0_ && state_base_ >= 0) {
        if (key == "vbd") return state0_[inst_.MOS9states + 0];
        if (key == "vbs") return state0_[inst_.MOS9states + 1];
        if (key == "vgs") return state0_[inst_.MOS9states + 2];
        if (key == "vds") return state0_[inst_.MOS9states + 3];
    }

    // --- Small-signal parameters (from instance, scaled by m) ---
    if (key == "id" || key == "cd") return inst_.MOS9cd * m;
    if (key == "gm")   return inst_.MOS9gm * m;
    if (key == "gds")  return inst_.MOS9gds * m;
    if (key == "gmb" || key == "gmbs") return inst_.MOS9gmbs * m;
    if (key == "gbd")  return inst_.MOS9gbd * m;
    if (key == "gbs")  return inst_.MOS9gbs * m;
    if (key == "vth" || key == "von") return inst_.MOS9von;
    if (key == "vdsat") return inst_.MOS9vdsat;

    // --- Capacitances ---
    if (key == "cbd" || key == "capbd") return inst_.MOS9capbd * m;
    if (key == "cbs" || key == "capbs") return inst_.MOS9capbs * m;
    if (key == "cgs" || key == "capgs") return inst_.MOS9cgs * m;
    if (key == "cgd" || key == "capgd") return inst_.MOS9cgd * m;
    if (key == "cgb" || key == "capgb") return inst_.MOS9cgb * m;

    // --- Geometry ---
    if (key == "w") return inst_.MOS9w;
    if (key == "l") return inst_.MOS9l;
    if (key == "m") return inst_.MOS9m;

    return std::nullopt;
}

// ---------------------------------------------------------------------------
// noise_sources -- MOS9 noise contributions
//
// Sources (following ngspice mos9noi.c):
//   1. Drain resistance thermal noise:  S = 4kT * gd   (d <-> dp)
//   2. Source resistance thermal noise:  S = 4kT * gs   (s <-> sp)
//   3. Channel thermal noise:            S = 4kT * (2/3) * |gm|   (dp <-> sp)
//   4. Flicker noise:  S = KF * |Id|^AF / (f * W_eff * m * Leff * Cox^2)
// ---------------------------------------------------------------------------
std::vector<Device::NoiseSource> MOS9Device::noise_sources(
        double freq, const std::vector<double>& /*dc_solution*/) const {
    if (!state0_ || state_base_ < 0)
        return {};

    std::vector<NoiseSource> sources;

    // Neospice node indices (ucb-1): -1 is ground.
    const int32_t d_node  = inst_.MOS9dNode - 1;
    const int32_t s_node  = inst_.MOS9sNode - 1;
    const int32_t dp_node = inst_.MOS9dNodePrime - 1;
    const int32_t sp_node = inst_.MOS9sNodePrime - 1;

    const double T = T_NOMINAL;
    const double m = inst_.MOS9m;

    // 1. Drain series resistance thermal noise
    if (inst_.MOS9drainConductance > 0.0) {
        double S_rd = 4.0 * BOLTZMANN * T * inst_.MOS9drainConductance;
        sources.push_back({d_node, dp_node, m * S_rd});
    }

    // 2. Source series resistance thermal noise
    if (inst_.MOS9sourceConductance > 0.0) {
        double S_rs = 4.0 * BOLTZMANN * T * inst_.MOS9sourceConductance;
        sources.push_back({s_node, sp_node, m * S_rs});
    }

    // 3. Channel thermal noise
    double gm = std::abs(inst_.MOS9gm);
    double S_ch = 4.0 * BOLTZMANN * T * (2.0 / 3.0) * gm;
    sources.push_back({dp_node, sp_node, m * S_ch});

    // 4. Flicker noise
    double KF = model_->MOS9fNcoef;
    double AF = model_->MOS9fNexp;
    double Id = std::abs(inst_.MOS9cd);
    if (KF > 0.0 && freq > 0.0 && Id > 0.0) {
        // MOS9 uses widthNarrow and latDiff adjustments
        double EffectiveWidth = inst_.MOS9w - 2 * model_->MOS9widthNarrow;
        double EffectiveLength = inst_.MOS9l - 2 * model_->MOS9latDiff;
        double coxSquared;
        if (model_->MOS9oxideCapFactor == 0.0) {
            // Assume tox = 1e-7 (same as ngspice)
            coxSquared = 3.9 * 8.854214871e-12 / 1e-7;
        } else {
            coxSquared = model_->MOS9oxideCapFactor;
        }
        coxSquared *= coxSquared;
        double S_fl = KF * std::pow(Id, AF) /
                      (freq * EffectiveWidth * m * EffectiveLength * coxSquared);
        sources.push_back({dp_node, sp_node, m * S_fl});
    }

    return sources;
}

} // namespace neospice
