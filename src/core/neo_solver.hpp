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

    // [3D] Row/column equilibration toggle. Default OFF: when off, behavior is
    // bit-for-bit identical to the pre-equilibration solver (the certified
    // baseline). Only ever enabled as a late convergence fallback in dc.cpp.
    void set_equilibrate(bool on) override { equilibrate_ = on; }

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

    // [3D] Equilibration state. row_idx_/col_idx_ record the (0-based) row and
    // column of each nonzero in element_ptrs_ order, captured in symbolic().
    // dr_/dc_ are the radix-2 row/column scale factors used to recover the
    // unscaled RHS/solution in solve(); they are all 1.0 when equilibration is
    // off, making the recovery a no-op.
    bool equilibrate_ = false;
    std::vector<int32_t> row_idx_;
    std::vector<int32_t> col_idx_;
    std::vector<double> dr_;
    std::vector<double> dc_;

    void load_real(const double* values);
    void load_complex(const double* ax);
    // [3D] Load scaled real values (D_r A D_c) into the matrix and add the
    // scaled diag_gmin to each diagonal. Computes dr_/dc_ from `values`.
    void load_real_equilibrated(const double* values, double diag_gmin);
};

}  // namespace neospice
