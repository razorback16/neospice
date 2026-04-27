#include "core/small_solver.hpp"
#include "core/amd.hpp"
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

    if (use_dense_) {
        lu_.resize(n_ * n_, 0.0);
        pivot_.resize(n_);
        lu_z_.resize(2 * n_ * n_, 0.0);
        pivot_z_.resize(n_);
    } else {
        amd_perm_ = amd_ordering(n_, col_ptr_.data(), row_idx_.data());
        amd_inv_.resize(n_);
        for (int32_t i = 0; i < n_; ++i) amd_inv_[amd_perm_[i]] = i;

        build_permuted_csc();

        x_work_.resize(n_, 0.0);
        pinv_.assign(n_, -1);
        piv_.resize(n_);
    }

    symbolized_ = true;
    factored_ = false;
    factored_z_ = false;
    sparse_factored_z_ = false;
}

void SmallSolver::build_permuted_csc() {
    int32_t nnz = static_cast<int32_t>(row_idx_.size());

    perm_cp_.assign(n_ + 1, 0);
    for (int32_t j = 0; j < n_; ++j) {
        int32_t pj = amd_inv_[j];
        perm_cp_[pj + 1] += col_ptr_[j + 1] - col_ptr_[j];
    }
    for (int32_t j = 0; j < n_; ++j)
        perm_cp_[j + 1] += perm_cp_[j];

    perm_ri_.resize(nnz);
    val_map_.resize(nnz);

    std::vector<int32_t> cursor(perm_cp_.begin(), perm_cp_.begin() + n_);
    for (int32_t j = 0; j < n_; ++j) {
        int32_t pj = amd_inv_[j];
        for (int32_t k = col_ptr_[j]; k < col_ptr_[j + 1]; ++k) {
            int32_t pi = amd_inv_[row_idx_[k]];
            int32_t pos = cursor[pj]++;
            perm_ri_[pos] = pi;
            val_map_[pos] = k;
        }
    }

    // Sort each column by row index
    for (int32_t j = 0; j < n_; ++j) {
        int32_t start = perm_cp_[j];
        int32_t end = perm_cp_[j + 1];
        for (int32_t a = start + 1; a < end; ++a) {
            int32_t ri = perm_ri_[a];
            int32_t vm = val_map_[a];
            int32_t b = a - 1;
            while (b >= start && perm_ri_[b] > ri) {
                perm_ri_[b + 1] = perm_ri_[b];
                val_map_[b + 1] = val_map_[b];
                --b;
            }
            perm_ri_[b + 1] = ri;
            val_map_[b + 1] = vm;
        }
    }
}

// Left-looking column-LU with threshold diagonal pivoting.
// Iterates k=0..col-1 for each column (simple and correct).
void SmallSolver::sparse_factor(const double* orig_values) {
    l_cp_.assign(n_ + 1, 0);
    u_cp_.assign(n_ + 1, 0);
    l_ri_.clear();
    l_val_.clear();
    u_ri_.clear();
    u_val_.clear();

    int32_t est = 7 * n_;
    l_ri_.reserve(est);
    l_val_.reserve(est);
    u_ri_.reserve(est);
    u_val_.reserve(est);

    pinv_.assign(n_, -1);
    piv_.assign(n_, -1);

    for (int32_t col = 0; col < n_; ++col) {
        // Scatter column of PAP^T into dense accumulator
        std::fill(x_work_.begin(), x_work_.end(), 0.0);
        for (int32_t p = perm_cp_[col]; p < perm_cp_[col + 1]; ++p)
            x_work_[perm_ri_[p]] = orig_values[val_map_[p]];

        // Left-looking: for each previous step k, apply L(:,k) update
        u_cp_[col] = static_cast<int32_t>(u_ri_.size());
        for (int32_t k = 0; k < col; ++k) {
            int32_t pr = piv_[k];
            double u_kj = x_work_[pr];
            if (u_kj == 0.0) continue;

            // Save U(k, col)
            u_ri_.push_back(k);
            u_val_.push_back(u_kj);

            // Subtract L(:,k) * u_kj
            for (int32_t p = l_cp_[k]; p < l_cp_[k + 1]; ++p)
                x_work_[l_ri_[p]] -= l_val_[p] * u_kj;
        }

        // Threshold pivot: prefer diagonal if large enough
        double col_max = 0.0;
        for (int32_t i = 0; i < n_; ++i) {
            if (pinv_[i] >= 0) continue;
            double v = std::abs(x_work_[i]);
            if (v > col_max) col_max = v;
        }

        int32_t pivot_row = -1;
        double diag_val = (pinv_[col] < 0) ? std::abs(x_work_[col]) : 0.0;

        if (diag_val >= PIVOT_THRESHOLD * col_max && diag_val > 0.0) {
            pivot_row = col;
        } else {
            double best = -1.0;
            for (int32_t i = 0; i < n_; ++i) {
                if (pinv_[i] >= 0) continue;
                double v = std::abs(x_work_[i]);
                if (v > best) { best = v; pivot_row = i; }
            }
        }

        if (pivot_row < 0 || std::abs(x_work_[pivot_row]) < 1e-30)
            throw std::runtime_error("SmallSolver: singular matrix");

        pinv_[pivot_row] = col;
        piv_[col] = pivot_row;

        double diag = x_work_[pivot_row];

        // U diagonal entry
        u_ri_.push_back(col);
        u_val_.push_back(diag);

        // L column: unpivoted rows with nonzero entries, divided by diagonal
        l_cp_[col] = static_cast<int32_t>(l_ri_.size());
        for (int32_t i = 0; i < n_; ++i) {
            if (pinv_[i] >= 0) continue;
            double val = x_work_[i];
            if (val != 0.0) {
                l_ri_.push_back(i);
                l_val_.push_back(val / diag);
            }
        }
        l_cp_[col + 1] = static_cast<int32_t>(l_ri_.size());
        u_cp_[col + 1] = static_cast<int32_t>(u_ri_.size());
    }
    factored_ = true;
}

