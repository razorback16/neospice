#pragma once
#include "core/matrix.hpp"
#include "core/solver_iface.hpp"
#include <memory>
#include <vector>

namespace neospice {

namespace solver { class SparseMatrix; }

class NeoSolver : public ISolver {
public:
    NeoSolver();
    ~NeoSolver() override;

    NeoSolver(const NeoSolver&) = delete;
    NeoSolver& operator=(const NeoSolver&) = delete;
    NeoSolver(NeoSolver&&) noexcept;
    NeoSolver& operator=(NeoSolver&&) noexcept;

    void symbolic(const SparsityPattern& pattern) override;
    bool numeric(const SparsityPattern& pattern, const NumericMatrix& mat,
                 double diag_gmin = 0.0) override;
    bool refactorize(const NumericMatrix& mat, double diag_gmin = 0.0) override;
    void solve(std::vector<double>& rhs) override;
    void numeric_complex(const SparsityPattern& pattern,
                         const std::vector<double>& ax) override;
    bool refactorize_complex(const std::vector<double>& ax) override;
    void solve_complex(std::vector<double>& rhs) override;
    const char* name() const override { return "markowitz"; }

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
