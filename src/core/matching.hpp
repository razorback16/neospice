#pragma once
#include <cstdint>
#include <vector>

namespace neospice {

// Maximum transversal: find a row permutation such that A[match[j], j] != 0
// for every column j (i.e., zero-free diagonal after row permutation).
// Input: CSC matrix (col_ptr[n+1], row_idx[nnz]).
// Returns: match[j] = row assigned to column j. match[j] = -1 if column j
// could not be matched (structurally singular).
// For identity-matchable matrices, returns the identity permutation.
std::vector<int32_t> maximum_transversal(int32_t n, const int32_t* col_ptr,
                                          const int32_t* row_idx);

}  // namespace neospice
