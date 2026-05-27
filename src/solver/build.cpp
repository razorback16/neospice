#include "solver/matrix.hpp"
#include <algorithm>
#include <cassert>

namespace neospice::solver {

void SparseMatrix::clear() {
    if (prev_complex_ || complex_) {
        for (int32_t i = size_; i > 0; --i) {
            auto* e = first_in_col_[i];
            while (e) {
                e->Real = 0.0;
                e->Imag = 0.0;
                e = e->NextInCol;
            }
        }
    } else {
        for (int32_t i = size_; i > 0; --i) {
            auto* e = first_in_col_[i];
            while (e) {
                e->Real = 0.0;
                e = e->NextInCol;
            }
        }
    }

    trash_can_.Real = 0.0;
    trash_can_.Imag = 0.0;
    error_ = SparseError::OK;
    factored_ = false;
    singular_col_ = 0;
    singular_row_ = 0;
    prev_complex_ = complex_;
}

double* SparseMatrix::get_element(int32_t row, int32_t col) {
    assert(row >= 0 && col >= 0);

    if (row == 0 || col == 0)
        return &trash_can_.Real;

    if constexpr (config::TRANSLATE) {
        translate(row, col);
        if (error_ == SparseError::NoMemory) return nullptr;
    } else {
        assert(needs_ordering_);
        if constexpr (config::EXPANDABLE) {
            if (row > size_ || col > size_)
                enlarge_matrix(std::max(row, col));
            if (error_ == SparseError::NoMemory) return nullptr;
        } else {
            assert(row <= size_ && col <= size_);
        }
    }

    MatrixElement* elem;
    if (row != col || (elem = diag_[row]) == nullptr) {
        elem = find_element_in_col(&first_in_col_[col], row, col, true);
    }
    return &elem->Real;
}

double* SparseMatrix::find_element(int32_t row, int32_t col) {
    assert(row >= 0 && col >= 0);

    if (row == 0 || col == 0)
        return &trash_can_.Real;

    if constexpr (config::TRANSLATE) {
        translate(row, col);
        if (error_ == SparseError::NoMemory) return nullptr;
    } else {
        assert(row <= size_ && col <= size_);
    }

    MatrixElement* elem;
    if (row != col || (elem = diag_[row]) == nullptr) {
        elem = find_element_in_col(&first_in_col_[col], row, col, false);
    }
    if (!elem) return nullptr;
    return &elem->Real;
}

MatrixElement* SparseMatrix::find_element_in_col(
    MatrixElement** last_addr, int32_t row, int32_t col, bool create) {

    auto* elem = *last_addr;

    while (elem) {
        if (elem->Row < row) {
            last_addr = &elem->NextInCol;
            elem = elem->NextInCol;
        } else if (elem->Row == row) {
            return elem;
        } else {
            break;
        }
    }

    if (create)
        return create_element(row, col, last_addr, false);
    return nullptr;
}

MatrixElement* SparseMatrix::create_element(
    int32_t row, int32_t col, MatrixElement** last_addr, bool fillin) {

    MatrixElement* elem;

    if (rows_linked_) {
        if (fillin) {
            elem = arena_.get_fillin();
            ++fillins_;
        } else {
            elem = arena_.get_element();
            ++originals_;
            needs_ordering_ = true;
        }

        if (row == col) diag_[row] = elem;

        elem->Row = row;
        elem->Col = col;
        elem->Real = 0.0;
        elem->Imag = 0.0;

        elem->NextInCol = *last_addr;
        *last_addr = elem;

        // Splice into row list
        MatrixElement* prev_in_row = nullptr;
        auto* scan = first_in_row_[row];
        while (scan && scan->Col < col) {
            prev_in_row = scan;
            scan = scan->NextInRow;
        }

        if (!prev_in_row) {
            elem->NextInRow = first_in_row_[row];
            first_in_row_[row] = elem;
        } else {
            elem->NextInRow = prev_in_row->NextInRow;
            prev_in_row->NextInRow = elem;
        }
    } else {
        elem = arena_.get_element();
        ++originals_;

        if (row == col) diag_[row] = elem;

        elem->Row = row;
        elem->Col = col;
        elem->Real = 0.0;
        elem->Imag = 0.0;

        elem->NextInCol = *last_addr;
        *last_addr = elem;
    }

    ++elements_;
    return elem;
}

void SparseMatrix::link_rows() {
    for (int32_t col = size_; col >= 1; --col) {
        auto* elem = first_in_col_[col];
        while (elem) {
            elem->Col = col;
            auto*& first = first_in_row_[elem->Row];
            elem->NextInRow = first;
            first = elem;
            elem = elem->NextInCol;
        }
    }
    rows_linked_ = true;
}

void SparseMatrix::translate(int32_t& row, int32_t& col) {
    int32_t ext_row = row;
    int32_t ext_col = col;

    if (ext_row > allocated_ext_size_ || ext_col > allocated_ext_size_) {
        expand_translation_arrays(std::max(ext_row, ext_col));
        if (error_ == SparseError::NoMemory) return;
    }

    if (ext_row > ext_size_ || ext_col > ext_size_)
        ext_size_ = std::max(ext_row, ext_col);

    int32_t int_row = ext_to_int_row_[ext_row];
    if (int_row == -1) {
        ++current_size_;
        ext_to_int_row_[ext_row] = current_size_;
        ext_to_int_col_[ext_row] = current_size_;
        int_row = current_size_;

        if constexpr (config::EXPANDABLE) {
            if (int_row > size_)
                enlarge_matrix(int_row);
            if (error_ == SparseError::NoMemory) return;
        } else {
            assert(int_row <= size_);
        }

        int_to_ext_row_[int_row] = ext_row;
        int_to_ext_col_[int_row] = ext_row;
    }

    int32_t int_col = ext_to_int_col_[ext_col];
    if (int_col == -1) {
        ++current_size_;
        ext_to_int_row_[ext_col] = current_size_;
        ext_to_int_col_[ext_col] = current_size_;
        int_col = current_size_;

        if constexpr (config::EXPANDABLE) {
            if (int_col > size_)
                enlarge_matrix(int_col);
            if (error_ == SparseError::NoMemory) return;
        } else {
            assert(int_col <= size_);
        }

        int_to_ext_row_[int_col] = ext_col;
        int_to_ext_col_[int_col] = ext_col;
    }

    row = int_row;
    col = int_col;
}

void SparseMatrix::enlarge_matrix(int32_t new_size) {
    int32_t old_alloc = allocated_size_;
    size_ = new_size;

    if (new_size <= old_alloc)
        return;

    new_size = std::max(new_size,
                        static_cast<int32_t>(config::EXPANSION_FACTOR * old_alloc));
    allocated_size_ = new_size;
    int32_t n1 = new_size + 1;

    int_to_ext_col_.resize(n1);
    int_to_ext_row_.resize(n1);
    diag_.resize(n1, nullptr);
    first_in_col_.resize(n1, nullptr);
    first_in_row_.resize(n1, nullptr);

    // Destroy internal vectors — will be recreated in order_and_factor()
    markowitz_row_.clear();
    markowitz_col_.clear();
    markowitz_prod_.clear();
    do_real_direct_.clear();
    do_cmplx_direct_.clear();
    intermediate_.clear();
    intermediate_ptrs_.clear();
    internal_vectors_allocated_ = false;

    for (int32_t i = old_alloc + 1; i <= new_size; ++i) {
        int_to_ext_col_[i] = i;
        int_to_ext_row_[i] = i;
    }
}

void SparseMatrix::expand_translation_arrays(int32_t new_size) {
    int32_t old_alloc = allocated_ext_size_;
    ext_size_ = new_size;

    if (new_size <= old_alloc)
        return;

    new_size = std::max(new_size,
                        static_cast<int32_t>(config::EXPANSION_FACTOR * old_alloc));
    allocated_ext_size_ = new_size;
    int32_t n1 = new_size + 1;

    ext_to_int_row_.resize(n1, -1);
    ext_to_int_col_.resize(n1, -1);
}

void SparseMatrix::create_internal_vectors() {
    if (internal_vectors_allocated_) return;

    int32_t n1 = size_ + 1;
    markowitz_row_.assign(n1, 0);
    markowitz_col_.assign(n1, 0);
    markowitz_prod_.assign(n1 + 1, 0);
    do_real_direct_.assign(n1, 0);
    do_cmplx_direct_.assign(n1, 0);

    intermediate_.assign(2 * n1, 0.0);
    intermediate_ptrs_.assign(n1, nullptr);

    internal_vectors_allocated_ = true;
}

} // namespace neospice::solver
