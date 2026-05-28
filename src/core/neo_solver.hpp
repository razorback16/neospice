#pragma once
#include "core/matrix.hpp"
#include <memory>
#include <vector>

namespace neospice {

namespace solver { class SparseMatrix; }

class NeoSolver {
public:
    NeoSolver();
    ~NeoSolver();

    NeoSolver(const NeoSolver&) = delete;
    NeoSolver& operator=(const NeoSolver&) = delete;
    NeoSolver(NeoSolver&&) noexcept;
    NeoSolver& operator=(NeoSolver&&) noexcept;

    void symbolic(const SparsityPattern& pattern);
    bool numeric(const SparsityPattern& pattern, const NumericMatrix& mat,
                 double diag_gmin = 0.0);
    bool refactorize(const NumericMatrix& mat, double diag_gmin = 0.0);
    void solve(std::vector<double>& rhs);
    void numeric_complex(const SparsityPattern& pattern,
                         const std::vector<double>& ax);
    bool refactorize_complex(const std::vector<double>& ax);
    void solve_complex(std::vector<double>& rhs);

private:
    int32_t n_ = 0;
    int32_t nnz_ = 0;
    bool symbolized_ = false;
    bool factored_ = false;
    bool factored_complex_ = false;
    bool preordered_ = false;

    std::unique_ptr<solver::SparseMatrix> matrix_;
    std::vector<double*> element_ptrs_;

    std::vector<double> rhs_1_;
    std::vector<double> sol_1_;
    std::vector<double> irhs_1_;
    std::vector<double> isol_1_;

    void load_real(const double* values);
    void load_complex(const double* ax);
};

}  // namespace neospice
