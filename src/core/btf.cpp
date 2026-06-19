/**********
Block triangular form / maximum-transversal logic traces to
SuiteSparse BTF by Timothy A. Davis (BSD-3-Clause).
See NOTICE and CREDITS.md.
**********/

#include "core/btf.hpp"
#include <algorithm>
#include <numeric>
#include <stack>

namespace neospice {

BTFResult btf_decompose(int32_t n, const int32_t* col_ptr, const int32_t* row_idx) {
    BTFResult result;
    result.perm.resize(n);
    result.inv_perm.resize(n);

    if (n <= 1) {
        std::iota(result.perm.begin(), result.perm.end(), 0);
        std::iota(result.inv_perm.begin(), result.inv_perm.end(), 0);
        result.nblocks = n;
        result.block_ptr.resize(n + 1);
        std::iota(result.block_ptr.begin(), result.block_ptr.end(), 0);
        return result;
    }

    // Tarjan's SCC algorithm
    std::vector<int32_t> index(n, -1);
    std::vector<int32_t> lowlink(n, 0);
    std::vector<bool> on_stack(n, false);
    std::stack<int32_t> stk;
    std::vector<std::vector<int32_t>> sccs;
    int32_t idx_counter = 0;

    // Iterative Tarjan to avoid stack overflow on large graphs
    struct Frame {
        int32_t node;
        int32_t col_pos;  // position in column iteration
    };

    for (int32_t start = 0; start < n; ++start) {
        if (index[start] >= 0) continue;

        std::stack<Frame> call_stack;
        call_stack.push({start, col_ptr[start]});
        index[start] = lowlink[start] = idx_counter++;
        on_stack[start] = true;
        stk.push(start);

        while (!call_stack.empty()) {
            auto& frame = call_stack.top();
            int32_t v = frame.node;
            bool pushed_child = false;

            while (frame.col_pos < col_ptr[v + 1]) {
                int32_t w = row_idx[frame.col_pos];
                ++frame.col_pos;
                if (w == v) continue;  // skip self-loop
                if (index[w] < 0) {
                    index[w] = lowlink[w] = idx_counter++;
                    on_stack[w] = true;
                    stk.push(w);
                    call_stack.push({w, col_ptr[w]});
                    pushed_child = true;
                    break;
                } else if (on_stack[w]) {
                    lowlink[v] = std::min(lowlink[v], index[w]);
                }
            }

            if (!pushed_child) {
                if (lowlink[v] == index[v]) {
                    std::vector<int32_t> scc;
                    int32_t w;
                    do {
                        w = stk.top(); stk.pop();
                        on_stack[w] = false;
                        scc.push_back(w);
                    } while (w != v);
                    sccs.push_back(std::move(scc));
                }
                call_stack.pop();
                if (!call_stack.empty()) {
                    lowlink[call_stack.top().node] =
                        std::min(lowlink[call_stack.top().node], lowlink[v]);
                }
            }
        }
    }

    // SCCs come out in reverse topological order from Tarjan's.
    // Reverse to get topological order (blocks processed bottom-up).
    std::reverse(sccs.begin(), sccs.end());

    // Build permutation: blocks in topological order
    result.nblocks = static_cast<int32_t>(sccs.size());
    result.block_ptr.resize(result.nblocks + 1);
    int32_t pos = 0;
    for (int32_t b = 0; b < result.nblocks; ++b) {
        result.block_ptr[b] = pos;
        std::sort(sccs[b].begin(), sccs[b].end());
        for (int32_t node : sccs[b]) {
            result.perm[pos] = node;
            result.inv_perm[node] = pos;
            ++pos;
        }
    }
    result.block_ptr[result.nblocks] = n;

    return result;
}

}  // namespace neospice
