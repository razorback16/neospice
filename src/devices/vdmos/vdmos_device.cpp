#include "devices/vdmos/vdmos_device.hpp"

#include "core/circuit.hpp"        // Circuit::node, tls_integrator_ctx
#include "core/types.hpp"          // SimOptions defaults
#include "devices/ckt_terr.hpp"
#include "devices/ucb_device_init.hpp"
#include "devices/ucb_utils.hpp"

#include <cmath>
#include <optional>
#include <stdexcept>
#include <string>

// Forward declarations for translated UCB functions.
namespace neospice::vdmos {
    int VDMOSsetup(Shim::Matrix*, VDMOSModel*, Shim::Ckt*, int*);
    int VDMOStemp(VDMOSModel*, Shim::Ckt*);
    int VDMOSload(VDMOSModel*, Shim::Ckt*);
}

namespace neospice {

using namespace neospice::vdmos;

// ---------------------------------------------------------------------------
// VDMOSModelCard destructor
// ---------------------------------------------------------------------------
VDMOSModelCard::~VDMOSModelCard() = default;

// ---------------------------------------------------------------------------
// make
// ---------------------------------------------------------------------------
std::unique_ptr<VDMOSDevice>
VDMOSDevice::make(std::string name,
        int32_t n_d, int32_t n_g, int32_t n_s, int32_t n_tj, int32_t n_tc,
        const Geom& geom, VDMOSModelCard& shared_card) {
    std::unique_ptr<VDMOSDevice> dev(new VDMOSDevice(std::move(name)));
    dev->model_ = &shared_card.ucb;

    auto& inst = dev->inst_;
    inst.VDMOSname = dev->name().c_str();
    inst.VDMOSmodPtr = dev->model_;

    // Node wiring (UCB convention).
    inst.VDMOSdNode = neo_to_ucb(n_d);
    inst.VDMOSgNode = neo_to_ucb(n_g);
    inst.VDMOSsNode = neo_to_ucb(n_s);
    inst.VDMOStempNode = neo_to_ucb(n_tj);
    inst.VDMOStcaseNode = neo_to_ucb(n_tc);

    // Geometry.
    inst.VDMOSm = geom.M;
    inst.VDMOSmGiven = (geom.M != 1.0) ? 1 : 0;

    // Thread onto the shared model's instance list.
    inst.VDMOSnextInstance = shared_card.ucb.VDMOSinstances;
    shared_card.ucb.VDMOSinstances = &inst;

    // Remember the widest real node index for ghost array sizing.
    int32_t widest = -1;
    for (int32_t n : {n_d, n_g, n_s, n_tj, n_tc}) if (n > widest) widest = n;
    dev->max_neo_node_ = widest;

    return dev;
}

// ---------------------------------------------------------------------------
// declare_internal_nodes
// ---------------------------------------------------------------------------
void VDMOSDevice::declare_internal_nodes(Circuit& ckt) {
    ucb_declare_internal_nodes<Shim::Matrix, Shim::Ckt>(
        ckt, name_,
        [this](Shim::Matrix& m, Shim::Ckt& c) {
            UCB_SPLICE_INSTANCE(VDMOS);
            int states = 0;
            int rc = VDMOSsetup(&m, model_, &c, &states);
            UCB_UNSPLICE_INSTANCE(VDMOS);
            return rc;
        },
        "VDMOSsetup", journal_, max_neo_node_);
}

// ---------------------------------------------------------------------------
// stamp_pattern
// ---------------------------------------------------------------------------
void VDMOSDevice::stamp_pattern(SparsityBuilder& builder) const {
    ucb_stamp_pattern(journal_, builder);
}

// ---------------------------------------------------------------------------
// assign_offsets
// ---------------------------------------------------------------------------
void VDMOSDevice::assign_offsets(const SparsityPattern& pattern) {
    const auto offsets = ucb_compute_offsets(journal_, pattern);

#define RESOLVE(f)                                                       \
    do {                                                                 \
        if (inst_.f >= 0)                                                \
            inst_.f = offsets[inst_.f];                                  \
    } while (0)

    RESOLVE(VDMOSDdPtr);
    RESOLVE(VDMOSGgPtr);
    RESOLVE(VDMOSSsPtr);
    RESOLVE(VDMOSDPdpPtr);
    RESOLVE(VDMOSSPspPtr);
    RESOLVE(VDMOSDdpPtr);
    RESOLVE(VDMOSGdpPtr);
    RESOLVE(VDMOSGspPtr);
    RESOLVE(VDMOSSspPtr);
    RESOLVE(VDMOSDPspPtr);
    RESOLVE(VDMOSDPdPtr);
    RESOLVE(VDMOSDPgPtr);
    RESOLVE(VDMOSSPgPtr);
    RESOLVE(VDMOSSPsPtr);
    RESOLVE(VDMOSSPdpPtr);
    RESOLVE(VDMOSGPgpPtr);
    RESOLVE(VDMOSGPdpPtr);
    RESOLVE(VDMOSGPspPtr);
    RESOLVE(VDMOSDPgpPtr);
    RESOLVE(VDMOSSPgpPtr);
    RESOLVE(VDMOSGgpPtr);
    RESOLVE(VDMOSGPgPtr);
    RESOLVE(VDMOSDsPtr);
    RESOLVE(VDMOSSdPtr);
    RESOLVE(VDIORPdPtr);
    RESOLVE(VDIODrpPtr);
    RESOLVE(VDIORPrpPtr);
    RESOLVE(VDIOSrpPtr);
    RESOLVE(VDIORPsPtr);
    RESOLVE(VDMOSTemptempPtr);
    RESOLVE(VDMOSTempdpPtr);
    RESOLVE(VDMOSTempspPtr);
    RESOLVE(VDMOSTempgpPtr);
    RESOLVE(VDMOSGPtempPtr);
    RESOLVE(VDMOSDPtempPtr);
    RESOLVE(VDMOSSPtempPtr);
    RESOLVE(VDIOTempposPrimePtr);
    RESOLVE(VDMOSTempdPtr);
    RESOLVE(VDIOPosPrimetempPtr);
    RESOLVE(VDMOSDtempPtr);
    RESOLVE(VDMOStempSPtr);
    RESOLVE(VDMOSSTempPtr);
    RESOLVE(VDMOSTcasetcasePtr);
    RESOLVE(VDMOSTcasetempPtr);
    RESOLVE(VDMOSTemptcasePtr);
    RESOLVE(VDMOSTptpPtr);
    RESOLVE(VDMOSTptcasePtr);
    RESOLVE(VDMOSTcasetpPtr);
    RESOLVE(VDMOSCktTcktTPtr);
    RESOLVE(VDMOSCktTtpPtr);
    RESOLVE(VDMOSTpcktTPtr);

#undef RESOLVE
}

// ---------------------------------------------------------------------------
// set_state_ptrs
// ---------------------------------------------------------------------------
void VDMOSDevice::set_state_ptrs(double* s0, double* s1, double* s2, double* s3, int32_t base) {
    state0_ = s0;
    state1_ = s1;
    state2_ = s2;
    state3_ = s3;
    state_base_ = base;
    inst_.VDMOSstates = base;
}

// ---------------------------------------------------------------------------
// evaluate
// ---------------------------------------------------------------------------
void VDMOSDevice::evaluate(const std::vector<double>& voltages,
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

    // First-call VDMOStemp.
    if (!temp_done_) {
        int rc = VDMOStemp(model_, &ckt);
        if (rc != Shim::OK) {
            throw std::runtime_error("VDMOStemp failed with rc=" + std::to_string(rc));
        }
        temp_done_ = true;
    }

    // Splice this instance as sole member, call load, then restore.
    VDMOSInstance* saved_head      = model_->VDMOSinstances;
    VDMOSInstance* saved_next_inst = inst_.VDMOSnextInstance;
    VDMOSModel*    saved_next_mod  = model_->VDMOSnextModel;
    model_->VDMOSinstances  = &inst_;
    inst_.VDMOSnextInstance = nullptr;
    model_->VDMOSnextModel  = nullptr;
    int rc = VDMOSload(model_, &ckt);
    model_->VDMOSinstances  = saved_head;
    inst_.VDMOSnextInstance = saved_next_inst;
    model_->VDMOSnextModel  = saved_next_mod;
    if (rc != Shim::OK) {
        throw std::runtime_error("VDMOSload failed with rc=" + std::to_string(rc));
    }

    last_noncon_ = ckt.CKTnoncon;

    // Fold ghost rhs contributions back into the real rhs.
    for (int32_t k = 1; k <= max_neo_node_ + 1 && k < n_ghost; ++k) {
        if (k - 1 < n_real) rhs[k - 1] += ghost_rhs_[k];
    }
}

// ---------------------------------------------------------------------------
// ac_stamp — TODO: implement G/C matrix split from ngspice AC load file
// ---------------------------------------------------------------------------
void VDMOSDevice::ac_stamp(const std::vector<double>& /*voltages*/,
                             NumericMatrix& /*G*/,
                             NumericMatrix& /*C*/) {
    // TODO: Port the AC stamp from the ngspice *acld.c file.
    // Conductances (gm, gds, ...) go into G; capacitances (Cgs, Cgd, ...) into C.
    // See existing implementations: bsim4v7_device.cpp, hisim2_device.cpp.

}

// ---------------------------------------------------------------------------
// compute_trunc
// ---------------------------------------------------------------------------
double VDMOSDevice::compute_trunc(const IntegratorCtx& ctx,
                               const SimOptions& opts) const {
    if (ctx.order < 1 || ctx.delta <= 0.0) return 1e30;
    if (!state0_ || !state1_ || !state2_ || !state3_) return 1e30;

    const double* states[] = {state0_, state1_, state2_, state3_};
    double dt_min = 1e30;
    ckt_terr(state_base_ + 4, states, ctx, opts, dt_min);
    ckt_terr(state_base_ + 7, states, ctx, opts, dt_min);
    ckt_terr(state_base_ + 12, states, ctx, opts, dt_min);
    ckt_terr(state_base_ + 15, states, ctx, opts, dt_min);
    return dt_min;
}

// ---------------------------------------------------------------------------
// device_converged
// ---------------------------------------------------------------------------
bool VDMOSDevice::device_converged() const {
    return last_noncon_ == 0;
}

// ---------------------------------------------------------------------------
// query_param
// ---------------------------------------------------------------------------
std::optional<double> VDMOSDevice::query_param(const std::string& name) const {
    const std::string key = str_tolower(name);
    const double m = inst_.VDMOSm;

    // Operating-point quantities (need a completed evaluate + state vector).
    if (state0_ && state_base_ >= 0) {
        auto st = [this](int rel) { return state0_[state_base_ + rel]; };
        // State offsets relative to VDMOSstates (see vdmos_def.hpp).
        constexpr int CAPGS = 3, CQGS = 5, CAPGD = 6, CQGD = 8;
        if (key == "id")   return inst_.VDMOScd * m;
        if (key == "is")   return -inst_.VDMOScd * m; // VDMOS: is = -id (ignoring gate)
        if (key == "ig")   return 0.0;
        if (key == "vgs")  return st(0); // VDMOSvgs at offset 0
        if (key == "vds")  return st(1); // VDMOSvds at offset 1
        if (key == "cgs")  return 2.0 * st(CAPGS) * m;
        if (key == "cgd")  return 2.0 * st(CAPGD) * m;
        if (key == "cds")  return inst_.VDIOcap * m;
        if (key == "idio") return st(10) * m; // VDIOcurrent at offset 10
        if (key == "cqgs") return st(CQGS) * m;
        if (key == "cqgd") return st(CQGD) * m;
        if (key == "gm")   return inst_.VDMOSgm * m;
        if (key == "gds")  return inst_.VDMOSgds * m;
        if (key == "von")  return inst_.VDMOSvon;
    }

    // Instance parameters.
    if (key == "m")        return inst_.VDMOSm;
    if (key == "temp")     return inst_.VDMOStemp - 273.15;
    if (key == "dtemp")    return inst_.VDMOSdtemp;
    if (key == "icvds")    return inst_.VDMOSicVDS;
    if (key == "icvgs")    return inst_.VDMOSicVGS;
    if (key == "thermal")  return static_cast<double>(inst_.VDMOSthermal);

    // Resistances are reported when the corresponding internal node exists.
    if (key == "rs") {
        return (inst_.VDMOSsourceConductance > 0.0)
                   ? 1.0 / inst_.VDMOSsourceConductance : 0.0;
    }
    if (key == "rd") {
        return (inst_.VDMOSdrainConductance > 0.0)
                   ? 1.0 / inst_.VDMOSdrainConductance : 0.0;
    }

    return std::nullopt;
}

// ---------------------------------------------------------------------------
// noise_sources — TODO: implement device noise model
// ---------------------------------------------------------------------------
std::vector<Device::NoiseSource> VDMOSDevice::noise_sources(
        double /*freq*/, const std::vector<double>& /*dc_solution*/) const {
    // TODO: Port noise sources from the ngspice *noise.c file.
    // Common noise types:
    //   Thermal: 4*k*T*G  (conductance noise)
    //   Shot:    2*q*|I|  (junction current noise)
    //   Flicker: KF*|I|^AF / f^EF  (1/f noise)
    // Use sim_temp() for temperature (inherited from Device base class).
    // See bjt_device.cpp and bsim4v7_device.cpp for examples.
    return {};

}

} // namespace neospice
