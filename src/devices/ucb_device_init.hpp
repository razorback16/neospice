#pragma once
// Shared template helpers for UCB device initialization:
//   declare_internal_nodes, stamp_pattern, assign_offsets.
//
// These factor out the identical boilerplate from all 16 UCB device adapters.

#include "core/circuit.hpp"
#include "core/matrix.hpp"
#include "core/types.hpp"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace neospice {

// ---------------------------------------------------------------------------
// UCB_SPLICE_INSTANCE / UCB_UNSPLICE_INSTANCE
//
// Save and restore linked-list pointers on the UCB model/instance so that
// a UCB entry-point (setup, load, temp) processes only one instance.
//
// Usage inside a function body where `model_` and `inst_` are in scope:
//
//   UCB_SPLICE_INSTANCE(BJT);
//   int rc = BJTsetup(&shim_matrix, model_, &ckt, &states);
//   UCB_UNSPLICE_INSTANCE(BJT);
//
// Fields accessed:  model_->PREFIXinstances   (Instance* head)
//                   inst_.PREFIXnextInstance   (Instance* next)
//                   model_->PREFIXnextModel    (Model*   next)
// ---------------------------------------------------------------------------
#define UCB_SPLICE_INSTANCE(PREFIX)                                          \
    auto* ucb_saved_head_      = model_->PREFIX##instances;                  \
    auto* ucb_saved_next_inst_ = inst_.PREFIX##nextInstance;                 \
    auto* ucb_saved_next_mod_  = model_->PREFIX##nextModel;                 \
    model_->PREFIX##instances  = &inst_;                                     \
    inst_.PREFIX##nextInstance = nullptr;                                    \
    model_->PREFIX##nextModel  = nullptr

#define UCB_UNSPLICE_INSTANCE(PREFIX)                                        \
    model_->PREFIX##instances  = ucb_saved_head_;                            \
    inst_.PREFIX##nextInstance = ucb_saved_next_inst_;                       \
    model_->PREFIX##nextModel  = ucb_saved_next_mod_

// ---------------------------------------------------------------------------
// ucb_declare_internal_nodes — run a UCB setup callable to discover
// internal node allocations and matrix reservation entries.
//
// Template parameters:
//   ShimMatrix — device-specific Shim::Matrix (e.g. neospice::bjt::Shim::Matrix)
//   ShimCkt    — device-specific Shim::Ckt
//   SetupFn    — callable: int(ShimMatrix&, ShimCkt&)
//                The caller is responsible for capturing model/instance and
//                performing the linked-list splice inside this callable.
//
// Outputs are written to `journal` and `max_neo_node`.
// ---------------------------------------------------------------------------
template <typename ShimMatrix, typename ShimCkt, typename SetupFn>
void ucb_declare_internal_nodes(
    Circuit& ckt,
    const std::string& device_name,
    SetupFn setup_fn,
    const char* setup_fn_name,
    std::vector<std::pair<int,int>>& journal,
    int32_t& max_neo_node)
{
    SparsityBuilder scratch(1);
    ShimMatrix shim_matrix(scratch);

    ShimCkt setup_ckt;
    setup_ckt.CKTtemp    = T_NOMINAL;
    setup_ckt.CKTnomTemp = T_NOMINAL;
    setup_ckt.CKTinternalNodeCounter = 1000;

    setup_ckt.node_alloc = [&ckt, &device_name](const char* name) -> int {
        std::string full = "__" + device_name + "_" + name;
        int32_t neo = ckt.node(full);
        ckt.mark_internal_node(neo);
        return neo + 1;  // UCB convention: ground=0, real>=1
    };

    int rc = setup_fn(shim_matrix, setup_ckt);
    if (rc != 0) {  // Shim::OK == 0 in all device namespaces
        throw std::runtime_error(
            std::string(setup_fn_name) + " failed with rc=" + std::to_string(rc));
    }

    const auto& raw = shim_matrix.reservation_journal();
    journal.assign(raw.begin(), raw.end());

    // Recompute max_neo_node to cover internal nodes.
    for (auto [r, c] : journal) {
        int mx = std::max(r, c);
        if (mx > 0) {
            int32_t neo = mx - 1;
            if (neo > max_neo_node) max_neo_node = neo;
        }
    }
}

// ---------------------------------------------------------------------------
// ucb_stamp_pattern — emit sparsity entries from the reservation journal.
// ---------------------------------------------------------------------------
inline void ucb_stamp_pattern(const std::vector<std::pair<int,int>>& journal,
                              SparsityBuilder& builder) {
    for (auto [r, c] : journal) {
        if (r <= 0 || c <= 0) continue;
        builder.add(r - 1, c - 1);
    }
}

// ---------------------------------------------------------------------------
// ucb_compute_offsets — translate journal indices to matrix offsets.
// Returns the offset vector for use with per-device RESOLVE() macros.
// ---------------------------------------------------------------------------
inline std::vector<MatrixOffset> ucb_compute_offsets(
    const std::vector<std::pair<int,int>>& journal,
    const SparsityPattern& pattern)
{
    std::vector<MatrixOffset> offsets(journal.size(), -1);
    for (std::size_t i = 0; i < journal.size(); ++i) {
        auto [r, c] = journal[i];
        if (r <= 0 || c <= 0) continue;
        offsets[i] = pattern.offset(r - 1, c - 1);
    }
    return offsets;
}

} // namespace neospice
