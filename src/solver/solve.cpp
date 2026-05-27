#include "solver/matrix.hpp"
#include <cassert>

namespace neospice::solver {

void SparseMatrix::solve(double* rhs, double* solution,
                         double* irhs, double* isolution) {
    assert(factored_);

    if (complex_) {
        solve_complex(rhs, solution, irhs, isolution);
        return;
    }

    double* inter = intermediate_.data();
    int32_t size = size_;

    for (int32_t i = size; i > 0; i--)
        inter[i] = rhs[int_to_ext_row_[i]];

    for (int32_t i = 1; i <= size; i++) {
        double temp;
        if ((temp = inter[i]) != 0.0) {
            auto* pivot = diag_[i];
            inter[i] = (temp *= pivot->Real);

            auto* elem = pivot->NextInCol;
            while (elem) {
                inter[elem->Row] -= temp * elem->Real;
                elem = elem->NextInCol;
            }
        }
    }

    for (int32_t i = size; i > 0; i--) {
        double temp = inter[i];
        auto* elem = diag_[i]->NextInRow;
        while (elem) {
            temp -= elem->Real * inter[elem->Col];
            elem = elem->NextInRow;
        }
        inter[i] = temp;
    }

    for (int32_t i = size; i > 0; i--)
        solution[int_to_ext_col_[i]] = inter[i];
}

void SparseMatrix::solve_complex(double* rhs, double* solution,
                                  double* irhs, double* isolution) {
    double* inter = intermediate_.data();
    int32_t size = size_;

    for (int32_t i = size; i > 0; i--) {
        int32_t ext = int_to_ext_row_[i];
        inter[2 * i]     = rhs[ext];
        inter[2 * i + 1] = irhs[ext];
    }

    for (int32_t i = 1; i <= size; i++) {
        double temp_r = inter[2 * i];
        double temp_i = inter[2 * i + 1];

        if (temp_r != 0.0 || temp_i != 0.0) {
            auto* pivot = diag_[i];
            complex_ops::mult_assign(temp_r, temp_i, pivot->Real, pivot->Imag);
            inter[2 * i]     = temp_r;
            inter[2 * i + 1] = temp_i;

            auto* elem = pivot->NextInCol;
            while (elem) {
                int32_t ri = 2 * elem->Row;
                inter[ri]     -= temp_r * elem->Real - temp_i * elem->Imag;
                inter[ri + 1] -= temp_r * elem->Imag + temp_i * elem->Real;
                elem = elem->NextInCol;
            }
        }
    }

    for (int32_t i = size; i > 0; i--) {
        double temp_r = inter[2 * i];
        double temp_i = inter[2 * i + 1];
        auto* elem = diag_[i]->NextInRow;

        while (elem) {
            int32_t ci = 2 * elem->Col;
            temp_r -= elem->Real * inter[ci]     - elem->Imag * inter[ci + 1];
            temp_i -= elem->Real * inter[ci + 1] + elem->Imag * inter[ci];
            elem = elem->NextInRow;
        }
        inter[2 * i]     = temp_r;
        inter[2 * i + 1] = temp_i;
    }

    for (int32_t i = size; i > 0; i--) {
        int32_t ext = int_to_ext_col_[i];
        solution[ext]  = inter[2 * i];
        isolution[ext] = inter[2 * i + 1];
    }
}

void SparseMatrix::solve_transposed(double* rhs, double* solution,
                                     double* irhs, double* isolution) {
    assert(factored_);

    if (complex_) {
        solve_complex_transposed(rhs, solution, irhs, isolution);
        return;
    }

    double* inter = intermediate_.data();
    int32_t size = size_;

    for (int32_t i = size; i > 0; i--)
        inter[i] = rhs[int_to_ext_col_[i]];

    for (int32_t i = 1; i <= size; i++) {
        double temp;
        if ((temp = inter[i]) != 0.0) {
            auto* elem = diag_[i]->NextInRow;
            while (elem) {
                inter[elem->Col] -= temp * elem->Real;
                elem = elem->NextInRow;
            }
        }
    }

    for (int32_t i = size; i > 0; i--) {
        auto* pivot = diag_[i];
        double temp = inter[i];
        auto* elem = pivot->NextInCol;
        while (elem) {
            temp -= elem->Real * inter[elem->Row];
            elem = elem->NextInCol;
        }
        inter[i] = temp * pivot->Real;
    }

    for (int32_t i = size; i > 0; i--)
        solution[int_to_ext_row_[i]] = inter[i];
}

void SparseMatrix::solve_complex_transposed(double* rhs, double* solution,
                                             double* irhs, double* isolution) {
    double* inter = intermediate_.data();
    int32_t size = size_;

    for (int32_t i = size; i > 0; i--) {
        int32_t ext = int_to_ext_col_[i];
        inter[2 * i]     = rhs[ext];
        inter[2 * i + 1] = irhs[ext];
    }

    for (int32_t i = 1; i <= size; i++) {
        double temp_r = inter[2 * i];
        double temp_i = inter[2 * i + 1];

        if (temp_r != 0.0 || temp_i != 0.0) {
            auto* elem = diag_[i]->NextInRow;
            while (elem) {
                int32_t ci = 2 * elem->Col;
                inter[ci]     -= temp_r * elem->Real - temp_i * elem->Imag;
                inter[ci + 1] -= temp_r * elem->Imag + temp_i * elem->Real;
                elem = elem->NextInRow;
            }
        }
    }

    for (int32_t i = size; i > 0; i--) {
        auto* pivot = diag_[i];
        double temp_r = inter[2 * i];
        double temp_i = inter[2 * i + 1];
        auto* elem = pivot->NextInCol;

        while (elem) {
            int32_t ri = 2 * elem->Row;
            temp_r -= inter[ri]     * elem->Real - inter[ri + 1] * elem->Imag;
            temp_i -= inter[ri]     * elem->Imag + inter[ri + 1] * elem->Real;
            elem = elem->NextInCol;
        }

        double res_r = temp_r * pivot->Real - temp_i * pivot->Imag;
        double res_i = temp_r * pivot->Imag + temp_i * pivot->Real;
        inter[2 * i]     = res_r;
        inter[2 * i + 1] = res_i;
    }

    for (int32_t i = size; i > 0; i--) {
        int32_t ext = int_to_ext_row_[i];
        solution[ext]  = inter[2 * i];
        isolution[ext] = inter[2 * i + 1];
    }
}

} // namespace neospice::solver
