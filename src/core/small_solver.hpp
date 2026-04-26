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
    int32_t n_ = 0;
    bool symbolized_ = false;
    bool factored_ = false;

    // CSC structure from symbolic
    std::vector<int32_t> col_ptr_;
    std::vector<int32_t> row_idx_;

    // Dense tier: column-major n x n LU factors (in-place)
    std::vector<double> lu_;
    std::vector<int32_t> pivot_;  // partial pivot permutation

    void scatter_to_dense(const double* csc_values);
    void dense_factor();
    void dense_solve(double* rhs) const;
};

}  // namespace neospice
