#include "devices/bsimsoi/bsimsoi_device.hpp"

#include "core/circuit.hpp"        // Circuit::node, tls_integrator_ctx
#include "core/types.hpp"          // SimOptions defaults
#include "devices/ucb_device_init.hpp"
#include "devices/ucb_utils.hpp"

#include <cmath>
#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <string>

// Forward declarations for translated UCB functions.
namespace neospice::bsimsoi {
    int B4SOIsetup(Shim::Matrix*, B4SOIModel*, Shim::Ckt*, int*);
    int B4SOItemp(B4SOIModel*, Shim::Ckt*);
    int B4SOIload(B4SOIModel*, Shim::Ckt*);
}

namespace neospice {

using namespace neospice::bsimsoi;

// ---------------------------------------------------------------------------
// B4SOIModelCard destructor
// ---------------------------------------------------------------------------
B4SOIModelCard::~B4SOIModelCard() {
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
std::unique_ptr<B4SOIDevice>
B4SOIDevice::make(std::string name,
        int32_t n_d, int32_t n_g, int32_t n_s, int32_t n_e, int32_t n_p, int32_t n_b,
        const Geom& geom, B4SOIModelCard& shared_card) {
    std::unique_ptr<B4SOIDevice> dev(new B4SOIDevice(std::move(name)));
    dev->ext_nodes_ = {n_d, n_g, n_s, n_e, n_p, n_b};
    dev->model_ = &shared_card.ucb;

    if (!shared_card.ucb.B4SOIversionGiven) {
        shared_card.ucb.B4SOIversion = 4.4;
        shared_card.ucb.B4SOIversionGiven = 1;
    }

    auto& inst = dev->inst_;
    inst.B4SOIname = dev->name().c_str();
    inst.B4SOImodPtr = dev->model_;

    // Node wiring (UCB convention).
    inst.B4SOIdNode = neo_to_ucb(n_d);
    inst.B4SOIgNodeExt = neo_to_ucb(n_g);
    inst.B4SOIsNode = neo_to_ucb(n_s);
    inst.B4SOIeNode = neo_to_ucb(n_e);
    inst.B4SOIpNode = neo_to_ucb(n_p);
    inst.B4SOIbNode = neo_to_ucb(n_b);

    // Geometry.
    inst.B4SOIw = geom.W;
    inst.B4SOIwGiven = 1;
    inst.B4SOIl = geom.L;
    inst.B4SOIlGiven = 1;
    inst.B4SOIm = geom.M;
    inst.B4SOImGiven = 1;
    inst.B4SOIdrainArea = geom.AD;
    inst.B4SOIdrainAreaGiven = (geom.AD != 0.0) ? 1 : 0;
    inst.B4SOIsourceArea = geom.AS;
    inst.B4SOIsourceAreaGiven = (geom.AS != 0.0) ? 1 : 0;
    inst.B4SOIdrainPerimeter = geom.PD;
    inst.B4SOIdrainPerimeterGiven = (geom.PD != 0.0) ? 1 : 0;
    inst.B4SOIsourcePerimeter = geom.PS;
    inst.B4SOIsourcePerimeterGiven = (geom.PS != 0.0) ? 1 : 0;
    inst.B4SOIdrainSquares = geom.NRD;
    inst.B4SOIdrainSquaresGiven = (geom.NRD != 0.0) ? 1 : 0;
    inst.B4SOIsourceSquares = geom.NRS;
    inst.B4SOIsourceSquaresGiven = (geom.NRS != 0.0) ? 1 : 0;
    inst.B4SOIsa = geom.SA;
    inst.B4SOIsaGiven = (geom.SA != 0.0) ? 1 : 0;
    inst.B4SOIsb = geom.SB;
    inst.B4SOIsbGiven = (geom.SB != 0.0) ? 1 : 0;
    inst.B4SOIsd = geom.SD;
    inst.B4SOIsdGiven = (geom.SD != 0.0) ? 1 : 0;
    inst.B4SOInf = geom.NF;
    inst.B4SOInfGiven = 1;

    // Thread onto the shared model's instance list.
    inst.B4SOInextInstance = shared_card.ucb.B4SOIinstances;
    shared_card.ucb.B4SOIinstances = &inst;

    // Remember the widest real node index for ghost array sizing.
    int32_t widest = -1;
    for (int32_t n : {n_d, n_g, n_s, n_e, n_p, n_b}) if (n > widest) widest = n;
    dev->max_neo_node_ = widest;

    return dev;
}

// ---------------------------------------------------------------------------
// declare_internal_nodes
// ---------------------------------------------------------------------------
void B4SOIDevice::declare_internal_nodes(Circuit& ckt) {
    ucb_declare_internal_nodes<Shim::Matrix, Shim::Ckt>(
        ckt, name_,
        [this](Shim::Matrix& m, Shim::Ckt& c) {
            UCB_SPLICE_INSTANCE(B4SOI);
            int states = 0;
            int rc = B4SOIsetup(&m, model_, &c, &states);
            UCB_UNSPLICE_INSTANCE(B4SOI);
            return rc;
        },
        "B4SOIsetup", journal_, max_neo_node_);
}

// ---------------------------------------------------------------------------
// stamp_pattern
// ---------------------------------------------------------------------------
void B4SOIDevice::stamp_pattern(SparsityBuilder& builder) const {
    ucb_stamp_pattern(journal_, builder);
}

// ---------------------------------------------------------------------------
// assign_offsets
// ---------------------------------------------------------------------------
void B4SOIDevice::assign_offsets(const SparsityPattern& pattern) {
    const auto offsets = ucb_compute_offsets(journal_, pattern);

#define RESOLVE(f)                                                       \
    do {                                                                 \
        if (inst_.f >= 0)                                                \
            inst_.f = offsets[inst_.f];                                  \
    } while (0)

    RESOLVE(B4SOIDBdpPtr);
    RESOLVE(B4SOIDBdbPtr);
    RESOLVE(B4SOIDBbPtr);
    RESOLVE(B4SOIDPdbPtr);
    RESOLVE(B4SOISBspPtr);
    RESOLVE(B4SOISBbPtr);
    RESOLVE(B4SOISBsbPtr);
    RESOLVE(B4SOISPsbPtr);
    RESOLVE(B4SOIBsbPtr);
    RESOLVE(B4SOIBdbPtr);
    RESOLVE(B4SOIDgPtr);
    RESOLVE(B4SOIDspPtr);
    RESOLVE(B4SOIDbPtr);
    RESOLVE(B4SOISdpPtr);
    RESOLVE(B4SOISgPtr);
    RESOLVE(B4SOISbPtr);
    RESOLVE(B4SOIPgPtr);
    RESOLVE(B4SOIGpPtr);
    RESOLVE(B4SOIGgmPtr);
    RESOLVE(B4SOIGgePtr);
    RESOLVE(B4SOIGMdpPtr);
    RESOLVE(B4SOIGMgPtr);
    RESOLVE(B4SOIGMspPtr);
    RESOLVE(B4SOIGMgmPtr);
    RESOLVE(B4SOIGMgePtr);
    RESOLVE(B4SOIGMePtr);
    RESOLVE(B4SOIGMbPtr);
    RESOLVE(B4SOISPgmPtr);
    RESOLVE(B4SOIDPgmPtr);
    RESOLVE(B4SOIEgmPtr);
    RESOLVE(B4SOIGEdpPtr);
    RESOLVE(B4SOIGEgPtr);
    RESOLVE(B4SOIGEgmPtr);
    RESOLVE(B4SOIGEgePtr);
    RESOLVE(B4SOIGEspPtr);
    RESOLVE(B4SOIGEbPtr);
    RESOLVE(B4SOIGePtr);
    RESOLVE(B4SOIDPePtr);
    RESOLVE(B4SOISPePtr);
    RESOLVE(B4SOIEePtr);
    RESOLVE(B4SOIEbPtr);
    RESOLVE(B4SOIBePtr);
    RESOLVE(B4SOIEgPtr);
    RESOLVE(B4SOIEdpPtr);
    RESOLVE(B4SOIEspPtr);
    RESOLVE(B4SOITemptempPtr);
    RESOLVE(B4SOITempdpPtr);
    RESOLVE(B4SOITempspPtr);
    RESOLVE(B4SOITempgPtr);
    RESOLVE(B4SOITempbPtr);
    RESOLVE(B4SOITempePtr);
    RESOLVE(B4SOIGtempPtr);
    RESOLVE(B4SOIDPtempPtr);
    RESOLVE(B4SOISPtempPtr);
    RESOLVE(B4SOIEtempPtr);
    RESOLVE(B4SOIBtempPtr);
    RESOLVE(B4SOIPtempPtr);
    RESOLVE(B4SOIBpPtr);
    RESOLVE(B4SOIPbPtr);
    RESOLVE(B4SOIPpPtr);
    RESOLVE(B4SOIDdPtr);
    RESOLVE(B4SOIGgPtr);
    RESOLVE(B4SOISsPtr);
    RESOLVE(B4SOIBbPtr);
    RESOLVE(B4SOIDPdpPtr);
    RESOLVE(B4SOISPspPtr);
    RESOLVE(B4SOIDdpPtr);
    RESOLVE(B4SOIGbPtr);
    RESOLVE(B4SOIGdpPtr);
    RESOLVE(B4SOIGspPtr);
    RESOLVE(B4SOISspPtr);
    RESOLVE(B4SOIBdpPtr);
    RESOLVE(B4SOIBspPtr);
    RESOLVE(B4SOIDPspPtr);
    RESOLVE(B4SOIDPdPtr);
    RESOLVE(B4SOIBgPtr);
    RESOLVE(B4SOIDPgPtr);
    RESOLVE(B4SOISPgPtr);
    RESOLVE(B4SOISPsPtr);
    RESOLVE(B4SOIDPbPtr);
    RESOLVE(B4SOISPbPtr);
    RESOLVE(B4SOISPdpPtr);
    RESOLVE(B4SOIVbsPtr);
    RESOLVE(B4SOIIdsPtr);
    RESOLVE(B4SOIIcPtr);
    RESOLVE(B4SOIIbsPtr);
    RESOLVE(B4SOIIbdPtr);
    RESOLVE(B4SOIIiiPtr);
    RESOLVE(B4SOIIgPtr);
    RESOLVE(B4SOIGiggPtr);
    RESOLVE(B4SOIGigdPtr);
    RESOLVE(B4SOIGigbPtr);
    RESOLVE(B4SOIIgidlPtr);
    RESOLVE(B4SOIItunPtr);
    RESOLVE(B4SOIIbpPtr);
    RESOLVE(B4SOICbbPtr);
    RESOLVE(B4SOICbdPtr);
    RESOLVE(B4SOICbgPtr);
    RESOLVE(B4SOIqbPtr);
    RESOLVE(B4SOIQbfPtr);
    RESOLVE(B4SOIQjsPtr);
    RESOLVE(B4SOIQjdPtr);

#undef RESOLVE
}

// ---------------------------------------------------------------------------
// set_state_ptrs
// ---------------------------------------------------------------------------
void B4SOIDevice::set_state_ptrs(double* s0, double* s1, double* s2, int32_t base) {
    state0_ = s0;
    state1_ = s1;
    state2_ = s2;
    state_base_ = base;
    inst_.B4SOIstates = base;
}

// ---------------------------------------------------------------------------
// evaluate
// ---------------------------------------------------------------------------
void B4SOIDevice::evaluate(const std::vector<double>& voltages,
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

    // State ring.
    ckt.CKTstate0 = state0_;
    ckt.CKTstate1 = state1_;
    ckt.CKTstate2 = state2_;

    // Ghost rhs / old-iterate pointers.
    ckt.CKTrhs    = ghost_rhs_.data();
    ckt.CKTrhsOld = ghost_voltages_.data();
    ckt.mat       = &mat;

    // First-call B4SOItemp.
    if (!temp_done_) {
        int rc = B4SOItemp(model_, &ckt);
        if (rc != Shim::OK) {
            throw std::runtime_error("B4SOItemp failed with rc=" + std::to_string(rc));
        }
        temp_done_ = true;
    }

    // Splice this instance as sole member, call load, then restore.
    B4SOIInstance* saved_head      = model_->B4SOIinstances;
    B4SOIInstance* saved_next_inst = inst_.B4SOInextInstance;
    B4SOIModel*    saved_next_mod  = model_->B4SOInextModel;
    model_->B4SOIinstances  = &inst_;
    inst_.B4SOInextInstance = nullptr;
    model_->B4SOInextModel  = nullptr;
    int rc = B4SOIload(model_, &ckt);
    model_->B4SOIinstances  = saved_head;
    inst_.B4SOInextInstance = saved_next_inst;
    model_->B4SOInextModel  = saved_next_mod;
    if (rc != Shim::OK) {
        throw std::runtime_error("B4SOIload failed with rc=" + std::to_string(rc));
    }

    last_noncon_ = ckt.CKTnoncon;

    // Fold ghost rhs contributions back into the real rhs.
    for (int32_t k = 1; k <= max_neo_node_ + 1 && k < n_ghost; ++k) {
        if (k - 1 < n_real) rhs[k - 1] += ghost_rhs_[k];
    }
}

// ---------------------------------------------------------------------------
// ac_stamp — ported from ngspice b4soiacld.c
// ---------------------------------------------------------------------------
void B4SOIDevice::ac_stamp(const std::vector<double>& /*voltages*/,
                             NumericMatrix& G,
                             NumericMatrix& C) {
    auto& here = inst_;
    auto* model = model_;
    const double m = here.B4SOIm;
    const int selfheat = (model->B4SOIshMod == 1) && (here.B4SOIrth0 != 0.0);

    // --- Mode-dependent local variables ---
    double Gm, Gmbs, Gme, GmT, FwdSum, RevSum;
    double cggb, cgdb, cgsb, cgT;
    double cdgb, cddb, cdsb, cdeb, cdT;
    double cbgb, cbdb, cbsb, cbeb, cbT;
    double ceeb, ceT;
    double cTt;
    double gbbg, gbbdp, gbbb, gbbp, gbbsp, gbbT, gbbe;
    double gddpg, gddpdp, gddpsp, gddpb, gddpT, gddpe;
    double gsspg, gsspdp, gsspsp, gsspb, gsspT, gsspe;
    double gppb, gppp;
    double gTtg, gTtb, gTtdp, gTtsp, gTtt, gTte;
    double gigg, gigd, gigs, gigb, gige, gigT;
    double gigpg, gigpp;
    double gIstotg, gIstotd, gIstots, gIstotb;
    double gIdtotg, gIdtotd, gIdtots, gIdtotb;
    double gIgtotg, gIgtotd, gIgtots, gIgtotb;
    double gcrgd, gcrgg, gcrgs, gcrgb, gcrg;
    double T0 = 0.0;

    if (here.B4SOImode >= 0) {
        // Forward mode
        Gm = here.B4SOIgm;
        Gmbs = here.B4SOIgmbs;
        Gme = here.B4SOIgme;
        GmT = model->B4SOItype * here.B4SOIgmT;
        FwdSum = Gm + Gmbs + Gme;
        RevSum = 0.0;

        cbgb = here.B4SOIcbgb;
        cbsb = here.B4SOIcbsb;
        cbdb = here.B4SOIcbdb;
        cbeb = here.B4SOIcbeb;
        cbT  = model->B4SOItype * here.B4SOIcbT;

        ceeb = here.B4SOIceeb;
        ceT  = model->B4SOItype * here.B4SOIceT;

        cggb = here.B4SOIcggb;
        cgsb = here.B4SOIcgsb;
        cgdb = here.B4SOIcgdb;
        cgT  = model->B4SOItype * here.B4SOIcgT;

        cdgb = here.B4SOIcdgb;
        cdsb = here.B4SOIcdsb;
        cddb = here.B4SOIcddb;
        cdeb = here.B4SOIcdeb;
        cdT  = model->B4SOItype * here.B4SOIcdT;

        cTt = here.pParam->B4SOIcth;

        gigg = here.B4SOIgigg;
        gigb = here.B4SOIgigb;
        gige = here.B4SOIgige;
        gigs = here.B4SOIgigs;
        gigd = here.B4SOIgigd;
        gigT = model->B4SOItype * here.B4SOIgigT;
        gigpg = here.B4SOIgigpg;
        gigpp = here.B4SOIgigpp;

        gbbg  = -here.B4SOIgbgs;
        gbbdp = -here.B4SOIgbds;
        gbbb  = -here.B4SOIgbbs;
        gbbp  = -here.B4SOIgbps;
        gbbT  = -model->B4SOItype * here.B4SOIgbT;
        gbbe  = -here.B4SOIgbes;

        if (here.B4SOIrbodyMod) {
            gbbdp = -here.B4SOIgiigidld;
            gbbb  = -here.B4SOIgbgiigbpb;
        }
        gbbsp = -(gbbg + gbbdp + gbbb + gbbp + gbbe);

        gddpg  = -here.B4SOIgjdg;
        gddpdp = -here.B4SOIgjdd;
        if (!here.B4SOIrbodyMod)
            gddpb = -here.B4SOIgjdb;
        else
            gddpb = here.B4SOIgiigidlb;
        gddpT  = -model->B4SOItype * here.B4SOIgjdT;
        gddpe  = -here.B4SOIgjde;
        gddpsp = -(gddpg + gddpdp + gddpb + gddpe);

        gsspg  = -here.B4SOIgjsg;
        gsspdp = -here.B4SOIgjsd;
        if (!here.B4SOIrbodyMod)
            gsspb = -here.B4SOIgjsb;
        else
            gsspb = 0.0;
        gsspT  = -model->B4SOItype * here.B4SOIgjsT;
        gsspe  = 0.0;
        gsspsp = -(gsspg + gsspdp + gsspb + gsspe);

        gppb = -here.B4SOIgbpbs;
        gppp = -here.B4SOIgbpps;

        gTtg  = here.B4SOIgtempg;
        gTtb  = here.B4SOIgtempb;
        gTtdp = here.B4SOIgtempd;
        gTtt  = here.B4SOIgtempT;
        gTte  = here.B4SOIgtempe;
        gTtsp = -(gTtg + gTtb + gTtdp + gTte);

        if (model->B4SOIigcMod) {
            gIstotg = here.B4SOIgIgsg + here.B4SOIgIgcsg;
            gIstotd = here.B4SOIgIgcsd;
            gIstots = here.B4SOIgIgss + here.B4SOIgIgcss;
            gIstotb = here.B4SOIgIgcsb;
            gIdtotg = here.B4SOIgIgdg + here.B4SOIgIgcdg;
            gIdtotd = here.B4SOIgIgdd + here.B4SOIgIgcdd;
            gIdtots = here.B4SOIgIgcds;
            gIdtotb = here.B4SOIgIgcdb;
            gIgtotg = gIstotg + gIdtotg;
            gIgtotd = gIstotd + gIdtotd;
            gIgtots = gIstots + gIdtots;
            gIgtotb = gIstotb + gIdtotb;
        } else {
            gIstotg = gIstotd = gIstots = gIstotb = 0.0;
            gIdtotg = gIdtotd = gIdtots = gIdtotb = 0.0;
            gIgtotg = gIgtotd = gIgtots = gIgtotb = 0.0;
        }

        // RF gate charge redistribution
        if (here.B4SOIrgateMod == 2)
            T0 = state0_[here.B4SOIvges] - state0_[here.B4SOIvgs];
        else if (here.B4SOIrgateMod == 3)
            T0 = state0_[here.B4SOIvgms] - state0_[here.B4SOIvgs];
        if (here.B4SOIrgateMod > 1) {
            gcrgd = here.B4SOIgcrgd * T0;
            gcrgg = here.B4SOIgcrgg * T0;
            gcrgs = here.B4SOIgcrgs * T0;
            gcrgb = here.B4SOIgcrgb * T0;
            gcrgg -= here.B4SOIgcrg;
            gcrg = here.B4SOIgcrg;
        } else {
            gcrg = gcrgd = gcrgg = gcrgs = gcrgb = 0.0;
        }
    } else {
        // Reverse mode
        Gm = -here.B4SOIgm;
        Gmbs = -here.B4SOIgmbs;
        Gme = -here.B4SOIgme;
        GmT = -model->B4SOItype * here.B4SOIgmT;
        FwdSum = 0.0;
        RevSum = -Gm - Gmbs - Gme;

        cdgb = -(here.B4SOIcdgb + here.B4SOIcggb + here.B4SOIcbgb);
        cdsb = -(here.B4SOIcddb + here.B4SOIcgdb + here.B4SOIcbdb);
        cddb = -(here.B4SOIcdsb + here.B4SOIcgsb + here.B4SOIcbsb);
        cdeb = -(here.B4SOIcdeb + here.B4SOIcbeb + here.B4SOIceeb);
        cdT  = -model->B4SOItype * (here.B4SOIcgT + here.B4SOIcbT
               + here.B4SOIcdT + here.B4SOIceT);

        ceeb = here.B4SOIceeb;
        ceT  = model->B4SOItype * here.B4SOIceT;

        cggb = here.B4SOIcggb;
        cgsb = here.B4SOIcgdb;
        cgdb = here.B4SOIcgsb;
        cgT  = model->B4SOItype * here.B4SOIcgT;

        cbgb = here.B4SOIcbgb;
        cbsb = here.B4SOIcbdb;
        cbdb = here.B4SOIcbsb;
        cbeb = here.B4SOIcbeb;
        cbT  = model->B4SOItype * here.B4SOIcbT;

        cTt = here.pParam->B4SOIcth;

        gigg = here.B4SOIgigg;
        gigb = here.B4SOIgigb;
        gige = here.B4SOIgige;
        gigs = here.B4SOIgigd;
        gigd = here.B4SOIgigs;
        gigT = model->B4SOItype * here.B4SOIgigT;
        gigpg = here.B4SOIgigpg;
        gigpp = here.B4SOIgigpp;

        gbbg  = -here.B4SOIgbgs;
        gbbb  = -here.B4SOIgbbs;
        gbbp  = -here.B4SOIgbps;
        gbbsp = -here.B4SOIgbds;
        gbbT  = -model->B4SOItype * here.B4SOIgbT;
        gbbe  = -here.B4SOIgbes;

        if (here.B4SOIrbodyMod) {
            gbbsp = -here.B4SOIgiigidld;
            gbbb  = -here.B4SOIgbgiigbpb;
        }
        gbbdp = -(gbbg + gbbsp + gbbb + gbbp + gbbe);

        gddpg  = -here.B4SOIgjsg;
        gddpsp = -here.B4SOIgjsd;
        if (!here.B4SOIrbodyMod)
            gddpb = -here.B4SOIgjsb;
        else
            gddpb = 0.0;
        gddpT  = -model->B4SOItype * here.B4SOIgjsT;
        gddpe  = 0.0;
        gddpdp = -(gddpg + gddpsp + gddpb + gddpe);

        gsspg  = -here.B4SOIgjdg;
        gsspsp = -here.B4SOIgjdd;
        if (!here.B4SOIrbodyMod)
            gsspb = -here.B4SOIgjdb;
        else
            gsspb = here.B4SOIgiigidlb;
        gsspT  = -model->B4SOItype * here.B4SOIgjdT;
        gsspe  = -here.B4SOIgjde;
        gsspdp = -(gsspg + gsspsp + gsspb + gsspe);

        gppb = -here.B4SOIgbpbs;
        gppp = -here.B4SOIgbpps;

        gTtg  = here.B4SOIgtempg;
        gTtb  = here.B4SOIgtempb;
        gTtsp = here.B4SOIgtempd;
        gTtt  = here.B4SOIgtempT;
        gTte  = here.B4SOIgtempe;
        gTtdp = -(gTtg + gTtb + gTtsp + gTte);

        if (model->B4SOIigcMod) {
            gIstotg = here.B4SOIgIgsg + here.B4SOIgIgcdg;
            gIstotd = here.B4SOIgIgcds;
            gIstots = here.B4SOIgIgss + here.B4SOIgIgcdd;
            gIstotb = here.B4SOIgIgcdb;
            gIdtotg = here.B4SOIgIgdg + here.B4SOIgIgcsg;
            gIdtotd = here.B4SOIgIgdd + here.B4SOIgIgcss;
            gIdtots = here.B4SOIgIgcsd;
            gIdtotb = here.B4SOIgIgcsb;
            gIgtotg = gIstotg + gIdtotg;
            gIgtotd = gIstotd + gIdtotd;
            gIgtots = gIstots + gIdtots;
            gIgtotb = gIstotb + gIdtotb;
        } else {
            gIstotg = gIstotd = gIstots = gIstotb = 0.0;
            gIdtotg = gIdtotd = gIdtots = gIdtotb = 0.0;
            gIgtotg = gIgtotd = gIgtots = gIgtotb = 0.0;
        }

        // RF gate charge redistribution (reverse)
        if (here.B4SOIrgateMod == 2)
            T0 = state0_[here.B4SOIvges] - state0_[here.B4SOIvgs];
        else if (here.B4SOIrgateMod == 3)
            T0 = state0_[here.B4SOIvgms] - state0_[here.B4SOIvgs];
        if (here.B4SOIrgateMod > 1) {
            gcrgd = here.B4SOIgcrgs * T0;  // swapped d<->s
            gcrgg = here.B4SOIgcrgg * T0;
            gcrgs = here.B4SOIgcrgd * T0;  // swapped d<->s
            gcrgb = here.B4SOIgcrgb * T0;
            gcrgg -= here.B4SOIgcrg;
            gcrg = here.B4SOIgcrg;
        } else {
            gcrg = gcrgd = gcrgg = gcrgs = gcrgb = 0.0;
        }
    }

    // --- Common quantities (not mode-dependent) ---
    double gdpr, gspr;
    if (!model->B4SOIrdsMod) {
        gdpr = here.B4SOIdrainConductance;
        gspr = here.B4SOIsourceConductance;
    } else {
        gdpr = gspr = 0.0;
    }

    const double gds = here.B4SOIgds;

    const double GSoverlapCap = here.B4SOIcgso;
    const double GDoverlapCap = here.B4SOIcgdo;
    const double GEoverlapCap = here.pParam->B4SOIcgeo;
    const double EDextrinsicCap = here.B4SOIgcde;
    const double ESextrinsicCap = here.B4SOIgcse;

    // --- Capacitance values (neospice: raw caps, no omega) ---
    // ngspice multiplies by omega; we strip that since the AC solver does it.
    double c_cgmgmb, c_cgmdb, c_cgmsb, c_cgmeb, c_cdgmb, c_csgmb, c_cegmb;
    double c_cedb, c_cdeb, c_cddb, c_ceeb, c_cesb, c_cssb, c_cseb;
    double c_cegb, c_ceT, c_cggb, c_cgdb, c_cgsb, c_cgeb, c_cgT;
    double c_cdgb, c_cdsb, c_cdT;
    double c_csgb, c_csdb, c_csT;
    double c_cbgb, c_cbdb, c_cbsb, c_cbeb, c_cbT;
    double c_cTt;
    double c_cdbb, c_csbb, c_cdbdb, c_csbsb, c_cjdbdp, c_cjsbsp;

    if (here.B4SOIrgateMod == 3) {
        c_cgmgmb = GDoverlapCap + GSoverlapCap + GEoverlapCap;
        c_cgmdb = -GDoverlapCap;
        c_cgmsb = -GSoverlapCap;
        c_cgmeb = -GEoverlapCap;
        c_cdgmb = c_cgmdb;
        c_csgmb = c_cgmsb;
        c_cegmb = c_cgmeb;

        c_cedb = -EDextrinsicCap;
        c_cdeb = cdeb - EDextrinsicCap;
        c_cddb = cddb + GDoverlapCap + EDextrinsicCap;
        c_ceeb = ceeb + GEoverlapCap + EDextrinsicCap + ESextrinsicCap;
        c_cesb = -ESextrinsicCap;
        c_cssb = GSoverlapCap + ESextrinsicCap - (cgsb + cbsb + cdsb);
        c_cseb = -(cbeb + cdeb + ceeb + ESextrinsicCap);

        c_cegb = 0.0;
        c_ceT  = ceT;
        c_cggb = here.B4SOIcggb;
        c_cgdb = cgdb;
        c_cgsb = cgsb;
        c_cgeb = 0.0;
        c_cgT  = cgT;

        c_cdgb = cdgb;
        c_cdsb = cdsb;
        c_cdT  = cdT;

        c_csgb = -(cggb + cbgb + cdgb);
        c_csdb = -(cgdb + cbdb + cddb);
        c_csT  = -(cgT + cbT + cdT + ceT);

        c_cbgb = cbgb;
        c_cbdb = cbdb;
        c_cbsb = cbsb;
        c_cbeb = cbeb;
        c_cbT  = cbT;
        c_cTt  = cTt;
    } else {
        c_cedb = -EDextrinsicCap;
        c_cdeb = cdeb - EDextrinsicCap;
        c_cddb = cddb + GDoverlapCap + EDextrinsicCap;
        c_ceeb = ceeb + GEoverlapCap + EDextrinsicCap + ESextrinsicCap;
        c_cesb = -ESextrinsicCap;
        c_cssb = GSoverlapCap + ESextrinsicCap - (cgsb + cbsb + cdsb);
        c_cseb = -(cbeb + cdeb + ceeb + ESextrinsicCap);

        c_cegb = -GEoverlapCap;
        c_ceT  = ceT;
        c_cggb = cggb + GDoverlapCap + GSoverlapCap + GEoverlapCap;
        c_cgdb = cgdb - GDoverlapCap;
        c_cgsb = cgsb - GSoverlapCap;
        c_cgeb = -GEoverlapCap;
        c_cgT  = cgT;

        c_cdgb = cdgb - GDoverlapCap;
        c_cdsb = cdsb;
        c_cdT  = cdT;

        c_csgb = -(cggb + cbgb + cdgb + GSoverlapCap);
        c_csdb = -(cgdb + cbdb + cddb);
        c_csT  = -(cgT + cbT + cdT + ceT);

        c_cbgb = cbgb;
        c_cbdb = cbdb;
        c_cbsb = cbsb;
        c_cbeb = cbeb;
        c_cbT  = cbT;
        c_cTt  = cTt;

        c_cdgmb = c_csgmb = c_cegmb = 0.0;
        c_cgmgmb = c_cgmdb = c_cgmsb = c_cgmeb = 0.0;
    }

    // --- rbodyMod-dependent capacitances ---
    if (here.B4SOImode >= 0) {
        if (!here.B4SOIrbodyMod) {
            c_cjdbdp = c_cjsbsp = 0.0;
            c_cdbb = -(c_cdgb + c_cddb + c_cdsb + c_cdgmb + c_cdeb);
            c_csbb = -(c_csgb + c_csdb + c_cssb + c_csgmb + c_cseb);
            c_cdbdb = 0.0;
            c_csbsb = 0.0;
            c_cbdb = here.B4SOIcbdb;
            c_cbsb = here.B4SOIcbsb;
        } else {
            c_cjdbdp = here.B4SOIcjdb;
            c_cjsbsp = here.B4SOIcjsb;
            c_cdbb = -(c_cdgb + c_cddb + c_cdsb + c_cdgmb + c_cdeb) + c_cjdbdp;
            c_csbb = -(c_csgb + c_csdb + c_cssb + c_csgmb + c_cseb) + c_cjsbsp;
            c_cdbdb = -here.B4SOIcjdb;
            c_csbsb = -here.B4SOIcjsb;
            c_cbdb = here.B4SOIcbdb - c_cdbdb;
            c_cbsb = here.B4SOIcbsb - c_csbsb;
        }
    } else {
        if (!here.B4SOIrbodyMod) {
            c_cjdbdp = c_cjsbsp = 0.0;
            c_cdbb = -(c_cdgb + c_cddb + c_cdsb + c_cdgmb + c_cdeb);
            c_csbb = -(c_csgb + c_csdb + c_cssb + c_csgmb + c_cseb);
            c_cdbdb = 0.0;
            c_csbsb = 0.0;
            c_cbdb = here.B4SOIcbsb;
            c_cbsb = here.B4SOIcbdb;
        } else {
            c_cjdbdp = here.B4SOIcjsb;
            c_cjsbsp = here.B4SOIcjdb;
            c_cdbb = -(c_cdgb + c_cddb + c_cdsb + c_cdgmb + c_cdeb) + c_cjdbdp;
            c_csbb = -(c_csgb + c_csdb + c_cssb + c_csgmb + c_cseb) + c_cjsbsp;
            c_cdbdb = -here.B4SOIcjsb;
            c_csbsb = -here.B4SOIcjdb;
            c_cbdb = here.B4SOIcbsb - c_cdbdb;
            c_cbsb = here.B4SOIcbdb - c_csbsb;
        }
    }

    // --- rdsMod stamps ---
    double gstot, gstotd, gstotg, gstots, gstotb;
    double gdtot, gdtotd, gdtotg, gdtots, gdtotb;
    if (model->B4SOIrdsMod == 1) {
        gstot  = here.B4SOIgstot;
        gstotd = here.B4SOIgstotd;
        gstotg = here.B4SOIgstotg;
        gstots = here.B4SOIgstots - gstot;
        gstotb = here.B4SOIgstotb;
        gdtot  = here.B4SOIgdtot;
        gdtotd = here.B4SOIgdtotd - gdtot;
        gdtotg = here.B4SOIgdtotg;
        gdtots = here.B4SOIgdtots;
        gdtotb = here.B4SOIgdtotb;
    } else {
        gstot = gstotd = gstotg = gstots = gstotb = 0.0;
        gdtot = gdtotd = gdtotg = gdtots = gdtotb = 0.0;
    }

    // =====================================================================
    // STAMP G and C matrices
    // =====================================================================

    const double geltd = here.B4SOIgrgeltd;

    // --- Gate network stamps (rgateMod variants) ---
    if (here.B4SOIrgateMod == 1) {
        G.add(here.B4SOIGEgePtr, m * geltd);
        G.add(here.B4SOIGgPtr,   m * (-geltd));
        G.add(here.B4SOIGEgPtr,  m * (-geltd));
        G.add(here.B4SOIGgPtr,   m * (geltd + gigg + gIgtotg));
        G.add(here.B4SOIGdpPtr,  m * (gigd + gIgtotd));
        G.add(here.B4SOIGspPtr,  m * (gigs + gIgtots));
        if (here.B4SOIsoiMod != 2)
            G.add(here.B4SOIGbPtr, m * (-(- gigb - gIgtotb)));
    } else if (here.B4SOIrgateMod == 2) {
        G.add(here.B4SOIGEgePtr, m * gcrg);
        G.add(here.B4SOIGEgPtr,  m * gcrgg);
        G.add(here.B4SOIGEdpPtr, m * gcrgd);
        G.add(here.B4SOIGEspPtr, m * gcrgs);
        if (here.B4SOIsoiMod != 2)
            G.add(here.B4SOIGEbPtr, m * gcrgb);
        G.add(here.B4SOIGgePtr,  m * (-gcrg));
        G.add(here.B4SOIGgPtr,   m * (-(gcrgg - gigg - gIgtotg)));
        G.add(here.B4SOIGdpPtr,  m * (-(gcrgd - gigd - gIgtotd)));
        G.add(here.B4SOIGspPtr,  m * (-(gcrgs - gigs - gIgtots)));
        if (here.B4SOIsoiMod != 2)
            G.add(here.B4SOIGbPtr, m * (-(gcrgb - gigb - gIgtotb)));
    } else if (here.B4SOIrgateMod == 3) {
        G.add(here.B4SOIGEgePtr, m * geltd);
        G.add(here.B4SOIGEgmPtr, m * (-geltd));
        G.add(here.B4SOIGMgePtr, m * (-geltd));
        G.add(here.B4SOIGMgmPtr, m * (geltd + gcrg));
        C.add(here.B4SOIGMgmPtr, m * c_cgmgmb);
        G.add(here.B4SOIGMdpPtr, m * gcrgd);
        C.add(here.B4SOIGMdpPtr, m * c_cgmdb);
        G.add(here.B4SOIGMgPtr,  m * gcrgg);
        G.add(here.B4SOIGMspPtr, m * gcrgs);
        C.add(here.B4SOIGMspPtr, m * c_cgmsb);
        if (here.B4SOIsoiMod != 2)
            G.add(here.B4SOIGMbPtr, m * gcrgb);
        C.add(here.B4SOIGMePtr,  m * c_cgmeb);

        C.add(here.B4SOIDPgmPtr, m * c_cdgmb);
        G.add(here.B4SOIGgmPtr,  m * (-gcrg));
        C.add(here.B4SOISPgmPtr, m * c_csgmb);
        C.add(here.B4SOIEgmPtr,  m * c_cegmb);

        G.add(here.B4SOIGgPtr,   m * (-(gcrgg - gigg - gIgtotg)));
        G.add(here.B4SOIGdpPtr,  m * (-(gcrgd - gigd - gIgtotd)));
        G.add(here.B4SOIGspPtr,  m * (-(gcrgs - gigs - gIgtots)));
        if (here.B4SOIsoiMod != 2)
            G.add(here.B4SOIGbPtr, m * (-(gcrgb - gigb - gIgtotb)));
    } else {
        // rgateMod == 0
        G.add(here.B4SOIGgPtr,   m * (gigg + gIgtotg));
        G.add(here.B4SOIGdpPtr,  m * (gigd + gIgtotd));
        G.add(here.B4SOIGspPtr,  m * (gigs + gIgtots));
        if (here.B4SOIsoiMod != 2)
            G.add(here.B4SOIGbPtr, m * (gigb + gIgtotb));
    }

    // --- rdsMod stamps on D/S external nodes ---
    if (model->B4SOIrdsMod) {
        G.add(here.B4SOIDgPtr,  m * gdtotg);
        G.add(here.B4SOIDspPtr, m * gdtots);
        G.add(here.B4SOISdpPtr, m * gstotd);
        G.add(here.B4SOISgPtr,  m * gstotg);
        if (here.B4SOIsoiMod != 2) {
            G.add(here.B4SOIDbPtr, m * gdtotb);
            G.add(here.B4SOISbPtr, m * gstotb);
        }
    }

    // --- E-node capacitance stamps ---
    C.add(here.B4SOIEdpPtr, m * c_cedb);
    C.add(here.B4SOIEspPtr, m * c_cesb);
    C.add(here.B4SOIDPePtr, m * c_cdeb);
    C.add(here.B4SOISPePtr, m * c_cseb);
    C.add(here.B4SOIEgPtr,  m * c_cegb);
    C.add(here.B4SOIGePtr,  m * c_cgeb);
    C.add(here.B4SOIEePtr,  m * c_ceeb);

    // --- G/DP/SP intrinsic capacitance stamps ---
    C.add(here.B4SOIGgPtr,   m * c_cggb);
    C.add(here.B4SOIGdpPtr,  m * c_cgdb);
    C.add(here.B4SOIGspPtr,  m * c_cgsb);
    C.add(here.B4SOIDPgPtr,  m * c_cdgb);
    C.add(here.B4SOIDPdpPtr, m * c_cddb);
    C.add(here.B4SOIDPspPtr, m * c_cdsb);
    C.add(here.B4SOISPgPtr,  m * c_csgb);
    C.add(here.B4SOISPdpPtr, m * c_csdb);
    C.add(here.B4SOISPspPtr, m * c_cssb);

    // --- B-node stamps (soiMod != 2) ---
    if (here.B4SOIsoiMod != 2) {
        C.add(here.B4SOIBePtr,  m * c_cbeb);
        C.add(here.B4SOIBgPtr,  m * c_cbgb);
        C.add(here.B4SOIBdpPtr, m * c_cbdb);
        C.add(here.B4SOIBspPtr, m * c_cbsb);
        C.add(here.B4SOIEbPtr,  m * (-(c_cegb + c_ceeb + c_cedb + c_cesb)));
        C.add(here.B4SOIGbPtr,  m * (-(c_cggb + c_cgdb + c_cgsb + c_cgeb)));
        C.add(here.B4SOIDPbPtr, m * c_cdbb);
        C.add(here.B4SOISPbPtr, m * c_csbb);
        C.add(here.B4SOIBbPtr,  m * (-(c_cbgb + c_cbdb + c_cbsb + c_cbeb)));
    }

    // --- Self-heating C stamps ---
    if (selfheat) {
        C.add(here.B4SOITemptempPtr, m * c_cTt);
        C.add(here.B4SOIDPtempPtr,   m * c_cdT);
        C.add(here.B4SOISPtempPtr,   m * c_csT);
        C.add(here.B4SOIBtempPtr,    m * c_cbT);
        C.add(here.B4SOIEtempPtr,    m * c_ceT);
        C.add(here.B4SOIGtempPtr,    m * c_cgT);
    }

    // --- SOI body connection G stamps (soiMod != 0) ---
    if (here.B4SOIsoiMod != 0) {
        G.add(here.B4SOIDPePtr, m * (Gme + gddpe));
        G.add(here.B4SOISPePtr, m * (gsspe - Gme));
        if (here.B4SOIsoiMod != 2) {
            G.add(here.B4SOIGePtr, m * gige);
            G.add(here.B4SOIBePtr, m * (-gige));
        }
    }

    // --- DP (drain prime) row G stamps ---
    G.add(here.B4SOIDPgPtr,  m * (Gm + gddpg - gIdtotg - gdtotg));
    G.add(here.B4SOIDPdpPtr, m * (gdpr + gds + gddpdp + RevSum - gIdtotd - gdtotd));
    G.add(here.B4SOIDPspPtr, m * (-(gds + FwdSum - gddpsp + gIdtots + gdtots)));
    G.add(here.B4SOIDPdPtr,  m * (-(gdpr + gdtot)));

    // --- SP (source prime) row G stamps ---
    G.add(here.B4SOISPgPtr,  m * (-(Gm - gsspg + gIstotg + gstotg)));
    G.add(here.B4SOISPdpPtr, m * (-(gds + RevSum - gsspdp + gIstotd + gstotd)));
    G.add(here.B4SOISPspPtr, m * (gspr + gds + FwdSum + gsspsp - gIstots - gstots));
    G.add(here.B4SOISPsPtr,  m * (-(gspr + gstot)));

    // --- B-node G stamps (soiMod != 2) ---
    if (here.B4SOIsoiMod != 2) {
        G.add(here.B4SOIBePtr,  m * gbbe);
        G.add(here.B4SOIBgPtr,  m * (gbbg - gigg));
        G.add(here.B4SOIBdpPtr, m * (gbbdp - gigd));
        G.add(here.B4SOIBspPtr, m * (gbbsp - gigs));
        G.add(here.B4SOIBbPtr,  m * (gbbb - gigb));
        G.add(here.B4SOISPbPtr, m * (-(Gmbs - gsspb + gIstotb + gstotb)));
        G.add(here.B4SOIDPbPtr, m * (-((-gddpb - Gmbs) + gIdtotb + gdtotb)));
    }

    // --- Self-heating G stamps ---
    if (selfheat) {
        G.add(here.B4SOIDPtempPtr,   m * (GmT + gddpT));
        G.add(here.B4SOISPtempPtr,   m * (-GmT + gsspT));
        G.add(here.B4SOIBtempPtr,    m * (gbbT - gigT));
        G.add(here.B4SOIGtempPtr,    m * gigT);
        G.add(here.B4SOITemptempPtr, m * (gTtt + 1.0 / here.pParam->B4SOIrth));
        G.add(here.B4SOITempgPtr,    m * gTtg);
        G.add(here.B4SOITempbPtr,    m * gTtb);
        G.add(here.B4SOITempdpPtr,   m * gTtdp);
        G.add(here.B4SOITempspPtr,   m * gTtsp);
        if (here.B4SOIsoiMod != 0)
            G.add(here.B4SOITempePtr, m * gTte);
    }

    // --- D/S external node stamps ---
    G.add(here.B4SOIDdPtr,  m * (gdpr + gdtot));
    G.add(here.B4SOIDdpPtr, m * (-(gdpr - gdtotd)));
    G.add(here.B4SOISsPtr,  m * (gspr + gstot));
    G.add(here.B4SOISspPtr, m * (-(gspr - gstots)));

    // --- Body contact (bodyMod) ---
    if (here.B4SOIbodyMod == 1) {
        G.add(here.B4SOIBpPtr, m * (-gppp));
        G.add(here.B4SOIPbPtr, m * gppb);
        G.add(here.B4SOIPpPtr, m * gppp);
    }

    // --- Ig_agbcp2 stamps (v4.1) ---
    G.add(here.B4SOIGgPtr, gigpg);
    if (here.B4SOIbodyMod == 1) {
        G.add(here.B4SOIPpPtr, m * (-gigpp));
        G.add(here.B4SOIPgPtr, m * (-gigpg));
        G.add(here.B4SOIGpPtr, m * gigpp);
    } else if (here.B4SOIbodyMod == 2) {
        G.add(here.B4SOIBbPtr, m * (-gigpp));
        G.add(here.B4SOIBgPtr, m * (-gigpg));
        G.add(here.B4SOIGbPtr, m * gigpp);
    }

    // --- rbodyMod stamps (v4.0) ---
    if (here.B4SOIrbodyMod) {
        C.add(here.B4SOIDPdbPtr, m * (-c_cjdbdp));
        G.add(here.B4SOIDPdbPtr, m * (-here.B4SOIGGjdb));
        C.add(here.B4SOISPsbPtr, m * (-c_cjsbsp));
        G.add(here.B4SOISPsbPtr, m * (-here.B4SOIGGjsb));

        C.add(here.B4SOIDBdpPtr, m * (-c_cjdbdp));
        G.add(here.B4SOIDBdpPtr, m * (-here.B4SOIGGjdb));
        C.add(here.B4SOIDBdbPtr, m * c_cjdbdp);
        G.add(here.B4SOIDBdbPtr, m * (here.B4SOIGGjdb + here.B4SOIgrbdb));

        G.add(here.B4SOIDBbPtr, m * (-here.B4SOIgrbdb));
        G.add(here.B4SOISBbPtr, m * (-here.B4SOIgrbsb));

        C.add(here.B4SOISBspPtr, m * (-c_cjsbsp));
        G.add(here.B4SOISBspPtr, m * (-here.B4SOIGGjsb));
        C.add(here.B4SOISBsbPtr, m * c_cjsbsp);
        G.add(here.B4SOISBsbPtr, m * (here.B4SOIGGjsb + here.B4SOIgrbsb));

        G.add(here.B4SOIBdbPtr, m * (-here.B4SOIgrbdb));
        G.add(here.B4SOIBsbPtr, m * (-here.B4SOIgrbsb));
        G.add(here.B4SOIBbPtr,  m * (here.B4SOIgrbsb + here.B4SOIgrbdb));
    }

    // --- Debug probes (debugMod) ---
    if (here.B4SOIdebugMod != 0) {
        G.add(here.B4SOIVbsPtr, 1.0);
        G.add(here.B4SOIIdsPtr, 1.0);
        G.add(here.B4SOIIcPtr, 1.0);
        G.add(here.B4SOIIbsPtr, 1.0);
        G.add(here.B4SOIIbdPtr, 1.0);
        G.add(here.B4SOIIiiPtr, 1.0);
        G.add(here.B4SOIIgidlPtr, 1.0);
        G.add(here.B4SOIItunPtr, 1.0);
        G.add(here.B4SOIIbpPtr, 1.0);
        G.add(here.B4SOICbgPtr, 1.0);
        G.add(here.B4SOICbbPtr, 1.0);
        G.add(here.B4SOICbdPtr, 1.0);
        G.add(here.B4SOIQbfPtr, 1.0);
        G.add(here.B4SOIQjsPtr, 1.0);
        G.add(here.B4SOIQjdPtr, 1.0);
    }
}

// ---------------------------------------------------------------------------
// compute_trunc
// ---------------------------------------------------------------------------
double B4SOIDevice::compute_trunc(const IntegratorCtx& ctx,
                               const SimOptions& opts) const {
    if (ctx.order < 2 || ctx.delta <= 0.0) return 1e30;
    if (!state0_ || !state1_ || !state2_) return 1e30;

    const double h0 = ctx.delta;
    const double h1 = ctx.delta_old[1];
    if (h1 <= 0.0) return 1e30;

    double dt_min = 1e30;
    const double lte_coeff = ctx.lte_coefficient();

    { // charge offset: inst_.B4SOIqb
        const int qcap = inst_.B4SOIqb;
        const double q0 = state0_[qcap], q1 = state1_[qcap], q2 = state2_[qcap];
        const double dd2 = ((q0 - q1) / h0 - (q1 - q2) / h1) / (h0 + h1);
        const double tol = opts.chgtol + opts.reltol * std::max(std::abs(q0), std::abs(q1));
        if (tol > 0.0 && std::abs(dd2) > 1e-30) {
            dt_min = std::min(dt_min, std::sqrt(opts.trtol * tol / (lte_coeff * std::abs(dd2))));
        }
    }
    { // charge offset: inst_.B4SOIqg
        const int qcap = inst_.B4SOIqg;
        const double q0 = state0_[qcap], q1 = state1_[qcap], q2 = state2_[qcap];
        const double dd2 = ((q0 - q1) / h0 - (q1 - q2) / h1) / (h0 + h1);
        const double tol = opts.chgtol + opts.reltol * std::max(std::abs(q0), std::abs(q1));
        if (tol > 0.0 && std::abs(dd2) > 1e-30) {
            dt_min = std::min(dt_min, std::sqrt(opts.trtol * tol / (lte_coeff * std::abs(dd2))));
        }
    }
    { // charge offset: inst_.B4SOIqd
        const int qcap = inst_.B4SOIqd;
        const double q0 = state0_[qcap], q1 = state1_[qcap], q2 = state2_[qcap];
        const double dd2 = ((q0 - q1) / h0 - (q1 - q2) / h1) / (h0 + h1);
        const double tol = opts.chgtol + opts.reltol * std::max(std::abs(q0), std::abs(q1));
        if (tol > 0.0 && std::abs(dd2) > 1e-30) {
            dt_min = std::min(dt_min, std::sqrt(opts.trtol * tol / (lte_coeff * std::abs(dd2))));
        }
    }
    { // charge offset: inst_.B4SOIqe
        const int qcap = inst_.B4SOIqe;
        const double q0 = state0_[qcap], q1 = state1_[qcap], q2 = state2_[qcap];
        const double dd2 = ((q0 - q1) / h0 - (q1 - q2) / h1) / (h0 + h1);
        const double tol = opts.chgtol + opts.reltol * std::max(std::abs(q0), std::abs(q1));
        if (tol > 0.0 && std::abs(dd2) > 1e-30) {
            dt_min = std::min(dt_min, std::sqrt(opts.trtol * tol / (lte_coeff * std::abs(dd2))));
        }
    }
    { // charge offset: inst_.B4SOIqth
        const int qcap = inst_.B4SOIqth;
        const double q0 = state0_[qcap], q1 = state1_[qcap], q2 = state2_[qcap];
        const double dd2 = ((q0 - q1) / h0 - (q1 - q2) / h1) / (h0 + h1);
        const double tol = opts.chgtol + opts.reltol * std::max(std::abs(q0), std::abs(q1));
        if (tol > 0.0 && std::abs(dd2) > 1e-30) {
            dt_min = std::min(dt_min, std::sqrt(opts.trtol * tol / (lte_coeff * std::abs(dd2))));
        }
    }
    { // charge offset: inst_.B4SOIqgmid
        const int qcap = inst_.B4SOIqgmid;
        const double q0 = state0_[qcap], q1 = state1_[qcap], q2 = state2_[qcap];
        const double dd2 = ((q0 - q1) / h0 - (q1 - q2) / h1) / (h0 + h1);
        const double tol = opts.chgtol + opts.reltol * std::max(std::abs(q0), std::abs(q1));
        if (tol > 0.0 && std::abs(dd2) > 1e-30) {
            dt_min = std::min(dt_min, std::sqrt(opts.trtol * tol / (lte_coeff * std::abs(dd2))));
        }
    }
    { // charge offset: inst_.B4SOIqbs
        const int qcap = inst_.B4SOIqbs;
        const double q0 = state0_[qcap], q1 = state1_[qcap], q2 = state2_[qcap];
        const double dd2 = ((q0 - q1) / h0 - (q1 - q2) / h1) / (h0 + h1);
        const double tol = opts.chgtol + opts.reltol * std::max(std::abs(q0), std::abs(q1));
        if (tol > 0.0 && std::abs(dd2) > 1e-30) {
            dt_min = std::min(dt_min, std::sqrt(opts.trtol * tol / (lte_coeff * std::abs(dd2))));
        }
    }
    { // charge offset: inst_.B4SOIqbd
        const int qcap = inst_.B4SOIqbd;
        const double q0 = state0_[qcap], q1 = state1_[qcap], q2 = state2_[qcap];
        const double dd2 = ((q0 - q1) / h0 - (q1 - q2) / h1) / (h0 + h1);
        const double tol = opts.chgtol + opts.reltol * std::max(std::abs(q0), std::abs(q1));
        if (tol > 0.0 && std::abs(dd2) > 1e-30) {
            dt_min = std::min(dt_min, std::sqrt(opts.trtol * tol / (lte_coeff * std::abs(dd2))));
        }
    }
    return dt_min;
}

// ---------------------------------------------------------------------------
// device_converged
// ---------------------------------------------------------------------------
bool B4SOIDevice::device_converged() const {
    return last_noncon_ == 0;
}

// ---------------------------------------------------------------------------
// query_param
// ---------------------------------------------------------------------------
std::optional<double> B4SOIDevice::query_param(const std::string& name) const {
    const std::string key = str_tolower(name);
    const double m = inst_.B4SOIm;

    // --- Operating-point parameters (scaled by multiplier m) ---
    if (key == "gm")                        return inst_.B4SOIgm * m;
    if (key == "gds")                       return inst_.B4SOIgds * m;
    if (key == "gmbs")                      return inst_.B4SOIgmbs * m;
    if (key == "vth" || key == "von")       return inst_.B4SOIvon;
    if (key == "vdsat")                     return inst_.B4SOIvdsat;
    if (key == "id" || key == "ids")        return inst_.B4SOIcd * m;
    if (key == "ibs")                       return inst_.B4SOIibs * m;
    if (key == "ibd")                       return inst_.B4SOIibd * m;
    if (key == "iii" || key == "isub")      return inst_.B4SOIiii * m;
    if (key == "ig")                        return inst_.B4SOIig * m;
    if (key == "igidl")                     return inst_.B4SOIigidl * m;
    if (key == "igisl")                     return inst_.B4SOIigisl * m;
    if (key == "igcs")                      return inst_.B4SOIIgcs * m;
    if (key == "igcd")                      return inst_.B4SOIIgcd * m;
    if (key == "igs")                       return inst_.B4SOIIgs * m;
    if (key == "igd")                       return inst_.B4SOIIgd * m;
    if (key == "igb")                       return inst_.B4SOIIgb * m;

    // --- Capacitances ---
    if (key == "cbg")                       return inst_.B4SOIcbg * m;
    if (key == "cbb")                       return inst_.B4SOIcbb * m;
    if (key == "cbd")                       return inst_.B4SOIcbd * m;

    // --- Charges ---
    if (key == "qg" && state0_)             return state0_[state_base_ + 14] * m;
    if (key == "qb" && state0_)             return state0_[state_base_ + 12] * m;
    if (key == "qd" && state0_)             return state0_[state_base_ + 16] * m;
    if (key == "qe" && state0_)             return state0_[state_base_ + 18] * m;

    // --- Terminal voltages from state vector ---
    if (state0_ && state_base_ >= 0) {
        if (key == "vbd")                   return state0_[state_base_ + 0];
        if (key == "vbs")                   return state0_[state_base_ + 1];
        if (key == "vgs")                   return state0_[state_base_ + 2];
        if (key == "vds")                   return state0_[state_base_ + 3];
        if (key == "ves")                   return state0_[state_base_ + 4];
        if (key == "vps")                   return state0_[state_base_ + 5];
    }

    // --- Geometry (no multiplier) ---
    if (key == "w")                         return inst_.B4SOIw;
    if (key == "l")                         return inst_.B4SOIl;
    if (key == "m")                         return inst_.B4SOIm;
    if (key == "nf")                        return inst_.B4SOInf;
    if (key == "sa")                        return inst_.B4SOIsa;
    if (key == "sb")                        return inst_.B4SOIsb;
    if (key == "sd")                        return inst_.B4SOIsd;
    if (key == "ad")                        return inst_.B4SOIdrainArea;
    if (key == "as")                        return inst_.B4SOIsourceArea;
    if (key == "pd")                        return inst_.B4SOIdrainPerimeter;
    if (key == "ps")                        return inst_.B4SOIsourcePerimeter;
    if (key == "nrd")                       return inst_.B4SOIdrainSquares;
    if (key == "nrs")                       return inst_.B4SOIsourceSquares;
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// noise_sources — ported from ngspice b4soinoi.c
// ---------------------------------------------------------------------------
static constexpr double NOISE_MINLOG = 1e-38;

std::vector<Device::NoiseSource> B4SOIDevice::noise_sources(
        double freq, const std::vector<double>& /*dc_solution*/) const {
    const auto* model = model_;
    const auto& inst  = inst_;
    const auto* pParam = inst.pParam;

    if (!pParam) return {};

    const double m   = inst.B4SOIm;
    const double kT  = BOLTZMANN * sim_temp_;
    const double fourKT = 4.0 * kT;

    // Node indices (neospice convention)
    const int32_t dp_neo = ucb_to_neo(inst.B4SOIdNodePrime);
    const int32_t sp_neo = ucb_to_neo(inst.B4SOIsNodePrime);
    const int32_t d_neo  = ucb_to_neo(inst.B4SOIdNode);
    const int32_t s_neo  = ucb_to_neo(inst.B4SOIsNode);
    const int32_t g_neo  = ucb_to_neo(inst.B4SOIgNode);
    const int32_t ge_neo = ucb_to_neo(inst.B4SOIgNodeExt);
    const int32_t gm_neo = ucb_to_neo(inst.B4SOIgNodeMid);
    const int32_t b_neo  = ucb_to_neo(inst.B4SOIbNode);
    const int32_t p_neo  = ucb_to_neo(inst.B4SOIpNode);
    const int32_t sb_neo = ucb_to_neo(inst.B4SOIsbNode);
    const int32_t db_neo = ucb_to_neo(inst.B4SOIdbNode);

    std::vector<NoiseSource> sources;
    sources.reserve(16);

    // --- Self-heating temperature ratio ---
    double tempRatioSH = 1.0;
    if ((model->B4SOIshMod == 1) && (inst.B4SOIrth0 != 0.0))
        tempRatioSH = inst.B4SOITempSH / sim_temp_;

    // --- Vdseff/cd ratio with limit ---
    double Vdseffovcd = 1.0e9;
    if (inst.B4SOIcd != 0.0) {
        Vdseffovcd = inst.B4SOIVdseff / inst.B4SOIcd;
        if (Vdseffovcd >= 1.0e9) Vdseffovcd = 1.0e9;
    }

    // -----------------------------------------------------------------------
    // 1. Drain / Source series resistance thermal noise
    // -----------------------------------------------------------------------
    double gspr, gdpr;
    double npart_beta = 0.0, npart_theta = 0.0;

    if (model->B4SOItnoiMod != 1) {
        // tnoiMod 0 or 2
        if (model->B4SOIrdsMod == 0) {
            gspr = inst.B4SOIsourceConductance;
            gdpr = inst.B4SOIdrainConductance;
        } else {
            gspr = inst.B4SOIgstot;
            gdpr = inst.B4SOIgdtot;
        }
    } else {
        // tnoiMod 1: holistic model modifies gspr/gdpr
        const double esat = 2.0 * inst.B4SOIvsattemp / inst.B4SOIueff;
        double T5 = (esat > 0.0 && pParam->B4SOIleff > 0.0)
                   ? inst.B4SOIVgsteff / esat / pParam->B4SOIleff : 0.0;
        T5 *= T5;
        npart_beta  = model->B4SOIrnoia * (1.0 + T5 * model->B4SOItnoia * pParam->B4SOIleff);
        npart_theta = model->B4SOIrnoib * (1.0 + T5 * model->B4SOItnoib * pParam->B4SOIleff);
        if (npart_theta > 0.9)
            npart_theta = 0.9;
        if (npart_theta > 0.9 * npart_beta)
            npart_theta = 0.9 * npart_beta;

        if (model->B4SOIrdsMod == 0) {
            gspr = inst.B4SOIsourceConductance;
            gdpr = inst.B4SOIdrainConductance;
        } else {
            gspr = inst.B4SOIgstot;
            gdpr = inst.B4SOIgdtot;
        }

        if (state0_ && inst.B4SOIidovVds > 0.0) {
            if (state0_[inst.B4SOIvds] >= 0.0)
                gspr = gspr * (1.0 + npart_theta * npart_theta * gspr / inst.B4SOIidovVds);
            else
                gdpr = gdpr * (1.0 + npart_theta * npart_theta * gdpr / inst.B4SOIidovVds);
        }
    }

    if (gdpr > 0.0)
        sources.push_back({dp_neo, d_neo, fourKT * gdpr * tempRatioSH * m});
    if (gspr > 0.0)
        sources.push_back({sp_neo, s_neo, fourKT * gspr * tempRatioSH * m});

    // -----------------------------------------------------------------------
    // 2. Gate resistance thermal noise (rgateMod 1, 2, 3)
    // -----------------------------------------------------------------------
    if (inst.B4SOIrgateMod == 1) {
        if (inst.B4SOIgrgeltd > 0.0)
            sources.push_back({g_neo, ge_neo, fourKT * inst.B4SOIgrgeltd * tempRatioSH * m});
    } else if (inst.B4SOIrgateMod == 2) {
        if (inst.B4SOIgrgeltd > 0.0 && inst.B4SOIgcrg > 0.0) {
            const double T0_r = 1.0 + inst.B4SOIgrgeltd / inst.B4SOIgcrg;
            const double T1_r = T0_r * T0_r;
            sources.push_back({g_neo, ge_neo,
                               fourKT * (inst.B4SOIgrgeltd / T1_r) * tempRatioSH * m});
        }
    } else if (inst.B4SOIrgateMod == 3) {
        if (inst.B4SOIgrgeltd > 0.0)
            sources.push_back({gm_neo, ge_neo, fourKT * inst.B4SOIgrgeltd * tempRatioSH * m});
    }

    // -----------------------------------------------------------------------
    // 3. Body resistance thermal noise (rbodyMod, rbsb/rbdb)
    // -----------------------------------------------------------------------
    if (inst.B4SOIrbodyMod) {
        if (inst.B4SOIgrbsb > 0.0)
            sources.push_back({b_neo, sb_neo, fourKT * inst.B4SOIgrbsb * m});
        if (inst.B4SOIgrbdb > 0.0)
            sources.push_back({b_neo, db_neo, fourKT * inst.B4SOIgrbdb * tempRatioSH * m});
    }

    // -----------------------------------------------------------------------
    // 4. Body contact thermal noise (bodyMod == 1)
    // -----------------------------------------------------------------------
    if (inst.B4SOIbodyMod == 1) {
        const double rbody_total = inst.B4SOIrbodyext + pParam->B4SOIrbody;
        if (rbody_total > 0.0)
            sources.push_back({b_neo, p_neo, fourKT * tempRatioSH / rbody_total * m});
    }

    // -----------------------------------------------------------------------
    // 5. Channel thermal noise (tnoiMod dependent)
    // -----------------------------------------------------------------------
    double channel_noise = 0.0;
    switch (model->B4SOItnoiMod) {
      case 0: {
        // Charge-based model
        const double ueff = inst.B4SOIueff;
        const double qinv_abs = std::fabs(inst.B4SOIqinv);
        const double Leff = pParam->B4SOIleff;
        const double Leff2 = Leff * Leff;
        const double rds_term = ueff * qinv_abs * inst.B4SOIrds;
        const double denom = Leff2 + rds_term;
        if (denom > 0.0) {
            const double G_ch = (ueff * qinv_abs / denom) * model->B4SOIntnoi;
            channel_noise = fourKT * G_ch * tempRatioSH * m;
        }
        break;
      }
      case 1: {
        // Holistic model
        const double T0_n = inst.B4SOIgm + inst.B4SOIgmbs + inst.B4SOIgds;
        const double T0_sq = T0_n * T0_n;
        const double esat = 2.0 * inst.B4SOIvsattemp / inst.B4SOIueff;
        double T5 = (esat > 0.0 && pParam->B4SOIleff > 0.0)
                   ? inst.B4SOIVgsteff / esat / pParam->B4SOIleff : 0.0;
        T5 *= T5;
        const double nb = model->B4SOIrnoia * (1.0 + T5 * model->B4SOItnoia * pParam->B4SOIleff);
        double nt = model->B4SOIrnoib * (1.0 + T5 * model->B4SOItnoib * pParam->B4SOIleff);
        if (nt > 0.9) nt = 0.9;
        if (nt > 0.9 * nb) nt = 0.9 * nb;

        const double igsquare = nt * nt * T0_sq * Vdseffovcd;
        const double T1_n = nb * (inst.B4SOIgm + inst.B4SOIgmbs) + inst.B4SOIgds;
        const double T2_n = T1_n * T1_n * Vdseffovcd;
        const double G_ch = std::max(0.0, T2_n - igsquare);
        channel_noise = fourKT * G_ch * tempRatioSH * m;
        break;
      }
      case 2:
      default: {
        // SPICE2 model
        const double G_ch = model->B4SOIntnoi * (2.0 / 3.0)
                          * std::fabs(inst.B4SOIgm + inst.B4SOIgds + inst.B4SOIgmbs);
        channel_noise = fourKT * G_ch * tempRatioSH * m;
        break;
      }
    }
    if (channel_noise > 0.0)
        sources.push_back({dp_neo, sp_neo, channel_noise});

    // -----------------------------------------------------------------------
    // 6. Flicker (1/f) noise
    // -----------------------------------------------------------------------
    if (freq > 0.0) {
        double flicker_noise = 0.0;
        const double cd_abs = std::fabs(inst.B4SOIcd);
        const double Leff = pParam->B4SOIleff;
        const double weff = pParam->B4SOIweff;
        const double nf = inst.B4SOInf;

        switch (model->B4SOIfnoiMod) {
          case 0: {
            // Simple KF/AF model
            if (model->B4SOIw0flk > 0.0) {
                // w0flk-scaled model
                const double cd_eff = std::fabs(inst.B4SOIcd / weff / nf * model->B4SOIw0flk);
                const double cd_safe = std::max(cd_eff, NOISE_MINLOG);
                if (model->B4SOIcox > 0.0 && Leff > 0.0)
                    flicker_noise = m * nf * weff / model->B4SOIw0flk
                                  * model->B4SOIkf * std::pow(cd_safe, model->B4SOIaf)
                                  / (std::pow(freq, model->B4SOIef)
                                     * std::pow(Leff, model->B4SOIbf) * model->B4SOIcox);
            } else {
                const double cd_safe = std::max(cd_abs, NOISE_MINLOG);
                if (model->B4SOIcox > 0.0 && Leff > 0.0)
                    flicker_noise = m * model->B4SOIkf * std::pow(cd_safe, model->B4SOIaf)
                                  / (std::pow(freq, model->B4SOIef)
                                     * std::pow(Leff, model->B4SOIbf) * model->B4SOIcox);
            }
            break;
          }
          case 1: {
            // Unified 1/f noise model (B4SOIEval1ovFNoise)
            double vgs_n = 0.0, vds_n = 0.0;
            if (state0_) {
                vgs_n = state0_[inst.B4SOIvgs];
                vds_n = state0_[inst.B4SOIvds];
            }
            if (vds_n < 0.0) {
                vds_n = -vds_n;
                vgs_n = vgs_n + vds_n;
            }

            // Eval1ovFNoise
            const double ueff = inst.B4SOIueff;
            const double esat = (ueff > 0.0) ? 2.0 * inst.B4SOIvsattemp / ueff : 1e10;
            double DelClm = 0.0;
            if (model->B4SOIem > 0.0 && pParam->B4SOIlitl > 0.0 && esat > 0.0) {
                const double T0_f = (((vds_n - inst.B4SOIVdseff) / pParam->B4SOIlitl)
                                    + model->B4SOIem) / esat;
                DelClm = pParam->B4SOIlitl * std::log(std::max(T0_f, NOISE_MINLOG));
                if (DelClm < 0.0) DelClm = 0.0;
            }

            const double EffFreq = std::pow(freq, model->B4SOIef);
            const double N0 = model->B4SOIcox * inst.B4SOIVgsteff / CHARGE_Q;
            const double Nl = model->B4SOIcox * inst.B4SOIVgsteff
                            * (1.0 - inst.B4SOIAbovVgst2Vtm * inst.B4SOIVdseff) / CHARGE_Q;

            // Use SH temperature if applicable
            const double temp_noise = ((model->B4SOIshMod == 1) && (inst.B4SOIrth0 != 0.0))
                                    ? inst.B4SOITempSH : sim_temp_;

            const double T3_f = model->B4SOIoxideTrapDensityA
                              * std::log(std::max((N0 + inst.B4SOInstar)
                                  / std::max(Nl + inst.B4SOInstar, NOISE_MINLOG), NOISE_MINLOG));
            const double T4_f = model->B4SOIoxideTrapDensityB * (N0 - Nl);
            const double T5_f = model->B4SOIoxideTrapDensityC * 0.5 * (N0 * N0 - Nl * Nl);

            double Ssi = 0.0;
            if (EffFreq > 0.0 && inst.B4SOIAbulk > 0.0 && model->B4SOIcox > 0.0
                && Leff > 0.0) {
                const double T1_f = CHARGE_Q * CHARGE_Q * BOLTZMANN * temp_noise
                                  * cd_abs * ueff;
                const double T2_f = 1.0e10 * EffFreq * inst.B4SOIAbulk * model->B4SOIcox
                                  * Leff * Leff;
                const double T6_f = BOLTZMANN * temp_noise * cd_abs * cd_abs;
                const double T7_f = 1.0e10 * EffFreq * Leff * Leff * weff * nf;
                const double Nls = Nl + inst.B4SOInstar;
                const double T8_f = model->B4SOIoxideTrapDensityA
                                  + model->B4SOIoxideTrapDensityB * Nl
                                  + model->B4SOIoxideTrapDensityC * Nl * Nl;
                const double T9_f = Nls * Nls;

                if (T2_f > 0.0 && T7_f > 0.0 && T9_f > 0.0)
                    Ssi = T1_f / T2_f * (T3_f + T4_f + T5_f)
                        + T6_f / T7_f * DelClm * T8_f / T9_f;
            }

            // Outer Swi term
            double Swi = 0.0;
            const double T10_f = model->B4SOIoxideTrapDensityA * BOLTZMANN * temp_noise;
            if (weff > 0.0 && nf > 0.0 && Leff > 0.0 && EffFreq > 0.0
                && inst.B4SOInstar > 0.0) {
                const double T11_f = weff * nf * Leff * EffFreq * 1.0e10
                                   * inst.B4SOInstar * inst.B4SOInstar;
                Swi = T10_f / T11_f * inst.B4SOIcd * inst.B4SOIcd;
            }

            const double T1_tot = Swi + Ssi;
            if (T1_tot > 0.0)
                flicker_noise = m * (Ssi * Swi) / T1_tot;
            break;
          }
          default:
            break;
        }

        if (flicker_noise > 0.0)
            sources.push_back({dp_neo, sp_neo, flicker_noise});
    }

    // -----------------------------------------------------------------------
    // 7. Floating body shot noise (source/drain junctions)
    // -----------------------------------------------------------------------
    {
        const double two_q = 2.0 * CHARGE_Q;
        const double noif = model->B4SOInoif;
        // Source junction: noif * Ibs
        if (std::fabs(inst.B4SOIibs) > 0.0)
            sources.push_back({sp_neo, b_neo, two_q * noif * std::fabs(inst.B4SOIibs) * m});
        // Drain junction: noif * Ibd
        if (std::fabs(inst.B4SOIibd) > 0.0)
            sources.push_back({dp_neo, b_neo, two_q * noif * std::fabs(inst.B4SOIibd) * m});
    }

    // -----------------------------------------------------------------------
    // 8. Gate tunneling shot noise
    // -----------------------------------------------------------------------
    {
        const double two_q = 2.0 * CHARGE_Q;
        // IGS + IGCS (gate to source)
        const double Igs_total = std::fabs(inst.B4SOIIgs + inst.B4SOIIgcs);
        if (Igs_total > 0.0)
            sources.push_back({g_neo, sp_neo, two_q * Igs_total * m});
        // IGD + IGCD (gate to drain)
        const double Igd_total = std::fabs(inst.B4SOIIgd + inst.B4SOIIgcd);
        if (Igd_total > 0.0)
            sources.push_back({g_neo, dp_neo, two_q * Igd_total * m});
        // IGB (gate to bulk)
        if (std::fabs(inst.B4SOIig) > 0.0)
            sources.push_back({g_neo, b_neo, two_q * std::fabs(inst.B4SOIig) * m});
    }

    return sources;
}

// ---------------------------------------------------------------------------
// set_ic — store instance initial conditions from M-card ic= parameter
// ---------------------------------------------------------------------------
void B4SOIDevice::set_ic(double vds, bool vds_given,
                          double vgs, bool vgs_given,
                          double vbs, bool vbs_given) {
    if (vds_given) { inst_.B4SOIicVDS = vds; inst_.B4SOIicVDSGiven = 1; }
    if (vgs_given) { inst_.B4SOIicVGS = vgs; inst_.B4SOIicVGSGiven = 1; }
    if (vbs_given) { inst_.B4SOIicVBS = vbs; inst_.B4SOIicVBSGiven = 1; }
}

} // namespace neospice
