#include "solver/matrix.hpp"
#include <algorithm>
#include <cassert>
#include <stdexcept>

namespace neospice::solver {

SparseMatrix::SparseMatrix(int32_t size, bool complex)
    : complex_(complex), prev_complex_(complex) {

    if constexpr (config::EXPANDABLE) {
        if (size < 0)
            throw std::invalid_argument("SparseMatrix: negative size");
    } else {
        if (size <= 0)
            throw std::invalid_argument("SparseMatrix: non-positive size");
    }

    int32_t alloc_size = std::max(size, config::MINIMUM_ALLOCATED_SIZE);
    int32_t n1 = alloc_size + 1;

    size_ = size;
    allocated_size_ = alloc_size;
    ext_size_ = size;
    allocated_ext_size_ = alloc_size;

    diag_.assign(n1, nullptr);
    first_in_col_.assign(n1, nullptr);
    first_in_row_.assign(n1, nullptr);

    int_to_ext_col_.resize(n1);
    int_to_ext_row_.resize(n1);
    for (int32_t i = 1; i <= alloc_size; ++i) {
        int_to_ext_row_[i] = i;
        int_to_ext_col_[i] = i;
    }

    if constexpr (config::TRANSLATE) {
        ext_to_int_col_.assign(n1, -1);
        ext_to_int_row_.assign(n1, -1);
        ext_to_int_col_[0] = 0;
        ext_to_int_row_[0] = 0;
    }

    arena_.init(config::SPACE_FOR_ELEMENTS * alloc_size,
                config::SPACE_FOR_FILL_INS * alloc_size);
}

std::pair<int32_t, int32_t> SparseMatrix::where_singular() const {
    if (error_ == SparseError::Singular || error_ == SparseError::ZeroDiag)
        return {singular_row_, singular_col_};
    return {0, 0};
}

} // namespace neospice::solver
