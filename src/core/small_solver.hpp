#pragma once
#include "core/linear_solver.hpp"
#include <vector>

namespace neospice {

class SmallSolver : public LinearSolver {
public:
    SmallSolver();
    ~SmallSolver() override = default;

    void symbolic(const SparsityPattern& pattern) override;
    void numeric(const SparsityPattern& pattern, const NumericMatrix& mat) override;
    void refactorize(const NumericMatrix& mat) override;
    void solve(std::vector<double>& rhs) override;
    void numeric_complex(const SparsityPattern& pattern,
                         const std::vector<double>& ax) override;
    void refactorize_complex(const std::vector<double>& ax) override;
    void solve_complex(std::vector<double>& rhs) override;

private:
    static constexpr int32_t DENSE_LIMIT = 12;
    // Diagonal pivot is accepted if |diag| >= threshold * max_col.
    // KLU uses 0.001 but relies on SuiteSparse AMD's high-quality ordering.
    // Our simpler AMD needs a stronger threshold for numerical stability.
    static constexpr double PIVOT_THRESHOLD = 0.1;

    int32_t n_ = 0;
    bool symbolized_ = false;
    bool factored_ = false;
    bool use_dense_ = true;

    // Original CSC from symbolic
    std::vector<int32_t> col_ptr_;
    std::vector<int32_t> row_idx_;

    // ---------- Dense tier (n < DENSE_LIMIT) ----------
    std::vector<double> lu_;
    std::vector<int32_t> pivot_;
    std::vector<double> lu_z_;
    std::vector<int32_t> pivot_z_;
    bool factored_z_ = false;

    void scatter_to_dense(const double* csc_values);
    void dense_factor();
    void dense_solve(double* rhs) const;
    void scatter_to_dense_complex(const double* ax);
    void dense_factor_complex();
    void dense_solve_complex(double* rhs) const;

    // ---------- Sparse tier (n >= DENSE_LIMIT) ----------
    // AMD ordering
    std::vector<int32_t> amd_perm_;    // P[new] = old_col
    std::vector<int32_t> amd_inv_;     // Q[old] = new_col

    // Permuted CSC: PAP^T
    std::vector<int32_t> perm_cp_;
    std::vector<int32_t> perm_ri_;
    std::vector<int32_t> val_map_;     // perm entry k came from original CSC index val_map_[k]

    // L in CSC: unit lower triangular (diagonal = 1 not stored)
    std::vector<int32_t> l_cp_;
    std::vector<int32_t> l_ri_;        // physical rows (in permuted space)
    std::vector<double>  l_val_;

    // U in CSC: upper triangular, diagonal is last entry per column
    std::vector<int32_t> u_cp_;
    std::vector<int32_t> u_ri_;        // logical row = factorization step
    std::vector<double>  u_val_;

    // Row permutation: pinv_[physical_row] = step, piv_[step] = physical_row
    std::vector<int32_t> pinv_;
    std::vector<int32_t> piv_;

    // Dense accumulator for factorization
    std::vector<double> x_work_;

    // Complex L/U values (same structure as real)
    std::vector<double> l_val_z_;
    std::vector<double> u_val_z_;
    bool sparse_factored_z_ = false;

    void build_permuted_csc();
    void sparse_factor(const double* orig_values);
    void sparse_refactor(const double* orig_values);
    void sparse_solve_real(double* b) const;
    void sparse_factor_complex(const double* orig_ax);
    void sparse_refactor_complex(const double* orig_ax);
    void sparse_solve_complex(double* b) const;
};

}  // namespace neospice
