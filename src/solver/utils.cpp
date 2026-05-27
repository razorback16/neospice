#include "solver/matrix.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>

namespace neospice::solver {

void SparseMatrix::mna_preorder() {
    assert(!factored_);

    if (rows_linked_) return;
    int32_t size = size_;
    reordered_ = true;

    int32_t start_at = 1;
    bool another_pass_needed;

    do {
        another_pass_needed = false;
        bool swapped = false;

        for (int32_t j = start_at; j <= size; j++) {
            if (diag_[j] == nullptr) {
                MatrixElement* twin1;
                MatrixElement* twin2;
                int32_t twins = count_twins(j, &twin1, &twin2);
                if (twins == 1) {
                    swap_cols(twin1, twin2);
                    swapped = true;
                } else if (twins > 1 && !another_pass_needed) {
                    another_pass_needed = true;
                    start_at = j;
                }
            }
        }

        if (another_pass_needed) {
            for (int32_t j = start_at; !swapped && j <= size; j++) {
                if (diag_[j] == nullptr) {
                    MatrixElement* twin1;
                    MatrixElement* twin2;
                    count_twins(j, &twin1, &twin2);
                    swap_cols(twin1, twin2);
                    swapped = true;
                }
            }
        }
    } while (another_pass_needed);
}

int32_t SparseMatrix::count_twins(int32_t col,
                                   MatrixElement** pp_twin1,
                                   MatrixElement** pp_twin2) {
    int32_t twins = 0;

    auto* twin1 = first_in_col_[col];
    while (twin1) {
        if (std::abs(twin1->Real) == 1.0) {
            int32_t row = twin1->Row;
            auto* twin2 = first_in_col_[row];
            while (twin2 && twin2->Row != col)
                twin2 = twin2->NextInCol;
            if (twin2 && std::abs(twin2->Real) == 1.0) {
                if (++twins >= 2) return twins;
                (*pp_twin1 = twin1)->Col = col;
                (*pp_twin2 = twin2)->Col = row;
            }
        }
        twin1 = twin1->NextInCol;
    }
    return twins;
}

void SparseMatrix::swap_cols(MatrixElement* twin1, MatrixElement* twin2) {
    int32_t col1 = twin1->Col;
    int32_t col2 = twin2->Col;

    std::swap(first_in_col_[col1], first_in_col_[col2]);
    std::swap(int_to_ext_col_[col1], int_to_ext_col_[col2]);

    if constexpr (config::TRANSLATE) {
        ext_to_int_col_[int_to_ext_col_[col2]] = col2;
        ext_to_int_col_[int_to_ext_col_[col1]] = col1;
    }

    diag_[col1] = twin2;
    diag_[col2] = twin1;
    interchanges_odd_ = !interchanges_odd_;
}

} // namespace neospice::solver
