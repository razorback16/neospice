#pragma once
#include <cstdint>
#include <vector>

namespace neospice {

// Minimum-degree ordering on an explicit graph with dense vertices deferred.
// Input: valid n-by-n CSC matrix (col_ptr[n+1], row_idx[nnz]); the graph
// is symmetrized internally. The historical name does not imply SuiteSparse
// AMD algorithm or permutation equivalence.
// Returns: permutation vector perm[n] where perm[new_pos] = old_col.
std::vector<int32_t> amd_ordering(int32_t n, const int32_t* col_ptr,
                                  const int32_t* row_idx);

}  // namespace neospice
