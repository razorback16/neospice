#include "devices/mos2/mos2_device.hpp"

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
namespace neospice::mos2 {
    int MOS2setup(Shim::Matrix*, MOS2Model*, Shim::Ckt*, int*);
    int MOS2temp(MOS2Model*, Shim::Ckt*);
    int MOS2load(MOS2Model*, Shim::Ckt*);
}

namespace neospice {

using namespace neospice::mos2;

// ---------------------------------------------------------------------------
// MOS2ModelCard destructor
// ---------------------------------------------------------------------------
MOS2ModelCard::~MOS2ModelCard() = default;

// ---------------------------------------------------------------------------
// make
// ---------------------------------------------------------------------------
std::unique_ptr<MOS2Device>
MOS2Device::make(std::string name,
        int32_t n_d, int32_t n_g, int32_t n_s, int32_t n_b,
        const Geom& geom, MOS2ModelCard& shared_card) {
    std::unique_ptr<MOS2Device> dev(new MOS2Device(std::move(name)));
    dev->ext_nodes_ = {n_d, n_g, n_s, n_b};
    dev->model_ = &shared_card.ucb;

    auto& inst = dev->inst_;
    inst.MOS2name = dev->name().c_str();
    inst.MOS2modPtr = dev->model_;

    // Node wiring (UCB convention).
    inst.MOS2dNode = neo_to_ucb(n_d);
    inst.MOS2gNode = neo_to_ucb(n_g);
    inst.MOS2sNode = neo_to_ucb(n_s);
    inst.MOS2bNode = neo_to_ucb(n_b);

    // Geometry.
    inst.MOS2w = geom.W;
    inst.MOS2wGiven = geom.wGiven ? 1 : 0;
    inst.MOS2l = geom.L;
    inst.MOS2lGiven = geom.lGiven ? 1 : 0;
    inst.MOS2drainArea = geom.AD;
    inst.MOS2drainAreaGiven = (geom.AD != 0.0) ? 1 : 0;
    inst.MOS2sourceArea = geom.AS;
    inst.MOS2sourceAreaGiven = (geom.AS != 0.0) ? 1 : 0;
    inst.MOS2drainPerimiter = geom.PD;
    inst.MOS2drainPerimiterGiven = (geom.PD != 0.0) ? 1 : 0;
    inst.MOS2sourcePerimiter = geom.PS;
    inst.MOS2sourcePerimiterGiven = (geom.PS != 0.0) ? 1 : 0;
    inst.MOS2drainSquares = geom.NRD;
    inst.MOS2drainSquaresGiven = (geom.NRD != 0.0) ? 1 : 0;
    inst.MOS2sourceSquares = geom.NRS;
    inst.MOS2sourceSquaresGiven = (geom.NRS != 0.0) ? 1 : 0;
    inst.MOS2m = geom.M;
    inst.MOS2mGiven = (geom.M != 1.0) ? 1 : 0;

    // Thread onto the shared model's instance list.
    inst.MOS2nextInstance = shared_card.ucb.MOS2instances;
    shared_card.ucb.MOS2instances = &inst;

    // Remember the widest real node index for ghost array sizing.
    int32_t widest = -1;
    for (int32_t n : {n_d, n_g, n_s, n_b}) if (n > widest) widest = n;
    dev->max_neo_node_ = widest;

    return dev;
}

// ---------------------------------------------------------------------------
// declare_internal_nodes
// ---------------------------------------------------------------------------
void MOS2Device::declare_internal_nodes(Circuit& ckt) {
    ucb_declare_internal_nodes<Shim::Matrix, Shim::Ckt>(
        ckt, name_,
        [this](Shim::Matrix& m, Shim::Ckt& c) {
            UCB_SPLICE_INSTANCE(MOS2);
            int states = 0;
            int rc = MOS2setup(&m, model_, &c, &states);
            UCB_UNSPLICE_INSTANCE(MOS2);
            return rc;
        },
        "MOS2setup", journal_, max_neo_node_);
}

// ---------------------------------------------------------------------------
// stamp_pattern
// ---------------------------------------------------------------------------
void MOS2Device::stamp_pattern(SparsityBuilder& builder) const {
    ucb_stamp_pattern(journal_, builder);
}

// ---------------------------------------------------------------------------
// assign_offsets
// ---------------------------------------------------------------------------
void MOS2Device::assign_offsets(const SparsityPattern& pattern) {
    const auto offsets = ucb_compute_offsets(journal_, pattern);

#define RESOLVE(f)                                                       \
    do {                                                                 \
        if (inst_.f >= 0)                                                \
            inst_.f = offsets[inst_.f];                                  \
    } while (0)

    RESOLVE(MOS2DdPtr);
    RESOLVE(MOS2GgPtr);
    RESOLVE(MOS2SsPtr);
    RESOLVE(MOS2BbPtr);
    RESOLVE(MOS2DPdpPtr);
    RESOLVE(MOS2SPspPtr);
    RESOLVE(MOS2DdpPtr);
    RESOLVE(MOS2GbPtr);
    RESOLVE(MOS2GdpPtr);
    RESOLVE(MOS2GspPtr);
    RESOLVE(MOS2SspPtr);
    RESOLVE(MOS2BdpPtr);
    RESOLVE(MOS2BspPtr);
    RESOLVE(MOS2DPspPtr);
    RESOLVE(MOS2DPdPtr);
    RESOLVE(MOS2BgPtr);
    RESOLVE(MOS2DPgPtr);
    RESOLVE(MOS2SPgPtr);
    RESOLVE(MOS2SPsPtr);
    RESOLVE(MOS2DPbPtr);
    RESOLVE(MOS2SPbPtr);
    RESOLVE(MOS2SPdpPtr);

#undef RESOLVE
}

// ---------------------------------------------------------------------------
// set_state_ptrs
// ---------------------------------------------------------------------------
void MOS2Device::set_state_ptrs(double* s0, double* s1, double* s2, double* s3, int32_t base) {
    state0_ = s0;
    state1_ = s1;
    state2_ = s2;
    state3_ = s3;
    state_base_ = base;
    inst_.MOS2states = base;
}

// ---------------------------------------------------------------------------
// evaluate
// ---------------------------------------------------------------------------
void MOS2Device::evaluate(const std::vector<double>& voltages,
                            NumericMatrix& mat,
                            std::vector<double>& rhs) {
    const int n_real = static_cast<int>(rhs.size());
    const int n_ghost = (max_neo_node_ >= 0 ? max_neo_node_ + 1 : 0) + 1;
    const OneBasedEvalArrays* shared_arrays = tls_one_based_eval_arrays;
    const bool use_shared_arrays = shared_arrays != nullptr &&
        shared_arrays->rhs_old != nullptr && shared_arrays->rhs != nullptr &&
        max_neo_node_ + 1 < shared_arrays->size;

    if (!use_shared_arrays) {
        ghost_voltages_.assign(n_ghost, 0.0);
        ghost_rhs_.assign(n_ghost, 0.0);
        for (int32_t k = 0; k <= max_neo_node_ && k < n_real; ++k) {
            ghost_voltages_[k + 1] = voltages[k];
        }
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
    ckt.CKTdeviceGainFact = sim_opts->device_gain_fact; // [3B] variable-gain homotopy
    ckt.CKTreltol  = sim_opts->reltol;
    ckt.CKTabstol  = sim_opts->abstol;
    ckt.CKTvoltTol = sim_opts->vntol;
    ckt.CKTbypass  = 0;
    ckt.CKTnoncon  = 0;

    // State ring.
    ckt.CKTstate0 = state0_;
    ckt.CKTstate1 = state1_;
    ckt.CKTstate2 = state2_;

    // RHS / old-iterate pointers. Prefer the shared Newton arrays;
    // they match ngspice's circuit-global CKTrhs/CKTrhsOld storage.
    if (use_shared_arrays) {
        ckt.CKTrhs    = shared_arrays->rhs;
        ckt.CKTrhsOld = shared_arrays->rhs_old;
    } else {
        ckt.CKTrhs    = ghost_rhs_.data();
        ckt.CKTrhsOld = ghost_voltages_.data();
    }
    ckt.mat       = &mat;

    // First-call MOS2temp.
    if (!temp_done_) {
        int rc = MOS2temp(model_, &ckt);
        if (rc != Shim::OK) {
            throw std::runtime_error("MOS2temp failed with rc=" + std::to_string(rc));
        }
        temp_done_ = true;
    }

    // Splice this instance as sole member, call load, then restore.
    MOS2Instance* saved_head      = model_->MOS2instances;
    MOS2Instance* saved_next_inst = inst_.MOS2nextInstance;
    MOS2Model*    saved_next_mod  = model_->MOS2nextModel;
    model_->MOS2instances  = &inst_;
    inst_.MOS2nextInstance = nullptr;
    model_->MOS2nextModel  = nullptr;
    int rc = MOS2load(model_, &ckt);
    model_->MOS2instances  = saved_head;
    inst_.MOS2nextInstance = saved_next_inst;
    model_->MOS2nextModel  = saved_next_mod;
    if (rc != Shim::OK) {
        throw std::runtime_error("MOS2load failed with rc=" + std::to_string(rc));
    }

    last_noncon_ = ckt.CKTnoncon;
    last_reltol_ = sim_opts->reltol;
    last_abstol_ = sim_opts->abstol;

    // Private ghost arrays need folding. Shared arrays are folded once
    // by the Newton driver after all UCB-style devices have stamped.
    if (!use_shared_arrays) {
        for (int32_t k = 1; k <= max_neo_node_ + 1 && k < n_ghost; ++k) {
            if (k - 1 < n_real) rhs[k - 1] += ghost_rhs_[k];
        }
    }
}

// ---------------------------------------------------------------------------
// ac_stamp — linearized small-signal AC stamp (G/C matrix split)
//
// Ported from ngspice mos2acld.c.  The AC solver combines the matrices
// as (G + jwC) at each frequency point.
// ---------------------------------------------------------------------------
void MOS2Device::ac_stamp(const std::vector<double>& /*voltages*/,
                            NumericMatrix& G,
                            NumericMatrix& C) {
    auto& inst = inst_;

    int xnrm, xrev;
    if (inst.MOS2mode < 0) {
        xnrm = 0;
        xrev = 1;
    } else {
        xnrm = 1;
        xrev = 0;
    }

    // Meyer's capacitor model
    double EffectiveLength = inst.MOS2l - 2 * model_->MOS2latDiff;
    double GateSourceOverlapCap = model_->MOS2gateSourceOverlapCapFactor *
                                  inst.MOS2m * inst.MOS2w;
    double GateDrainOverlapCap  = model_->MOS2gateDrainOverlapCapFactor *
                                  inst.MOS2m * inst.MOS2w;
    double GateBulkOverlapCap   = model_->MOS2gateBulkOverlapCapFactor *
                                  inst.MOS2m * EffectiveLength;

    // capgs/capgd/capgb from state — note the doubling (2x) per Meyer model
    double capgs = (state0_[inst.MOS2states + 4] +
                    state0_[inst.MOS2states + 4] +
                    GateSourceOverlapCap);
    double capgd = (state0_[inst.MOS2states + 7] +
                    state0_[inst.MOS2states + 7] +
                    GateDrainOverlapCap);
    double capgb = (state0_[inst.MOS2states + 10] +
                    state0_[inst.MOS2states + 10] +
                    GateBulkOverlapCap);
    double capbd = inst.MOS2capbd;
    double capbs = inst.MOS2capbs;

    // --- C matrix stamps (capacitances) ---
    C.add(inst.MOS2GgPtr,    capgd + capgs + capgb);
    C.add(inst.MOS2BbPtr,    capgb + capbd + capbs);
    C.add(inst.MOS2DPdpPtr,  capgd + capbd);
    C.add(inst.MOS2SPspPtr,  capgs + capbs);
    C.add(inst.MOS2GbPtr,   -capgb);
    C.add(inst.MOS2GdpPtr,  -capgd);
    C.add(inst.MOS2GspPtr,  -capgs);
    C.add(inst.MOS2BgPtr,   -capgb);
    C.add(inst.MOS2BdpPtr,  -capbd);
    C.add(inst.MOS2BspPtr,  -capbs);
    C.add(inst.MOS2DPgPtr,  -capgd);
    C.add(inst.MOS2DPbPtr,  -capbd);
    C.add(inst.MOS2SPgPtr,  -capgs);
    C.add(inst.MOS2SPbPtr,  -capbs);

    // --- G matrix stamps (conductances) ---
    G.add(inst.MOS2DdPtr,    inst.MOS2drainConductance);
    G.add(inst.MOS2SsPtr,    inst.MOS2sourceConductance);
    G.add(inst.MOS2BbPtr,    inst.MOS2gbd + inst.MOS2gbs);
    G.add(inst.MOS2DPdpPtr,  inst.MOS2drainConductance +
                              inst.MOS2gds + inst.MOS2gbd +
                              xrev * (inst.MOS2gm + inst.MOS2gmbs));
    G.add(inst.MOS2SPspPtr,  inst.MOS2sourceConductance +
                              inst.MOS2gds + inst.MOS2gbs +
                              xnrm * (inst.MOS2gm + inst.MOS2gmbs));
    G.add(inst.MOS2DdpPtr,  -inst.MOS2drainConductance);
    G.add(inst.MOS2SspPtr,  -inst.MOS2sourceConductance);
    G.add(inst.MOS2BdpPtr,  -inst.MOS2gbd);
    G.add(inst.MOS2BspPtr,  -inst.MOS2gbs);
    G.add(inst.MOS2DPdPtr,  -inst.MOS2drainConductance);
    G.add(inst.MOS2DPgPtr,   (xnrm - xrev) * inst.MOS2gm);
    G.add(inst.MOS2DPbPtr,  -inst.MOS2gbd + (xnrm - xrev) * inst.MOS2gmbs);
    G.add(inst.MOS2DPspPtr, -inst.MOS2gds -
                              xnrm * (inst.MOS2gm + inst.MOS2gmbs));
    G.add(inst.MOS2SPgPtr,  -(xnrm - xrev) * inst.MOS2gm);
    G.add(inst.MOS2SPsPtr,  -inst.MOS2sourceConductance);
    G.add(inst.MOS2SPbPtr,  -inst.MOS2gbs - (xnrm - xrev) * inst.MOS2gmbs);
    G.add(inst.MOS2SPdpPtr, -inst.MOS2gds -
                              xrev * (inst.MOS2gm + inst.MOS2gmbs));
}

// ---------------------------------------------------------------------------
// compute_trunc — device-specific local truncation error for time stepping
//
// Charge offsets: qgs=5, qgd=8, qgb=11, qbd=13, qbs=15
// ---------------------------------------------------------------------------
double MOS2Device::compute_trunc(const IntegratorCtx& ctx,
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
double MOS2Device::solution_value(const std::vector<double>& solution, int node) const {
    if (node <= 0) return 0.0;
    const int idx = node - 1;
    if (idx < 0 || idx >= static_cast<int>(solution.size())) return 0.0;
    return solution[idx];
}

bool MOS2Device::conv_test(const std::vector<double>& solution) const {
    if (!state0_ || !model_) return true;

    const double type = static_cast<double>(model_->MOS2type);
    const double vbs = type * (solution_value(solution, inst_.MOS2bNode) -
                               solution_value(solution, inst_.MOS2sNodePrime));
    const double vgs = type * (solution_value(solution, inst_.MOS2gNode) -
                               solution_value(solution, inst_.MOS2sNodePrime));
    const double vds = type * (solution_value(solution, inst_.MOS2dNodePrime) -
                               solution_value(solution, inst_.MOS2sNodePrime));
    const double vbd = vbs - vds;
    const double vgd = vgs - vds;
    const double vgdo = state0_[inst_.MOS2vgs] - state0_[inst_.MOS2vds];

    const double delvbs = vbs - state0_[inst_.MOS2vbs];
    const double delvbd = vbd - state0_[inst_.MOS2vbd];
    const double delvgs = vgs - state0_[inst_.MOS2vgs];
    const double delvds = vds - state0_[inst_.MOS2vds];
    const double delvgd = vgd - vgdo;

    double cdhat;
    if (inst_.MOS2mode >= 0) {
        cdhat = inst_.MOS2cd - inst_.MOS2gbd * delvbd +
                inst_.MOS2gmbs * delvbs + inst_.MOS2gm * delvgs +
                inst_.MOS2gds * delvds;
    } else {
        cdhat = inst_.MOS2cd - (inst_.MOS2gbd - inst_.MOS2gmbs) * delvbd -
                inst_.MOS2gm * delvgd + inst_.MOS2gds * delvds;
    }

    const double cb = inst_.MOS2cbs + inst_.MOS2cbd;
    const double cbhat = cb + inst_.MOS2gbd * delvbd + inst_.MOS2gbs * delvbs;

    double tol = last_reltol_ * std::max(std::abs(cdhat), std::abs(inst_.MOS2cd)) + last_abstol_;
    if (std::abs(cdhat - inst_.MOS2cd) >= tol)
        return false;

    tol = last_reltol_ * std::max(std::abs(cbhat), std::abs(cb)) + last_abstol_;
    return std::abs(cbhat - cb) <= tol;
}

bool MOS2Device::device_converged() const {
    return last_noncon_ == 0;
}

bool MOS2Device::device_converged(const std::vector<double>& solution) const {
    return last_noncon_ == 0 && conv_test(solution);
}

// ---------------------------------------------------------------------------
// set_ic — Initial conditions for VDS, VGS, VBS
// ---------------------------------------------------------------------------
void MOS2Device::set_ic(double vds, bool vds_given,
                        double vgs, bool vgs_given,
                        double vbs, bool vbs_given) {
    if (vds_given) { inst_.MOS2icVDS = vds; inst_.MOS2icVDSGiven = 1; }
    if (vgs_given) { inst_.MOS2icVGS = vgs; inst_.MOS2icVGSGiven = 1; }
    if (vbs_given) { inst_.MOS2icVBS = vbs; inst_.MOS2icVBSGiven = 1; }
}

// ---------------------------------------------------------------------------
// query_param — post-simulation parameter query
// ---------------------------------------------------------------------------
std::optional<double>
MOS2Device::query_param(const std::string& name) const {
    const std::string key = str_tolower(name);
    const double m = inst_.MOS2m;

    // --- Operating-point voltages from state vector ---
    if (state0_ && state_base_ >= 0) {
        if (key == "vbd") return state0_[inst_.MOS2states + 0];
        if (key == "vbs") return state0_[inst_.MOS2states + 1];
        if (key == "vgs") return state0_[inst_.MOS2states + 2];
        if (key == "vds") return state0_[inst_.MOS2states + 3];
    }

    // --- Small-signal parameters (from instance, scaled by m) ---
    if (key == "id" || key == "cd") return inst_.MOS2cd * m;
    if (key == "gm")   return inst_.MOS2gm * m;
    if (key == "gds")  return inst_.MOS2gds * m;
    if (key == "gmb" || key == "gmbs") return inst_.MOS2gmbs * m;
    if (key == "gbd")  return inst_.MOS2gbd * m;
    if (key == "gbs")  return inst_.MOS2gbs * m;
    if (key == "vth" || key == "von") return inst_.MOS2von;
    if (key == "vdsat") return inst_.MOS2vdsat;

    // --- Capacitances ---
    if (key == "cbd" || key == "capbd") return inst_.MOS2capbd * m;
    if (key == "cbs" || key == "capbs") return inst_.MOS2capbs * m;
    if (key == "cgs" || key == "capgs") return inst_.MOS2cgs * m;
    if (key == "cgd" || key == "capgd") return inst_.MOS2cgd * m;
    if (key == "cgb" || key == "capgb") return inst_.MOS2cgb * m;

    // --- Geometry ---
    if (key == "w") return inst_.MOS2w;
    if (key == "l") return inst_.MOS2l;
    if (key == "m") return inst_.MOS2m;

    return std::nullopt;
}

// ---------------------------------------------------------------------------
// noise_sources — MOS2 noise contributions
//
// Sources (following ngspice mos2noi.c):
//   1. Drain resistance thermal noise:  S = 4kT * gd   (d <-> dp)
//   2. Source resistance thermal noise:  S = 4kT * gs   (s <-> sp)
//   3. Channel thermal noise:            S = 4kT * (2/3) * |gm|   (dp <-> sp)
//   4. Flicker noise:  S = KF * |Id|^AF / (f * W * m * Leff * Cox^2)
// ---------------------------------------------------------------------------
std::vector<Device::NoiseSource> MOS2Device::noise_sources(
        double freq, const std::vector<double>& /*dc_solution*/) const {
    if (!state0_ || state_base_ < 0)
        return {};

    std::vector<NoiseSource> sources;

    // Neospice node indices (ucb-1): -1 is ground.
    const int32_t d_node  = inst_.MOS2dNode - 1;
    const int32_t s_node  = inst_.MOS2sNode - 1;
    const int32_t dp_node = inst_.MOS2dNodePrime - 1;
    const int32_t sp_node = inst_.MOS2sNodePrime - 1;

    const double T = T_NOMINAL;
    const double m = inst_.MOS2m;

    // 1. Drain series resistance thermal noise
    if (inst_.MOS2drainConductance > 0.0) {
        double S_rd = 4.0 * BOLTZMANN * T * inst_.MOS2drainConductance;
        sources.push_back({d_node, dp_node, m * S_rd});
    }

    // 2. Source series resistance thermal noise
    if (inst_.MOS2sourceConductance > 0.0) {
        double S_rs = 4.0 * BOLTZMANN * T * inst_.MOS2sourceConductance;
        sources.push_back({s_node, sp_node, m * S_rs});
    }

    // 3. Channel thermal noise
    double gm = std::abs(inst_.MOS2gm);
    double S_ch = 4.0 * BOLTZMANN * T * (2.0 / 3.0) * gm;
    sources.push_back({dp_node, sp_node, m * S_ch});

    // 4. Flicker noise
    double KF = model_->MOS2fNcoef;
    double AF = model_->MOS2fNexp;
    double Id = std::abs(inst_.MOS2cd);
    if (KF > 0.0 && freq > 0.0 && Id > 0.0) {
        double EffectiveLength = inst_.MOS2l - 2 * model_->MOS2latDiff;
        double coxSquared;
        if (model_->MOS2oxideCapFactor == 0.0) {
            // Assume tox = 1e-7 (same as ngspice)
            coxSquared = 3.9 * 8.854214871e-12 / 1e-7;
        } else {
            coxSquared = model_->MOS2oxideCapFactor;
        }
        coxSquared *= coxSquared;
        double S_fl = KF * std::pow(Id, AF) /
                      (freq * inst_.MOS2w * m * EffectiveLength * coxSquared);
        sources.push_back({dp_node, sp_node, m * S_fl});
    }

    return sources;
}

} // namespace neospice
