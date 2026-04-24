#include "devices/vbic/vbic_device.hpp"

#include "core/circuit.hpp"        // Circuit::node, tls_integrator_ctx
#include "core/types.hpp"          // SimOptions defaults
#include "devices/ucb_device_init.hpp"
#include "devices/ucb_utils.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <string>

// Forward declarations for translated UCB functions.
namespace neospice::vbic {
    int VBICsetup(Shim::Matrix*, VBICModel*, Shim::Ckt*, int*);
    int VBICtemp(VBICModel*, Shim::Ckt*);
    int VBICload(VBICModel*, Shim::Ckt*);
}

namespace neospice {

using namespace neospice::vbic;

// ---------------------------------------------------------------------------
// VBICModelCard destructor
// ---------------------------------------------------------------------------
VBICModelCard::~VBICModelCard() = default;

// ---------------------------------------------------------------------------
// make
// ---------------------------------------------------------------------------
std::unique_ptr<VBICDevice>
VBICDevice::make(std::string name,
        int32_t n_coll, int32_t n_base, int32_t n_emit, int32_t n_subs,
        const Geom& geom, VBICModelCard& shared_card) {
    std::unique_ptr<VBICDevice> dev(new VBICDevice(std::move(name)));
    dev->model_ = &shared_card.ucb;

    auto& inst = dev->inst_;
    inst.VBICname = dev->name().c_str();
    inst.VBICmodPtr = dev->model_;

    // Node wiring (UCB convention).
    inst.VBICcollNode = neo_to_ucb(n_coll);
    inst.VBICbaseNode = neo_to_ucb(n_base);
    inst.VBICemitNode = neo_to_ucb(n_emit);
    inst.VBICsubsNode = neo_to_ucb(n_subs);

    // Geometry.
    inst.VBICarea = geom.area;
    inst.VBICareaGiven = geom.area_given ? 1 : 0;
    inst.VBICm = geom.m;
    inst.VBICmGiven = geom.m_given ? 1 : 0;

    // Thread onto the shared model's instance list.
    inst.VBICnextInstance = shared_card.ucb.VBICinstances;
    shared_card.ucb.VBICinstances = &inst;

    // Remember the widest real node index for ghost array sizing.
    int32_t widest = -1;
    for (int32_t n : {n_coll, n_base, n_emit, n_subs}) if (n > widest) widest = n;
    dev->max_neo_node_ = widest;

    return dev;
}

// ---------------------------------------------------------------------------
// declare_internal_nodes
// ---------------------------------------------------------------------------
void VBICDevice::declare_internal_nodes(Circuit& ckt) {
    ucb_declare_internal_nodes<Shim::Matrix, Shim::Ckt>(
        ckt, name_,
        [this](Shim::Matrix& m, Shim::Ckt& c) {
            UCB_SPLICE_INSTANCE(VBIC);
            int states = 0;
            int rc = VBICsetup(&m, model_, &c, &states);
            UCB_UNSPLICE_INSTANCE(VBIC);
            return rc;
        },
        "VBICsetup", journal_, max_neo_node_);
}

// ---------------------------------------------------------------------------
// stamp_pattern
// ---------------------------------------------------------------------------
void VBICDevice::stamp_pattern(SparsityBuilder& builder) const {
    ucb_stamp_pattern(journal_, builder);
}

// ---------------------------------------------------------------------------
// assign_offsets
// ---------------------------------------------------------------------------
void VBICDevice::assign_offsets(const SparsityPattern& pattern) {
    const auto offsets = ucb_compute_offsets(journal_, pattern);

#define RESOLVE(f)                                                       \
    do {                                                                 \
        if (inst_.f >= 0)                                                \
            inst_.f = offsets[inst_.f];                                  \
    } while (0)

    RESOLVE(VBICcollCollPtr);
    RESOLVE(VBICbaseBasePtr);
    RESOLVE(VBICemitEmitPtr);
    RESOLVE(VBICsubsSubsPtr);
    RESOLVE(VBICcollCXCollCXPtr);
    RESOLVE(VBICcollCICollCIPtr);
    RESOLVE(VBICbaseBXBaseBXPtr);
    RESOLVE(VBICbaseBIBaseBIPtr);
    RESOLVE(VBICemitEIEmitEIPtr);
    RESOLVE(VBICbaseBPBaseBPPtr);
    RESOLVE(VBICsubsSISubsSIPtr);
    RESOLVE(VBICbaseEmitPtr);
    RESOLVE(VBICemitBasePtr);
    RESOLVE(VBICbaseCollPtr);
    RESOLVE(VBICcollBasePtr);
    RESOLVE(VBICcollCollCXPtr);
    RESOLVE(VBICbaseBaseBXPtr);
    RESOLVE(VBICemitEmitEIPtr);
    RESOLVE(VBICsubsSubsSIPtr);
    RESOLVE(VBICcollCXCollCIPtr);
    RESOLVE(VBICcollCXBaseBXPtr);
    RESOLVE(VBICcollCXBaseBIPtr);
    RESOLVE(VBICcollCXBaseBPPtr);
    RESOLVE(VBICcollCIBaseBIPtr);
    RESOLVE(VBICcollCIEmitEIPtr);
    RESOLVE(VBICbaseBXBaseBIPtr);
    RESOLVE(VBICbaseBXEmitEIPtr);
    RESOLVE(VBICbaseBXBaseBPPtr);
    RESOLVE(VBICbaseBXSubsSIPtr);
    RESOLVE(VBICbaseBIEmitEIPtr);
    RESOLVE(VBICbaseBPSubsSIPtr);
    RESOLVE(VBICcollCXCollPtr);
    RESOLVE(VBICbaseBXBasePtr);
    RESOLVE(VBICemitEIEmitPtr);
    RESOLVE(VBICsubsSISubsPtr);
    RESOLVE(VBICcollCICollCXPtr);
    RESOLVE(VBICbaseBICollCXPtr);
    RESOLVE(VBICbaseBPCollCXPtr);
    RESOLVE(VBICbaseBXCollCIPtr);
    RESOLVE(VBICbaseBICollCIPtr);
    RESOLVE(VBICemitEICollCIPtr);
    RESOLVE(VBICbaseBPCollCIPtr);
    RESOLVE(VBICbaseBIBaseBXPtr);
    RESOLVE(VBICemitEIBaseBXPtr);
    RESOLVE(VBICbaseBPBaseBXPtr);
    RESOLVE(VBICsubsSIBaseBXPtr);
    RESOLVE(VBICemitEIBaseBIPtr);
    RESOLVE(VBICbaseBPBaseBIPtr);
    RESOLVE(VBICsubsSICollCIPtr);
    RESOLVE(VBICsubsSIBaseBIPtr);
    RESOLVE(VBICsubsSIBaseBPPtr);

#undef RESOLVE
}

// ---------------------------------------------------------------------------
// set_state_ptrs
// ---------------------------------------------------------------------------
void VBICDevice::set_state_ptrs(double* s0, double* s1, double* s2, int32_t base) {
    state0_ = s0;
    state1_ = s1;
    state2_ = s2;
    state_base_ = base;
    inst_.VBICstate = base;
}

// ---------------------------------------------------------------------------
// evaluate
// ---------------------------------------------------------------------------
void VBICDevice::evaluate(const std::vector<double>& voltages,
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

    // First-call VBICtemp.
    if (!temp_done_) {
        int rc = VBICtemp(model_, &ckt);
        if (rc != Shim::OK) {
            throw std::runtime_error("VBICtemp failed with rc=" + std::to_string(rc));
        }
        temp_done_ = true;
    }

    // Splice this instance as sole member, call load, then restore.
    VBICInstance* saved_head      = model_->VBICinstances;
    VBICInstance* saved_next_inst = inst_.VBICnextInstance;
    VBICModel*    saved_next_mod  = model_->VBICnextModel;
    model_->VBICinstances  = &inst_;
    inst_.VBICnextInstance = nullptr;
    model_->VBICnextModel  = nullptr;
    int rc = VBICload(model_, &ckt);
    model_->VBICinstances  = saved_head;
    inst_.VBICnextInstance = saved_next_inst;
    model_->VBICnextModel  = saved_next_mod;
    if (rc != Shim::OK) {
        throw std::runtime_error("VBICload failed with rc=" + std::to_string(rc));
    }

    last_noncon_ = ckt.CKTnoncon;

    // Fold ghost rhs contributions back into the real rhs.
    for (int32_t k = 1; k <= max_neo_node_ + 1 && k < n_ghost; ++k) {
        if (k - 1 < n_real) rhs[k - 1] += ghost_rhs_[k];
    }
}

// ---------------------------------------------------------------------------
// set_ic — Initial conditions for VBE and VCE
// ---------------------------------------------------------------------------
void VBICDevice::set_ic(double vbe, bool vbe_given,
                        double vce, bool vce_given) {
    if (vbe_given) { inst_.VBICicVBE = vbe; inst_.VBICicVBEGiven = 1; }
    if (vce_given) { inst_.VBICicVCE = vce; inst_.VBICicVCEGiven = 1; }
}

// ---------------------------------------------------------------------------
// ac_stamp — linearized small-signal AC stamp (G/C matrix split)
//
// Translates ngspice vbicacld.c complex-matrix stamping into separate
// G (conductance) and C (capacitance) matrices.  The AC solver combines
// them as (G + jwC) at each frequency point.
//
// VBIC state vector layout for conductances and capacitances:
//   Ibe_Vbei  = state[10]   Ibex_Vbex = state[12]
//   Itzf_Vbei = state[14]   Itzf_Vbci = state[15]
//   Itzr_Vbci = state[17]   Itzr_Vbei = state[18]
//   Ibc_Vbci  = state[20]   Ibc_Vbei  = state[21]
//   Ibep_Vbep = state[23]
//   Irci_Vrci = state[25]   Irci_Vbci = state[26]  Irci_Vbcx = state[27]
//   Irbi_Vrbi = state[29]   Irbi_Vbei = state[30]  Irbi_Vbci = state[31]
//   Irbp_Vrbp = state[33]   Irbp_Vbep = state[34]  Irbp_Vbci = state[35]
//   Ibcp_Vbcp = state[55]
//   Iccp_Vbep = state[57]   Iccp_Vbci = state[58]  Iccp_Vbcp = state[59]
//   Ircx_Vrcx = state[62]   Irbx_Vrbx = state[63]
//   Irs_Vrs   = state[64]   Ire_Vre   = state[65]
//
// Charge capacitance dQ/dV:
//   cqbe     = state[37]   cqbeci  = state[38]
//   cqbex    = state[40]   cqbc    = state[42]
//   cqbcx    = state[44]   cqbep   = state[46]
//   cqbepci  = state[47]   cqbcp   = state[61]
// ---------------------------------------------------------------------------
void VBICDevice::ac_stamp(const std::vector<double>& /*voltages*/,
                            NumericMatrix& G, NumericMatrix& C) {
    auto& inst = inst_;
    if (!state0_ || state_base_ < 0) return;

    // --- Conductance values from state vector ---
    const double Ibe_Vbei  = state0_[state_base_ + 10];
    const double Ibex_Vbex = state0_[state_base_ + 12];
    const double Itzf_Vbei = state0_[state_base_ + 14];
    const double Itzf_Vbci = state0_[state_base_ + 15];
    const double Itzr_Vbci = state0_[state_base_ + 17];
    const double Itzr_Vbei = state0_[state_base_ + 18];
    const double Ibc_Vbci  = state0_[state_base_ + 20];
    const double Ibc_Vbei  = state0_[state_base_ + 21];
    const double Ibep_Vbep = state0_[state_base_ + 23];
    const double Irci_Vrci = state0_[state_base_ + 25];
    const double Irci_Vbci = state0_[state_base_ + 26];
    const double Irci_Vbcx = state0_[state_base_ + 27];
    const double Irbi_Vrbi = state0_[state_base_ + 29];
    const double Irbi_Vbei = state0_[state_base_ + 30];
    const double Irbi_Vbci = state0_[state_base_ + 31];
    const double Irbp_Vrbp = state0_[state_base_ + 33];
    const double Irbp_Vbep = state0_[state_base_ + 34];
    const double Irbp_Vbci = state0_[state_base_ + 35];
    const double Ibcp_Vbcp = state0_[state_base_ + 55];
    const double Iccp_Vbep = state0_[state_base_ + 57];
    const double Iccp_Vbci = state0_[state_base_ + 58];
    const double Iccp_Vbcp = state0_[state_base_ + 59];
    const double Ircx_Vrcx = state0_[state_base_ + 62];
    const double Irbx_Vrbx = state0_[state_base_ + 63];
    const double Irs_Vrs   = state0_[state_base_ + 64];
    const double Ire_Vre   = state0_[state_base_ + 65];

    // --- Capacitance values (dQ/dV) from state vector ---
    const double XQbe_Vbei  = state0_[state_base_ + 37];  // cqbe
    const double XQbe_Vbci  = state0_[state_base_ + 38];  // cqbeci
    const double XQbex_Vbex = state0_[state_base_ + 40];  // cqbex
    const double XQbc_Vbci  = state0_[state_base_ + 42];  // cqbc
    const double XQbcx_Vbcx = state0_[state_base_ + 44];  // cqbcx
    const double XQbep_Vbep = state0_[state_base_ + 46];  // cqbep
    const double XQbep_Vbci = state0_[state_base_ + 47];  // cqbepci
    const double XQbcp_Vbcp = state0_[state_base_ + 61];  // cqbcp

    // --- G matrix: real part (conductances) ---
    // Ibe stamp
    G.add(inst.VBICbaseBIBaseBIPtr,  Ibe_Vbei);
    G.add(inst.VBICbaseBIEmitEIPtr, -Ibe_Vbei);
    G.add(inst.VBICemitEIBaseBIPtr, -Ibe_Vbei);
    G.add(inst.VBICemitEIEmitEIPtr,  Ibe_Vbei);
    // Ibex stamp
    G.add(inst.VBICbaseBXBaseBXPtr,  Ibex_Vbex);
    G.add(inst.VBICbaseBXEmitEIPtr, -Ibex_Vbex);
    G.add(inst.VBICemitEIBaseBXPtr, -Ibex_Vbex);
    G.add(inst.VBICemitEIEmitEIPtr,  Ibex_Vbex);
    // Itzf stamp
    G.add(inst.VBICcollCIBaseBIPtr,  Itzf_Vbei);
    G.add(inst.VBICcollCIEmitEIPtr, -Itzf_Vbei);
    G.add(inst.VBICcollCIBaseBIPtr,  Itzf_Vbci);
    G.add(inst.VBICcollCICollCIPtr, -Itzf_Vbci);
    G.add(inst.VBICemitEIBaseBIPtr, -Itzf_Vbei);
    G.add(inst.VBICemitEIEmitEIPtr,  Itzf_Vbei);
    G.add(inst.VBICemitEIBaseBIPtr, -Itzf_Vbci);
    G.add(inst.VBICemitEICollCIPtr,  Itzf_Vbci);
    // Itzr stamp
    G.add(inst.VBICemitEIBaseBIPtr,  Itzr_Vbci);
    G.add(inst.VBICemitEICollCIPtr, -Itzr_Vbci);
    G.add(inst.VBICemitEIBaseBIPtr,  Itzr_Vbei);
    G.add(inst.VBICemitEIEmitEIPtr, -Itzr_Vbei);
    G.add(inst.VBICcollCIBaseBIPtr, -Itzr_Vbci);
    G.add(inst.VBICcollCICollCIPtr,  Itzr_Vbci);
    G.add(inst.VBICcollCIBaseBIPtr, -Itzr_Vbei);
    G.add(inst.VBICcollCIEmitEIPtr,  Itzr_Vbei);
    // Ibc stamp
    G.add(inst.VBICbaseBIBaseBIPtr,  Ibc_Vbci);
    G.add(inst.VBICbaseBICollCIPtr, -Ibc_Vbci);
    G.add(inst.VBICbaseBIBaseBIPtr,  Ibc_Vbei);
    G.add(inst.VBICbaseBIEmitEIPtr, -Ibc_Vbei);
    G.add(inst.VBICcollCIBaseBIPtr, -Ibc_Vbci);
    G.add(inst.VBICcollCICollCIPtr,  Ibc_Vbci);
    G.add(inst.VBICcollCIBaseBIPtr, -Ibc_Vbei);
    G.add(inst.VBICcollCIEmitEIPtr,  Ibc_Vbei);
    // Ibep stamp
    G.add(inst.VBICbaseBXBaseBXPtr,  Ibep_Vbep);
    G.add(inst.VBICbaseBXBaseBPPtr, -Ibep_Vbep);
    G.add(inst.VBICbaseBPBaseBXPtr, -Ibep_Vbep);
    G.add(inst.VBICbaseBPBaseBPPtr,  Ibep_Vbep);
    // Ircx stamp
    G.add(inst.VBICcollCollPtr,      Ircx_Vrcx);
    G.add(inst.VBICcollCXCollCXPtr,  Ircx_Vrcx);
    G.add(inst.VBICcollCXCollPtr,   -Ircx_Vrcx);
    G.add(inst.VBICcollCollCXPtr,   -Ircx_Vrcx);
    // Irci stamp
    G.add(inst.VBICcollCXCollCXPtr,  Irci_Vrci);
    G.add(inst.VBICcollCXCollCIPtr, -Irci_Vrci);
    G.add(inst.VBICcollCXBaseBIPtr,  Irci_Vbci);
    G.add(inst.VBICcollCXCollCIPtr, -Irci_Vbci);
    G.add(inst.VBICcollCXBaseBIPtr,  Irci_Vbcx);
    G.add(inst.VBICcollCXCollCXPtr, -Irci_Vbcx);
    G.add(inst.VBICcollCICollCXPtr, -Irci_Vrci);
    G.add(inst.VBICcollCICollCIPtr,  Irci_Vrci);
    G.add(inst.VBICcollCIBaseBIPtr, -Irci_Vbci);
    G.add(inst.VBICcollCICollCIPtr,  Irci_Vbci);
    G.add(inst.VBICcollCIBaseBIPtr, -Irci_Vbcx);
    G.add(inst.VBICcollCICollCXPtr,  Irci_Vbcx);
    // Irbx stamp
    G.add(inst.VBICbaseBasePtr,      Irbx_Vrbx);
    G.add(inst.VBICbaseBXBaseBXPtr,  Irbx_Vrbx);
    G.add(inst.VBICbaseBXBasePtr,   -Irbx_Vrbx);
    G.add(inst.VBICbaseBaseBXPtr,   -Irbx_Vrbx);
    // Irbi stamp
    G.add(inst.VBICbaseBXBaseBXPtr,  Irbi_Vrbi);
    G.add(inst.VBICbaseBXBaseBIPtr, -Irbi_Vrbi);
    G.add(inst.VBICbaseBXBaseBIPtr,  Irbi_Vbei);
    G.add(inst.VBICbaseBXEmitEIPtr, -Irbi_Vbei);
    G.add(inst.VBICbaseBXBaseBIPtr,  Irbi_Vbci);
    G.add(inst.VBICbaseBXCollCIPtr, -Irbi_Vbci);
    G.add(inst.VBICbaseBIBaseBXPtr, -Irbi_Vrbi);
    G.add(inst.VBICbaseBIBaseBIPtr,  Irbi_Vrbi);
    G.add(inst.VBICbaseBIBaseBIPtr, -Irbi_Vbei);
    G.add(inst.VBICbaseBIEmitEIPtr,  Irbi_Vbei);
    G.add(inst.VBICbaseBIBaseBIPtr, -Irbi_Vbci);
    G.add(inst.VBICbaseBICollCIPtr,  Irbi_Vbci);
    // Ire stamp
    G.add(inst.VBICemitEmitPtr,      Ire_Vre);
    G.add(inst.VBICemitEIEmitEIPtr,  Ire_Vre);
    G.add(inst.VBICemitEIEmitPtr,   -Ire_Vre);
    G.add(inst.VBICemitEmitEIPtr,   -Ire_Vre);
    // Irbp stamp
    G.add(inst.VBICbaseBPBaseBPPtr,  Irbp_Vrbp);
    G.add(inst.VBICbaseBPCollCXPtr, -Irbp_Vrbp);
    G.add(inst.VBICbaseBPBaseBXPtr,  Irbp_Vbep);
    G.add(inst.VBICbaseBPBaseBPPtr, -Irbp_Vbep);
    G.add(inst.VBICbaseBPBaseBIPtr,  Irbp_Vbci);
    G.add(inst.VBICbaseBPCollCIPtr, -Irbp_Vbci);
    G.add(inst.VBICcollCXBaseBPPtr, -Irbp_Vrbp);
    G.add(inst.VBICcollCXCollCXPtr,  Irbp_Vrbp);
    G.add(inst.VBICcollCXBaseBXPtr, -Irbp_Vbep);
    G.add(inst.VBICcollCXBaseBPPtr,  Irbp_Vbep);
    G.add(inst.VBICcollCXBaseBIPtr, -Irbp_Vbci);
    G.add(inst.VBICcollCXCollCIPtr,  Irbp_Vbci);
    // Ibcp stamp
    G.add(inst.VBICsubsSISubsSIPtr,  Ibcp_Vbcp);
    G.add(inst.VBICsubsSIBaseBPPtr, -Ibcp_Vbcp);
    G.add(inst.VBICbaseBPSubsSIPtr, -Ibcp_Vbcp);
    G.add(inst.VBICbaseBPBaseBPPtr,  Ibcp_Vbcp);
    // Iccp stamp
    G.add(inst.VBICbaseBXBaseBXPtr,  Iccp_Vbep);
    G.add(inst.VBICbaseBXBaseBPPtr, -Iccp_Vbep);
    G.add(inst.VBICbaseBXBaseBIPtr,  Iccp_Vbci);
    G.add(inst.VBICbaseBXCollCIPtr, -Iccp_Vbci);
    G.add(inst.VBICbaseBXSubsSIPtr,  Iccp_Vbcp);
    G.add(inst.VBICbaseBXBaseBPPtr, -Iccp_Vbcp);
    G.add(inst.VBICsubsSIBaseBXPtr, -Iccp_Vbep);
    G.add(inst.VBICsubsSIBaseBPPtr,  Iccp_Vbep);
    G.add(inst.VBICsubsSIBaseBIPtr, -Iccp_Vbci);
    G.add(inst.VBICsubsSICollCIPtr,  Iccp_Vbci);
    G.add(inst.VBICsubsSISubsSIPtr, -Iccp_Vbcp);
    G.add(inst.VBICsubsSIBaseBPPtr,  Iccp_Vbcp);
    // Irs stamp
    G.add(inst.VBICsubsSubsPtr,      Irs_Vrs);
    G.add(inst.VBICsubsSISubsSIPtr,  Irs_Vrs);
    G.add(inst.VBICsubsSISubsPtr,   -Irs_Vrs);
    G.add(inst.VBICsubsSubsSIPtr,   -Irs_Vrs);

    // --- C matrix: imaginary part (capacitances) ---
    // Qbe stamp
    C.add(inst.VBICbaseBIBaseBIPtr,  XQbe_Vbei);
    C.add(inst.VBICbaseBIEmitEIPtr, -XQbe_Vbei);
    C.add(inst.VBICbaseBIBaseBIPtr,  XQbe_Vbci);
    C.add(inst.VBICbaseBICollCIPtr, -XQbe_Vbci);
    C.add(inst.VBICemitEIBaseBIPtr, -XQbe_Vbei);
    C.add(inst.VBICemitEIEmitEIPtr,  XQbe_Vbei);
    C.add(inst.VBICemitEIBaseBIPtr, -XQbe_Vbci);
    C.add(inst.VBICemitEICollCIPtr,  XQbe_Vbci);
    // Qbex stamp
    C.add(inst.VBICbaseBXBaseBXPtr,  XQbex_Vbex);
    C.add(inst.VBICbaseBXEmitEIPtr, -XQbex_Vbex);
    C.add(inst.VBICemitEIBaseBXPtr, -XQbex_Vbex);
    C.add(inst.VBICemitEIEmitEIPtr,  XQbex_Vbex);
    // Qbc stamp
    C.add(inst.VBICbaseBIBaseBIPtr,  XQbc_Vbci);
    C.add(inst.VBICbaseBICollCIPtr, -XQbc_Vbci);
    C.add(inst.VBICcollCIBaseBIPtr, -XQbc_Vbci);
    C.add(inst.VBICcollCICollCIPtr,  XQbc_Vbci);
    // Qbcx stamp
    C.add(inst.VBICbaseBIBaseBIPtr,  XQbcx_Vbcx);
    C.add(inst.VBICbaseBICollCXPtr, -XQbcx_Vbcx);
    C.add(inst.VBICcollCXBaseBIPtr, -XQbcx_Vbcx);
    C.add(inst.VBICcollCXCollCXPtr,  XQbcx_Vbcx);
    // Qbep stamp
    C.add(inst.VBICbaseBXBaseBXPtr,  XQbep_Vbep);
    C.add(inst.VBICbaseBXBaseBPPtr, -XQbep_Vbep);
    C.add(inst.VBICbaseBXBaseBIPtr,  XQbep_Vbci);
    C.add(inst.VBICbaseBXCollCIPtr, -XQbep_Vbci);
    C.add(inst.VBICbaseBPBaseBXPtr, -XQbep_Vbep);
    C.add(inst.VBICbaseBPBaseBPPtr,  XQbep_Vbep);
    C.add(inst.VBICbaseBPBaseBIPtr, -XQbep_Vbci);
    C.add(inst.VBICbaseBPCollCIPtr,  XQbep_Vbci);
    // Qbcp stamp
    C.add(inst.VBICsubsSISubsSIPtr,  XQbcp_Vbcp);
    C.add(inst.VBICsubsSIBaseBPPtr, -XQbcp_Vbcp);
    C.add(inst.VBICbaseBPSubsSIPtr, -XQbcp_Vbcp);
    C.add(inst.VBICbaseBPBaseBPPtr,  XQbcp_Vbcp);
}

// ---------------------------------------------------------------------------
// compute_trunc — device-specific local truncation error for time stepping
//
// VBIC charge state variables (from NIintegrate calls in load):
//   qbe  = state[36],  qbex = state[39],  qbc  = state[41]
//   qbcx = state[43],  qbep = state[45],  qbeo = state[48]
//   qbco = state[51],  qbcp = state[60]
// ---------------------------------------------------------------------------
double VBICDevice::compute_trunc(const IntegratorCtx& ctx,
                              const SimOptions& opts) const {
    if (ctx.order < 2 || ctx.delta <= 0.0)
        return 1e30;

    if (!state0_ || !state1_ || !state2_)
        return 1e30;

    const double lte_coeff = ctx.lte_coefficient();
    const double h0 = ctx.delta;
    const double h1 = ctx.delta_old[1];
    if (h1 <= 0.0) return 1e30;

    // Charge offsets relative to state base
    static const int charge_offsets[] = {36, 39, 41, 43, 45, 48, 51, 60};
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
bool VBICDevice::device_converged() const {
    return last_noncon_ == 0;
}

// ---------------------------------------------------------------------------
// query_param — post-simulation parameter query
//
// State vector layout for operating point parameters:
//   vbei       = state[0]    vbex  = state[1]
//   vbci       = state[2]    vbcx  = state[3]
//   vbep       = state[4]    vrci  = state[5]
//   vrbi       = state[6]    vrbp  = state[7]
//   vbcp       = state[8]
//   ibe        = state[9]    ibe_Vbei   = state[10]
//   ibex       = state[11]   ibex_Vbex  = state[12]
//   itzf       = state[13]   itzf_Vbei  = state[14]  itzf_Vbci = state[15]
//   itzr       = state[16]   itzr_Vbci  = state[17]  itzr_Vbei = state[18]
//   ibc        = state[19]   ibc_Vbci   = state[20]  ibc_Vbei  = state[21]
//   ibep       = state[22]   ibep_Vbep  = state[23]
//   irci       = state[24]
//   irbi       = state[28]
//   irbp       = state[32]
//   ibcp       = state[54]   ibcp_Vbcp  = state[55]
//   iccp       = state[56]
// ---------------------------------------------------------------------------
std::optional<double>
VBICDevice::query_param(const std::string& name) const {
    const std::string key = str_tolower(name);
    const double m = inst_.VBICm;

    // Operating point from state vector
    if (state0_ && state_base_ >= 0) {
        if (key == "vbe" || key == "vbei")  return state0_[state_base_ + 0];
        if (key == "vbex") return state0_[state_base_ + 1];
        if (key == "vbc" || key == "vbci")  return state0_[state_base_ + 2];
        if (key == "vbcx") return state0_[state_base_ + 3];
        if (key == "vbep") return state0_[state_base_ + 4];

        // Currents (scaled by multiplier)
        if (key == "ibe")  return state0_[state_base_ + 9] * m;
        if (key == "ibex") return state0_[state_base_ + 11] * m;
        if (key == "ic" || key == "itzf")  return state0_[state_base_ + 13] * m;
        if (key == "itzr") return state0_[state_base_ + 16] * m;
        if (key == "ibc")  return state0_[state_base_ + 19] * m;
        if (key == "ibep") return state0_[state_base_ + 22] * m;
        if (key == "ibcp") return state0_[state_base_ + 54] * m;
        if (key == "iccp") return state0_[state_base_ + 56] * m;

        // Conductances (scaled by multiplier)
        if (key == "gpi")  return state0_[state_base_ + 10] * m;  // dIbe/dVbei
        if (key == "gm")   return state0_[state_base_ + 14] * m;  // dItzf/dVbei
        if (key == "go")   return state0_[state_base_ + 15] * m;  // dItzf/dVbci
        if (key == "gmu")  return state0_[state_base_ + 20] * m;  // dIbc/dVbci

        // Capacitances
        if (key == "cbe")  return inst_.VBICcapbe;
        if (key == "cbex") return inst_.VBICcapbex;
        if (key == "cbc")  return inst_.VBICcapbc;
        if (key == "cbcx") return inst_.VBICcapbcx;
        if (key == "cbep") return inst_.VBICcapbep;
        if (key == "cbcp") return inst_.VBICcapbcp;
    }

    // Geometry
    if (key == "area") return inst_.VBICarea;
    if (key == "m")    return inst_.VBICm;

    return std::nullopt;
}

// ---------------------------------------------------------------------------
// noise_sources — VBIC noise contributions (thermal + shot + flicker)
//
// Sources (following ngspice vbicnoise.c):
//   1. Thermal noise on Rcx:  collCX  <-> coll     (Ircx_Vrcx conductance)
//   2. Thermal noise on Rci:  collCX  <-> collCI   (Irci_Vrci conductance)
//   3. Thermal noise on Rbx:  baseBX  <-> base     (Irbx_Vrbx conductance)
//   4. Thermal noise on Rbi:  baseBX  <-> baseBI   (Irbi_Vrbi conductance)
//   5. Thermal noise on Re:   emitEI  <-> emit     (Ire_Vre conductance)
//   6. Thermal noise on Rbp:  baseBP  <-> collCX   (Irbp_Vrbp conductance)
//   7. Thermal noise on Rs:   subsSI  <-> subs     (Irs_Vrs conductance)
//   8. Shot noise on Ic:      collCI  <-> emitEI
//   9. Shot noise on Ib:      baseBI  <-> emitEI
//  10. Shot noise on Ibep:    baseBX  <-> baseBP
//  11. Shot noise on Iccp:    baseBX  <-> subsSI
//  12. Flicker noise on Ibe:  baseBI  <-> emitEI
//  13. Flicker noise on Ibep: baseBX  <-> baseBP
// ---------------------------------------------------------------------------
std::vector<Device::NoiseSource> VBICDevice::noise_sources(
        double freq, const std::vector<double>& /*dc_solution*/) const {
    if (!state0_ || state_base_ < 0)
        return {};

    std::vector<NoiseSource> sources;

    // Neospice node indices (ucb-1): -1 is ground.
    const int32_t coll_node     = inst_.VBICcollNode     - 1;
    const int32_t base_node     = inst_.VBICbaseNode     - 1;
    const int32_t emit_node     = inst_.VBICemitNode     - 1;
    const int32_t subs_node     = inst_.VBICsubsNode     - 1;
    const int32_t collCX_node   = inst_.VBICcollCXNode   - 1;
    const int32_t collCI_node   = inst_.VBICcollCINode   - 1;
    const int32_t baseBX_node   = inst_.VBICbaseBXNode   - 1;
    const int32_t baseBI_node   = inst_.VBICbaseBINode   - 1;
    const int32_t baseBP_node   = inst_.VBICbaseBPNode   - 1;
    const int32_t emitEI_node   = inst_.VBICemitEINode   - 1;
    const int32_t subsSI_node   = inst_.VBICsubsSINode   - 1;

    const double T  = T_NOMINAL;
    const double m  = inst_.VBICm;

    // Conductances from state vector
    const double Ircx_Vrcx = state0_[state_base_ + 62];
    const double Irci_Vrci = state0_[state_base_ + 25];
    const double Irbx_Vrbx = state0_[state_base_ + 63];
    const double Irbi_Vrbi = state0_[state_base_ + 29];
    const double Ire_Vre   = state0_[state_base_ + 65];
    const double Irbp_Vrbp = state0_[state_base_ + 33];
    const double Irs_Vrs   = state0_[state_base_ + 64];

    // DC currents
    const double Itzf = std::abs(state0_[state_base_ + 13]);
    const double Ibe  = std::abs(state0_[state_base_ + 9]);
    const double Ibep = std::abs(state0_[state_base_ + 22]);
    const double Iccp = std::abs(state0_[state_base_ + 56]);

    // 1. Thermal noise on series resistances
    if (Ircx_Vrcx > 0.0)
        sources.push_back({collCX_node, coll_node,
                           m * 4.0 * BOLTZMANN * T * Ircx_Vrcx});
    if (Irci_Vrci > 0.0)
        sources.push_back({collCX_node, collCI_node,
                           m * 4.0 * BOLTZMANN * T * Irci_Vrci});
    if (Irbx_Vrbx > 0.0)
        sources.push_back({baseBX_node, base_node,
                           m * 4.0 * BOLTZMANN * T * Irbx_Vrbx});
    if (Irbi_Vrbi > 0.0)
        sources.push_back({baseBX_node, baseBI_node,
                           m * 4.0 * BOLTZMANN * T * Irbi_Vrbi});
    if (Ire_Vre > 0.0)
        sources.push_back({emitEI_node, emit_node,
                           m * 4.0 * BOLTZMANN * T * Ire_Vre});
    if (Irbp_Vrbp > 0.0)
        sources.push_back({baseBP_node, collCX_node,
                           m * 4.0 * BOLTZMANN * T * Irbp_Vrbp});
    if (Irs_Vrs > 0.0)
        sources.push_back({subsSI_node, subs_node,
                           m * 4.0 * BOLTZMANN * T * Irs_Vrs});

    // 2. Shot noise on collector and base currents
    sources.push_back({collCI_node, emitEI_node,
                       m * 2.0 * CHARGE_Q * Itzf});
    sources.push_back({baseBI_node, emitEI_node,
                       m * 2.0 * CHARGE_Q * Ibe});
    if (Ibep > 0.0)
        sources.push_back({baseBX_node, baseBP_node,
                           m * 2.0 * CHARGE_Q * Ibep});
    if (Iccp > 0.0)
        sources.push_back({baseBX_node, subsSI_node,
                           m * 2.0 * CHARGE_Q * Iccp});

    // 3. Flicker (1/f) noise on Ibe and Ibep
    const double KF  = model_->VBICfNcoef;
    const double AF  = model_->VBICfNexpA;
    const double BF  = model_->VBICfNexpB;

    if (KF > 0.0 && freq > 0.0) {
        if (Ibe > 0.0) {
            double Ibe_per = Ibe / m;
            double S_flk = m * KF * std::pow(Ibe_per, AF) / std::pow(freq, BF);
            sources.push_back({baseBI_node, emitEI_node, S_flk});
        }
        if (Ibep > 0.0) {
            double Ibep_per = Ibep / m;
            double S_flk = m * KF * std::pow(Ibep_per, AF) / std::pow(freq, BF);
            sources.push_back({baseBX_node, baseBP_node, S_flk});
        }
    }

    return sources;
}

} // namespace neospice
