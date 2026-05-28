#include "core/neo_solver.hpp"
#include "solver/matrix.hpp"
#include <cstddef>
#include <stdexcept>

namespace neospice {

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

    int32_t k = 0;
    for (int32_t j = 0; j < n_; ++j)
        for (int32_t p = csc.col_ptr[j]; p < csc.col_ptr[j + 1]; ++p)
            element_ptrs_[k++] = matrix_->get_element(csc.row_idx[p] + 1, j + 1);

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

    load_real(mat.data());
    if (!preordered_) {
        matrix_->mna_preorder();
        preordered_ = true;
    }
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

    load_real(mat.data());
    matrix_->add_diag_gmin(diag_gmin);
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

    for (int32_t i = 0; i < n_; ++i)
        rhs_1_[i + 1] = rhs[i];

    matrix_->solve(rhs_1_.data(), sol_1_.data());

    for (int32_t i = 0; i < n_; ++i)
        rhs[i] = sol_1_[i + 1];
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
