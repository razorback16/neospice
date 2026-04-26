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
    static constexpr int32_t DENSE_LIMIT = 25;

    int32_t n_ = 0;
    bool symbolized_ = false;
    bool factored_ = false;
    bool use_dense_ = true;

    // CSC structure from symbolic
    std::vector<int32_t> col_ptr_;
    std::vector<int32_t> row_idx_;

    // AMD ordering (sparse tier, n >= DENSE_LIMIT)
    std::vector<int32_t> amd_perm_;    // AMD column permutation P[new] = old
    std::vector<int32_t> amd_inv_;     // inverse: amd_inv_[old] = new

    // Dense tier: column-major n x n LU factors (in-place)
    std::vector<double> lu_;
    std::vector<int32_t> pivot_;  // partial pivot permutation

    // Dense complex tier: interleaved real/imag in column-major n×n layout
    std::vector<double> lu_z_;        // 2*n*n doubles (interleaved)
    std::vector<int32_t> pivot_z_;    // pivot permutation for complex
    bool factored_z_ = false;

    void scatter_to_dense(const double* csc_values);
    void scatter_to_dense_amd(const double* csc_values);
    void dense_factor();
    void dense_solve(double* rhs) const;

    void scatter_to_dense_complex(const double* ax);
    void scatter_to_dense_complex_amd(const double* ax);
    void dense_factor_complex();
    void dense_solve_complex(double* rhs) const;
};

}  // namespace neospice