// Refactorize: same pivot order and L/U structure, recompute values.
void SmallSolver::sparse_refactor(const double* orig_values) {
    // x_work_ is zero-initialized from symbolic(); we maintain this invariant
    // by clearing only the positions we touch in each column.
    for (int32_t col = 0; col < n_; ++col) {
        // Scatter column of PAP^T (set only nonzero positions)
        for (int32_t p = perm_cp_[col]; p < perm_cp_[col + 1]; ++p)
            x_work_[perm_ri_[p]] = orig_values[val_map_[p]];

        // Left-looking: iterate stored U entries only
        int32_t u_end = u_cp_[col + 1] - 1;
        for (int32_t up = u_cp_[col]; up < u_end; ++up) {
            int32_t k = u_ri_[up];
            int32_t pr = piv_[k];
            double u_kj = x_work_[pr];
            u_val_[up] = u_kj;
            if (u_kj == 0.0) continue;

            for (int32_t p = l_cp_[k]; p < l_cp_[k + 1]; ++p)
                x_work_[l_ri_[p]] -= l_val_[p] * u_kj;
        }

        double diag = x_work_[piv_[col]];
        if (std::abs(diag) < 1e-30)
            throw std::runtime_error("SmallSolver: singular matrix in refactor");
        u_val_[u_end] = diag;

        for (int32_t p = l_cp_[col]; p < l_cp_[col + 1]; ++p)
            l_val_[p] = x_work_[l_ri_[p]] / diag;

        // Clear touched positions (restore x_work_ to zero)
        for (int32_t p = perm_cp_[col]; p < perm_cp_[col + 1]; ++p)
            x_work_[perm_ri_[p]] = 0.0;
        for (int32_t up = u_cp_[col]; up < u_end; ++up) {
            x_work_[piv_[u_ri_[up]]] = 0.0;
            for (int32_t p = l_cp_[u_ri_[up]]; p < l_cp_[u_ri_[up] + 1]; ++p)
                x_work_[l_ri_[p]] = 0.0;
        }
        x_work_[piv_[col]] = 0.0;
        for (int32_t p = l_cp_[col]; p < l_cp_[col + 1]; ++p)
            x_work_[l_ri_[p]] = 0.0;
    }
}

// Solve PAP^T·(Px) = P·b via LU: Ly = RPb, Ux' = y, x = P^T x'
void SmallSolver::sparse_solve_real(double* b) const {
    // y[step] = b_permuted[piv_[step]]  where b_permuted[i] = b[amd_perm_[i]]
    std::vector<double> y(n_);
    for (int32_t k = 0; k < n_; ++k)
        y[k] = b[amd_perm_[piv_[k]]];

    // Forward substitution: L is unit lower triangular
    for (int32_t col = 0; col < n_; ++col) {
        for (int32_t p = l_cp_[col]; p < l_cp_[col + 1]; ++p) {
            int32_t step = pinv_[l_ri_[p]];
            y[step] -= l_val_[p] * y[col];
        }
    }

    // Backward substitution: U
    for (int32_t col = n_ - 1; col >= 0; --col) {
        int32_t diag_pos = u_cp_[col + 1] - 1;
        y[col] /= u_val_[diag_pos];
        for (int32_t p = u_cp_[col]; p < diag_pos; ++p)
            y[u_ri_[p]] -= u_val_[p] * y[col];
    }

    // Unpermute: x[amd_perm_[k]] = y[k]
    for (int32_t k = 0; k < n_; ++k)
        b[amd_perm_[k]] = y[k];
}

