#pragma once
#include "core/matrix.hpp"
#include <suitesparse/klu.h>
#include <vector>

namespace cudaspice {

/// Thin wrapper around KLU that provides symbolic analysis, numeric
/// factorization, refactorization, and triangular solve.
///
/// Typical usage:
///   KLUSolver solver;
///   solver.symbolic(pattern);          // once per sparsity pattern
///   solver.numeric(pattern, mat);      // full factorization
///   solver.solve(rhs);                 // solve Ax = b in-place
///
///   // same pattern, new values:
///   solver.refactorize(mat);
///   solver.solve(rhs);
class KLUSolver {
public:
    KLUSolver();
    ~KLUSolver();

    KLUSolver(const KLUSolver&) = delete;
    KLUSolver& operator=(const KLUSolver&) = delete;

    /// Perform symbolic analysis (ordering + BTF).  Call once per sparsity
    /// pattern.  Frees any existing symbolic/numeric objects first.
    void symbolic(const SparsityPattern& pattern);

    /// Full numeric factorization.  Must call symbolic() first.
    void numeric(const SparsityPattern& pattern, const NumericMatrix& mat);

    /// Refactorize with the same sparsity pattern but updated values.
    /// Faster than a full numeric() call.  Requires a prior numeric() call.
    void refactorize(const NumericMatrix& mat);

    /// Solve Ax = b in-place (rhs is overwritten with the solution).
    /// Requires a prior numeric() or refactorize() call.
    void solve(std::vector<double>& rhs);

private:
    klu_common*   common_;
    klu_symbolic* symbolic_;
    klu_numeric*  numeric_;

    // CSC arrays kept alive between symbolic/numeric/refactorize calls.
    std::vector<int32_t> col_ptr_;
    std::vector<int32_t> row_idx_;
};

}  // namespace cudaspice
