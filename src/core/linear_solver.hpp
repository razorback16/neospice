#pragma once
#include "core/matrix.hpp"
#include <memory>
#include <vector>

namespace neospice {

class LinearSolver {
public:
    virtual ~LinearSolver() = default;

    LinearSolver(const LinearSolver&) = delete;
    LinearSolver& operator=(const LinearSolver&) = delete;

    virtual void symbolic(const SparsityPattern& pattern) = 0;
    virtual void numeric(const SparsityPattern& pattern, const NumericMatrix& mat) = 0;
    virtual void refactorize(const NumericMatrix& mat) = 0;
    virtual void solve(std::vector<double>& rhs) = 0;
    virtual void numeric_complex(const SparsityPattern& pattern,
                                 const std::vector<double>& ax) = 0;
    virtual void refactorize_complex(const std::vector<double>& ax) = 0;
    virtual void solve_complex(std::vector<double>& rhs) = 0;

protected:
    LinearSolver() = default;
};

std::unique_ptr<LinearSolver> create_solver(int32_t n);

}  // namespace neospice