// ---- Dense tier (unchanged) ----

void SmallSolver::scatter_to_dense(const double* csc_values) {
    std::fill(lu_.begin(), lu_.end(), 0.0);
    for (int32_t j = 0; j < n_; ++j)
        for (int32_t k = col_ptr_[j]; k < col_ptr_[j + 1]; ++k)
            lu_[j * n_ + row_idx_[k]] = csc_values[k];
}

void SmallSolver::dense_factor() {
    for (int32_t k = 0; k < n_; ++k) {
        int32_t best = k;
        double best_val = std::abs(lu_[k * n_ + k]);
        for (int32_t i = k + 1; i < n_; ++i) {
            double v = std::abs(lu_[k * n_ + i]);
            if (v > best_val) { best = i; best_val = v; }
        }
        pivot_[k] = best;
        if (best != k)
            for (int32_t j = 0; j < n_; ++j)
                std::swap(lu_[j * n_ + k], lu_[j * n_ + best]);
        double diag = lu_[k * n_ + k];
        if (std::abs(diag) < 1e-30)
            throw std::runtime_error("SmallSolver: singular matrix");
        for (int32_t i = k + 1; i < n_; ++i) {
            lu_[k * n_ + i] /= diag;
            for (int32_t j = k + 1; j < n_; ++j)
                lu_[j * n_ + i] -= lu_[k * n_ + i] * lu_[j * n_ + k];
        }
    }
}

void SmallSolver::dense_solve(double* rhs) const {
    for (int32_t k = 0; k < n_; ++k)
        if (pivot_[k] != k) std::swap(rhs[k], rhs[pivot_[k]]);
    for (int32_t k = 0; k < n_; ++k)
        for (int32_t i = k + 1; i < n_; ++i)
            rhs[i] -= lu_[k * n_ + i] * rhs[k];
    for (int32_t k = n_ - 1; k >= 0; --k) {
        rhs[k] /= lu_[k * n_ + k];
        for (int32_t i = 0; i < k; ++i)
            rhs[i] -= lu_[k * n_ + i] * rhs[k];
    }
}

// ---- Dispatch: numeric / refactorize / solve ----

void SmallSolver::numeric(const SparsityPattern& pattern, const NumericMatrix& mat) {
    if (!symbolized_)
        throw std::logic_error("SmallSolver::numeric: symbolic() not called");
    CSCData csc = pattern.to_csc();
    col_ptr_ = std::move(csc.col_ptr);
    row_idx_ = std::move(csc.row_idx);
    if (use_dense_) {
        scatter_to_dense(mat.data());
        dense_factor();
    } else {
        sparse_factor(mat.data());
    }
    factored_ = true;
}

void SmallSolver::refactorize(const NumericMatrix& mat) {
    if (!symbolized_)
        throw std::logic_error("SmallSolver::refactorize: symbolic() not called");
    if (!factored_)
        throw std::logic_error("SmallSolver::refactorize: numeric() not called");
    if (use_dense_) {
        scatter_to_dense(mat.data());
        dense_factor();
    } else {
        sparse_refactor(mat.data());
    }
}

void SmallSolver::solve(std::vector<double>& rhs) {
    if (!symbolized_)
        throw std::logic_error("SmallSolver::solve: symbolic() not called");
    if (!factored_)
        throw std::logic_error("SmallSolver::solve: numeric() not called");
    if (static_cast<int32_t>(rhs.size()) != n_)
        throw std::invalid_argument("SmallSolver::solve: rhs size mismatch");
    if (use_dense_)
        dense_solve(rhs.data());
    else
        sparse_solve_real(rhs.data());
}

// ---- Dense complex tier ----

