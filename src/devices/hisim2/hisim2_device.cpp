#include "devices/hisim2/hisim2_device.hpp"

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
namespace neospice::hisim2 {
    int HSM2setup(Shim::Matrix*, HSM2Model*, Shim::Ckt*, int*);
    int HSM2temp(HSM2Model*, Shim::Ckt*);
    int HSM2load(HSM2Model*, Shim::Ckt*);
}

namespace neospice {

using namespace neospice::hisim2;

// ---------------------------------------------------------------------------
// HSM2ModelCard destructor
// ---------------------------------------------------------------------------
HSM2ModelCard::~HSM2ModelCard() = default;

// ---------------------------------------------------------------------------
// make
// ---------------------------------------------------------------------------
std::unique_ptr<HSM2Device>
HSM2Device::make(std::string name,
        int32_t n_d, int32_t n_g, int32_t n_s, int32_t n_b,
        const Geom& geom, HSM2ModelCard& shared_card) {
    std::unique_ptr<HSM2Device> dev(new HSM2Device(std::move(name)));
    dev->model_ = &shared_card.ucb;
    dev->ext_nodes_ = {n_d, n_g, n_s, n_b};

    auto& inst = dev->inst_;
    inst.HSM2name = dev->name().c_str();
    inst.HSM2modPtr = dev->model_;

    // Node wiring (UCB convention).
    inst.HSM2dNode = neo_to_ucb(n_d);
    inst.HSM2gNode = neo_to_ucb(n_g);
    inst.HSM2sNode = neo_to_ucb(n_s);
    inst.HSM2bNode = neo_to_ucb(n_b);

    // Geometry.
    inst.HSM2_l = geom.L;
    inst.HSM2_l_Given = 1;
    inst.HSM2_w = geom.W;
    inst.HSM2_w_Given = 1;
    inst.HSM2_m = geom.M;
    if (geom.M != 1.0) inst.HSM2_m_Given = 1;
    inst.HSM2_ad = geom.AD;
    inst.HSM2_ad_Given = (geom.AD != 0.0) ? 1 : 0;
    inst.HSM2_as = geom.AS;
    inst.HSM2_as_Given = (geom.AS != 0.0) ? 1 : 0;
    inst.HSM2_pd = geom.PD;
    inst.HSM2_pd_Given = (geom.PD != 0.0) ? 1 : 0;
    inst.HSM2_ps = geom.PS;
    inst.HSM2_ps_Given = (geom.PS != 0.0) ? 1 : 0;
    inst.HSM2_nrd = geom.NRD;
    inst.HSM2_nrd_Given = (geom.NRD != 0.0) ? 1 : 0;
    inst.HSM2_nrs = geom.NRS;
    inst.HSM2_nrs_Given = (geom.NRS != 0.0) ? 1 : 0;
    inst.HSM2_nf = geom.NF;
    inst.HSM2_nf_Given = 1;

    // Thread onto the shared model's instance list.
    inst.HSM2nextInstance = shared_card.ucb.HSM2instances;
    shared_card.ucb.HSM2instances = &inst;

    // Remember the widest real node index for ghost array sizing.
    int32_t widest = -1;
    for (int32_t n : {n_d, n_g, n_s, n_b}) if (n > widest) widest = n;
    dev->max_neo_node_ = widest;

    return dev;
}

// ---------------------------------------------------------------------------
// declare_internal_nodes
// ---------------------------------------------------------------------------
void HSM2Device::declare_internal_nodes(Circuit& ckt) {
    ucb_declare_internal_nodes<Shim::Matrix, Shim::Ckt>(
        ckt, name_,
        [this](Shim::Matrix& m, Shim::Ckt& c) {
            UCB_SPLICE_INSTANCE(HSM2);
            int states = 0;
            int rc = HSM2setup(&m, model_, &c, &states);
            UCB_UNSPLICE_INSTANCE(HSM2);
            return rc;
        },
        "HSM2setup", journal_, max_neo_node_);
}

// ---------------------------------------------------------------------------
// stamp_pattern
// ---------------------------------------------------------------------------
void HSM2Device::stamp_pattern(SparsityBuilder& builder) const {
    ucb_stamp_pattern(journal_, builder);
}

// ---------------------------------------------------------------------------
// assign_offsets
// ---------------------------------------------------------------------------
void HSM2Device::assign_offsets(const SparsityPattern& pattern) {
    const auto offsets = ucb_compute_offsets(journal_, pattern);

#define RESOLVE(f)                                                       \
    do {                                                                 \
        if (inst_.f >= 0)                                                \
            inst_.f = offsets[inst_.f];                                  \
    } while (0)

    RESOLVE(HSM2GgPtr);
    RESOLVE(HSM2GgpPtr);
    RESOLVE(HSM2GdpPtr);
    RESOLVE(HSM2GspPtr);
    RESOLVE(HSM2GbpPtr);
    RESOLVE(HSM2GPgPtr);
    RESOLVE(HSM2GPgpPtr);
    RESOLVE(HSM2GPdpPtr);
    RESOLVE(HSM2GPspPtr);
    RESOLVE(HSM2GPbpPtr);
    RESOLVE(HSM2DPdPtr);
    RESOLVE(HSM2DPdpPtr);
    RESOLVE(HSM2DPgpPtr);
    RESOLVE(HSM2DPspPtr);
    RESOLVE(HSM2DPbpPtr);
    RESOLVE(HSM2DPdbPtr);
    RESOLVE(HSM2DdPtr);
    RESOLVE(HSM2DdpPtr);
    RESOLVE(HSM2SPsPtr);
    RESOLVE(HSM2SPspPtr);
    RESOLVE(HSM2SPgpPtr);
    RESOLVE(HSM2SPdpPtr);
    RESOLVE(HSM2SPbpPtr);
    RESOLVE(HSM2SPsbPtr);
    RESOLVE(HSM2SsPtr);
    RESOLVE(HSM2SspPtr);
    RESOLVE(HSM2BPgpPtr);
    RESOLVE(HSM2BPbpPtr);
    RESOLVE(HSM2BPdpPtr);
    RESOLVE(HSM2BPspPtr);
    RESOLVE(HSM2BPbPtr);
    RESOLVE(HSM2BPdbPtr);
    RESOLVE(HSM2BPsbPtr);
    RESOLVE(HSM2DBdpPtr);
    RESOLVE(HSM2DBdbPtr);
    RESOLVE(HSM2DBbpPtr);
    RESOLVE(HSM2DBbPtr);
    RESOLVE(HSM2SBspPtr);
    RESOLVE(HSM2SBbpPtr);
    RESOLVE(HSM2SBbPtr);
    RESOLVE(HSM2SBsbPtr);
    RESOLVE(HSM2BsbPtr);
    RESOLVE(HSM2BbpPtr);
    RESOLVE(HSM2BdbPtr);
    RESOLVE(HSM2BbPtr);

#undef RESOLVE
}

// ---------------------------------------------------------------------------
// set_state_ptrs
// ---------------------------------------------------------------------------
void HSM2Device::set_state_ptrs(double* s0, double* s1, double* s2, int32_t base) {
    state0_ = s0;
    state1_ = s1;
    state2_ = s2;
    state_base_ = base;
    inst_.HSM2states = base;
}

// ---------------------------------------------------------------------------
// evaluate
// ---------------------------------------------------------------------------
void HSM2Device::evaluate(const std::vector<double>& voltages,
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

    // First-call HSM2temp.
    if (!temp_done_) {
        int rc = HSM2temp(model_, &ckt);
        if (rc != Shim::OK) {
            throw std::runtime_error("HSM2temp failed with rc=" + std::to_string(rc));
        }
        temp_done_ = true;
    }

    // Splice this instance as sole member, call load, then restore.
    HSM2Instance* saved_head      = model_->HSM2instances;
    HSM2Instance* saved_next_inst = inst_.HSM2nextInstance;
    HSM2Model*    saved_next_mod  = model_->HSM2nextModel;
    model_->HSM2instances  = &inst_;
    inst_.HSM2nextInstance = nullptr;
    model_->HSM2nextModel  = nullptr;
    int rc = HSM2load(model_, &ckt);
    model_->HSM2instances  = saved_head;
    inst_.HSM2nextInstance = saved_next_inst;
    model_->HSM2nextModel  = saved_next_mod;
    if (rc != Shim::OK) {
        throw std::runtime_error("HSM2load failed with rc=" + std::to_string(rc));
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
bool HSM2Device::device_converged() const {
    return last_noncon_ == 0;
}

// ---------------------------------------------------------------------------
// set_ic
// ---------------------------------------------------------------------------
void HSM2Device::set_ic(double vds, bool vds_given,
                          double vgs, bool vgs_given,
                          double vbs, bool vbs_given) {
    if (vds_given) { inst_.HSM2_icVDS = vds; inst_.HSM2_icVDS_Given = 1; }
    if (vgs_given) { inst_.HSM2_icVGS = vgs; inst_.HSM2_icVGS_Given = 1; }
    if (vbs_given) { inst_.HSM2_icVBS = vbs; inst_.HSM2_icVBS_Given = 1; }
}

// ---------------------------------------------------------------------------
// ac_stamp — linearized small-signal AC stamp
//
// Translates the ngspice HSM2acLoad() complex-matrix stamping into
// separate G (conductance) and C (capacitance) matrices.
//
// In ngspice, the complex matrix has interleaved real/imag parts:
//   *(ptr)   += real_part
//   *(ptr+1) += imag_part
//
// In neospice's AC framework, G gets the conductance (real) entries
// and C gets the capacitance entries.  The AC solver combines them
// as (G + j*omega*C) at each frequency point.
//
// The y-parameter decomposition:
//   y_real = -xcap_imag + g_terms   =>  g_terms go to G, xcap_imag part to C
//   y_imag =  xcap_real + ...       =>  xcap_real part goes to C
//
// Since xcap_r = cap_real * omega and the neospice AC solver multiplies
// C entries by omega, we stamp cap_real directly into C.
// For the imaginary cross terms (from NQS), we also stamp the _imag
// coefficients into G (since they produce real contributions).
// ---------------------------------------------------------------------------
void HSM2Device::ac_stamp(const std::vector<double>& /*voltages*/,
                            NumericMatrix& G, NumericMatrix& C) {
    auto& here = inst_;
    auto* model = model_;

    const double m = here.HSM2_m;

    const double gdpr = here.HSM2drainConductance;
    const double gspr = here.HSM2sourceConductance;
    const double gds  = here.HSM2_gds;
    const double gbd  = here.HSM2_gbd;
    const double gbs  = here.HSM2_gbs;
    const double capbd = here.HSM2_capbd;
    const double capbs = here.HSM2_capbs;

    double gm, gmbs, FwdSum, RevSum;
    double gbbdp, gbbsp, gbdpg, gbdpb, gbdpdp, gbdpsp;
    double gbspdp, gbspg, gbspb, gbspsp;
    double gIbtotg, gIbtotd, gIbtots, gIbtotb;
    double gIgtotg, gIgtotd, gIgtots, gIgtotb;
    double gIdtotg, gIdtotd, gIdtots, gIdtotb;
    double gIstotg, gIstotd, gIstots, gIstotb;

    // Capacitance variables (real part = cap value, imag part from NQS)
    double cggb_real = 0.0, cgsb_real = 0.0, cgdb_real = 0.0;
    double cbgb_real = 0.0, cbsb_real = 0.0, cbdb_real = 0.0;
    double cdgb_real = 0.0, cdsb_real = 0.0, cddb_real = 0.0;
    double csgb_real = 0.0, cssb_real = 0.0, csdb_real = 0.0;
    double cggb_imag = 0.0, cgsb_imag = 0.0, cgdb_imag = 0.0;
    double cbgb_imag = 0.0, cbsb_imag = 0.0, cbdb_imag = 0.0;
    double cdgb_imag = 0.0, cdsb_imag = 0.0, cddb_imag = 0.0;
    double csgb_imag = 0.0, cssb_imag = 0.0, csdb_imag = 0.0;

    // NQS overlap cap additional terms (imaginary omega coefficients)
    double pyggb_r = 0.0, pygdb_r = 0.0, pygsb_r = 0.0, pygbb_r = 0.0;
    double pybgb_r = 0.0, pybdb_r = 0.0, pybsb_r = 0.0, pybbb_r = 0.0;
    double pydgb_r = 0.0, pyddb_r = 0.0, pydsb_r = 0.0, pydbb_r = 0.0;
    double pysgb_r = 0.0, pysdb_r = 0.0, pyssb_r = 0.0, pysbb_r = 0.0;

    // NQS terms
    if (model->HSM2_conqs) {
        double tau  = here.HSM2_tau;
        double taub = here.HSM2_taub;

        double Xd      = here.HSM2_Xd;
        double Xd_dVgs = here.HSM2_Xd_dVgs;
        double Xd_dVds = here.HSM2_Xd_dVds;
        double Xd_dVbs = here.HSM2_Xd_dVbs;

        double Qi      = here.HSM2_Qi;
        double Qi_dVgs = here.HSM2_Qi_dVgs;
        double Qi_dVds = here.HSM2_Qi_dVds;
        double Qi_dVbs = here.HSM2_Qi_dVbs;

        double Qb_dVgs = here.HSM2_Qb_dVgs;
        double Qb_dVds = here.HSM2_Qb_dVds;
        double Qb_dVbs = here.HSM2_Qb_dVbs;

        // For NQS, the AC frequency-dependent terms require omega.
        // In the neospice AC framework, the AC solver provides omega through
        // the IntegratorCtx.  However, our ac_stamp() interface doesn't
        // receive omega directly.  We can get it from tls_integrator_ctx.
        const IntegratorCtx* ic = tls_integrator_ctx;
        double omega = (ic && ic->ac_freq > 0.0) ? (2.0 * M_PI * ic->ac_freq) : 1.0;

        double T1 = 1.0 + (tau * omega) * (tau * omega);
        double T2 = tau * omega / T1;
        double T3 = 1.0 + (taub * omega) * (taub * omega);
        double T4 = taub * omega / T3;

        cddb_real = Xd_dVds*Qi + Xd/T1*Qi_dVds;
        cdgb_real = Xd_dVgs*Qi + Xd/T1*Qi_dVgs;
        double cdbs_real = Xd_dVbs*Qi + Xd/T1*Qi_dVbs;
        cdsb_real = -(cddb_real + cdgb_real + cdbs_real);

        cddb_imag = -T2*Xd*Qi_dVds;
        cdgb_imag = -T2*Xd*Qi_dVgs;
        double cdbs_imag = -T2*Xd*Qi_dVbs;
        cdsb_imag = -(cddb_imag + cdgb_imag + cdbs_imag);

        csdb_real = -Xd_dVds*Qi + (1.0-Xd)/T1*Qi_dVds;
        csgb_real = -Xd_dVgs*Qi + (1.0-Xd)/T1*Qi_dVgs;
        double csbs_real = -Xd_dVbs*Qi + (1.0-Xd)/T1*Qi_dVbs;
        cssb_real = -(csdb_real + csgb_real + csbs_real);

        csdb_imag = -T2*(1.0-Xd)*Qi_dVds;
        csgb_imag = -T2*(1.0-Xd)*Qi_dVgs;
        double csbs_imag = -T2*(1.0-Xd)*Qi_dVbs;
        cssb_imag = -(csdb_imag + csgb_imag + csbs_imag);

        cbdb_real = Qb_dVds/T3;
        cbgb_real = Qb_dVgs/T3;
        double cbbs_real = Qb_dVbs/T3;
        cbsb_real = -(cbdb_real + cbgb_real + cbbs_real);

        cbdb_imag = -T4*Qb_dVds;
        cbgb_imag = -T4*Qb_dVgs;
        double cbbs_imag = -T4*Qb_dVbs;
        cbsb_imag = -(cbdb_imag + cbgb_imag + cbbs_imag);

        cgdb_real = -Qi_dVds/T1 - Qb_dVds/T3;
        cggb_real = -Qi_dVgs/T1 - Qb_dVgs/T3;
        double cgbs_real = -Qi_dVbs/T1 - Qb_dVbs/T3;
        cgsb_real = -(cgdb_real + cggb_real + cgbs_real);

        cgdb_imag = T2*Qi_dVds + T4*Qb_dVds;
        cggb_imag = T2*Qi_dVgs + T4*Qb_dVgs;
        double cgbs_imag = T2*Qi_dVbs + T4*Qb_dVbs;
        cgsb_imag = -(cgdb_imag + cggb_imag + cgbs_imag);
    }

    if (here.HSM2_mode >= 0) {
        // Forward mode
        gm   = here.HSM2_gm;
        gmbs = here.HSM2_gmbs;
        FwdSum = gm + gmbs;
        RevSum = 0.0;

        gbbdp = -here.HSM2_gbds;
        gbbsp = here.HSM2_gbds + here.HSM2_gbgs + here.HSM2_gbbs;

        gbdpg  = here.HSM2_gbgs;
        gbdpb  = here.HSM2_gbbs;
        gbdpdp = here.HSM2_gbds;
        gbdpsp = -(gbdpg + gbdpb + gbdpdp);

        gbspdp = 0.0;
        gbspg  = 0.0;
        gbspb  = 0.0;
        gbspsp = 0.0;

        if (model->HSM2_coiigs) {
            gIbtotg = here.HSM2_gigbg;
            gIbtotd = here.HSM2_gigbd;
            gIbtots = here.HSM2_gigbs;
            gIbtotb = here.HSM2_gigbb;
            gIstotg = here.HSM2_gigsg;
            gIstotd = here.HSM2_gigsd;
            gIstots = here.HSM2_gigss;
            gIstotb = here.HSM2_gigsb;
            gIdtotg = here.HSM2_gigdg;
            gIdtotd = here.HSM2_gigdd;
            gIdtots = here.HSM2_gigds;
            gIdtotb = here.HSM2_gigdb;
        } else {
            gIbtotg = gIbtotd = gIbtots = gIbtotb = 0.0;
            gIstotg = gIstotd = gIstots = gIstotb = 0.0;
            gIdtotg = gIdtotd = gIdtots = gIdtotb = 0.0;
        }

        if (model->HSM2_coiigs) {
            gIgtotg = gIbtotg + gIstotg + gIdtotg;
            gIgtotd = gIbtotd + gIstotd + gIdtotd;
            gIgtots = gIbtots + gIstots + gIdtots;
            gIgtotb = gIbtotb + gIstotb + gIdtotb;
        } else {
            gIgtotg = gIgtotd = gIgtots = gIgtotb = 0.0;
        }

        if (model->HSM2_conqs) {
            // NQS: overlap cap terms for forward mode
            if (model->HSM2_coadov == 1) {
                pydgb_r = (here.HSM2_cdgo - here.HSM2_cqyg);
                pyddb_r = (here.HSM2_cddo - here.HSM2_cqyd);
                pydsb_r = (here.HSM2_cdso + here.HSM2_cqyg + here.HSM2_cqyd + here.HSM2_cqyb);
                pydbb_r = -(pydgb_r + pyddb_r + pydsb_r);

                pysgb_r = here.HSM2_csgo;
                pysdb_r = here.HSM2_csdo;
                pyssb_r = here.HSM2_csso;
                pysbb_r = -(pysgb_r + pysdb_r + pyssb_r);

                pyggb_r = (-(here.HSM2_cgdo + here.HSM2_cgbo + here.HSM2_cgso) + here.HSM2_cqyg);
                pygdb_r = (here.HSM2_cgdo + here.HSM2_cqyd);
                pygsb_r = (here.HSM2_cgso - here.HSM2_cqyg - here.HSM2_cqyd - here.HSM2_cqyb);
                pygbb_r = -(pyggb_r + pygdb_r + pygsb_r);
            }
        } else {
            // QS mode
            cggb_real = here.HSM2_cggb;
            cgsb_real = here.HSM2_cgsb;
            cgdb_real = here.HSM2_cgdb;
            cggb_imag = cgsb_imag = cgdb_imag = 0.0;

            cbgb_real = here.HSM2_cbgb;
            cbsb_real = here.HSM2_cbsb;
            cbdb_real = here.HSM2_cbdb;
            cbgb_imag = cbsb_imag = cbdb_imag = 0.0;

            cdgb_real = here.HSM2_cdgb;
            cdsb_real = here.HSM2_cdsb;
            cddb_real = here.HSM2_cddb;
            cdgb_imag = cdsb_imag = cddb_imag = 0.0;

            csgb_real = -(cdgb_real + cggb_real + cbgb_real);
            cssb_real = -(cdsb_real + cgsb_real + cbsb_real);
            csdb_real = -(cddb_real + cgdb_real + cbdb_real);
            csgb_imag = cssb_imag = csdb_imag = 0.0;

            pyggb_r = pygdb_r = pygsb_r = pygbb_r = 0.0;
            pybgb_r = pybdb_r = pybsb_r = pybbb_r = 0.0;
            pydgb_r = pyddb_r = pydsb_r = pydbb_r = 0.0;
            pysgb_r = pysdb_r = pyssb_r = pysbb_r = 0.0;
        }
    } else {
        // Reverse mode
        gm   = -here.HSM2_gm;
        gmbs = -here.HSM2_gmbs;
        FwdSum = 0.0;
        RevSum = -(gm + gmbs);

        gbbsp = -here.HSM2_gbds;
        gbbdp = here.HSM2_gbds + here.HSM2_gbgs + here.HSM2_gbbs;

        gbdpg  = 0.0;
        gbdpsp = 0.0;
        gbdpb  = 0.0;
        gbdpdp = 0.0;

        gbspg  = here.HSM2_gbgs;
        gbspsp = here.HSM2_gbds;
        gbspb  = here.HSM2_gbbs;
        gbspdp = -(gbspg + gbspsp + gbspb);

        if (model->HSM2_coiigs) {
            gIbtotg = here.HSM2_gigbg;
            gIbtotd = here.HSM2_gigbd;
            gIbtots = here.HSM2_gigbs;
            gIbtotb = here.HSM2_gigbb;
            gIstotg = here.HSM2_gigsg;
            gIstotd = here.HSM2_gigsd;
            gIstots = here.HSM2_gigss;
            gIstotb = here.HSM2_gigsb;
            gIdtotg = here.HSM2_gigdg;
            gIdtotd = here.HSM2_gigdd;
            gIdtots = here.HSM2_gigds;
            gIdtotb = here.HSM2_gigdb;
        } else {
            gIbtotg = gIbtotd = gIbtots = gIbtotb = 0.0;
            gIstotg = gIstotd = gIstots = gIstotb = 0.0;
            gIdtotg = gIdtotd = gIdtots = gIdtotb = 0.0;
        }

        if (model->HSM2_coiigs) {
            gIgtotg = gIbtotg + gIstotg + gIdtotg;
            gIgtotd = gIbtotd + gIstotd + gIdtotd;
            gIgtots = gIbtots + gIstots + gIdtots;
            gIgtotb = gIbtotb + gIstotb + gIdtotb;
        } else {
            gIgtotg = gIgtotd = gIgtots = gIgtotb = 0.0;
        }

        if (model->HSM2_conqs) {
            // NQS reverse: swap d with s
            double T1;
            T1 = cgsb_real; cgsb_real = cgdb_real; cgdb_real = T1;
            T1 = cgsb_imag; cgsb_imag = cgdb_imag; cgdb_imag = T1;

            T1 = cdgb_real; cdgb_real = csgb_real; csgb_real = T1;
            T1 = cdsb_real; cdsb_real = csdb_real; csdb_real = T1;
            T1 = cddb_real; cddb_real = cssb_real; cssb_real = T1;
            T1 = cdgb_imag; cdgb_imag = csgb_imag; csgb_imag = T1;
            T1 = cdsb_imag; cdsb_imag = csdb_imag; csdb_imag = T1;
            T1 = cddb_imag; cddb_imag = cssb_imag; cssb_imag = T1;

            T1 = cbsb_real; cbsb_real = cbdb_real; cbdb_real = T1;
            T1 = cbsb_imag; cbsb_imag = cbdb_imag; cbdb_imag = T1;

            if (model->HSM2_coadov == 1) {
                pydgb_r = here.HSM2_csgo;
                pyddb_r = here.HSM2_csso;
                pydsb_r = here.HSM2_csdo;
                pydbb_r = -(pydgb_r + pyddb_r + pydsb_r);

                pysgb_r = (here.HSM2_cdgo - here.HSM2_cqyg);
                pysdb_r = (here.HSM2_cdso + here.HSM2_cqyg + here.HSM2_cqyd + here.HSM2_cqyb);
                pyssb_r = (here.HSM2_cddo - here.HSM2_cqyd);
                pysbb_r = -(pysgb_r + pysdb_r + pyssb_r);

                pyggb_r = (-(here.HSM2_cgdo + here.HSM2_cgbo + here.HSM2_cgso) + here.HSM2_cqyg);
                pygdb_r = (here.HSM2_cgso - here.HSM2_cqyg - here.HSM2_cqyd - here.HSM2_cqyb);
                pygsb_r = (here.HSM2_cgdo + here.HSM2_cqyd);
                pygbb_r = -(pyggb_r + pygdb_r + pygsb_r);
            }
        } else {
            // QS reverse mode
            cggb_real = here.HSM2_cggb;
            cgsb_real = here.HSM2_cgdb;   // swapped
            cgdb_real = here.HSM2_cgsb;
            cggb_imag = cgsb_imag = cgdb_imag = 0.0;

            cbgb_real = here.HSM2_cbgb;
            cbsb_real = here.HSM2_cbdb;   // swapped
            cbdb_real = here.HSM2_cbsb;
            cbgb_imag = cbsb_imag = cbdb_imag = 0.0;

            csgb_real = here.HSM2_cdgb;
            cssb_real = here.HSM2_cddb;
            csdb_real = here.HSM2_cdsb;
            csgb_imag = cssb_imag = csdb_imag = 0.0;

            cdgb_real = -(csgb_real + cggb_real + cbgb_real);
            cdsb_real = -(cssb_real + cgsb_real + cbsb_real);
            cddb_real = -(csdb_real + cgdb_real + cbdb_real);
            cdgb_imag = cdsb_imag = cddb_imag = 0.0;

            pyggb_r = pygdb_r = pygsb_r = pygbb_r = 0.0;
            pybgb_r = pybdb_r = pybsb_r = pybbb_r = 0.0;
            pydgb_r = pyddb_r = pydsb_r = pydbb_r = 0.0;
            pysgb_r = pysdb_r = pyssb_r = pysbb_r = 0.0;
        }
    }

    // Compute fourth-row bulk extra terms (these come from overlap caps)
    pybdb_r = -(pyddb_r + pygdb_r + pysdb_r);
    pybgb_r = -(pydgb_r + pyggb_r + pysgb_r);
    pybsb_r = -(pydsb_r + pygsb_r + pyssb_r);
    pybbb_r = -(pydbb_r + pygbb_r + pysbb_r);

    // Compute the intrinsic y-parameter components
    // y_real = -xcap_imag + g_terms  where xcap_imag = cap_imag * omega
    // y_imag = xcap_real             where xcap_real = cap_real * omega
    // Since neospice multiplies C by omega, we stamp cap_real directly into C.
    // The _imag parts contribute to G through the -xcap_imag = -(cap_imag*omega) term,
    // but this frequency-dependent G cannot be represented with a single G matrix.
    //
    // For QS mode: _imag = 0, so this is straightforward: cap_real -> C, g_terms -> G.
    // For NQS mode: the _imag terms create frequency-dependent conductance that
    // cannot be perfectly represented. We include the real capacitance part in C
    // and put the DC-bias dependent conductance terms in G.

    // Diode/leakage conductance additional terms for DP, SP, GP, BP rows
    double pydgb_g = gbdpg - gIdtotg + here.HSM2_gigidlgs;
    double pyddb_g = gbd + gbdpdp - gIdtotd + here.HSM2_gigidlds;
    double pydsb_g = gbdpsp - gIdtots - (here.HSM2_gigidlgs + here.HSM2_gigidlds + here.HSM2_gigidlbs);
    double pydbb_g = gbdpb - gIdtotb + here.HSM2_gigidlbs;
    if (!here.HSM2_corbnet) pydbb_g += -gbd;

    double pysgb_g = gbspg - gIstotg + here.HSM2_gigislgd;
    double pysdb_g = gbspdp - gIstotd - (here.HSM2_gigislsd + here.HSM2_gigislgd + here.HSM2_gigislbd);
    double pyssb_g = gbs + gbspsp - gIstots + here.HSM2_gigislsd;
    double pysbb_g = gbspb - gIstotb + here.HSM2_gigislbd;
    if (!here.HSM2_corbnet) pysbb_g += -gbs;

    double pyggb_g = gIgtotg;
    double grg = 0.0;
    if (here.HSM2_corg == 1) {
        grg = here.HSM2_grg;
        pyggb_g += grg;
    }
    double pygdb_g = gIgtotd;
    double pygsb_g = gIgtots;
    double pygbb_g = gIgtotb;

    double pybgb_g = -here.HSM2_gbgs - gIbtotg - here.HSM2_gigidlgs - here.HSM2_gigislgd;
    double pybdb_g = gbbdp - gIbtotd
        - here.HSM2_gigidlds + (here.HSM2_gigislgd + here.HSM2_gigislsd + here.HSM2_gigislbd);
    if (!here.HSM2_corbnet) pybdb_g += -gbd;
    double pybsb_g = gbbsp - gIbtots
        + (here.HSM2_gigidlgs + here.HSM2_gigidlds + here.HSM2_gigidlbs) - here.HSM2_gigislsd;
    if (!here.HSM2_corbnet) pybsb_g += -gbs;
    double pybbb_g = -here.HSM2_gbbs - gIbtotb - here.HSM2_gigidlbs - here.HSM2_gigislbd;
    if (!here.HSM2_corbnet) pybbb_g += gbd + gbs;

    // Cbd/Cbs junction cap (goes to C matrix)
    double pyddb_c = capbd;
    double pyssb_c = capbs;
    double pydbb_c = 0.0, pysbb_c = 0.0;
    double pybdb_c = 0.0, pybsb_c = 0.0, pybbb_c = 0.0;
    if (!here.HSM2_corbnet) {
        pydbb_c = -capbd;
        pysbb_c = -capbs;
        pybdb_c = -capbd;
        pybsb_c = -capbs;
        pybbb_c = capbd + capbs;
    }

    // ---- Stamp G matrix (conductance) ----
    // Intrinsic y-parameter G terms: gm, gds, gmbs
    // DP row
    G.add(here.HSM2DPdpPtr, m * (gdpr + gds + RevSum + pyddb_g));
    G.add(here.HSM2DPdPtr,  m * (-gdpr));
    G.add(here.HSM2DPgpPtr, m * (gm + pydgb_g));
    G.add(here.HSM2DPspPtr, m * (-(gds + FwdSum) + pydsb_g));
    G.add(here.HSM2DPbpPtr, m * (gmbs + pydbb_g));

    // D row
    G.add(here.HSM2DdpPtr, m * (-gdpr));
    G.add(here.HSM2DdPtr,  m * gdpr);

    // SP row
    G.add(here.HSM2SPdpPtr, m * (-(gds + RevSum) + pysdb_g));
    G.add(here.HSM2SPgpPtr, m * (-gm + pysgb_g));
    G.add(here.HSM2SPspPtr, m * (gspr + gds + FwdSum + pyssb_g));
    G.add(here.HSM2SPsPtr,  m * (-gspr));
    G.add(here.HSM2SPbpPtr, m * (-gmbs + pysbb_g));

    // S row
    G.add(here.HSM2SspPtr, m * (-gspr));
    G.add(here.HSM2SsPtr,  m * gspr);

    // GP row
    G.add(here.HSM2GPgpPtr, m * pyggb_g);
    G.add(here.HSM2GPdpPtr, m * pygdb_g);
    G.add(here.HSM2GPspPtr, m * pygsb_g);
    G.add(here.HSM2GPbpPtr, m * pygbb_g);

    // BP row
    G.add(here.HSM2BPgpPtr, m * pybgb_g);
    G.add(here.HSM2BPdpPtr, m * pybdb_g);
    G.add(here.HSM2BPspPtr, m * pybsb_g);
    G.add(here.HSM2BPbpPtr, m * pybbb_g);

    // Gate resistance (G node to GP node)
    if (here.HSM2_corg == 1) {
        G.add(here.HSM2GgPtr,  m * grg);
        G.add(here.HSM2GPgPtr, m * (-grg));
        G.add(here.HSM2GgpPtr, m * (-grg));
    }

    // ---- Stamp C matrix (capacitance) ----
    // Intrinsic caps (real part of NQS or QS caps)
    // GP row
    C.add(here.HSM2GPgpPtr, m * (cggb_real + pyggb_r));
    C.add(here.HSM2GPdpPtr, m * (cgdb_real + pygdb_r));
    C.add(here.HSM2GPspPtr, m * (cgsb_real + pygsb_r));
    C.add(here.HSM2GPbpPtr, m * (-(cggb_real + cgdb_real + cgsb_real) + pygbb_r));

    // DP row
    C.add(here.HSM2DPgpPtr, m * (cdgb_real + pydgb_r));
    C.add(here.HSM2DPdpPtr, m * (cddb_real + pyddb_r + pyddb_c));
    C.add(here.HSM2DPspPtr, m * (cdsb_real + pydsb_r));
    C.add(here.HSM2DPbpPtr, m * (-(cdgb_real + cddb_real + cdsb_real) + pydbb_r + pydbb_c));

    // SP row
    C.add(here.HSM2SPgpPtr, m * (csgb_real + pysgb_r));
    C.add(here.HSM2SPdpPtr, m * (csdb_real + pysdb_r));
    C.add(here.HSM2SPspPtr, m * (cssb_real + pyssb_r + pyssb_c));
    C.add(here.HSM2SPbpPtr, m * (-(csgb_real + csdb_real + cssb_real) + pysbb_r + pysbb_c));

    // BP row
    C.add(here.HSM2BPgpPtr, m * (cbgb_real + pybgb_r));
    C.add(here.HSM2BPdpPtr, m * (cbdb_real + pybdb_r + pybdb_c));
    C.add(here.HSM2BPspPtr, m * (cbsb_real + pybsb_r + pybsb_c));
    C.add(here.HSM2BPbpPtr, m * (-(cbgb_real + cbdb_real + cbsb_real) + pybbb_r + pybbb_c));

    // Body network stamps
    if (here.HSM2_corbnet == 1) {
        // DP-DB, SP-SB junction cap stamps
        C.add(here.HSM2DPdbPtr, m * (-capbd));
        C.add(here.HSM2SPsbPtr, m * (-capbs));

        C.add(here.HSM2DBdpPtr, m * (-capbd));
        C.add(here.HSM2DBdbPtr, m * capbd);

        C.add(here.HSM2SBspPtr, m * (-capbs));
        C.add(here.HSM2SBsbPtr, m * capbs);

        // Body resistance network (G matrix)
        G.add(here.HSM2DPdbPtr, m * (-gbd));
        G.add(here.HSM2SPsbPtr, m * (-gbs));

        G.add(here.HSM2DBdpPtr, m * (-gbd));
        G.add(here.HSM2DBdbPtr, m * (gbd + here.HSM2_grbpd + here.HSM2_grbdb));
        G.add(here.HSM2DBbpPtr, m * (-here.HSM2_grbpd));
        G.add(here.HSM2DBbPtr,  m * (-here.HSM2_grbdb));

        G.add(here.HSM2BPdbPtr, m * (-here.HSM2_grbpd));
        G.add(here.HSM2BPbPtr,  m * (-here.HSM2_grbpb));
        G.add(here.HSM2BPsbPtr, m * (-here.HSM2_grbps));
        G.add(here.HSM2BPbpPtr, m * (here.HSM2_grbpd + here.HSM2_grbps + here.HSM2_grbpb));

        G.add(here.HSM2SBspPtr, m * (-gbs));
        G.add(here.HSM2SBbpPtr, m * (-here.HSM2_grbps));
        G.add(here.HSM2SBbPtr,  m * (-here.HSM2_grbsb));
        G.add(here.HSM2SBsbPtr, m * (gbs + here.HSM2_grbps + here.HSM2_grbsb));

        G.add(here.HSM2BdbPtr, m * (-here.HSM2_grbdb));
        G.add(here.HSM2BbpPtr, m * (-here.HSM2_grbpb));
        G.add(here.HSM2BsbPtr, m * (-here.HSM2_grbsb));
        G.add(here.HSM2BbPtr,  m * (here.HSM2_grbsb + here.HSM2_grbdb + here.HSM2_grbpb));
    }
}

// ---------------------------------------------------------------------------
// compute_trunc — device-specific local truncation error
//
// HiSIM2 charge state variables:
//   qb  at offset 8  (cqb at 9)
//   qg  at offset 10 (cqg at 11)
//   qd  at offset 12 (cqd at 13)
//   qbs at offset 14 (cqbs at 15)
//   qbd at offset 16 (cqbd at 17)
// ---------------------------------------------------------------------------
double HSM2Device::compute_trunc(const IntegratorCtx& ctx,
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
    static constexpr int charge_offsets[] = { 8, 10, 12, 14, 16 };  // qb, qg, qd, qbs, qbd
    static constexpr int ncharges = 5;

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
// query_param — post-simulation parameter query
// ---------------------------------------------------------------------------
std::optional<double>
HSM2Device::query_param(const std::string& name) const {
    const std::string key = str_tolower(name);
    const double m = inst_.HSM2_m;

    // --- Operating-point parameters (scaled by multiplier m) ---
    if (key == "gm")                        return inst_.HSM2_gm * m;
    if (key == "gds")                       return inst_.HSM2_gds * m;
    if (key == "gmbs")                      return inst_.HSM2_gmbs * m;
    if (key == "vth" || key == "von")       return inst_.HSM2_von;
    if (key == "vdsat")                     return inst_.HSM2_vdsat;
    if (key == "id" || key == "ids")        return inst_.HSM2_ids * m;
    if (key == "ibs")                       return inst_.HSM2_ibs * m;
    if (key == "ibd")                       return inst_.HSM2_ibd * m;
    if (key == "isub")                      return inst_.HSM2_isub * m;
    if (key == "gbd")                       return inst_.HSM2_gbd * m;
    if (key == "gbs")                       return inst_.HSM2_gbs * m;

    // --- Gate tunneling currents ---
    if (key == "igs")                       return inst_.HSM2_igs * m;
    if (key == "igd")                       return inst_.HSM2_igd * m;
    if (key == "igb")                       return inst_.HSM2_igb * m;
    if (key == "igidl")                     return inst_.HSM2_igidl * m;
    if (key == "igisl")                     return inst_.HSM2_igisl * m;

    // --- Capacitances ---
    if (key == "cgg")                       return inst_.HSM2_cggb * m;
    if (key == "cgd")                       return inst_.HSM2_cgdb * m;
    if (key == "cgs")                       return inst_.HSM2_cgsb * m;
    if (key == "cdg")                       return inst_.HSM2_cdgb * m;
    if (key == "cdd")                       return inst_.HSM2_cddb * m;
    if (key == "cds")                       return inst_.HSM2_cdsb * m;
    if (key == "cbg")                       return inst_.HSM2_cbgb * m;
    if (key == "cbd_cap" || key == "cbdb")  return inst_.HSM2_cbdb * m;
    if (key == "cbs_cap" || key == "cbsb")  return inst_.HSM2_cbsb * m;

    // --- Charges ---
    if (key == "qg")                        return inst_.HSM2_qg * m;
    if (key == "qd")                        return inst_.HSM2_qd * m;
    if (key == "qs")                        return inst_.HSM2_qs * m;
    if (key == "qb")                        return inst_.HSM2_qb * m;

    // --- Overlap caps ---
    if (key == "cgdo")                      return inst_.HSM2_cgdo * m;
    if (key == "cgso")                      return inst_.HSM2_cgso * m;
    if (key == "cgbo")                      return inst_.HSM2_cgbo * m;

    // --- Junction capacitances ---
    if (key == "capbd")                     return inst_.HSM2_capbd * m;
    if (key == "capbs")                     return inst_.HSM2_capbs * m;

    // --- Terminal voltages from state vector ---
    if (state0_ && state_base_ >= 0) {
        if (key == "vbd")                   return state0_[state_base_ + 0];
        if (key == "vbs")                   return state0_[state_base_ + 1];
        if (key == "vgs")                   return state0_[state_base_ + 2];
        if (key == "vds")                   return state0_[state_base_ + 3];
    }

    // --- Geometry (no multiplier) ---
    if (key == "w")                         return inst_.HSM2_w;
    if (key == "l")                         return inst_.HSM2_l;
    if (key == "m")                         return inst_.HSM2_m;
    if (key == "nf")                        return inst_.HSM2_nf;

    return std::nullopt;  // unrecognized parameter
}

// ---------------------------------------------------------------------------
// noise_sources — HiSIM2 noise model
//
// Implements noise sources from hsm2noi.c:
//   1. Drain/source series resistance thermal noise
//   2. Channel thermal noise (HiSIM model)
//   3. Flicker (1/f) noise (HiSIM model)
//   4. Shot noise (Igs, Igd, Igb)
// ---------------------------------------------------------------------------
std::vector<Device::NoiseSource>
HSM2Device::noise_sources(double freq,
                            const std::vector<double>& /*dc_solution*/) const {
    const auto* model = model_;
    const auto& inst  = inst_;

    const double m     = inst.HSM2_m;
    const double TTEMP = (inst.HSM2_temp_Given) ? inst.HSM2_ktemp
                       : (inst.HSM2_dtemp_Given) ? (sim_temp_ + inst.HSM2_dtemp)
                       : sim_temp_;
    const double fourKT = 4.0 * BOLTZMANN * TTEMP;

    // Node indices (neospice convention)
    const int32_t dp_neo = ucb_to_neo(inst.HSM2dNodePrime);
    const int32_t sp_neo = ucb_to_neo(inst.HSM2sNodePrime);
    const int32_t d_neo  = ucb_to_neo(inst.HSM2dNode);
    const int32_t s_neo  = ucb_to_neo(inst.HSM2sNode);
    const int32_t gp_neo = ucb_to_neo(inst.HSM2gNodePrime);
    const int32_t bp_neo = ucb_to_neo(inst.HSM2bNodePrime);

    std::vector<NoiseSource> sources;
    sources.reserve(7);

    // -----------------------------------------------------------------------
    // 1. Drain / Source series resistance thermal noise
    // -----------------------------------------------------------------------
    if (model->HSM2_corsrd < 0) {
        const double gdpr = inst.HSM2drainConductance;
        const double gspr = inst.HSM2sourceConductance;

        if (gdpr > 0.0)
            sources.push_back({dp_neo, d_neo, fourKT * gdpr * m});
        if (gspr > 0.0)
            sources.push_back({sp_neo, s_neo, fourKT * gspr * m});
    }

    // -----------------------------------------------------------------------
    // 2. Channel thermal noise (HiSIM model)
    // -----------------------------------------------------------------------
    double G = 0.0;
    if (model->HSM2_corsrd <= 0 || inst.HSM2internalGd <= 0.0) {
        G = inst.HSM2_noithrml;
    } else {
        if (inst.HSM2_noithrml * inst.HSM2internalGd * inst.HSM2internalGs > 0.0) {
            G = inst.HSM2_noithrml * inst.HSM2internalGd * inst.HSM2internalGs
              / (inst.HSM2_noithrml * inst.HSM2internalGd
                + inst.HSM2internalGd * inst.HSM2internalGs
                + inst.HSM2_noithrml * inst.HSM2internalGs);
        }
    }
    double channel_noise = fourKT * G * m;
    if (channel_noise > 0.0)
        sources.push_back({dp_neo, sp_neo, channel_noise});

    // -----------------------------------------------------------------------
    // 3. Flicker (1/f) noise (HiSIM model)
    // -----------------------------------------------------------------------
    if (freq > 0.0 && inst.HSM2_noiflick != 0.0) {
        double flicker = m * inst.HSM2_noiflick / std::pow(freq, model->HSM2_falph);
        if (flicker > 0.0)
            sources.push_back({dp_neo, sp_neo, flicker});
    }

    // -----------------------------------------------------------------------
    // 4. Shot noise (gate tunneling: Igs, Igd, Igb)
    // -----------------------------------------------------------------------
    const double CHARGE_Q = 1.60217663e-19;
    if (std::abs(inst.HSM2_igs) > 0.0)
        sources.push_back({gp_neo, sp_neo, 2.0 * CHARGE_Q * std::abs(inst.HSM2_igs) * m});
    if (std::abs(inst.HSM2_igd) > 0.0)
        sources.push_back({gp_neo, dp_neo, 2.0 * CHARGE_Q * std::abs(inst.HSM2_igd) * m});
    if (std::abs(inst.HSM2_igb) > 0.0)
        sources.push_back({gp_neo, bp_neo, 2.0 * CHARGE_Q * std::abs(inst.HSM2_igb) * m});

    return sources;
}

} // namespace neospice
