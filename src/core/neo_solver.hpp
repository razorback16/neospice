#pragma once
#include "core/matrix.hpp"
#include <vector>

namespace neospice {

class NeoSolver {
public:
    NeoSolver();
    ~NeoSolver() = default;

    NeoSolver(const NeoSolver&) = delete;
    NeoSolver& operator=(const NeoSolver&) = delete;

    void symbolic(const SparsityPattern& pattern);
    bool numeric(const SparsityPattern& pattern, const NumericMatrix& mat);
    bool refactorize(const NumericMatrix& mat);
    void solve(std::vector<double>& rhs);
    void numeric_complex(const SparsityPattern& pattern,
                         const std::vector<double>& ax);
    bool refactorize_complex(const std::vector<double>& ax);
    void solve_complex(std::vector<double>& rhs);

private:
    static constexpr int32_t DENSE_LIMIT = 12;
    // Diagonal pivot is accepted if |diag| >= threshold * max_col.
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
    bool dense_factor();
    void dense_solve(double* rhs) const;
    void scatter_to_dense_complex(const double* ax);
    bool dense_factor_complex();
    void dense_solve_complex(double* rhs) const;

    // ---------- Sparse tier (n >= DENSE_LIMIT) ----------
    // Maximum transversal row permutation (match_perm_[col] = row)
    std::vector<int32_t> match_perm_;

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
    std::vector<double> xr_work_;     // complex factorization real workspace
    std::vector<double> xi_work_;     // complex factorization imag workspace
    std::vector<double> yr_work_;     // complex solve real workspace
    std::vector<double> yi_work_;     // complex solve imag workspace

    // Complex L/U values (same structure as real)
    std::vector<double> l_val_z_;
    std::vector<double> u_val_z_;
    bool sparse_factored_z_ = false;

    void build_permuted_csc();
    bool sparse_factor(const double* orig_values);
    bool sparse_refactor(const double* orig_values);
    void sparse_solve_real(double* b) const;
    void sparse_factor_complex(const double* orig_ax);
    bool sparse_refactor_complex(const double* orig_ax);
    void sparse_solve_complex(double* b);
};

}  // namespace neospice
