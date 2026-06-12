#include "core/node_classify.hpp"
#include "core/circuit.hpp"
#include "devices/vsource.hpp"
#include <vector>

namespace neospice {

// A node tied through a DC independent voltage source to ground is at the
// source's rail voltage; a node tied via a DC source to an already-seeded node
// is at that node's voltage plus/minus the source value.  We discover these by
// iterating the voltage-source list to a fixed point: each pass propagates a
// freshly-seeded neighbour one source-hop further.  The graph of ideal voltage
// sources is shallow in practice (rails sit one hop from ground), so a small
// bounded number of passes settles it.
//
// Constraint relation for a DC source between (np, nn):
//     V(np) - V(nn) = dc_value
// With ground (GROUND_INTERNAL = -1) pinned at 0 V.
std::vector<double> compute_initial_guess(const Circuit& ckt) {
    const int32_t n = ckt.num_vars();
    std::vector<double> guess(n, 0.0);
    if (n <= 0) return guess;

    // Which node variables already have a committed seed value.
    std::vector<char> seeded(n, 0);

    // Honour pins first so we never override .nodeset/.ic and so they act as
    // anchors that rail propagation can build upon.  .nodeset wins over .ic.
    std::vector<char> pinned(n, 0);
    for (const auto& [node_id, value] : ckt.ic) {
        int32_t idx = static_cast<int32_t>(node_id);
        if (idx >= 0 && idx < n) {
            guess[idx] = value;
            seeded[idx] = 1;
            pinned[idx] = 1;
        }
    }
    for (const auto& [node_id, value] : ckt.nodeset) {
        int32_t idx = static_cast<int32_t>(node_id);
        if (idx >= 0 && idx < n) {
            guess[idx] = value;
            seeded[idx] = 1;
            pinned[idx] = 1;
        }
    }

    // Collect DC voltage sources up front (cheap; avoids repeated dynamic_cast).
    struct VSrc { int32_t np; int32_t nn; double dc; };
    std::vector<VSrc> vsrcs;
    for (const auto& dev : ckt.devices()) {
        const auto* vs = dynamic_cast<const VSource*>(dev.get());
        if (!vs) continue;
        // Only anchor on sources whose DC operating-point value is meaningful:
        // a plain DC source, or any source that declared an explicit DC value.
        if (vs->source_function() != SourceFunction::DC && !vs->dc_given())
            continue;
        vsrcs.push_back({vs->pos_node(), vs->neg_node(), vs->dc_value()});
    }

    // Treat ground as a known-0 anchor: a value() lambda returns the current
    // best estimate for any node, with ground == 0.
    auto value_of = [&](int32_t idx) -> double {
        if (idx < 0) return 0.0;          // ground
        return guess[idx];
    };
    auto is_known = [&](int32_t idx) -> bool {
        if (idx < 0) return true;         // ground is always known
        return seeded[idx] != 0;
    };

    // Fixed-point relaxation.  Bounded by the number of sources: every pass that
    // does no work terminates the loop; otherwise at least one node gets seeded,
    // so at most vsrcs.size() passes are ever needed.
    bool progress = true;
    std::size_t passes = 0;
    const std::size_t max_passes = vsrcs.size() + 1;
    while (progress && passes < max_passes) {
        progress = false;
        ++passes;
        for (const auto& s : vsrcs) {
            const bool np_known = is_known(s.np);
            const bool nn_known = is_known(s.nn);
            // Exactly one endpoint known -> propagate to the other.
            // V(np) - V(nn) = dc.
            if (np_known && !nn_known && s.nn >= 0 && !pinned[s.nn]) {
                guess[s.nn] = value_of(s.np) - s.dc;
                seeded[s.nn] = 1;
                progress = true;
            } else if (nn_known && !np_known && s.np >= 0 && !pinned[s.np]) {
                guess[s.np] = value_of(s.nn) + s.dc;
                seeded[s.np] = 1;
                progress = true;
            }
        }
    }

    return guess;
}

} // namespace neospice
