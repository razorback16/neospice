#pragma once
// Adapter bridging the neospice Device interface to the UCB BSIM4v7 code.
//
// BSIM4v7Device owns exactly one BSIM4v7Instance and a non-owning pointer
// to a shared BSIM4v7ModelCard (one card per .model directive; many
// instances can reference the same card).  It is the ONLY file in
// src/devices/bsim4v7/ that is not governed by the Phase-1a mechanical
// translation rules — everything else is UCB-shaped code preserved
// verbatim (or lightly shimmed).
//
// Node-index convention: neospice uses GROUND_INTERNAL = -1 for ground
// and >=0 for real nodes.  UCB BSIM4 uses 0 for ground and >=1 for real
// nodes.  The adapter translates (ucb = neo + 1; ground stays 0) before
// calling BSIM4setup/temp/load, and translates back via journal walk
// when resolving MatrixOffsets.  See bsim4v7_device.cpp for the details.

#include "devices/device.hpp"
#include "devices/bsim4v7/bsim4v7_def.hpp"
#include "devices/bsim4v7/bsim4v7_shim.hpp"
#include <memory>
#include <utility>
#include <vector>

namespace neospice {

// One .model card.  The shared_ptr lives in the parser's symbol table so
// many BSIM4v7Device instances can reference the same underlying UCB
// BSIM4v7Model.  BSIM4v7Model's instance list (BSIM4instances) is
// threaded per-instance by make() — we do not share instance lists.
struct BSIM4v7ModelCard {
    bsim4v7::BSIM4v7Model ucb{};   // aggregate UCB model fields

    BSIM4v7ModelCard() = default;
    ~BSIM4v7ModelCard();

    // Non-copyable / non-movable: BSIM4v7Model is threaded with raw
    // pointers (BSIM4instances, pSizeDependParamKnot); copying would
    // alias them.  Instance-sharing is accomplished via reference to a
    // stable card stored elsewhere (parser symbol table).
    BSIM4v7ModelCard(const BSIM4v7ModelCard&)            = delete;
    BSIM4v7ModelCard& operator=(const BSIM4v7ModelCard&) = delete;
};

class BSIM4v7Device : public Device {
public:
    // Instance geometry parameters (unused fields are harmless; UCB
    // treats unset = use default).
    struct Geom {
        double W   = 1e-6;
        double L   = 1e-7;
        double NF  = 1.0;
        double AD  = 0.0, AS = 0.0, PD = 0.0, PS = 0.0;
        double NRD = 0.0, NRS = 0.0;
        double SA  = 0.0, SB = 0.0, SD = 0.0;
    };

    // Factory.  nd/ng/ns/nb are neospice node indices (GROUND_INTERNAL=-1).
    // The shared_card must outlive the returned device (the parser's
    // model-symbol table owns it).
    static std::unique_ptr<BSIM4v7Device> make(
        std::string name, int32_t nd, int32_t ng, int32_t ns, int32_t nb,
        const Geom& geom, BSIM4v7ModelCard& shared_card);

    // Device interface
    void stamp_pattern(SparsityBuilder& builder) const override;
    void assign_offsets(const SparsityPattern& pattern) override;
    void evaluate(const std::vector<double>& voltages,
                  NumericMatrix& mat, std::vector<double>& rhs) override;

    int32_t state_vars() const override { return 29; /* BSIM4numStates */ }
    void set_state_ptrs(double* s0, double* s1, double* s2, int32_t base) override;

private:
    explicit BSIM4v7Device(std::string name) : Device(std::move(name)) {}

    // mutable: BSIM4setup writes through stamp_pattern (const) by design.
    mutable bsim4v7::BSIM4v7Instance inst_{};
    bsim4v7::BSIM4v7Model*           model_ = nullptr;

    // Journal of (row, col) reservations in UCB-convention coords (0 =
    // ground, >=1 = real).  Populated by stamp_pattern via a private
    // Shim::Matrix wrapping the caller's SparsityBuilder; we also walk
    // the journal in assign_offsets to rewrite each BSIM4*Ptr field.
    mutable std::vector<std::pair<int,int>> journal_;

    // State-ring ptrs cached by set_state_ptrs (rebound on rotate_state).
    double* state0_ = nullptr;
    double* state1_ = nullptr;
    double* state2_ = nullptr;
    int32_t state_base_ = -1;

    // One-shot flag guarding the first-call BSIM4temp() invocation.
    mutable bool temp_done_ = false;

    // Cached neospice node indices (so evaluate() can size the ghost rhs/
    // voltage arrays without re-interrogating the instance).
    int32_t max_neo_node_ = -1;
};

} // namespace neospice
