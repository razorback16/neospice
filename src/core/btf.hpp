#pragma once
#include <cstdint>
#include <vector>

namespace neospice {

struct BTFResult {
    int32_t nblocks = 0;                // number of diagonal blocks
    std::vector<int32_t> perm;          // perm[new] = old
    std::vector<int32_t> inv_perm;      // inv_perm[old] = new
    std::vector<int32_t> block_ptr;     // block k spans permuted rows [block_ptr[k], block_ptr[k+1])
};

// Decompose a square matrix into Block Triangular Form using Tarjan's SCC algorithm.
// Input: CSC matrix (col_ptr[n+1], row_idx[nnz]).
// For n=0 returns empty result. For structurally irreducible matrices, returns a single block.
BTFResult btf_decompose(int32_t n, const int32_t* col_ptr, const int32_t* row_idx);

}  // namespace neospice
