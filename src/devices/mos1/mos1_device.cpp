#include "devices/mos1/mos1_device.hpp"

#include "core/circuit.hpp"        // Circuit::node, tls_integrator_ctx
#include "core/types.hpp"          // SimOptions defaults

#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>

// Forward declarations for translated UCB functions.
namespace neospice::mos1 {
    int MOS1setup(Shim::Matrix*, MOS1Model*, Shim::Ckt*, int*);
    int MOS1temp(MOS1Model*, Shim::Ckt*);
    int MOS1load(MOS1Model*, Shim::Ckt*);
}

namespace neospice {

using namespace neospice::mos1;

// ---------------------------------------------------------------------------
// MOS1ModelCard destructor
// ---------------------------------------------------------------------------
MOS1ModelCard::~MOS1ModelCard() = default;

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
std::unique_ptr<MOS1Device>
MOS1Device::make(std::string name,
        int32_t n_d, int32_t n_g, int32_t n_s, int32_t n_b,
        const Geom& geom, MOS1ModelCard& shared_card) {
    std::unique_ptr<MOS1Device> dev(new MOS1Device(std::move(name)));
    dev->model_ = &shared_card.ucb;

    auto& inst = dev->inst_;
    inst.MOS1name = dev->name().c_str();
    inst.MOS1modPtr = dev->model_;

    // Node wiring (UCB convention).
    inst.MOS1dNode = neo_to_ucb(n_d);
    inst.MOS1gNode = neo_to_ucb(n_g);
    inst.MOS1sNode = neo_to_ucb(n_s);
    inst.MOS1bNode = neo_to_ucb(n_b);

    // Geometry.
    inst.MOS1w = geom.W;
    inst.MOS1wGiven = (geom.W != 1e-6) ? 1 : 0;
    inst.MOS1l = geom.L;
    inst.MOS1lGiven = (geom.L != 1e-4) ? 1 : 0;
    inst.MOS1drainArea = geom.AD;
    inst.MOS1drainAreaGiven = (geom.AD != 0.0) ? 1 : 0;
    inst.MOS1sourceArea = geom.AS;
    inst.MOS1sourceAreaGiven = (geom.AS != 0.0) ? 1 : 0;
    inst.MOS1drainPerimiter = geom.PD;
    inst.MOS1drainPerimiterGiven = (geom.PD != 0.0) ? 1 : 0;
    inst.MOS1sourcePerimiter = geom.PS;
    inst.MOS1sourcePerimiterGiven = (geom.PS != 0.0) ? 1 : 0;
    inst.MOS1drainSquares = geom.NRD;
    inst.MOS1drainSquaresGiven = (geom.NRD != 0.0) ? 1 : 0;
    inst.MOS1sourceSquares = geom.NRS;
    inst.MOS1sourceSquaresGiven = (geom.NRS != 0.0) ? 1 : 0;
    inst.MOS1m = geom.M;
    inst.MOS1mGiven = (geom.M != 1.0) ? 1 : 0;

    // Thread onto the shared model's instance list.
    inst.MOS1nextInstance = shared_card.ucb.MOS1instances;
    shared_card.ucb.MOS1instances = &inst;

    // Remember the widest real node index for ghost array sizing.
    int32_t widest = -1;
    for (int32_t n : {n_d, n_g, n_s, n_b}) if (n > widest) widest = n;
    dev->max_neo_node_ = widest;

    return dev;
}

// ---------------------------------------------------------------------------
// declare_internal_nodes
// ---------------------------------------------------------------------------
void MOS1Device::declare_internal_nodes(Circuit& ckt) {
    SparsityBuilder scratch(1);
    Shim::Matrix shim_matrix(scratch);

    Shim::Ckt setup_ckt;
    setup_ckt.CKTtemp    = T_NOMINAL;
    setup_ckt.CKTnomTemp = T_NOMINAL;
    setup_ckt.CKTinternalNodeCounter = 1000;

    setup_ckt.node_alloc = [&ckt, this](const char* name) -> int {
        std::string full = "__" + name_ + "_" + name;
        int32_t neo = ckt.node(full);
        ckt.mark_internal_node(neo);
        return neo + 1;  // UCB convention: ground=0, real>=1
    };

    // Splice this instance as sole member so MOS1setup only processes *this*.
    MOS1Instance* saved_head      = model_->MOS1instances;
    MOS1Instance* saved_next_inst = inst_.MOS1nextInstance;
    MOS1Model*    saved_next_mod  = model_->MOS1nextModel;
    model_->MOS1instances  = &inst_;
    inst_.MOS1nextInstance = nullptr;
    model_->MOS1nextModel  = nullptr;

    int states = 0;
    int rc = MOS1setup(&shim_matrix, model_, &setup_ckt, &states);

    model_->MOS1instances  = saved_head;
    inst_.MOS1nextInstance = saved_next_inst;
    model_->MOS1nextModel  = saved_next_mod;

    if (rc != Shim::OK) {
        throw std::runtime_error("MOS1setup failed with rc=" + std::to_string(rc));
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
void MOS1Device::stamp_pattern(SparsityBuilder& builder) const {
    for (auto [r, c] : journal_) {
        if (r <= 0 || c <= 0) continue;
        builder.add(r - 1, c - 1);
    }
}

// ---------------------------------------------------------------------------
// assign_offsets
// ---------------------------------------------------------------------------
void MOS1Device::assign_offsets(const SparsityPattern& pattern) {
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

    RESOLVE(MOS1DdPtr);
    RESOLVE(MOS1GgPtr);
    RESOLVE(MOS1SsPtr);
    RESOLVE(MOS1BbPtr);
    RESOLVE(MOS1DPdpPtr);
    RESOLVE(MOS1SPspPtr);
    RESOLVE(MOS1DdpPtr);
    RESOLVE(MOS1GbPtr);
    RESOLVE(MOS1GdpPtr);
    RESOLVE(MOS1GspPtr);
    RESOLVE(MOS1SspPtr);
    RESOLVE(MOS1BdpPtr);
    RESOLVE(MOS1BspPtr);
    RESOLVE(MOS1DPspPtr);
    RESOLVE(MOS1DPdPtr);
    RESOLVE(MOS1BgPtr);
    RESOLVE(MOS1DPgPtr);
    RESOLVE(MOS1SPgPtr);
    RESOLVE(MOS1SPsPtr);
    RESOLVE(MOS1DPbPtr);
    RESOLVE(MOS1SPbPtr);
    RESOLVE(MOS1SPdpPtr);

#undef RESOLVE
}

// ---------------------------------------------------------------------------
// set_state_ptrs
// ---------------------------------------------------------------------------
void MOS1Device::set_state_ptrs(double* s0, double* s1, double* s2, int32_t base) {
    state0_ = s0;
    state1_ = s1;
    state2_ = s2;
    state_base_ = base;
    inst_.MOS1states = base;
}

// ---------------------------------------------------------------------------
// evaluate
// ---------------------------------------------------------------------------
void MOS1Device::evaluate(const std::vector<double>& voltages,
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

    // First-call MOS1temp.
    if (!temp_done_) {
        int rc = MOS1temp(model_, &ckt);
        if (rc != Shim::OK) {
            throw std::runtime_error("MOS1temp failed with rc=" + std::to_string(rc));
        }
        temp_done_ = true;
    }

    // Splice this instance as sole member, call load, then restore.
    MOS1Instance* saved_head      = model_->MOS1instances;
    MOS1Instance* saved_next_inst = inst_.MOS1nextInstance;
    MOS1Model*    saved_next_mod  = model_->MOS1nextModel;
    model_->MOS1instances  = &inst_;
    inst_.MOS1nextInstance = nullptr;
    model_->MOS1nextModel  = nullptr;
    int rc = MOS1load(model_, &ckt);
    model_->MOS1instances  = saved_head;
    inst_.MOS1nextInstance = saved_next_inst;
    model_->MOS1nextModel  = saved_next_mod;
    if (rc != Shim::OK) {
        throw std::runtime_error("MOS1load failed with rc=" + std::to_string(rc));
    }

    last_noncon_ = ckt.CKTnoncon;

    // Fold ghost rhs contributions back into the real rhs.
    for (int32_t k = 1; k <= max_neo_node_ + 1 && k < n_ghost; ++k) {
        if (k - 1 < n_real) rhs[k - 1] += ghost_rhs_[k];
    }
}

// ---------------------------------------------------------------------------
// ac_stamp — linearized small-signal AC stamp (G/C matrix split)
//
// Ported from ngspice mos1acld.c.  The AC solver combines the matrices
// as (G + jwC) at each frequency point.
// ---------------------------------------------------------------------------
void MOS1Device::ac_stamp(const std::vector<double>& /*voltages*/,
                            NumericMatrix& G,
                            NumericMatrix& C) {
    auto& inst = inst_;

    int xnrm, xrev;
    if (inst.MOS1mode < 0) {
        xnrm = 0;
        xrev = 1;
    } else {
        xnrm = 1;
        xrev = 0;
    }

    // Meyer's capacitor model
    double EffectiveLength = inst.MOS1l - 2 * model_->MOS1latDiff;
    double GateSourceOverlapCap = model_->MOS1gateSourceOverlapCapFactor *
                                  inst.MOS1m * inst.MOS1w;
    double GateDrainOverlapCap  = model_->MOS1gateDrainOverlapCapFactor *
                                  inst.MOS1m * inst.MOS1w;
    double GateBulkOverlapCap   = model_->MOS1gateBulkOverlapCapFactor *
                                  inst.MOS1m * EffectiveLength;

    // capgs/capgd/capgb from state — note the doubling (2x) per Meyer model
    double capgs = (state0_[inst.MOS1states + 4] +
                    state0_[inst.MOS1states + 4] +
                    GateSourceOverlapCap);
    double capgd = (state0_[inst.MOS1states + 7] +
                    state0_[inst.MOS1states + 7] +
                    GateDrainOverlapCap);
    double capgb = (state0_[inst.MOS1states + 10] +
                    state0_[inst.MOS1states + 10] +
                    GateBulkOverlapCap);
    double capbd = inst.MOS1capbd;
    double capbs = inst.MOS1capbs;

    // --- C matrix stamps (capacitances) ---
    C.add(inst.MOS1GgPtr,    capgd + capgs + capgb);
    C.add(inst.MOS1BbPtr,    capgb + capbd + capbs);
    C.add(inst.MOS1DPdpPtr,  capgd + capbd);
    C.add(inst.MOS1SPspPtr,  capgs + capbs);
    C.add(inst.MOS1GbPtr,   -capgb);
    C.add(inst.MOS1GdpPtr,  -capgd);
    C.add(inst.MOS1GspPtr,  -capgs);
    C.add(inst.MOS1BgPtr,   -capgb);
    C.add(inst.MOS1BdpPtr,  -capbd);
    C.add(inst.MOS1BspPtr,  -capbs);
    C.add(inst.MOS1DPgPtr,  -capgd);
    C.add(inst.MOS1DPbPtr,  -capbd);
    C.add(inst.MOS1SPgPtr,  -capgs);
    C.add(inst.MOS1SPbPtr,  -capbs);

    // --- G matrix stamps (conductances) ---
    G.add(inst.MOS1DdPtr,    inst.MOS1drainConductance);
    G.add(inst.MOS1SsPtr,    inst.MOS1sourceConductance);
    G.add(inst.MOS1BbPtr,    inst.MOS1gbd + inst.MOS1gbs);
    G.add(inst.MOS1DPdpPtr,  inst.MOS1drainConductance +
                              inst.MOS1gds + inst.MOS1gbd +
                              xrev * (inst.MOS1gm + inst.MOS1gmbs));
    G.add(inst.MOS1SPspPtr,  inst.MOS1sourceConductance +
                              inst.MOS1gds + inst.MOS1gbs +
                              xnrm * (inst.MOS1gm + inst.MOS1gmbs));
    G.add(inst.MOS1DdpPtr,  -inst.MOS1drainConductance);
    G.add(inst.MOS1SspPtr,  -inst.MOS1sourceConductance);
    G.add(inst.MOS1BdpPtr,  -inst.MOS1gbd);
    G.add(inst.MOS1BspPtr,  -inst.MOS1gbs);
    G.add(inst.MOS1DPdPtr,  -inst.MOS1drainConductance);
    G.add(inst.MOS1DPgPtr,   (xnrm - xrev) * inst.MOS1gm);
    G.add(inst.MOS1DPbPtr,  -inst.MOS1gbd + (xnrm - xrev) * inst.MOS1gmbs);
    G.add(inst.MOS1DPspPtr, -inst.MOS1gds -
                              xnrm * (inst.MOS1gm + inst.MOS1gmbs));
    G.add(inst.MOS1SPgPtr,  -(xnrm - xrev) * inst.MOS1gm);
    G.add(inst.MOS1SPsPtr,  -inst.MOS1sourceConductance);
    G.add(inst.MOS1SPbPtr,  -inst.MOS1gbs - (xnrm - xrev) * inst.MOS1gmbs);
    G.add(inst.MOS1SPdpPtr, -inst.MOS1gds -
                              xrev * (inst.MOS1gm + inst.MOS1gmbs));
}

// ---------------------------------------------------------------------------
// compute_trunc — device-specific local truncation error for time stepping
//
// Charge offsets: qgs=5, qgd=8, qgb=11, qbd=13, qbs=15
// ---------------------------------------------------------------------------
double MOS1Device::compute_trunc(const IntegratorCtx& ctx,
                              const SimOptions& opts) const {
    if (ctx.order < 2 || ctx.delta <= 0.0)
        return 1e30;

    if (!state0_ || !state1_ || !state2_)
        return 1e30;

    const double lte_coeff = 2.0 / 9.0;
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
bool MOS1Device::device_converged() const {
    return last_noncon_ == 0;
}

// ---------------------------------------------------------------------------
// set_ic — Initial conditions for VDS, VGS, VBS
// ---------------------------------------------------------------------------
void MOS1Device::set_ic(double vds, bool vds_given,
                        double vgs, bool vgs_given,
                        double vbs, bool vbs_given) {
    if (vds_given) { inst_.MOS1icVDS = vds; inst_.MOS1icVDSGiven = 1; }
    if (vgs_given) { inst_.MOS1icVGS = vgs; inst_.MOS1icVGSGiven = 1; }
    if (vbs_given) { inst_.MOS1icVBS = vbs; inst_.MOS1icVBSGiven = 1; }
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
MOS1Device::query_param(const std::string& name) const {
    const std::string key = str_tolower(name);
    const double m = inst_.MOS1m;

    // --- Operating-point voltages from state vector ---
    if (state0_ && state_base_ >= 0) {
        if (key == "vbd") return state0_[inst_.MOS1states + 0];
        if (key == "vbs") return state0_[inst_.MOS1states + 1];
        if (key == "vgs") return state0_[inst_.MOS1states + 2];
        if (key == "vds") return state0_[inst_.MOS1states + 3];
    }

    // --- Small-signal parameters (from instance, scaled by m) ---
    if (key == "id" || key == "cd") return inst_.MOS1cd * m;
    if (key == "gm")   return inst_.MOS1gm * m;
    if (key == "gds")  return inst_.MOS1gds * m;
    if (key == "gmb" || key == "gmbs") return inst_.MOS1gmbs * m;
    if (key == "gbd")  return inst_.MOS1gbd * m;
    if (key == "gbs")  return inst_.MOS1gbs * m;
    if (key == "vth" || key == "von") return inst_.MOS1von;
    if (key == "vdsat") return inst_.MOS1vdsat;

    // --- Capacitances ---
    if (key == "cbd" || key == "capbd") return inst_.MOS1capbd * m;
    if (key == "cbs" || key == "capbs") return inst_.MOS1capbs * m;
    if (key == "cgs" || key == "capgs") return inst_.MOS1cgs * m;
    if (key == "cgd" || key == "capgd") return inst_.MOS1cgd * m;
    if (key == "cgb" || key == "capgb") return inst_.MOS1cgb * m;

    // --- Geometry ---
    if (key == "w") return inst_.MOS1w;
    if (key == "l") return inst_.MOS1l;
    if (key == "m") return inst_.MOS1m;

    return std::nullopt;
}

// ---------------------------------------------------------------------------
// noise_sources — MOS1 noise contributions
//
// Sources (following ngspice mos1noi.c):
//   1. Drain resistance thermal noise:  S = 4kT * gd   (d <-> dp)
//   2. Source resistance thermal noise:  S = 4kT * gs   (s <-> sp)
//   3. Channel thermal noise:            S = 4kT * (2/3) * |gm|   (dp <-> sp)
//   4. Flicker noise:  S = KF * |Id|^AF / (f * W * m * Leff * Cox^2)
// ---------------------------------------------------------------------------
std::vector<Device::NoiseSource> MOS1Device::noise_sources(
        double freq, const std::vector<double>& /*dc_solution*/) const {
    if (!state0_ || state_base_ < 0)
        return {};

    std::vector<NoiseSource> sources;

    // Neospice node indices (ucb-1): -1 is ground.
    const int32_t d_node  = inst_.MOS1dNode - 1;
    const int32_t s_node  = inst_.MOS1sNode - 1;
    const int32_t dp_node = inst_.MOS1dNodePrime - 1;
    const int32_t sp_node = inst_.MOS1sNodePrime - 1;

    const double T = T_NOMINAL;
    const double m = inst_.MOS1m;

    // 1. Drain series resistance thermal noise
    if (inst_.MOS1drainConductance > 0.0) {
        double S_rd = 4.0 * BOLTZMANN * T * inst_.MOS1drainConductance;
        sources.push_back({d_node, dp_node, m * S_rd});
    }

    // 2. Source series resistance thermal noise
    if (inst_.MOS1sourceConductance > 0.0) {
        double S_rs = 4.0 * BOLTZMANN * T * inst_.MOS1sourceConductance;
        sources.push_back({s_node, sp_node, m * S_rs});
    }

    // 3. Channel thermal noise
    double gm = std::abs(inst_.MOS1gm);
    double S_ch = 4.0 * BOLTZMANN * T * (2.0 / 3.0) * gm;
    sources.push_back({dp_node, sp_node, m * S_ch});

    // 4. Flicker noise
    double KF = model_->MOS1fNcoef;
    double AF = model_->MOS1fNexp;
    double Id = std::abs(inst_.MOS1cd);
    if (KF > 0.0 && freq > 0.0 && Id > 0.0) {
        double EffectiveLength = inst_.MOS1l - 2 * model_->MOS1latDiff;
        double coxSquared;
        if (model_->MOS1oxideCapFactor == 0.0) {
            // Assume tox = 1e-7 (same as ngspice)
            coxSquared = 3.9 * 8.854214871e-12 / 1e-7;
        } else {
            coxSquared = model_->MOS1oxideCapFactor;
        }
        coxSquared *= coxSquared;
        double S_fl = KF * std::pow(Id, AF) /
                      (freq * inst_.MOS1w * m * EffectiveLength * coxSquared);
        sources.push_back({dp_node, sp_node, m * S_fl});
    }

    return sources;
}

} // namespace neospice
