#include "core/neo_solver.hpp"
#include "solver/matrix.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace neospice {

namespace {
// [3D] Round a positive value to the nearest power of two (in log2). Scaling a
// double by an exact power of two only shifts its exponent — the mantissa is
// bit-exact — so the scaled factorization is numerically equivalent to the
// unscaled one and the recovered solution is provably identical for
// well-conditioned systems. This is exactly why LAPACK's dgeequ rounds
// equilibration factors to radix-2.
inline double pow2_round(double v) {
    if (!(v > 0.0) || !std::isfinite(v))
        return 1.0;  // empty/degenerate row or column: leave it unscaled
    int e = 0;
    // frexp: v = m * 2^e with m in [0.5, 1). The two candidate powers of two
    // are 2^(e-1) (=0.5*2^e) and 2^e; the boundary in log2 is m == sqrt(1/2).
    double m = std::frexp(v, &e);
    return (m < 0.70710678118654752440) ? std::ldexp(1.0, e - 1)
                                        : std::ldexp(1.0, e);
}
}  // namespace

static_assert(
    offsetof(solver::MatrixElement, Imag) ==
    offsetof(solver::MatrixElement, Real) + sizeof(double),
    "Real and Imag must be adjacent in MatrixElement");

NeoSolver::NeoSolver() = default;
NeoSolver::~NeoSolver() = default;
NeoSolver::NeoSolver(NeoSolver&&) noexcept = default;
NeoSolver& NeoSolver::operator=(NeoSolver&&) noexcept = default;

void NeoSolver::load_real(const double* values) {
    matrix_->set_real();
    matrix_->clear_for_load();
    for (int32_t k = 0; k < nnz_; ++k)
        *element_ptrs_[k] = values[k];
}

// [3D] Compute radix-2 row/column scale factors D_r, D_c from the supplied
// values and load the scaled matrix (D_r A D_c) plus the scaled diagonal
// regularization. dr_[i]·dc_[i]·diag_gmin is added to each diagonal so the
// regularization composes correctly with scaling. Two passes (row then col,
// then a refining row pass) give a well-balanced result.
void NeoSolver::load_real_equilibrated(const double* values, double diag_gmin) {
    dr_.assign(n_, 1.0);
    dc_.assign(n_, 1.0);

    // Row infinity-norm sweep: D_r[i] = pow2(1 / max_j |A_ij|).
    std::vector<double> row_max(n_, 0.0);
    for (int32_t k = 0; k < nnz_; ++k) {
        double a = std::abs(values[k]);
        if (a > row_max[row_idx_[k]]) row_max[row_idx_[k]] = a;
    }
    for (int32_t i = 0; i < n_; ++i)
        dr_[i] = pow2_round(row_max[i] > 0.0 ? 1.0 / row_max[i] : 1.0);

    // Column infinity-norm sweep on the row-scaled matrix:
    // D_c[j] = pow2(1 / max_i |D_r[i] A_ij|).
    std::vector<double> col_max(n_, 0.0);
    for (int32_t k = 0; k < nnz_; ++k) {
        double a = std::abs(dr_[row_idx_[k]] * values[k]);
        if (a > col_max[col_idx_[k]]) col_max[col_idx_[k]] = a;
    }
    for (int32_t j = 0; j < n_; ++j)
        dc_[j] = pow2_round(col_max[j] > 0.0 ? 1.0 / col_max[j] : 1.0);

    // One refining row sweep on the fully-scaled matrix.
    std::fill(row_max.begin(), row_max.end(), 0.0);
    for (int32_t k = 0; k < nnz_; ++k) {
        double a = std::abs(dr_[row_idx_[k]] * values[k] * dc_[col_idx_[k]]);
        if (a > row_max[row_idx_[k]]) row_max[row_idx_[k]] = a;
    }
    for (int32_t i = 0; i < n_; ++i)
        if (row_max[i] > 0.0)
            dr_[i] *= pow2_round(1.0 / row_max[i]);

    // Load scaled values: (D_r A D_c).
    matrix_->set_real();
    matrix_->clear_for_load();
    for (int32_t k = 0; k < nnz_; ++k)
        *element_ptrs_[k] = dr_[row_idx_[k]] * values[k] * dc_[col_idx_[k]];

    // Add the scaled diagonal regularization directly (cannot use the solver's
    // uniform add_diag_gmin, because each diagonal needs dr_[i]*dc_[i]*gmin).
    if (diag_gmin != 0.0)
        for (int32_t k = 0; k < nnz_; ++k)
            if (row_idx_[k] == col_idx_[k])
                *element_ptrs_[k] += dr_[row_idx_[k]] * dc_[row_idx_[k]] * diag_gmin;
}

