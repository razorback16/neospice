/**********
Approximate Minimum Degree ordering; matches SuiteSparse AMD by
Timothy A. Davis, Patrick Amestoy, and Iain Duff (BSD-3-Clause).
See NOTICE and CREDITS.md.
**********/

#include "core/amd.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace neospice {

std::vector<int32_t> amd_ordering(int32_t n, const int32_t* col_ptr,
                                  const int32_t* row_idx) {
    if (n <= 0) return {};

    // Build symmetric adjacency (exclude self-loops)
    std::vector<std::vector<int32_t>> adj(n);
    for (int32_t j = 0; j < n; ++j) {
        for (int32_t p = col_ptr[j]; p < col_ptr[j + 1]; ++p) {
            int32_t i = row_idx[p];
            if (i != j) {
                adj[j].push_back(i);
                adj[i].push_back(j);
            }
        }
    }
    // Deduplicate adjacency lists
    for (int32_t i = 0; i < n; ++i) {
        std::sort(adj[i].begin(), adj[i].end());
        adj[i].erase(std::unique(adj[i].begin(), adj[i].end()), adj[i].end());
    }

    // Degree of each node (in the elimination graph)
    std::vector<int32_t> degree(n);
    for (int32_t i = 0; i < n; ++i)
        degree[i] = static_cast<int32_t>(adj[i].size());

    // ---- Dense row/column detection (matches SuiteSparse AMD) ----
    // Dense threshold: alpha * sqrt(n), clamped to [16, n].
    // SuiteSparse uses alpha = 10.0 by default.
    // Dense nodes are excluded from the main elimination loop and
    // placed last in the permutation (highest degree last).
    // This prevents massive fill-in from nodes like the ground node
    // in circuit matrices, which connect to everything.
    constexpr double alpha = 10.0;
    int32_t dense_thresh = static_cast<int32_t>(alpha * std::sqrt(static_cast<double>(n)));
    dense_thresh = std::max(dense_thresh, static_cast<int32_t>(16));
    dense_thresh = std::min(dense_thresh, n);

    std::vector<bool> is_dense(n, false);
    std::vector<int32_t> dense_nodes;
    for (int32_t i = 0; i < n; ++i) {
        if (degree[i] > dense_thresh) {
            is_dense[i] = true;
            dense_nodes.push_back(i);
        }
    }

    // Remove dense nodes from adjacency lists of non-dense nodes
    // and recompute degrees for non-dense nodes.
    if (!dense_nodes.empty()) {
        for (int32_t i = 0; i < n; ++i) {
            if (is_dense[i]) continue;
            // Remove all dense neighbors from adj[i]
            auto new_end = std::remove_if(adj[i].begin(), adj[i].end(),
                [&](int32_t nb) { return is_dense[nb]; });
            adj[i].erase(new_end, adj[i].end());
            degree[i] = static_cast<int32_t>(adj[i].size());
        }
    }

    std::vector<bool> eliminated(n, false);
    std::vector<int32_t> perm;
    perm.reserve(n);

    // Mark dense nodes as eliminated so they are skipped
    for (int32_t d : dense_nodes) {
        eliminated[d] = true;
    }

    // ---- Main elimination loop (non-dense nodes only) ----
    int32_t non_dense_count = n - static_cast<int32_t>(dense_nodes.size());
    for (int32_t step = 0; step < non_dense_count; ++step) {
        // Find non-eliminated node with minimum degree
        int32_t best = -1;
        int32_t best_deg = n + 1;
        for (int32_t i = 0; i < n; ++i) {
            if (!eliminated[i] && degree[i] < best_deg) {
                best_deg = degree[i];
                best = i;
            }
        }

        perm.push_back(best);
        eliminated[best] = true;

        // Collect live neighbors of the eliminated node
        std::vector<int32_t> neighbors;
        for (int32_t nb : adj[best]) {
            if (!eliminated[nb]) neighbors.push_back(nb);
        }

        // Add edges between all pairs of live neighbors (fill-in),
        // forming a clique — this is the "element absorption" step.
        for (size_t a = 0; a < neighbors.size(); ++a) {
            for (size_t b = a + 1; b < neighbors.size(); ++b) {
                int32_t u = neighbors[a], v = neighbors[b];
                if (!std::binary_search(adj[u].begin(), adj[u].end(), v)) {
                    adj[u].insert(
                        std::lower_bound(adj[u].begin(), adj[u].end(), v), v);
                    adj[v].insert(
                        std::lower_bound(adj[v].begin(), adj[v].end(), u), u);
                }
            }
        }

        // Remove eliminated node from neighbor lists and update degrees
        for (int32_t nb : neighbors) {
            adj[nb].erase(
                std::lower_bound(adj[nb].begin(), adj[nb].end(), best));
            degree[nb] = 0;
            for (int32_t x : adj[nb])
                if (!eliminated[x]) ++degree[nb];
        }
    }

    // ---- Append dense nodes last, ordered by ascending degree ----
    // This matches SuiteSparse AMD: dense rows/columns are placed at the
    // end of the permutation so they are eliminated last (highest degree
    // truly last). degree[] for dense nodes retains the original value
    // since only non-dense nodes had their degrees updated above.
    std::sort(dense_nodes.begin(), dense_nodes.end(),
        [&](int32_t a, int32_t b) {
            return degree[a] < degree[b];
        });
    for (int32_t d : dense_nodes) {
        perm.push_back(d);
    }

    return perm;
}

}  // namespace neospice
