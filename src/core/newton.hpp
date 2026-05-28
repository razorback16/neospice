#pragma once
#include "core/matrix.hpp"
#include "core/types.hpp"
#include <cstdint>
#include <vector>

namespace neospice {

class Circuit;
class NeoSolver;

struct NewtonWorkspace {
    explicit NewtonWorkspace(const SparsityPattern& pattern);

    void ensure_size(int32_t n);

    NumericMatrix mat;
    std::vector<double> rhs;
    std::vector<double> old_solution;
    std::vector<double> old_state0;
    std::vector<double> proposed;
    std::vector<double> one_based_solution;
    std::vector<double> one_based_rhs;
    int32_t matrix_size = 0;
    int32_t matrix_nnz = 0;
};

struct NewtonResult {
    bool converged = false;
    int iterations = 0;
    double residual = 0.0;          // max |rhs[i]| at final iteration
    int32_t worst_node_idx = -1;    // node with largest residual
};

NewtonResult newton_solve(Circuit& ckt, NeoSolver& solver,
                          std::vector<double>& solution,
                          const SimOptions& opts);

NewtonResult newton_solve(Circuit& ckt, NeoSolver& solver,
                          std::vector<double>& solution,
                          const SimOptions& opts,
                          NewtonWorkspace& workspace);

} // namespace neospice