void NeoSolver::load_complex(const double* ax) {
    matrix_->set_complex();
    matrix_->clear_for_load();
    for (int32_t k = 0; k < nnz_; ++k) {
        element_ptrs_[k][0] = ax[2 * k];
        element_ptrs_[k][1] = ax[2 * k + 1];
    }
}

void NeoSolver::symbolic(const SparsityPattern& pattern) {
    n_ = pattern.size();
    CSCData csc = pattern.to_csc();
    nnz_ = static_cast<int32_t>(csc.row_idx.size());

    factored_ = false;
    factored_complex_ = false;
    preordered_ = false;

    matrix_ = std::make_unique<solver::SparseMatrix>(n_);
    element_ptrs_.resize(nnz_);
    // [3D] Record each nonzero's (row, col) in element_ptrs_ order so
    // equilibration can apply per-row/col scale factors. Default scales are 1.0
    // so the off path recovers the unscaled RHS/solution exactly.
    row_idx_.resize(nnz_);
    col_idx_.resize(nnz_);
    dr_.assign(n_, 1.0);
    dc_.assign(n_, 1.0);

    int32_t k = 0;
    for (int32_t j = 0; j < n_; ++j)
        for (int32_t p = csc.col_ptr[j]; p < csc.col_ptr[j + 1]; ++p) {
            row_idx_[k] = csc.row_idx[p];
            col_idx_[k] = j;
            element_ptrs_[k++] = matrix_->get_element(csc.row_idx[p] + 1, j + 1);
        }

    rhs_1_.resize(n_ + 1);
    sol_1_.resize(n_ + 1);
    irhs_1_.resize(n_ + 1);
    isol_1_.resize(n_ + 1);

    symbolized_ = true;
}

bool NeoSolver::numeric(const SparsityPattern& /*pattern*/,
                         const NumericMatrix& mat,
                         double diag_gmin) {
    if (!symbolized_)
        throw std::logic_error("NeoSolver::numeric: symbolic() not called");

    // [3D] When equilibration is on, load the scaled matrix (D_r A D_c) and add
    // the scaled diag_gmin ourselves; otherwise the original (bit-identical)
    // path: load raw values and let the solver add the uniform diag_gmin.
    if (equilibrate_) {
        load_real_equilibrated(mat.data(), diag_gmin);
    } else {
        load_real(mat.data());
    }
    if (!preordered_) {
        matrix_->mna_preorder();
        preordered_ = true;
    }
    if (!equilibrate_)
        matrix_->add_diag_gmin(diag_gmin);
    auto err = matrix_->order_and_factor(nullptr, 1e-3, 1e-13, true);
    if (err == solver::SparseError::NoMemory)
        throw std::runtime_error("NeoSolver::numeric: out of memory");
    factored_ = true;
    factored_complex_ = false;
    return err == solver::SparseError::Singular;
}

bool NeoSolver::refactorize(const NumericMatrix& mat, double diag_gmin) {
    if (!symbolized_)
        throw std::logic_error("NeoSolver::refactorize: symbolic() not called");
    if (!factored_)
        throw std::logic_error("NeoSolver::refactorize: numeric() not called");

    // [3D] Recompute scale factors for the new values (Newton updates them every
    // iteration) when equilibration is on; otherwise the bit-identical path.
    if (equilibrate_) {
        load_real_equilibrated(mat.data(), diag_gmin);
    } else {
        load_real(mat.data());
        matrix_->add_diag_gmin(diag_gmin);
    }
    auto err = matrix_->factor();
    if (err == solver::SparseError::NoMemory)
        throw std::runtime_error("NeoSolver::refactorize: out of memory");
    return err == solver::SparseError::Singular;
}

