#include "core/small_solver.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace neospice {

SmallSolver::SmallSolver() = default;

void SmallSolver::symbolic(const SparsityPattern& pattern) {
    n_ = pattern.size();
    CSCData csc = pattern.to_csc();
    col_ptr_ = std::move(csc.col_ptr);
    row_idx_ = std::move(csc.row_idx);
    lu_.resize(n_ * n_, 0.0);
    pivot_.resize(n_);
    symbolized_ = true;
    factored_ = false;
}

void SmallSolver::scatter_to_dense(const double* csc_values) {
    std::fill(lu_.begin(), lu_.end(), 0.0);
    for (int32_t j = 0; j < n_; ++j) {
        for (int32_t k = col_ptr_[j]; k < col_ptr_[j + 1]; ++k) {
            int32_t i = row_idx_[k];
            lu_[j * n_ + i] = csc_values[k];
        }
    }
}

void SmallSolver::dense_factor() {
    for (int32_t k = 0; k < n_; ++k) {
        // Find pivot: row with max |lu_[k*n_ + row]| for row in [k, n_)
        int32_t best = k;
        double best_val = std::abs(lu_[k * n_ + k]);
        for (int32_t i = k + 1; i < n_; ++i) {
            double v = std::abs(lu_[k * n_ + i]);
            if (v > best_val) {
                best = i;
                best_val = v;
            }
        }
        pivot_[k] = best;

        // Swap rows k and best across ALL columns
        if (best != k) {
            for (int32_t j = 0; j < n_; ++j)
                std::swap(lu_[j * n_ + k], lu_[j * n_ + best]);
        }

        // Check for singular
        double diag = lu_[k * n_ + k];
        if (std::abs(diag) < 1e-30)
            throw std::runtime_error("SmallSolver: singular matrix");

        // Compute multipliers and eliminate
        for (int32_t i = k + 1; i < n_; ++i) {
            lu_[k * n_ + i] /= diag;  // L multiplier stored below diagonal
            for (int32_t j = k + 1; j < n_; ++j) {
                lu_[j * n_ + i] -= lu_[k * n_ + i] * lu_[j * n_ + k];
            }
        }
    }
}

void SmallSolver::dense_solve(double* rhs) const {
    // Apply pivot permutation (forward)
    for (int32_t k = 0; k < n_; ++k) {
        if (pivot_[k] != k)
            std::swap(rhs[k], rhs[pivot_[k]]);
    }

    // Forward substitution (L is unit lower triangular, stored below diagonal)
    for (int32_t k = 0; k < n_; ++k) {
        for (int32_t i = k + 1; i < n_; ++i) {
            rhs[i] -= lu_[k * n_ + i] * rhs[k];
        }
    }

    // Backward substitution (U is upper triangular, stored on+above diagonal)
    for (int32_t k = n_ - 1; k >= 0; --k) {
        rhs[k] /= lu_[k * n_ + k];
        for (int32_t i = 0; i < k; ++i) {
            rhs[i] -= lu_[k * n_ + i] * rhs[k];
        }
    }
}

void SmallSolver::numeric(const SparsityPattern& pattern, const NumericMatrix& mat) {
    if (!symbolized_)
        throw std::logic_error("SmallSolver::numeric: symbolic() not called");
    // Refresh CSC structure in case pattern changed
    CSCData csc = pattern.to_csc();
    col_ptr_ = std::move(csc.col_ptr);
    row_idx_ = std::move(csc.row_idx);
    scatter_to_dense(mat.data());
    dense_factor();
    factored_ = true;
}

void SmallSolver::refactorize(const NumericMatrix& mat) {
    if (!symbolized_)
        throw std::logic_error("SmallSolver::refactorize: symbolic() not called");
    if (!factored_)
        throw std::logic_error("SmallSolver::refactorize: numeric() not called");
    scatter_to_dense(mat.data());
    dense_factor();
}

void SmallSolver::solve(std::vector<double>& rhs) {
    if (!symbolized_)
        throw std::logic_error("SmallSolver::solve: symbolic() not called");
    if (!factored_)
        throw std::logic_error("SmallSolver::solve: numeric() not called");
    if (static_cast<int32_t>(rhs.size()) != n_)
        throw std::invalid_argument("SmallSolver::solve: rhs size mismatch");
    dense_solve(rhs.data());
}

void SmallSolver::numeric_complex(const SparsityPattern& /*pattern*/,
                                  const std::vector<double>& /*ax*/) {
    throw std::logic_error("SmallSolver::numeric_complex: not yet implemented");
}

void SmallSolver::refactorize_complex(const std::vector<double>& /*ax*/) {
    throw std::logic_error("SmallSolver::refactorize_complex: not yet implemented");
}

void SmallSolver::solve_complex(std::vector<double>& /*rhs*/) {
    throw std::logic_error("SmallSolver::solve_complex: not yet implemented");
}

}  // namespace neospice
