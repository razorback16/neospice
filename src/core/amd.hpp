#pragma once
#include <cstdint>
#include <vector>

namespace neospice {

// Approximate Minimum Degree ordering.
// Input: CSC matrix (col_ptr[n+1], row_idx[nnz]) — must be structurally symmetric.
// Returns: permutation vector perm[n] where perm[new_pos] = old_col.
std::vector<int32_t> amd_ordering(int32_t n, const int32_t* col_ptr,
                                  const int32_t* row_idx);

}  // namespace neospice