void SmallSolver::scatter_to_dense_complex(const double* ax) {
    std::fill(lu_z_.begin(), lu_z_.end(), 0.0);
    for (int32_t j = 0; j < n_; ++j)
        for (int32_t k = col_ptr_[j]; k < col_ptr_[j + 1]; ++k) {
            int32_t idx = 2 * (j * n_ + row_idx_[k]);
            lu_z_[idx]     = ax[2 * k];
            lu_z_[idx + 1] = ax[2 * k + 1];
        }
}

void SmallSolver::dense_factor_complex() {
    for (int32_t k = 0; k < n_; ++k) {
        int32_t best = k;
        double best_abs = std::hypot(lu_z_[2*(k*n_+k)], lu_z_[2*(k*n_+k)+1]);
        for (int32_t i = k + 1; i < n_; ++i) {
            double v = std::hypot(lu_z_[2*(k*n_+i)], lu_z_[2*(k*n_+i)+1]);
            if (v > best_abs) { best = i; best_abs = v; }
        }
        pivot_z_[k] = best;
        if (best != k)
            for (int32_t j = 0; j < n_; ++j) {
                int32_t a = 2*(j*n_+k), b = 2*(j*n_+best);
                std::swap(lu_z_[a], lu_z_[b]);
                std::swap(lu_z_[a+1], lu_z_[b+1]);
            }
        double dr = lu_z_[2*(k*n_+k)], di = lu_z_[2*(k*n_+k)+1];
        double denom = dr*dr + di*di;
        if (denom < 1e-60)
            throw std::runtime_error("SmallSolver: singular complex matrix");
        for (int32_t i = k + 1; i < n_; ++i) {
            double ar = lu_z_[2*(k*n_+i)], ai = lu_z_[2*(k*n_+i)+1];
            double mr = (ar*dr + ai*di) / denom;
            double mi = (ai*dr - ar*di) / denom;
            lu_z_[2*(k*n_+i)] = mr;
            lu_z_[2*(k*n_+i)+1] = mi;
            for (int32_t j = k + 1; j < n_; ++j) {
                double er = lu_z_[2*(j*n_+k)], ei = lu_z_[2*(j*n_+k)+1];
                lu_z_[2*(j*n_+i)]   -= mr*er - mi*ei;
                lu_z_[2*(j*n_+i)+1] -= mr*ei + mi*er;
            }
        }
    }
}

void SmallSolver::dense_solve_complex(double* rhs) const {
    for (int32_t k = 0; k < n_; ++k)
        if (pivot_z_[k] != k) {
            std::swap(rhs[2*k], rhs[2*pivot_z_[k]]);
            std::swap(rhs[2*k+1], rhs[2*pivot_z_[k]+1]);
        }
    for (int32_t k = 0; k < n_; ++k)
        for (int32_t i = k + 1; i < n_; ++i) {
            double mr = lu_z_[2*(k*n_+i)], mi = lu_z_[2*(k*n_+i)+1];
            double xr = rhs[2*k], xi = rhs[2*k+1];
            rhs[2*i]   -= mr*xr - mi*xi;
            rhs[2*i+1] -= mr*xi + mi*xr;
        }
    for (int32_t k = n_-1; k >= 0; --k) {
        double dr = lu_z_[2*(k*n_+k)], di = lu_z_[2*(k*n_+k)+1];
        double denom = dr*dr + di*di;
        double xr = rhs[2*k], xi = rhs[2*k+1];
        rhs[2*k]   = (xr*dr + xi*di) / denom;
        rhs[2*k+1] = (xi*dr - xr*di) / denom;
        for (int32_t i = 0; i < k; ++i) {
            double ur = lu_z_[2*(k*n_+i)], ui = lu_z_[2*(k*n_+i)+1];
            rhs[2*i]   -= ur*rhs[2*k] - ui*rhs[2*k+1];
            rhs[2*i+1] -= ur*rhs[2*k+1] + ui*rhs[2*k];
        }
    }
}

// ---- Sparse complex ----