void NeoSolver::solve(std::vector<double>& rhs) {
    if (!symbolized_)
        throw std::logic_error("NeoSolver::solve: symbolic() not called");
    if (!factored_)
        throw std::logic_error("NeoSolver::solve: numeric() not called");
    if (static_cast<int32_t>(rhs.size()) != n_)
        throw std::invalid_argument("NeoSolver::solve: rhs size mismatch");

    // [3D] Scale the RHS by D_r: the factored matrix is (D_r A D_c), so we solve
    // (D_r A D_c) y = (D_r b). dr_/dc_ are exactly 1.0 when equilibration is off,
    // and multiplying a finite double by 1.0 is a bit-exact no-op, so the
    // default path is unchanged.
    if (equilibrate_) {
        for (int32_t i = 0; i < n_; ++i)
            rhs_1_[i + 1] = dr_[i] * rhs[i];
    } else {
        for (int32_t i = 0; i < n_; ++i)
            rhs_1_[i + 1] = rhs[i];
    }

    matrix_->solve(rhs_1_.data(), sol_1_.data());

    // [3D] Recover the unscaled solution x = D_c y.
    if (equilibrate_) {
        for (int32_t i = 0; i < n_; ++i)
            rhs[i] = dc_[i] * sol_1_[i + 1];
    } else {
        for (int32_t i = 0; i < n_; ++i)
            rhs[i] = sol_1_[i + 1];
    }
}

void NeoSolver::numeric_complex(const SparsityPattern& /*pattern*/,
                                 const std::vector<double>& ax) {
    if (!symbolized_)
        throw std::logic_error("NeoSolver::numeric_complex: symbolic() not called");
    if (static_cast<int32_t>(ax.size()) != 2 * nnz_)
        throw std::invalid_argument("NeoSolver::numeric_complex: ax size mismatch");

    load_complex(ax.data());
    if (!preordered_) {
        matrix_->mna_preorder();
        preordered_ = true;
    }
    auto err = matrix_->order_and_factor(nullptr, 1e-3, 1e-13, true);
    if (err == solver::SparseError::NoMemory)
        throw std::runtime_error("NeoSolver::numeric_complex: out of memory");
    factored_complex_ = true;
    factored_ = false;
}

bool NeoSolver::refactorize_complex(const std::vector<double>& ax) {
    if (!symbolized_)
        throw std::logic_error("NeoSolver::refactorize_complex: symbolic() not called");
    if (!factored_complex_)
        throw std::logic_error("NeoSolver::refactorize_complex: numeric_complex() not called");
    if (static_cast<int32_t>(ax.size()) != 2 * nnz_)
        throw std::invalid_argument("NeoSolver::refactorize_complex: ax size mismatch");

    load_complex(ax.data());
    auto err = matrix_->factor();
    return err == solver::SparseError::Singular;
}

void NeoSolver::solve_complex(std::vector<double>& rhs) {
    if (!symbolized_)
        throw std::logic_error("NeoSolver::solve_complex: symbolic() not called");
    if (!factored_complex_)
        throw std::logic_error("NeoSolver::solve_complex: numeric_complex() not called");
    if (static_cast<int32_t>(rhs.size()) != 2 * n_)
        throw std::invalid_argument("NeoSolver::solve_complex: rhs size must be 2*n");

    for (int32_t i = 0; i < n_; ++i) {
        rhs_1_[i + 1] = rhs[2 * i];
        irhs_1_[i + 1] = rhs[2 * i + 1];
    }

    matrix_->solve(rhs_1_.data(), sol_1_.data(), irhs_1_.data(), isol_1_.data());

    for (int32_t i = 0; i < n_; ++i) {
        rhs[2 * i] = sol_1_[i + 1];
        rhs[2 * i + 1] = isol_1_[i + 1];
    }
}

}  // namespace neospice
