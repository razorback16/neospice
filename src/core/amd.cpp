#include "core/amd.hpp"
#include <algorithm>
#include <numeric>
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

    std::vector<bool> eliminated(n, false);
    std::vector<int32_t> perm;
    perm.reserve(n);

    for (int32_t step = 0; step < n; ++step) {
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

    return perm;
}

}  // namespace neospice
