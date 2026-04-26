#include "core/small_solver.hpp"
#include <suitesparse/amd.h>
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

    use_dense_ = (n_ < DENSE_LIMIT);

    if (!use_dense_) {
        // AMD ordering for sparse tier
        amd_perm_.resize(n_);
        amd_inv_.resize(n_);
        int status = amd_order(n_, col_ptr_.data(), row_idx_.data(),
                               amd_perm_.data(), nullptr, nullptr);
        if (status != AMD_OK && status != AMD_OK_BUT_JUMBLED) {
            // Fall back to identity permutation
            for (int32_t i = 0; i < n_; ++i) amd_perm_[i] = i;
        }
        // Build inverse permutation
        for (int32_t i = 0; i < n_; ++i) amd_inv_[amd_perm_[i]] = i;
    }

    lu_.resize(n_ * n_, 0.0);
    pivot_.resize(n_);
    lu_z_.resize(2 * n_ * n_, 0.0);
    pivot_z_.resize(n_);
    symbolized_ = true;
    factored_ = false;
    factored_z_ = false;
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

void SmallSolver::scatter_to_dense_amd(const double* csc_values) {
    std::fill(lu_.begin(), lu_.end(), 0.0);
    // Scatter with AMD column+row permutation: PAP^T
    // For CSC entry (row=i, col=j) with value v:
    //   permuted position: (amd_inv_[i], amd_inv_[j])
    for (int32_t j = 0; j < n_; ++j) {
        int32_t pj = amd_inv_[j];  // permuted column
        for (int32_t k = col_ptr_[j]; k < col_ptr_[j + 1]; ++k) {
            int32_t pi = amd_inv_[row_idx_[k]];  // permuted row
            lu_[pj * n_ + pi] = csc_values[k];
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
    if (use_dense_) {
        scatter_to_dense(mat.data());
    } else {
        scatter_to_dense_amd(mat.data());
    }
    dense_factor();
    factored_ = true;
}

void SmallSolver::refactorize(const NumericMatrix& mat) {
    if (!symbolized_)
        throw std::logic_error("SmallSolver::refactorize: symbolic() not called");
    if (!factored_)
        throw std::logic_error("SmallSolver::refactorize: numeric() not called");
    if (use_dense_) {
        scatter_to_dense(mat.data());
    } else {
        scatter_to_dense_amd(mat.data());
    }
    dense_factor();
}

void SmallSolver::solve(std::vector<double>& rhs) {
    if (!symbolized_)
        throw std::logic_error("SmallSolver::solve: symbolic() not called");
    if (!factored_)
        throw std::logic_error("SmallSolver::solve: numeric() not called");
    if (static_cast<int32_t>(rhs.size()) != n_)
        throw std::invalid_argument("SmallSolver::solve: rhs size mismatch");

    if (use_dense_) {
        dense_solve(rhs.data());
    } else {
        // Permute RHS: tmp[amd_inv_[i]] = rhs[i]
        std::vector<double> tmp(n_);
        for (int32_t i = 0; i < n_; ++i) tmp[amd_inv_[i]] = rhs[i];
        // Solve in permuted space
        dense_solve(tmp.data());
        // Un-permute: rhs[i] = tmp[amd_inv_[i]]
        for (int32_t i = 0; i < n_; ++i) rhs[i] = tmp[amd_inv_[i]];
    }
}

void SmallSolver::scatter_to_dense_complex(const double* ax) {
    std::fill(lu_z_.begin(), lu_z_.end(), 0.0);
    for (int32_t j = 0; j < n_; ++j) {
        for (int32_t k = col_ptr_[j]; k < col_ptr_[j + 1]; ++k) {
            int32_t i = row_idx_[k];
            int32_t idx = 2 * (j * n_ + i);
            lu_z_[idx]     = ax[2 * k];
            lu_z_[idx + 1] = ax[2 * k + 1];
        }
    }
}

void SmallSolver::scatter_to_dense_complex_amd(const double* ax) {
    std::fill(lu_z_.begin(), lu_z_.end(), 0.0);
    for (int32_t j = 0; j < n_; ++j) {
        int32_t pj = amd_inv_[j];
        for (int32_t k = col_ptr_[j]; k < col_ptr_[j + 1]; ++k) {
            int32_t pi = amd_inv_[row_idx_[k]];
            int32_t idx = 2 * (pj * n_ + pi);
            lu_z_[idx]     = ax[2 * k];
            lu_z_[idx + 1] = ax[2 * k + 1];
        }
    }
}

void SmallSolver::dense_factor_complex() {
    for (int32_t k = 0; k < n_; ++k) {
        // Find pivot: row with max |z| in column k, rows [k, n_)
        int32_t best = k;
        double best_abs = std::hypot(lu_z_[2 * (k * n_ + k)], lu_z_[2 * (k * n_ + k) + 1]);
        for (int32_t i = k + 1; i < n_; ++i) {
            double v = std::hypot(lu_z_[2 * (k * n_ + i)], lu_z_[2 * (k * n_ + i) + 1]);
            if (v > best_abs) { best = i; best_abs = v; }
        }
        pivot_z_[k] = best;

        // Swap rows k and best across ALL columns
        if (best != k) {
            for (int32_t j = 0; j < n_; ++j) {
                int32_t a = 2 * (j * n_ + k);
                int32_t b = 2 * (j * n_ + best);
                std::swap(lu_z_[a], lu_z_[b]);
                std::swap(lu_z_[a + 1], lu_z_[b + 1]);
            }
        }

        // Check for singular
        double dr = lu_z_[2 * (k * n_ + k)];
        double di = lu_z_[2 * (k * n_ + k) + 1];
        double denom = dr * dr + di * di;
        if (denom < 1e-60)
            throw std::runtime_error("SmallSolver: singular complex matrix");

        // Compute multipliers and eliminate
        for (int32_t i = k + 1; i < n_; ++i) {
            // L multiplier = lu_z[k,i] / lu_z[k,k]
            double ar = lu_z_[2 * (k * n_ + i)];
            double ai = lu_z_[2 * (k * n_ + i) + 1];
            double mr = (ar * dr + ai * di) / denom;
            double mi = (ai * dr - ar * di) / denom;
            lu_z_[2 * (k * n_ + i)] = mr;
            lu_z_[2 * (k * n_ + i) + 1] = mi;

            // Eliminate: for j = k+1..n-1: lu_z[j,i] -= m * lu_z[j,k]
            for (int32_t j = k + 1; j < n_; ++j) {
                double er = lu_z_[2 * (j * n_ + k)];
                double ei = lu_z_[2 * (j * n_ + k) + 1];
                lu_z_[2 * (j * n_ + i)]     -= (mr * er - mi * ei);
                lu_z_[2 * (j * n_ + i) + 1] -= (mr * ei + mi * er);
            }
        }
    }
}

void SmallSolver::dense_solve_complex(double* rhs) const {
    // Apply pivot permutation
    for (int32_t k = 0; k < n_; ++k) {
        if (pivot_z_[k] != k) {
            std::swap(rhs[2 * k], rhs[2 * pivot_z_[k]]);
            std::swap(rhs[2 * k + 1], rhs[2 * pivot_z_[k] + 1]);
        }
    }

    // Forward substitution (L is unit lower triangular)
    for (int32_t k = 0; k < n_; ++k) {
        for (int32_t i = k + 1; i < n_; ++i) {
            double mr = lu_z_[2 * (k * n_ + i)];
            double mi = lu_z_[2 * (k * n_ + i) + 1];
            double xr = rhs[2 * k];
            double xi = rhs[2 * k + 1];
            rhs[2 * i]     -= (mr * xr - mi * xi);
            rhs[2 * i + 1] -= (mr * xi + mi * xr);
        }
    }

    // Backward substitution (U on+above diagonal)
    for (int32_t k = n_ - 1; k >= 0; --k) {
        double dr = lu_z_[2 * (k * n_ + k)];
        double di = lu_z_[2 * (k * n_ + k) + 1];
        double denom = dr * dr + di * di;
        // rhs[k] /= diag
        double xr = rhs[2 * k];
        double xi = rhs[2 * k + 1];
        rhs[2 * k]     = (xr * dr + xi * di) / denom;
        rhs[2 * k + 1] = (xi * dr - xr * di) / denom;

        for (int32_t i = 0; i < k; ++i) {
            double ur = lu_z_[2 * (k * n_ + i)];
            double ui = lu_z_[2 * (k * n_ + i) + 1];
            rhs[2 * i]     -= (ur * rhs[2 * k] - ui * rhs[2 * k + 1]);
            rhs[2 * i + 1] -= (ur * rhs[2 * k + 1] + ui * rhs[2 * k]);
        }
    }
}

void SmallSolver::numeric_complex(const SparsityPattern& pattern,
                                  const std::vector<double>& ax) {
    if (!symbolized_)
        throw std::logic_error("SmallSolver::numeric_complex: symbolic() not called");
    CSCData csc = pattern.to_csc();
    col_ptr_ = std::move(csc.col_ptr);
    row_idx_ = std::move(csc.row_idx);
    if (use_dense_) {
        scatter_to_dense_complex(ax.data());
    } else {
        scatter_to_dense_complex_amd(ax.data());
    }
    dense_factor_complex();
    factored_z_ = true;
}

void SmallSolver::refactorize_complex(const std::vector<double>& ax) {
    if (!symbolized_)
        throw std::logic_error("SmallSolver::refactorize_complex: symbolic() not called");
    if (!factored_z_)
        throw std::logic_error("SmallSolver::refactorize_complex: numeric_complex() not called");
    if (use_dense_) {
        scatter_to_dense_complex(ax.data());
    } else {
        scatter_to_dense_complex_amd(ax.data());
    }
    dense_factor_complex();
}

void SmallSolver::solve_complex(std::vector<double>& rhs) {
    if (!symbolized_)
        throw std::logic_error("SmallSolver::solve_complex: symbolic() not called");
    if (!factored_z_)
        throw std::logic_error("SmallSolver::solve_complex: numeric_complex() not called");
    if (static_cast<int32_t>(rhs.size()) != 2 * n_)
        throw std::invalid_argument("SmallSolver::solve_complex: rhs size must be 2*n");

    if (use_dense_) {
        dense_solve_complex(rhs.data());
    } else {
        // Permute RHS
        std::vector<double> tmp(2 * n_);
        for (int32_t i = 0; i < n_; ++i) {
            tmp[2 * amd_inv_[i]]     = rhs[2 * i];
            tmp[2 * amd_inv_[i] + 1] = rhs[2 * i + 1];
        }
        dense_solve_complex(tmp.data());
        // Un-permute
        for (int32_t i = 0; i < n_; ++i) {
            rhs[2 * i]     = tmp[2 * amd_inv_[i]];
            rhs[2 * i + 1] = tmp[2 * amd_inv_[i] + 1];
        }
    }
}

}  // namespace neospice
