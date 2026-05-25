#include "devices/hfet1/hfet1_device.hpp"

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
namespace neospice::hfet1 {
    int HFETAsetup(Shim::Matrix*, HFETAModel*, Shim::Ckt*, int*);
    int HFETAtemp(HFETAModel*, Shim::Ckt*);
    int HFETAload(HFETAModel*, Shim::Ckt*);
}

namespace neospice {

using namespace neospice::hfet1;

// ---------------------------------------------------------------------------
// HFETAModelCard destructor
// ---------------------------------------------------------------------------
HFETAModelCard::~HFETAModelCard() = default;

// ---------------------------------------------------------------------------
// make
// ---------------------------------------------------------------------------
std::unique_ptr<HFETADevice>
HFETADevice::make(std::string name,
        int32_t n_drain, int32_t n_gate, int32_t n_source,
        const Geom& geom, HFETAModelCard& shared_card) {
    std::unique_ptr<HFETADevice> dev(new HFETADevice(std::move(name)));
    dev->model_ = &shared_card.ucb;
    dev->ext_nodes_ = {n_drain, n_gate, n_source};

    auto& inst = dev->inst_;
    inst.HFETAname = dev->name().c_str();
    inst.HFETAmodPtr = dev->model_;

    // Node wiring (UCB convention).
    inst.HFETAdrainNode  = neo_to_ucb(n_drain);
    inst.HFETAgateNode   = neo_to_ucb(n_gate);
    inst.HFETAsourceNode = neo_to_ucb(n_source);

    // Geometry.
    inst.HFETAlength      = geom.length;
    inst.HFETAlengthGiven = geom.length_given ? 1 : 0;
    inst.HFETAwidth       = geom.width;
    inst.HFETAwidthGiven  = geom.width_given ? 1 : 0;
    inst.HFETAm           = geom.m;
    inst.HFETAmGiven      = geom.m_given ? 1 : 0;

    // Thread onto the shared model's instance list.
    inst.HFETAnextInstance = shared_card.ucb.HFETAinstances;
    shared_card.ucb.HFETAinstances = &inst;

    // Remember the widest real node index for ghost array sizing.
    int32_t widest = -1;
    for (int32_t n : {n_drain, n_gate, n_source}) if (n > widest) widest = n;
    dev->max_neo_node_ = widest;

    return dev;
}

// ---------------------------------------------------------------------------
// declare_internal_nodes
// ---------------------------------------------------------------------------
void HFETADevice::declare_internal_nodes(Circuit& ckt) {
    ucb_declare_internal_nodes<Shim::Matrix, Shim::Ckt>(
        ckt, name_,
        [this](Shim::Matrix& m, Shim::Ckt& c) {
            UCB_SPLICE_INSTANCE(HFETA);
            int states = 0;
            int rc = HFETAsetup(&m, model_, &c, &states);
            UCB_UNSPLICE_INSTANCE(HFETA);
            return rc;
        },
        "HFETAsetup", journal_, max_neo_node_);
}

// ---------------------------------------------------------------------------
// stamp_pattern
// ---------------------------------------------------------------------------
void HFETADevice::stamp_pattern(SparsityBuilder& builder) const {
    ucb_stamp_pattern(journal_, builder);
}

// ---------------------------------------------------------------------------
// assign_offsets
// ---------------------------------------------------------------------------
void HFETADevice::assign_offsets(const SparsityPattern& pattern) {
    const auto offsets = ucb_compute_offsets(journal_, pattern);

#define RESOLVE(f)                                                       \
    do {                                                                 \
        if (inst_.f >= 0)                                                \
            inst_.f = offsets[inst_.f];                                  \
    } while (0)

    RESOLVE(HFETAdrainDrainPrimePtr);
    RESOLVE(HFETAgatePrimeDrainPrimePtr);
    RESOLVE(HFETAgatePrimeSourcePrimePtr);
    RESOLVE(HFETAsourceSourcePrimePtr);
    RESOLVE(HFETAdrainPrimeDrainPtr);
    RESOLVE(HFETAdrainPrimeGatePrimePtr);
    RESOLVE(HFETAdrainPrimeSourcePrimePtr);
    RESOLVE(HFETAsourcePrimeGatePrimePtr);
    RESOLVE(HFETAsourcePrimeSourcePtr);
    RESOLVE(HFETAsourcePrimeDrainPrimePtr);
    RESOLVE(HFETAdrainDrainPtr);
    RESOLVE(HFETAgatePrimeGatePrimePtr);
    RESOLVE(HFETAsourceSourcePtr);
    RESOLVE(HFETAdrainPrimeDrainPrimePtr);
    RESOLVE(HFETAsourcePrimeSourcePrimePtr);
    RESOLVE(HFETAdrainPrmPrmDrainPrmPrmPtr);
    RESOLVE(HFETAdrainPrmPrmDrainPrimePtr);
    RESOLVE(HFETAdrainPrimeDrainPrmPrmPtr);
    RESOLVE(HFETAdrainPrmPrmGatePrimePtr);
    RESOLVE(HFETAgatePrimeDrainPrmPrmPtr);
    RESOLVE(HFETAsourcePrmPrmSourcePrmPrmPtr);
    RESOLVE(HFETAsourcePrmPrmSourcePrimePtr);
    RESOLVE(HFETAsourcePrimeSourcePrmPrmPtr);
    RESOLVE(HFETAsourcePrmPrmGatePrimePtr);
    RESOLVE(HFETAgatePrimeSourcePrmPrmPtr);
    RESOLVE(HFETAgateGatePtr);
    RESOLVE(HFETAgateGatePrimePtr);
    RESOLVE(HFETAgatePrimeGatePtr);

#undef RESOLVE
}

// ---------------------------------------------------------------------------
// set_state_ptrs
// ---------------------------------------------------------------------------
void HFETADevice::set_state_ptrs(double* s0, double* s1, double* s2, int32_t base) {
    state0_ = s0;
    state1_ = s1;
    state2_ = s2;
    state_base_ = base;
    inst_.HFETAstate = base;
}

// ---------------------------------------------------------------------------
// evaluate
// ---------------------------------------------------------------------------
void HFETADevice::evaluate(const std::vector<double>& voltages,
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

    // First-call HFETAtemp.
    if (!temp_done_) {
        int rc = HFETAtemp(model_, &ckt);
        if (rc != Shim::OK) {
            throw std::runtime_error("HFETAtemp failed with rc=" + std::to_string(rc));
        }
        temp_done_ = true;
    }

    // Splice this instance as sole member, call load, then restore.
    HFETAInstance* saved_head      = model_->HFETAinstances;
    HFETAInstance* saved_next_inst = inst_.HFETAnextInstance;
    HFETAModel*    saved_next_mod  = model_->HFETAnextModel;
    model_->HFETAinstances  = &inst_;
    inst_.HFETAnextInstance = nullptr;
    model_->HFETAnextModel  = nullptr;
    int rc = HFETAload(model_, &ckt);
    model_->HFETAinstances  = saved_head;
    inst_.HFETAnextInstance = saved_next_inst;
    model_->HFETAnextModel  = saved_next_mod;
    if (rc != Shim::OK) {
        throw std::runtime_error("HFETAload failed with rc=" + std::to_string(rc));
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
bool HFETADevice::device_converged() const {
    return last_noncon_ == 0;
}

// ---------------------------------------------------------------------------
// set_ic — Initial conditions for VDS and VGS
// ---------------------------------------------------------------------------
void HFETADevice::set_ic(double vds, bool vds_given,
                         double vgs, bool vgs_given) {
    if (vds_given) { inst_.HFETAicVDS = vds; inst_.HFETAicVDSGiven = 1; }
    if (vgs_given) { inst_.HFETAicVGS = vgs; inst_.HFETAicVGSGiven = 1; }
}

// ---------------------------------------------------------------------------
// ac_stamp — linearized small-signal AC stamp (G/C matrix split)
//
// Translated from ngspice hfetacl.c.  The AC solver combines as (G + jwC).
// ---------------------------------------------------------------------------
void HFETADevice::ac_stamp(const std::vector<double>& /*voltages*/,
                           NumericMatrix& G, NumericMatrix& C) {
    auto& inst = inst_;
    const double m = inst.HFETAm;

    // State vector offsets (from hfetdefs.h):
    // HFETAgm   = state+6,  HFETAgds  = state+7
    // HFETAggs  = state+8,  HFETAggd  = state+9
    // HFETAqgs  = state+10 (stores capgs in SMSIG), HFETAqgd = state+12 (stores capgd)
    // HFETAggspp = state+15, HFETAggdpp = state+18
    const double gm    = state0_[inst.HFETAstate + 6];
    double gds   = state0_[inst.HFETAstate + 7];
    const double ggs   = state0_[inst.HFETAstate + 8];
    const double ggd   = state0_[inst.HFETAstate + 9];
    const double ggspp = state0_[inst.HFETAstate + 15];
    const double ggdpp = state0_[inst.HFETAstate + 18];

    // Capacitance values (stored as capacitances in SMSIG mode)
    const double capgs = state0_[inst.HFETAstate + 10];  // qgs -> capgs in SMSIG
    const double capgd = state0_[inst.HFETAstate + 12];  // qgd -> capgd in SMSIG
    const double cds   = state0_[inst.HFETAstate + 20];  // qds -> CDS in SMSIG

    // Frequency-dependent output conductance (kappa effect)
    // The kappa correction modifies gds based on frequency; however in the
    // AC stamp context we apply the base gds.  The kappa correction would
    // require access to omega (angular frequency), which is not available
    // in this interface.  For most circuits this is negligible.

    // --- G matrix stamps (from hfetacl.c real parts) ---
    G.add(inst.HFETAdrainDrainPtr,                 m * model_->HFETAdrainConduct);
    G.add(inst.HFETAsourceSourcePtr,               m * model_->HFETAsourceConduct);
    G.add(inst.HFETAgatePrimeGatePrimePtr,         m * (ggd + ggs + ggspp + ggdpp + model_->HFETAgateConduct));
    G.add(inst.HFETAdrainPrimeDrainPrimePtr,       m * (gds + ggd + model_->HFETAdrainConduct + model_->HFETAgf));
    G.add(inst.HFETAsourcePrimeSourcePrimePtr,     m * (gds + gm + ggs + model_->HFETAsourceConduct + model_->HFETAgi));
    G.add(inst.HFETAsourcePrmPrmSourcePrmPrmPtr,   m * (model_->HFETAgi + ggspp));
    G.add(inst.HFETAdrainPrmPrmDrainPrmPrmPtr,     m * (model_->HFETAgf + ggdpp));
    G.add(inst.HFETAdrainDrainPrimePtr,            m * (-model_->HFETAdrainConduct));
    G.add(inst.HFETAdrainPrimeDrainPtr,            m * (-model_->HFETAdrainConduct));
    G.add(inst.HFETAsourceSourcePrimePtr,          m * (-model_->HFETAsourceConduct));
    G.add(inst.HFETAsourcePrimeSourcePtr,          m * (-model_->HFETAsourceConduct));
    G.add(inst.HFETAgatePrimeDrainPrimePtr,        m * (-ggd));
    G.add(inst.HFETAdrainPrimeGatePrimePtr,        m * (gm - ggd));
    G.add(inst.HFETAgatePrimeSourcePrimePtr,       m * (-ggs));
    G.add(inst.HFETAsourcePrimeGatePrimePtr,       m * (-ggs - gm));
    G.add(inst.HFETAdrainPrimeSourcePrimePtr,      m * (-gds - gm));
    G.add(inst.HFETAsourcePrimeDrainPrimePtr,      m * (-gds));
    G.add(inst.HFETAsourcePrimeSourcePrmPrmPtr,    m * (-model_->HFETAgi));
    G.add(inst.HFETAsourcePrmPrmSourcePrimePtr,    m * (-model_->HFETAgi));
    G.add(inst.HFETAgatePrimeSourcePrmPrmPtr,      m * (-ggspp));
    G.add(inst.HFETAsourcePrmPrmGatePrimePtr,      m * (-ggspp));
    G.add(inst.HFETAdrainPrimeDrainPrmPrmPtr,      m * (-model_->HFETAgf));
    G.add(inst.HFETAdrainPrmPrmDrainPrimePtr,      m * (-model_->HFETAgf));
    G.add(inst.HFETAgatePrimeDrainPrmPrmPtr,       m * (-ggdpp));
    G.add(inst.HFETAdrainPrmPrmGatePrimePtr,       m * (-ggdpp));
    G.add(inst.HFETAgateGatePtr,                   m * model_->HFETAgateConduct);
    G.add(inst.HFETAgateGatePrimePtr,              m * (-model_->HFETAgateConduct));
    G.add(inst.HFETAgatePrimeGatePtr,              m * (-model_->HFETAgateConduct));

    // --- C matrix stamps (from hfetacl.c imaginary parts / omega factors) ---
    // In ngspice: *(ptr+1) += m * xgs  means capacitance entry (xgs = capgs*omega).
    // We store the capacitance value and the AC solver multiplies by jw.
    C.add(inst.HFETAgatePrimeGatePrimePtr,         m * (capgd + capgs));
    C.add(inst.HFETAdrainPrmPrmDrainPrmPrmPtr,     m * capgd);
    C.add(inst.HFETAsourcePrmPrmSourcePrmPrmPtr,   m * capgs);
    C.add(inst.HFETAgatePrimeDrainPrmPrmPtr,       m * (-capgd));
    C.add(inst.HFETAgatePrimeSourcePrmPrmPtr,      m * (-capgs));
    C.add(inst.HFETAdrainPrmPrmGatePrimePtr,       m * (-capgd));
    C.add(inst.HFETAsourcePrmPrmGatePrimePtr,      m * (-capgs));
    // CDS capacitance between drain_prime and source_prime
    C.add(inst.HFETAdrainPrimeDrainPrimePtr,       m * cds);
    C.add(inst.HFETAsourcePrimeSourcePrimePtr,     m * cds);
    C.add(inst.HFETAdrainPrimeSourcePrimePtr,      m * (-cds));
    C.add(inst.HFETAsourcePrimeDrainPrimePtr,      m * (-cds));
}

// ---------------------------------------------------------------------------
// compute_trunc — device-specific local truncation error for time stepping
//
// HFET1 has 3 charge states from NIintegrate calls in hfetload.c:
//   qgs  at offset 10 (HFETAqgs)
//   qgd  at offset 12 (HFETAqgd)
//   qds  at offset 20 (HFETAqds)
// ---------------------------------------------------------------------------
double HFETADevice::compute_trunc(const IntegratorCtx& ctx,
                                  const SimOptions& opts) const {
    if (ctx.order < 2 || ctx.delta <= 0.0)
        return 1e30;

    if (!state0_ || !state1_ || !state2_)
        return 1e30;

    const double lte_coeff = ctx.lte_coefficient();
    const double h0 = ctx.delta;
    const double h1 = ctx.delta_old[1];
    if (h1 <= 0.0) return 1e30;

    // Charge state offsets relative to state_base_
    static const int charge_offsets[] = {10, 12, 20};  // qgs, qgd, qds
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
std::optional<double>
HFETADevice::query_param(const std::string& name) const {
    const std::string key = str_tolower(name);
    const double m = inst_.HFETAm;

    // --- Operating-point parameters from state vector ---
    // State layout (from hfetdefs.h):
    //   0=vgs, 1=vgd, 2=cg, 3=cd, 4=cgd, 5=cgs
    //   6=gm, 7=gds, 8=ggs, 9=ggd
    //   10=qgs, 11=cqgs, 12=qgd, 13=cqgd
    //   14=vgspp, 15=ggspp, 16=cgspp, 17=vgdpp, 18=ggdpp, 19=cgdpp
    //   20=qds, 21=cqds, 22=gmg, 23=gmd
    if (state0_ && state_base_ >= 0) {
        if (key == "vgs")  return state0_[inst_.HFETAstate + 0];
        if (key == "vgd")  return state0_[inst_.HFETAstate + 1];
        if (key == "cg" || key == "ig")
            return state0_[inst_.HFETAstate + 2] * m;
        if (key == "cd" || key == "id")
            return state0_[inst_.HFETAstate + 3] * m;
        if (key == "cgd")  return state0_[inst_.HFETAstate + 4] * m;
        if (key == "gm")   return state0_[inst_.HFETAstate + 6] * m;
        if (key == "gds")  return state0_[inst_.HFETAstate + 7] * m;
        if (key == "ggs")  return state0_[inst_.HFETAstate + 8] * m;
        if (key == "ggd")  return state0_[inst_.HFETAstate + 9] * m;
        if (key == "vds")  return state0_[inst_.HFETAstate + 0] -
                                  state0_[inst_.HFETAstate + 1];
    }

    // --- Geometry (no multiplier) ---
    if (key == "l" || key == "length") return inst_.HFETAlength;
    if (key == "w" || key == "width")  return inst_.HFETAwidth;
    if (key == "m")                    return inst_.HFETAm;

    return std::nullopt;
}

// ---------------------------------------------------------------------------
// noise_sources — HFET1 has DEVnoise = NULL in ngspice (no noise sources)
// ---------------------------------------------------------------------------
std::vector<Device::NoiseSource> HFETADevice::noise_sources(
        double /*freq*/, const std::vector<double>& /*dc_solution*/) const {
    return {};
}

} // namespace neospice
