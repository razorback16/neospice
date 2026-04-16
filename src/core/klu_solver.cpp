#include "core/klu_solver.hpp"
#include <suitesparse/klu.h>
#include <stdexcept>

namespace cudaspice {

KLUSolver::KLUSolver()
    : common_(new klu_common),
      symbolic_(nullptr),
      numeric_(nullptr)
{
    klu_defaults(common_);
}

KLUSolver::~KLUSolver()
{
    if (numeric_) {
        klu_free_numeric(&numeric_, common_);
    }
    if (symbolic_) {
        klu_free_symbolic(&symbolic_, common_);
    }
    delete common_;
}

void KLUSolver::symbolic(const SparsityPattern& pattern)
{
    // Free existing factorizations whenever the pattern changes.
    if (numeric_) {
        klu_free_numeric(&numeric_, common_);
        numeric_ = nullptr;
    }
    if (symbolic_) {
        klu_free_symbolic(&symbolic_, common_);
        symbolic_ = nullptr;
    }

    CSCData csc = pattern.to_csc();
    col_ptr_ = std::move(csc.col_ptr);
    row_idx_ = std::move(csc.row_idx);

    symbolic_ = klu_analyze(
        pattern.size(),
        col_ptr_.data(),
        row_idx_.data(),
        common_);

    if (!symbolic_) {
        throw std::runtime_error("KLUSolver::symbolic: klu_analyze failed");
    }
}

void KLUSolver::numeric(const SparsityPattern& pattern, const NumericMatrix& mat)
{
    if (!symbolic_) {
        throw std::logic_error("KLUSolver::numeric: call symbolic() first");
    }

    // Free any prior numeric object.
    if (numeric_) {
        klu_free_numeric(&numeric_, common_);
        numeric_ = nullptr;
    }

    // Refresh CSC structure (pattern may have been rebuilt).
    CSCData csc = pattern.to_csc();
    col_ptr_ = std::move(csc.col_ptr);
    row_idx_ = std::move(csc.row_idx);

    numeric_ = klu_factor(
        col_ptr_.data(),
        row_idx_.data(),
        const_cast<double*>(mat.data()),
        symbolic_,
        common_);

    if (!numeric_) {
        throw std::runtime_error("KLUSolver::numeric: klu_factor failed");
    }
}

void KLUSolver::refactorize(const NumericMatrix& mat)
{
    if (!symbolic_) {
        throw std::logic_error("KLUSolver::refactorize: call symbolic() first");
    }
    if (!numeric_) {
        throw std::logic_error("KLUSolver::refactorize: call numeric() first");
    }

    int ok = klu_refactor(
        col_ptr_.data(),
        row_idx_.data(),
        const_cast<double*>(mat.data()),
        symbolic_,
        numeric_,
        common_);

    if (!ok) {
        throw std::runtime_error("KLUSolver::refactorize: klu_refactor failed");
    }
}

void KLUSolver::solve(std::vector<double>& rhs)
{
    if (!symbolic_) {
        throw std::logic_error("KLUSolver::solve: call symbolic() first");
    }
    if (!numeric_) {
        throw std::logic_error("KLUSolver::solve: call numeric() or refactorize() first");
    }

    int32_t n = symbolic_->n;
    if (static_cast<int32_t>(rhs.size()) != n) {
        throw std::invalid_argument("KLUSolver::solve: rhs size does not match matrix dimension");
    }

    int ok = klu_solve(
        symbolic_,
        numeric_,
        n,      // ldim
        1,      // nrhs
        rhs.data(),
        common_);

    if (!ok) {
        throw std::runtime_error("KLUSolver::solve: klu_solve failed");
    }
}

}  // namespace cudaspice
