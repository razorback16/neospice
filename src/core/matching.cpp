/**********
Maximum bipartite matching (maximum transversal) following
SuiteSparse BTF (Timothy A. Davis, BSD-3-Clause).
See NOTICE and CREDITS.md.
**********/

#include "core/matching.hpp"
#include <algorithm>
#include <queue>

namespace neospice {

std::vector<int32_t> maximum_transversal(int32_t n, const int32_t* col_ptr,
                                          const int32_t* row_idx) {
    // match_col[j] = row assigned to column j (-1 = unmatched)
    std::vector<int32_t> match_col(n, -1);
    // match_row[i] = column assigned to row i (-1 = unmatched)
    std::vector<int32_t> match_row(n, -1);

    if (n == 0) return match_col;

    // Phase 1: Cheap initialization — match each column to its diagonal row
    // when the structural entry exists. This preserves identity permutation
    // for well-structured matrices.
    for (int32_t j = 0; j < n; ++j) {
        for (int32_t p = col_ptr[j]; p < col_ptr[j + 1]; ++p) {
            if (row_idx[p] == j) {
                if (match_row[j] < 0) {
                    match_col[j] = j;
                    match_row[j] = j;
                }
                break;
            }
        }
    }

    // Phase 2: Cheap off-diagonal matching — for unmatched columns, grab
    // the first available row.
    for (int32_t j = 0; j < n; ++j) {
        if (match_col[j] >= 0) continue;
        for (int32_t p = col_ptr[j]; p < col_ptr[j + 1]; ++p) {
            int32_t i = row_idx[p];
            if (match_row[i] < 0) {
                match_col[j] = i;
                match_row[i] = j;
                break;
            }
        }
    }

    // Phase 3: BFS augmenting paths for remaining unmatched columns.
    //
    // BFS explores an alternating tree from an unmatched column:
    //   column -> (free edge) -> row -> (matched edge) -> column -> ...
    // When we reach a free (unmatched) row, we augment along the path.
    //
    // row_from[i]: the column that reached row i via a free edge in the BFS.
    // Used to trace back the augmenting path.
    std::vector<int32_t> row_from(n);
    std::queue<int32_t> bfs_queue;

    for (int32_t start_col = 0; start_col < n; ++start_col) {
        if (match_col[start_col] >= 0) continue;

        std::fill(row_from.begin(), row_from.end(), -1);
        while (!bfs_queue.empty()) bfs_queue.pop();
        bfs_queue.push(start_col);

        int32_t free_row = -1;

        while (!bfs_queue.empty()) {
            int32_t j = bfs_queue.front();
            bfs_queue.pop();

            for (int32_t p = col_ptr[j]; p < col_ptr[j + 1]; ++p) {
                int32_t i = row_idx[p];
                if (row_from[i] >= 0) continue;  // already visited
                row_from[i] = j;

                if (match_row[i] < 0) {
                    free_row = i;
                    break;
                }
                // Row i is matched — continue BFS from its matched column
                bfs_queue.push(match_row[i]);
            }
            if (free_row >= 0) break;
        }

        if (free_row < 0) continue;  // structurally singular

        // Augment along the alternating path: free_row -> row_from -> ...
        int32_t i = free_row;
        while (i >= 0) {
            int32_t j = row_from[i];
            int32_t prev_row = match_col[j];  // row previously matched to j (-1 for start_col)
            match_col[j] = i;
            match_row[i] = j;
            i = prev_row;
        }
    }

    return match_col;
}

}  // namespace neospice
