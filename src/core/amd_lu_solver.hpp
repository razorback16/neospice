#pragma once
#include "core/matrix.hpp"
#include "core/neo_solver.hpp"
#include "core/solver_iface.hpp"
#include <memory>
#include <vector>

namespace neospice {

// AMD-ordered, left-looking (Gilbert-Peierls) sparse LU solver with threshold
// partial pivoting. Drop-in replacement for NeoSolver on the real DC/transient
// path, selectable via NEOSPICE_SOLVER=amdlu / NEOSPICE_FORCE_AMDLU=1.
//
// Design notes:
//  - symbolic(): computes an AMD column ordering on the structurally-symmetric
//    pattern (A | A^T) and records the CSC layout of A used to scatter values.
//  - numeric()/refactorize(): factor with partial pivoting (KLU-style tol).
//  - solve(): permuted L\ then U\ substitution with row pivot perm + AMD col
//    perm + RHS permutation, matching the LU produced by numeric().
//  - complex variants delegate to an internal NeoSolver (AC/noise stay on the
//    Markowitz path). This keeps scope to the real DC/transient path.
class AmdLuSolver : public ISolver {
public:
    AmdLuSolver();
    ~AmdLuSolver() override;

    AmdLuSolver(const AmdLuSolver&) = delete;
    AmdLuSolver& operator=(const AmdLuSolver&) = delete;

    void symbolic(const SparsityPattern& pattern) override;
    bool numeric(const SparsityPattern& pattern, const NumericMatrix& mat,
                 double diag_gmin = 0.0) override;
    bool refactorize(const NumericMatrix& mat, double diag_gmin = 0.0) override;
    void solve(std::vector<double>& rhs) override;
    void numeric_complex(const SparsityPattern& pattern,
                         const std::vector<double>& ax) override;
    bool refactorize_complex(const std::vector<double>& ax) override;
    void solve_complex(std::vector<double>& rhs) override;

private:
    int32_t n_ = 0;
    int32_t nnz_ = 0;
    bool symbolized_ = false;
    bool factored_ = false;

    // Pivot threshold (KLU default 0.001): accept the diagonal candidate if its
    // magnitude is >= tol * max-magnitude in the column.
    double pivot_tol_ = 1e-3;

    // ---- Symbolic data ----
    // CSC structure of A (the original, un-permuted matrix), matching the order
    // of NumericMatrix values exactly so values[] scatters directly.
    std::vector<int32_t> a_colptr_;   // n+1
    std::vector<int32_t> a_rowidx_;   // nnz (row indices for each value slot)
    // AMD column ordering: q_[k] = original column eliminated at step k.
    std::vector<int32_t> q_;          // n  (perm: new -> old)

    // ---- Numeric data (filled by factor) ----
    // L (unit lower) and U (upper) in CSC, indexed by the elimination order
    // (i.e. "row k" here means the k-th pivot). U(k,k) is last in U col, L(k,k)=1
    // is first in L col.
    std::vector<int32_t> Lp_, Li_;
    std::vector<double> Lx_;
    std::vector<int32_t> Up_, Ui_;
    std::vector<double> Ux_;
    // pinv_[orig_row] = pivot step at which that row became pivotal, or -1.
    std::vector<int32_t> pinv_;

    // ---- Refactor replay data (KLU-style fast path) ----
    // Recorded by factor_() so refactorize() can recompute numeric values along
    // the SAME structure and pivot order with no DFS and no pivot search.
    //
    // For each value slot p of A (matching NumericMatrix values[]), the
    // pivot-step row it scatters into during the per-column solve. This is just
    // pinv_[a_rowidx_[p]] precomputed once, so refactor can scatter directly.
    std::vector<int32_t> scatter_row_;  // nnz: pivot-step row for value slot p
    bool replay_ready_ = false;         // true once factor_ recorded replay data

    // scratch for solve
    std::vector<double> x_;

    // Last full-factor diag_gmin, reused by the replay path.
    double last_gmin_ = 0.0;

    // Diagnostics / test hooks: count which path refactorize() took.
    int32_t refactor_fast_count_ = 0;     // replay (no pivot) succeeded
    int32_t refactor_fallback_count_ = 0;  // fell back to full factor_()

    bool factor_(const NumericMatrix& mat, double diag_gmin);
    bool refactor_replay_(const NumericMatrix& mat, double diag_gmin);

public:
    // Test/diagnostic accessors for the refactor fast-path counters.
    int32_t refactor_fast_count() const { return refactor_fast_count_; }
    int32_t refactor_fallback_count() const { return refactor_fallback_count_; }

private:

    // Complex ops delegate to a Markowitz solver.
    std::unique_ptr<NeoSolver> complex_solver_;
};

}  // namespace neospice