void SmallSolver::sparse_factor_complex(const double* orig_ax) {
    int32_t l_nnz = static_cast<int32_t>(l_ri_.size());
    int32_t u_nnz = static_cast<int32_t>(u_ri_.size());
    l_val_z_.resize(2 * l_nnz);
    u_val_z_.resize(2 * u_nnz);

    std::vector<double> xr(n_, 0.0), xi(n_, 0.0);

    for (int32_t col = 0; col < n_; ++col) {
        std::fill(xr.begin(), xr.end(), 0.0);
        std::fill(xi.begin(), xi.end(), 0.0);

        for (int32_t p = perm_cp_[col]; p < perm_cp_[col + 1]; ++p) {
            int32_t row = perm_ri_[p];
            int32_t oi = val_map_[p];
            xr[row] = orig_ax[2 * oi];
            xi[row] = orig_ax[2 * oi + 1];
        }

        // Left-looking: iterate through U entries for this column
        int32_t u_start = u_cp_[col];
        int32_t u_end = u_cp_[col + 1] - 1;  // last = diagonal

        for (int32_t up = u_start; up < u_end; ++up) {
            int32_t k = u_ri_[up];
            int32_t pr = piv_[k];
            double ur = xr[pr], ui = xi[pr];

            u_val_z_[2*up]   = ur;
            u_val_z_[2*up+1] = ui;

            if (ur == 0.0 && ui == 0.0) continue;

            for (int32_t p = l_cp_[k]; p < l_cp_[k + 1]; ++p) {
                int32_t row = l_ri_[p];
                double lr = l_val_z_[2*p], li = l_val_z_[2*p+1];
                xr[row] -= lr*ur - li*ui;
                xi[row] -= lr*ui + li*ur;
            }
        }

        // Diagonal
        int32_t pivot_row = piv_[col];
        double dr = xr[pivot_row], di = xi[pivot_row];
        double denom = dr*dr + di*di;
        if (denom < 1e-60)
            throw std::runtime_error("SmallSolver: singular complex matrix");

        u_val_z_[2*u_end]   = dr;
        u_val_z_[2*u_end+1] = di;

        // L values: x / diag
        for (int32_t p = l_cp_[col]; p < l_cp_[col + 1]; ++p) {
            int32_t row = l_ri_[p];
            double ar = xr[row], ai = xi[row];
            l_val_z_[2*p]   = (ar*dr + ai*di) / denom;
            l_val_z_[2*p+1] = (ai*dr - ar*di) / denom;
        }
    }

    sparse_factored_z_ = true;
}

void SmallSolver::sparse_refactor_complex(const double* orig_ax) {
    // xr/xi are member-sized workspaces; maintained at zero between columns
    std::vector<double> xr(n_, 0.0), xi(n_, 0.0);

    for (int32_t col = 0; col < n_; ++col) {
        for (int32_t p = perm_cp_[col]; p < perm_cp_[col + 1]; ++p) {
            int32_t row = perm_ri_[p];
            int32_t oi = val_map_[p];
            xr[row] = orig_ax[2 * oi];
            xi[row] = orig_ax[2 * oi + 1];
        }

        int32_t u_end = u_cp_[col + 1] - 1;

        for (int32_t up = u_cp_[col]; up < u_end; ++up) {
            int32_t k = u_ri_[up];
            int32_t pr = piv_[k];
            double ur = xr[pr], ui = xi[pr];

            u_val_z_[2*up]   = ur;
            u_val_z_[2*up+1] = ui;

            if (ur == 0.0 && ui == 0.0) continue;

            for (int32_t p = l_cp_[k]; p < l_cp_[k + 1]; ++p) {
                int32_t row = l_ri_[p];
                double lr = l_val_z_[2*p], li = l_val_z_[2*p+1];
                xr[row] -= lr*ur - li*ui;
                xi[row] -= lr*ui + li*ur;
            }
        }

        int32_t pivot_row = piv_[col];
        double dr = xr[pivot_row], di = xi[pivot_row];
        double denom = dr*dr + di*di;
        if (denom < 1e-60)
            throw std::runtime_error("SmallSolver: singular complex in refactor");

        u_val_z_[2*u_end]   = dr;
        u_val_z_[2*u_end+1] = di;

        for (int32_t p = l_cp_[col]; p < l_cp_[col + 1]; ++p) {
            int32_t row = l_ri_[p];
            double ar = xr[row], ai = xi[row];
            l_val_z_[2*p]   = (ar*dr + ai*di) / denom;
            l_val_z_[2*p+1] = (ai*dr - ar*di) / denom;
        }

        // Clear touched positions
        for (int32_t p = perm_cp_[col]; p < perm_cp_[col + 1]; ++p) {
            xr[perm_ri_[p]] = 0.0;
            xi[perm_ri_[p]] = 0.0;
        }
        for (int32_t up = u_cp_[col]; up < u_end; ++up) {
            int32_t pr = piv_[u_ri_[up]];
            xr[pr] = 0.0; xi[pr] = 0.0;
            for (int32_t p = l_cp_[u_ri_[up]]; p < l_cp_[u_ri_[up] + 1]; ++p) {
                xr[l_ri_[p]] = 0.0; xi[l_ri_[p]] = 0.0;
            }
        }
        xr[pivot_row] = 0.0; xi[pivot_row] = 0.0;
        for (int32_t p = l_cp_[col]; p < l_cp_[col + 1]; ++p) {
            xr[l_ri_[p]] = 0.0; xi[l_ri_[p]] = 0.0;
        }
    }
}

