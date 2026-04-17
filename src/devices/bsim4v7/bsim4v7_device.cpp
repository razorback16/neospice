#include "devices/bsim4v7/bsim4v7_device.hpp"

#include "core/circuit.hpp"        // for tls_integrator_ctx / IntegratorCtx
#include "core/types.hpp"          // SimOptions defaults

#include <cstdlib>                 // std::free for pSizeDependParamKnot
#include <stdexcept>
#include <string>

// Forward declaration for BSIM4load — bsim4v7_load.cpp defines it but
// bsim4v7_def.hpp does not list it alongside BSIM4setup / BSIM4temp.
namespace neospice::bsim4v7 {
    int BSIM4load(BSIM4v7Model* model, Shim::Ckt* ckt);
}

namespace neospice {

using namespace neospice::bsim4v7;

// ---------------------------------------------------------------------------
// BSIM4v7ModelCard destructor — free the pSizeDependParamKnot linked list
// that BSIM4temp() mallocs (one knot per distinct (L,W,NF) tuple across
// instances).  BSIM4v7Model is an aggregate so it can't own an RAII handle
// itself; the card wraps it and does the cleanup.
// ---------------------------------------------------------------------------
BSIM4v7ModelCard::~BSIM4v7ModelCard() {
    auto* p = ucb.pSizeDependParamKnot;
    while (p) {
        auto* next = p->pNext;
        std::free(p);
        p = next;
    }
    ucb.pSizeDependParamKnot = nullptr;
}

// ---------------------------------------------------------------------------
// Neospice→UCB node translation.
// Neospice uses GROUND_INTERNAL = -1 for ground and consecutive non-negative
// indices for real nodes.  UCB uses 0 for ground and >=1 for real nodes.
// ---------------------------------------------------------------------------
static inline int neo_to_ucb(int32_t neo) {
    return (neo < 0) ? 0 : (neo + 1);
}

// ---------------------------------------------------------------------------
// make — initialise a BSIM4v7Instance, link it into the model's instance
// chain, and copy geometry fields. Node indices are stored in UCB form so
// BSIM4setup/temp/load all see consistent coordinates.
// ---------------------------------------------------------------------------
std::unique_ptr<BSIM4v7Device>
BSIM4v7Device::make(std::string name,
                    int32_t nd, int32_t ng, int32_t ns, int32_t nb,
                    const Geom& geom, BSIM4v7ModelCard& shared_card) {
    std::unique_ptr<BSIM4v7Device> dev(new BSIM4v7Device(std::move(name)));
    dev->model_ = &shared_card.ucb;

    // Our translated kernel is BSIM4 v4.7.0. bsim4v7_setup.cpp defaults an
    // un-given BSIM4version to "4.6.0" and bsim4v7_check.cpp warns when the
    // translated code disagrees. Stamp the real version here (parser will
    // do this properly in T8 when it knows the .model source line).
    if (!shared_card.ucb.BSIM4versionGiven) {
        shared_card.ucb.BSIM4version = "4.7.0";
        shared_card.ucb.BSIM4versionGiven = 1;
    }

    auto& inst = dev->inst_;
    inst.BSIM4name     = dev->name().c_str();
    inst.BSIM4modPtr   = dev->model_;

    // Node wiring (UCB convention).
    inst.BSIM4dNode    = neo_to_ucb(nd);
    inst.BSIM4gNodeExt = neo_to_ucb(ng);
    inst.BSIM4sNode    = neo_to_ucb(ns);
    inst.BSIM4bNode    = neo_to_ucb(nb);
    // *NodePrime fields are zero-initialized and will be assigned by
    // BSIM4setup (== node or an internal allocation via add_internal_node).
    // dbNode/sbNode/qNode likewise get their values from setup.

    // Geometry.  Mark each as Given so BSIM4setup/temp honour the passed
    // value instead of falling back to the model default.
    inst.BSIM4w            = geom.W;  inst.BSIM4wGiven            = 1;
    inst.BSIM4l            = geom.L;  inst.BSIM4lGiven            = 1;
    inst.BSIM4nf           = geom.NF; inst.BSIM4nfGiven           = 1;
    inst.BSIM4drainArea       = geom.AD; inst.BSIM4drainAreaGiven       = (geom.AD != 0.0);
    inst.BSIM4sourceArea      = geom.AS; inst.BSIM4sourceAreaGiven      = (geom.AS != 0.0);
    inst.BSIM4drainPerimeter  = geom.PD; inst.BSIM4drainPerimeterGiven  = (geom.PD != 0.0);
    inst.BSIM4sourcePerimeter = geom.PS; inst.BSIM4sourcePerimeterGiven = (geom.PS != 0.0);
    inst.BSIM4drainSquares    = geom.NRD; inst.BSIM4drainSquaresGiven   = (geom.NRD != 0.0);
    inst.BSIM4sourceSquares   = geom.NRS; inst.BSIM4sourceSquaresGiven  = (geom.NRS != 0.0);
    inst.BSIM4sa = geom.SA; inst.BSIM4saGiven = (geom.SA != 0.0);
    inst.BSIM4sb = geom.SB; inst.BSIM4sbGiven = (geom.SB != 0.0);
    inst.BSIM4sd = geom.SD; inst.BSIM4sdGiven = (geom.SD != 0.0);

    // Thread onto the shared model's instance list (append at head — the
    // UCB setup loop iterates model->BSIM4instances which is fine).
    inst.BSIM4nextInstance = shared_card.ucb.BSIM4instances;
    shared_card.ucb.BSIM4instances = &inst;

    // Remember the widest real node index we were handed so the ghost
    // rhs/voltage arrays in evaluate() are sized correctly.
    int32_t widest = -1;
    for (int32_t n : {nd, ng, ns, nb}) if (n > widest) widest = n;
    dev->max_neo_node_ = widest;

    return dev;
}

// ---------------------------------------------------------------------------
// stamp_pattern — run BSIM4setup once to populate per-instance pParam and
// to journal every (row, col) reservation.  UCB-convention coords are
// stored in journal_; the corresponding real (row-1, col-1) pairs are
// appended to the caller's real SparsityBuilder.  Ground-touching
// reservations (r==0 || c==0 in UCB form) are deliberately skipped — the
// associated BSIM4*Ptr fields are left at -1 and any stamp into them is
// guarded by an rgateMod/rbodyMod/rdsMod/trnqsMod branch in b4ld.c that
// our simple NMOS case never enters.
// ---------------------------------------------------------------------------
void BSIM4v7Device::stamp_pattern(SparsityBuilder& builder) const {
    // Shim::Matrix wraps a SparsityBuilder reference.  We give it a
    // short-lived *throwaway* builder whose outputs we ignore; the real
    // builder receives a shifted-down-by-1 copy of each non-ground entry.
    SparsityBuilder scratch(1);  // size doesn't matter — we discard its pattern
    Shim::Matrix shim_matrix(scratch);

    // Fresh Shim::Ckt for the setup call — only CKTtemp/CKTnomTemp are
    // read (and CKTinternalNodeCounter for add_internal_node, which our
    // minimal case does not trigger).  BSIM4setup does not consume the
    // user-configured temperature (that happens in BSIM4temp, which the
    // adapter re-runs on the first evaluate() once the real options are
    // published).  T_NOMINAL is a safe placeholder.
    Shim::Ckt setup_ckt;
    setup_ckt.CKTtemp    = T_NOMINAL;
    setup_ckt.CKTnomTemp = T_NOMINAL;
    // Seed above any plausible real-node index.  Phase-1b only supports
    // the intrinsic path (no internal nodes); see the post-setup guard
    // below.  Phase-2 internal-node plumbing is tracked in
    //   docs/superpowers/plans/2026-04-16-milestone4-bsim4-ucb-z-port-phase1b.md
    // under "T7 follow-up / F1 (internal nodes)".  The work spans
    // Circuit (new allocate_internal_node + internal_node_count virtual),
    // Device interface (set_internal_nodes hook), and this adapter
    // (translate UCB 1000+k IDs back to Circuit var indices during stamp
    // and evaluate, and size the ghost arrays to cover the extras).
    setup_ckt.CKTinternalNodeCounter = 1000;

    int states = 0;
    int rc = BSIM4setup(&shim_matrix,
                        const_cast<BSIM4v7Model*>(model_),
                        &setup_ckt, &states);
    if (rc != Shim::OK) {
        throw std::runtime_error("BSIM4setup failed with rc=" + std::to_string(rc));
    }

    // Phase-1b: reject any model card that allocated internal nodes.
    // BSIM4setup only calls add_internal_node when rgateMod/rbodyMod/
    // rdsMod/trnqsMod/rsh are enabled; those paths need Circuit-side
    // wiring we haven't done yet (see F1 blocker and plan reference
    // above).  Without this guard, internal-node IDs (>=1000) leak into
    // the RHS ghost array in evaluate() and index out of bounds.
    if (setup_ckt.CKTinternalNodeCounter != 1000) {
        throw std::runtime_error(
            "BSIM4v7Device: model card allocated internal nodes "
            "(rgateMod/rbodyMod/rdsMod/trnqsMod/rsh). "
            "Phase-1b only supports the intrinsic path; "
            "wire internals through Circuit in Phase-2.");
    }

    // Journal is in UCB coordinates; we copy it out and shift per-entry below.
    const auto& journal = shim_matrix.reservation_journal();
    journal_.assign(journal.begin(), journal.end());

    // Promote non-ground entries to the real builder, shifting from UCB
    // coords (>=1) back to neospice coords (>=0).
    for (auto [r, c] : journal_) {
        if (r <= 0 || c <= 0) continue;   // ground-touching entry — skip
        builder.add(r - 1, c - 1);
    }
}

// ---------------------------------------------------------------------------
// assign_offsets — walk the journal we captured during stamp_pattern and
// rewrite every BSIM4*Ptr field.  The journal index used by BSIM4setup
// became the initial value of the field; we replace it with the real
// MatrixOffset from the finalized pattern.  Ground-touching reservations
// (UCB 0 in either coord) resolve to -1; any stamp into them must be
// suppressed by the load-side branch guards.
// ---------------------------------------------------------------------------
void BSIM4v7Device::assign_offsets(const SparsityPattern& pattern) {
    std::vector<MatrixOffset> offsets(journal_.size(), -1);
    for (std::size_t i = 0; i < journal_.size(); ++i) {
        auto [r, c] = journal_[i];
        if (r <= 0 || c <= 0) continue;   // ground -> -1
        offsets[i] = pattern.offset(r - 1, c - 1);
    }

    // RESOLVE(f): if inst_.f is a valid journal index (>= 0), replace it
    // with the real MatrixOffset from `offsets`.  Ground-touching
    // reservations were already set to -1 by make_elt and stay -1.
#define RESOLVE(f)                                                       \
    do {                                                                 \
        if (inst_.f >= 0)                                                \
            inst_.f = offsets[inst_.f];                                  \
    } while (0)

    // --- Unconditional TSTALLOC sites (see bsim4v7_setup.cpp:2376-2408) ---
    RESOLVE(BSIM4DPbpPtr);  RESOLVE(BSIM4GPbpPtr);  RESOLVE(BSIM4SPbpPtr);
    RESOLVE(BSIM4BPdpPtr);  RESOLVE(BSIM4BPgpPtr);  RESOLVE(BSIM4BPspPtr);
    RESOLVE(BSIM4BPbpPtr);
    RESOLVE(BSIM4DdPtr);    RESOLVE(BSIM4GPgpPtr);  RESOLVE(BSIM4SsPtr);
    RESOLVE(BSIM4DPdpPtr);  RESOLVE(BSIM4SPspPtr);
    RESOLVE(BSIM4DdpPtr);   RESOLVE(BSIM4GPdpPtr);  RESOLVE(BSIM4GPspPtr);
    RESOLVE(BSIM4SspPtr);   RESOLVE(BSIM4DPspPtr);  RESOLVE(BSIM4DPdPtr);
    RESOLVE(BSIM4DPgpPtr);  RESOLVE(BSIM4SPgpPtr);  RESOLVE(BSIM4SPsPtr);
    RESOLVE(BSIM4SPdpPtr);
    RESOLVE(BSIM4QqPtr);    RESOLVE(BSIM4QbpPtr);   RESOLVE(BSIM4QdpPtr);
    RESOLVE(BSIM4QspPtr);   RESOLVE(BSIM4QgpPtr);   RESOLVE(BSIM4DPqPtr);
    RESOLVE(BSIM4SPqPtr);   RESOLVE(BSIM4GPqPtr);

    // --- rgateMod != 0 ---
    RESOLVE(BSIM4GEgePtr);  RESOLVE(BSIM4GEgpPtr);  RESOLVE(BSIM4GPgePtr);
    RESOLVE(BSIM4GEdpPtr);  RESOLVE(BSIM4GEspPtr);  RESOLVE(BSIM4GEbpPtr);
    RESOLVE(BSIM4GMdpPtr);  RESOLVE(BSIM4GMgpPtr);  RESOLVE(BSIM4GMgmPtr);
    RESOLVE(BSIM4GMgePtr);  RESOLVE(BSIM4GMspPtr);  RESOLVE(BSIM4GMbpPtr);
    RESOLVE(BSIM4DPgmPtr);  RESOLVE(BSIM4GPgmPtr);  RESOLVE(BSIM4GEgmPtr);
    RESOLVE(BSIM4SPgmPtr);  RESOLVE(BSIM4BPgmPtr);

    // --- rbodyMod in {1, 2} ---
    RESOLVE(BSIM4DPdbPtr);  RESOLVE(BSIM4SPsbPtr);
    RESOLVE(BSIM4DBdpPtr);  RESOLVE(BSIM4DBdbPtr);  RESOLVE(BSIM4DBbpPtr);
    RESOLVE(BSIM4DBbPtr);
    RESOLVE(BSIM4BPdbPtr);  RESOLVE(BSIM4BPbPtr);   RESOLVE(BSIM4BPsbPtr);
    RESOLVE(BSIM4SBspPtr);  RESOLVE(BSIM4SBbpPtr);  RESOLVE(BSIM4SBbPtr);
    RESOLVE(BSIM4SBsbPtr);
    RESOLVE(BSIM4BdbPtr);   RESOLVE(BSIM4BbpPtr);   RESOLVE(BSIM4BsbPtr);
    RESOLVE(BSIM4BbPtr);

    // --- model->BSIM4rdsMod ---
    RESOLVE(BSIM4DgpPtr);   RESOLVE(BSIM4DspPtr);   RESOLVE(BSIM4DbpPtr);
    RESOLVE(BSIM4SdpPtr);   RESOLVE(BSIM4SgpPtr);   RESOLVE(BSIM4SbpPtr);

#undef RESOLVE

    // Count sanity — 70 RESOLVE() invocations above match the 70
    // TSTALLOC invocation sites in bsim4v7_setup.cpp (verified by
    // `grep -oE 'RESOLVE\(BSIM4' bsim4v7_device.cpp | wc -l` and
    // `grep -cE '^[^/]*\\bTSTALLOC\\(' bsim4v7_setup.cpp` minus the
    // single `#define TSTALLOC` line).  If either number changes, update
    // both or the device will silently leave pointer IDs unresolved.
}

// ---------------------------------------------------------------------------
// set_state_ptrs — cache the Circuit's state-ring pointers and bind
// inst_.BSIM4states to our base offset.  BSIM4load reads/writes state via
// CKTstate0/1/2 + (BSIM4states + local-offset) so the base captured here
// must match the Circuit's ring slotting from finalize()/rotate_state().
// ---------------------------------------------------------------------------
void BSIM4v7Device::set_state_ptrs(double* s0, double* s1, double* s2, int32_t base) {
    state0_ = s0;
    state1_ = s1;
    state2_ = s2;
    state_base_ = base;
    inst_.BSIM4states = base;
}

// ---------------------------------------------------------------------------
// evaluate — the load path.  Builds a Shim::Ckt tied to the Circuit's
// integrator context and state buffers, re-runs BSIM4temp on first call
// (UCB puts all size-dependent preprocessing there), then calls
// BSIM4load to stamp matrix + rhs.
//
// Because BSIM4load indexes rhs via node index (e.g. CKTrhs+dNodePrime),
// we allocate *ghost* rhs/voltage vectors with an extra slot at index 0
// (UCB ground) so it can scribble without undershooting.  After load we
// fold ghost_rhs[i] (i>=1) back into the real rhs[i-1].  Matrix stamps
// go directly to the real NumericMatrix — assign_offsets already
// rewrote every BSIM4*Ptr to a real offset.
// ---------------------------------------------------------------------------
void BSIM4v7Device::evaluate(const std::vector<double>& voltages,
                             NumericMatrix& mat,
                             std::vector<double>& rhs) {
    const int n_real = static_cast<int>(rhs.size());
    const int n_ghost = (max_neo_node_ >= 0 ? max_neo_node_ + 1 : 0) + 1;
    // n_ghost covers UCB indices 0..max_neo_node_+1, which is all real
    // nodes this device can write to.

    // --- Ghost shifted arrays kept as members so the per-evaluate() cost
    // stays at O(n_ghost) assignment instead of 2x heap alloc/free.  The
    // size is a function of max_neo_node_ (stable for a device's lifetime);
    // the assign(n_ghost, 0.0) both resizes on first call and zeroes on
    // every subsequent call without releasing storage.
    ghost_voltages_.assign(n_ghost, 0.0);
    ghost_rhs_.assign(n_ghost, 0.0);
    for (int32_t k = 0; k <= max_neo_node_ && k < n_real; ++k) {
        ghost_voltages_[k + 1] = voltages[k];
    }

    Shim::Ckt ckt;

    // Integrator context — read from the thread-local set by newton_solve.
    // If absent (caller forgot the guard), fall back to DC op defaults so
    // we at least give BSIM4load consistent bits.
    const IntegratorCtx* ic = tls_integrator_ctx;
    if (ic) {
        ckt.CKTmode  = ic->mode;
        ckt.CKTorder = ic->order;
        ckt.CKTdelta = ic->delta;
        for (int i = 0; i < 8; ++i) ckt.CKTag[i]       = ic->ag[i];
        for (int i = 0; i < 8; ++i) ckt.CKTdeltaOld[i] = ic->delta_old[i];
    } else {
        ckt.CKTmode  = 0x70 | 0x200;  // MODEDC | MODEINITJCT
        ckt.CKTorder = 1;
    }

    // Pull user-configured SimOptions (temp + tolerances) off the
    // IntegratorCtx where dc.cpp / transient.cpp published them.  Fall
    // back to T_NOMINAL and library defaults if no driver is live (e.g.
    // in a direct unit-test call).
    const SimOptions* sim_opts = (ic ? ic->options : nullptr);
    SimOptions fallback;
    if (!sim_opts) sim_opts = &fallback;
    ckt.CKTtemp    = sim_opts->temp;
    ckt.CKTnomTemp = sim_opts->temp;   // nom/temp not separated in neospice yet
    ckt.CKTgmin    = sim_opts->gmin;
    ckt.CKTreltol  = sim_opts->reltol;
    ckt.CKTabstol  = sim_opts->abstol;
    ckt.CKTvoltTol = sim_opts->vntol;
    ckt.CKTbypass  = 0;   // no bypass in Phase-1b
    ckt.CKTnoncon  = 0;

    // State ring.
    ckt.CKTstate0 = state0_;
    ckt.CKTstate1 = state1_;
    ckt.CKTstate2 = state2_;

    // Ghost rhs / old-iterate pointers.  const_cast because BSIM4load
    // reads CKTrhsOld as plain double* even though it only reads.
    ckt.CKTrhs    = ghost_rhs_.data();
    ckt.CKTrhsOld = ghost_voltages_.data();
    ckt.mat       = &mat;

    // First-call BSIM4temp (UCB preprocesses temperature-dependent
    // size parameters here; pParam is allocated in BSIM4setup but filled
    // in BSIM4temp).
    if (!temp_done_) {
        int rc = BSIM4temp(model_, &ckt);
        if (rc != Shim::OK) {
            throw std::runtime_error(
                "BSIM4temp failed with rc=" + std::to_string(rc));
        }
        temp_done_ = true;
    }

    int rc = BSIM4load(model_, &ckt);
    if (rc != Shim::OK) {
        throw std::runtime_error(
            "BSIM4load failed with rc=" + std::to_string(rc));
    }

    // Fold ghost rhs contributions (indices 1..max_neo_node_+1) back into
    // the real rhs (indices 0..max_neo_node_).  Ghost index 0 (UCB
    // ground) is discarded — BSIM4 only writes there when a ground node
    // is specified, which happens only via guarded branches for our
    // simple setup.  If a guard ever leaks, it lands in ghost[0] and is
    // silently dropped (matching ngspice's behaviour where ground-row
    // writes are no-ops in the solver).
    for (int32_t k = 1; k <= max_neo_node_ + 1 && k < n_ghost; ++k) {
        if (k - 1 < n_real) rhs[k - 1] += ghost_rhs_[k];
    }
}

} // namespace neospice
