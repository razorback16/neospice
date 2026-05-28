#include "devices/hfet2/hfet2_device.hpp"

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
namespace neospice::hfet2 {
    int HFET2setup(Shim::Matrix*, HFET2Model*, Shim::Ckt*, int*);
    int HFET2temp(HFET2Model*, Shim::Ckt*);
    int HFET2load(HFET2Model*, Shim::Ckt*);
}

namespace neospice {

using namespace neospice::hfet2;

// ---------------------------------------------------------------------------
// HFET2ModelCard destructor
// ---------------------------------------------------------------------------
HFET2ModelCard::~HFET2ModelCard() = default;

// ---------------------------------------------------------------------------
// make
// ---------------------------------------------------------------------------
std::unique_ptr<HFET2Device>
HFET2Device::make(std::string name,
        int32_t n_drain, int32_t n_gate, int32_t n_source,
        const Geom& geom, HFET2ModelCard& shared_card) {
    std::unique_ptr<HFET2Device> dev(new HFET2Device(std::move(name)));
    dev->model_ = &shared_card.ucb;
    dev->ext_nodes_ = {n_drain, n_gate, n_source};

    auto& inst = dev->inst_;
    inst.HFET2name = dev->name().c_str();
    inst.HFET2modPtr = dev->model_;

    // Node wiring (UCB convention).
    inst.HFET2drainNode = neo_to_ucb(n_drain);
    inst.HFET2gateNode = neo_to_ucb(n_gate);
    inst.HFET2sourceNode = neo_to_ucb(n_source);

    // Geometry.
    inst.HFET2length = geom.L;
    inst.HFET2lengthGiven = (geom.L != 1e-6) ? 1 : 0;
    inst.HFET2width = geom.W;
    inst.HFET2widthGiven = (geom.W != 20e-6) ? 1 : 0;
    inst.HFET2m = geom.M;
    inst.HFET2mGiven = (geom.M != 1.0) ? 1 : 0;

    // Thread onto the shared model's instance list.
    inst.HFET2nextInstance = shared_card.ucb.HFET2instances;
    shared_card.ucb.HFET2instances = &inst;

    // Remember the widest real node index for ghost array sizing.
    int32_t widest = -1;
    for (int32_t n : {n_drain, n_gate, n_source}) if (n > widest) widest = n;
    dev->max_neo_node_ = widest;

    return dev;
}

// ---------------------------------------------------------------------------
// declare_internal_nodes
// ---------------------------------------------------------------------------
void HFET2Device::declare_internal_nodes(Circuit& ckt) {
    ucb_declare_internal_nodes<Shim::Matrix, Shim::Ckt>(
        ckt, name_,
        [this](Shim::Matrix& m, Shim::Ckt& c) {
            UCB_SPLICE_INSTANCE(HFET2);
            int states = 0;
            int rc = HFET2setup(&m, model_, &c, &states);
            UCB_UNSPLICE_INSTANCE(HFET2);
            return rc;
        },
        "HFET2setup", journal_, max_neo_node_);
}

// ---------------------------------------------------------------------------
// stamp_pattern
// ---------------------------------------------------------------------------
void HFET2Device::stamp_pattern(SparsityBuilder& builder) const {
    ucb_stamp_pattern(journal_, builder);
}

// ---------------------------------------------------------------------------
// assign_offsets
// ---------------------------------------------------------------------------
void HFET2Device::assign_offsets(const SparsityPattern& pattern) {
    const auto offsets = ucb_compute_offsets(journal_, pattern);

#define RESOLVE(f)                                                       \
    do {                                                                 \
        if (inst_.f >= 0)                                                \
            inst_.f = offsets[inst_.f];                                  \
    } while (0)

    RESOLVE(HFET2drainDrainPrimePtr);
    RESOLVE(HFET2gateDrainPrimePtr);
    RESOLVE(HFET2gateSourcePrimePtr);
    RESOLVE(HFET2sourceSourcePrimePtr);
    RESOLVE(HFET2drainPrimeDrainPtr);
    RESOLVE(HFET2drainPrimeGatePtr);
    RESOLVE(HFET2drainPriHFET2ourcePrimePtr);
    RESOLVE(HFET2sourcePrimeGatePtr);
    RESOLVE(HFET2sourcePriHFET2ourcePtr);
    RESOLVE(HFET2sourcePrimeDrainPrimePtr);
    RESOLVE(HFET2drainDrainPtr);
    RESOLVE(HFET2gateGatePtr);
    RESOLVE(HFET2sourceSourcePtr);
    RESOLVE(HFET2drainPrimeDrainPrimePtr);
    RESOLVE(HFET2sourcePriHFET2ourcePrimePtr);

#undef RESOLVE
}

// ---------------------------------------------------------------------------
// set_state_ptrs
// ---------------------------------------------------------------------------
void HFET2Device::set_state_ptrs(double* s0, double* s1, double* s2, double* s3, int32_t base) {
    state0_ = s0;
    state1_ = s1;
    state2_ = s2;
    state3_ = s3;
    state_base_ = base;
    inst_.HFET2state = base;
}

// ---------------------------------------------------------------------------
// evaluate
// ---------------------------------------------------------------------------
void HFET2Device::evaluate(const std::vector<double>& voltages,
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

    // First-call HFET2temp.
    if (!temp_done_) {
        int rc = HFET2temp(model_, &ckt);
        if (rc != Shim::OK) {
            throw std::runtime_error("HFET2temp failed with rc=" + std::to_string(rc));
        }
        temp_done_ = true;
    }

    // Splice this instance as sole member, call load, then restore.
    HFET2Instance* saved_head      = model_->HFET2instances;
    HFET2Instance* saved_next_inst = inst_.HFET2nextInstance;
    HFET2Model*    saved_next_mod  = model_->HFET2nextModel;
    model_->HFET2instances  = &inst_;
    inst_.HFET2nextInstance = nullptr;
    model_->HFET2nextModel  = nullptr;
    int rc = HFET2load(model_, &ckt);
    model_->HFET2instances  = saved_head;
    inst_.HFET2nextInstance = saved_next_inst;
    model_->HFET2nextModel  = saved_next_mod;
    if (rc != Shim::OK) {
        throw std::runtime_error("HFET2load failed with rc=" + std::to_string(rc));
    }

    last_noncon_ = ckt.CKTnoncon;

    // Private ghost arrays need folding. Shared arrays are folded once
    // by the Newton driver after all UCB-style devices have stamped.
    if (!use_shared_arrays) {
        for (int32_t k = 1; k <= max_neo_node_ + 1 && k < n_ghost; ++k) {
            if (k - 1 < n_real) rhs[k - 1] += ghost_rhs_[k];
        }
    }
}

// ---------------------------------------------------------------------------
// ac_stamp -- linearized small-signal AC stamp (G/C matrix split)
//
// Ported from ngspice hfet2acl.c.  The AC solver combines the matrices
// as (G + jwC) at each frequency point.
//
// State layout:
//   offset 5: gm,  offset 6: gds
//   offset 7: ggs, offset 8: ggd
//   offset 9: qgs (capacitance for AC), offset 11: qgd (capacitance for AC)
// ---------------------------------------------------------------------------
void HFET2Device::ac_stamp(const std::vector<double>& /*voltages*/,
                             NumericMatrix& G,
                             NumericMatrix& C) {
    auto& inst = inst_;
    const double m = inst.HFET2m;

    // Conductances from model
    const double gdpr = model_->HFET2drainConduct;
    const double gspr = model_->HFET2sourceConduct;

    // Operating point values from state vector
    const double gm  = state0_[inst.HFET2state + 5];  // HFET2gm
    const double gds = state0_[inst.HFET2state + 6];  // HFET2gds
    const double ggs = state0_[inst.HFET2state + 7];  // HFET2ggs
    const double ggd = state0_[inst.HFET2state + 8];  // HFET2ggd

    // Capacitance values (stored as small-signal params during MODEINITSMSIG)
    const double xgs = state0_[inst.HFET2state + 9];   // capgs (HFET2qgs)
    const double xgd = state0_[inst.HFET2state + 11];  // capgd (HFET2qgd)

    // --- G matrix stamps (conductances) ---
    G.add(inst.HFET2drainDrainPtr,              m * gdpr);
    G.add(inst.HFET2gateGatePtr,                m * (ggd + ggs));
    G.add(inst.HFET2sourceSourcePtr,            m * gspr);
    G.add(inst.HFET2drainPrimeDrainPrimePtr,    m * (gdpr + gds + ggd));
    G.add(inst.HFET2sourcePriHFET2ourcePrimePtr, m * (gspr + gds + gm + ggs));
    G.add(inst.HFET2drainDrainPrimePtr,         m * (-gdpr));
    G.add(inst.HFET2gateDrainPrimePtr,          m * (-ggd));
    G.add(inst.HFET2gateSourcePrimePtr,         m * (-ggs));
    G.add(inst.HFET2sourceSourcePrimePtr,       m * (-gspr));
    G.add(inst.HFET2drainPrimeDrainPtr,         m * (-gdpr));
    G.add(inst.HFET2drainPrimeGatePtr,          m * (-ggd + gm));
    G.add(inst.HFET2drainPriHFET2ourcePrimePtr, m * (-gds - gm));
    G.add(inst.HFET2sourcePrimeGatePtr,         m * (-ggs - gm));
    G.add(inst.HFET2sourcePriHFET2ourcePtr,     m * (-gspr));
    G.add(inst.HFET2sourcePrimeDrainPrimePtr,   m * (-gds));

    // --- C matrix stamps (capacitances) ---
    C.add(inst.HFET2gateGatePtr,                m * (xgd + xgs));
    C.add(inst.HFET2drainPrimeDrainPrimePtr,    m * xgd);
    C.add(inst.HFET2sourcePriHFET2ourcePrimePtr, m * xgs);
    C.add(inst.HFET2gateDrainPrimePtr,          m * (-xgd));
    C.add(inst.HFET2gateSourcePrimePtr,         m * (-xgs));
    C.add(inst.HFET2drainPrimeGatePtr,          m * (-xgd));
    C.add(inst.HFET2sourcePrimeGatePtr,         m * (-xgs));
}

// ---------------------------------------------------------------------------
// compute_trunc -- device-specific local truncation error for time stepping
//
// Charge offsets: qgs=9, qgd=11
// ---------------------------------------------------------------------------
double HFET2Device::compute_trunc(const IntegratorCtx& ctx,
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
// device_converged
// ---------------------------------------------------------------------------
bool HFET2Device::device_converged() const {
    return last_noncon_ == 0;
}

// ---------------------------------------------------------------------------
// set_ic -- Initial conditions for VDS and VGS
// ---------------------------------------------------------------------------
void HFET2Device::set_ic(double vds, bool vds_given,
                         double vgs, bool vgs_given) {
    if (vds_given) { inst_.HFET2icVDS = vds; inst_.HFET2icVDSGiven = 1; }
    if (vgs_given) { inst_.HFET2icVGS = vgs; inst_.HFET2icVGSGiven = 1; }
}

// ---------------------------------------------------------------------------
// query_param -- post-simulation parameter query
// ---------------------------------------------------------------------------
std::optional<double>
HFET2Device::query_param(const std::string& name) const {
    const std::string key = str_tolower(name);
    const double m = inst_.HFET2m;

    // --- Operating-point values from state vector ---
    if (state0_ && state_base_ >= 0) {
        if (key == "vgs")  return state0_[inst_.HFET2state + 0];  // HFET2vgs
        if (key == "vgd")  return state0_[inst_.HFET2state + 1];  // HFET2vgd
        if (key == "vds")  return state0_[inst_.HFET2state + 0] - state0_[inst_.HFET2state + 1];
        if (key == "cg")   return state0_[inst_.HFET2state + 2] * m;  // HFET2cg
        if (key == "cd" || key == "id")
            return state0_[inst_.HFET2state + 3] * m;  // HFET2cd
        if (key == "cgd")  return state0_[inst_.HFET2state + 4] * m;  // HFET2cgd
        if (key == "gm")   return state0_[inst_.HFET2state + 5] * m;  // HFET2gm
        if (key == "gds")  return state0_[inst_.HFET2state + 6] * m;  // HFET2gds
        if (key == "ggs")  return state0_[inst_.HFET2state + 7] * m;  // HFET2ggs
        if (key == "ggd")  return state0_[inst_.HFET2state + 8] * m;  // HFET2ggd
    }

    // --- Geometry (not scaled by m) ---
    if (key == "l") return inst_.HFET2length;
    if (key == "w") return inst_.HFET2width;
    if (key == "m") return inst_.HFET2m;

    return std::nullopt;
}

// ---------------------------------------------------------------------------
// noise_sources -- HFET2 has no noise model in ngspice (DEVnoise = NULL)
// ---------------------------------------------------------------------------
std::vector<Device::NoiseSource> HFET2Device::noise_sources(
        double /*freq*/, const std::vector<double>& /*dc_solution*/) const {
    // ngspice HFET2 has no noise model (DEVnoise = NULL in hfet2init.c).
    return {};
}

} // namespace neospice