void SmallSolver::sparse_solve_complex(double* b) const {
    std::vector<double> yr(n_), yi(n_);
    for (int32_t k = 0; k < n_; ++k) {
        int32_t orig = amd_perm_[piv_[k]];
        yr[k] = b[2 * orig];
        yi[k] = b[2 * orig + 1];
    }

    // Forward sub: L unit lower
    for (int32_t col = 0; col < n_; ++col)
        for (int32_t p = l_cp_[col]; p < l_cp_[col + 1]; ++p) {
            int32_t step = pinv_[l_ri_[p]];
            double lr = l_val_z_[2*p], li = l_val_z_[2*p+1];
            yr[step] -= lr*yr[col] - li*yi[col];
            yi[step] -= lr*yi[col] + li*yr[col];
        }

    // Backward sub: U
    for (int32_t col = n_-1; col >= 0; --col) {
        int32_t dp = u_cp_[col+1] - 1;
        double dr = u_val_z_[2*dp], di = u_val_z_[2*dp+1];
        double denom = dr*dr + di*di;
        double xr = yr[col], xiv = yi[col];
        yr[col] = (xr*dr + xiv*di) / denom;
        yi[col] = (xiv*dr - xr*di) / denom;
        for (int32_t p = u_cp_[col]; p < dp; ++p) {
            int32_t step = u_ri_[p];
            double ur = u_val_z_[2*p], ui = u_val_z_[2*p+1];
            yr[step] -= ur*yr[col] - ui*yi[col];
            yi[step] -= ur*yi[col] + ui*yr[col];
        }
    }

    for (int32_t k = 0; k < n_; ++k) {
        b[2*amd_perm_[k]]     = yr[k];
        b[2*amd_perm_[k] + 1] = yi[k];
    }
}

// ---- Complex dispatch ----

void SmallSolver::numeric_complex(const SparsityPattern& pattern,
                                  const std::vector<double>& ax) {
    if (!symbolized_)
        throw std::logic_error("SmallSolver::numeric_complex: symbolic() not called");
    CSCData csc = pattern.to_csc();
    col_ptr_ = std::move(csc.col_ptr);
    row_idx_ = std::move(csc.row_idx);
    if (use_dense_) {
        scatter_to_dense_complex(ax.data());
        dense_factor_complex();
        factored_z_ = true;
    } else {
        if (!factored_) {
            // Need real factorization to establish L/U structure and pivots
            std::vector<double> mag(ax.size() / 2);
            for (size_t i = 0; i < mag.size(); ++i)
                mag[i] = std::hypot(ax[2*i], ax[2*i+1]);
            sparse_factor(mag.data());
        }
        sparse_factor_complex(ax.data());
    }
}

void SmallSolver::refactorize_complex(const std::vector<double>& ax) {
    if (!symbolized_)
        throw std::logic_error("SmallSolver::refactorize_complex: symbolic() not called");
    if (use_dense_) {
        if (!factored_z_)
            throw std::logic_error("SmallSolver::refactorize_complex: numeric_complex() not called");
        scatter_to_dense_complex(ax.data());
        dense_factor_complex();
    } else {
        if (!sparse_factored_z_)
            throw std::logic_error("SmallSolver::refactorize_complex: numeric_complex() not called");
        sparse_refactor_complex(ax.data());
    }
}

void SmallSolver::solve_complex(std::vector<double>& rhs) {
    if (!symbolized_)
        throw std::logic_error("SmallSolver::solve_complex: symbolic() not called");
    if (static_cast<int32_t>(rhs.size()) != 2 * n_)
        throw std::invalid_argument("SmallSolver::solve_complex: rhs size must be 2*n");
    if (use_dense_) {
        if (!factored_z_)
            throw std::logic_error("SmallSolver::solve_complex: numeric_complex() not called");
        dense_solve_complex(rhs.data());
    } else {
        if (!sparse_factored_z_)
            throw std::logic_error("SmallSolver::solve_complex: numeric_complex() not called");
        sparse_solve_complex(rhs.data());
    }
}

}  // namespace neospice
