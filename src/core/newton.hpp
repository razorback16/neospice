#pragma once
#include "core/types.hpp"
#include <cstdint>
#include <vector>

namespace neospice {

class Circuit;
class NeoSolver;

struct NewtonResult {
    bool converged = false;
    int iterations = 0;
    std::vector<double> solution;
    double residual = 0.0;          // max |rhs[i]| at final iteration
    int32_t worst_node_idx = -1;    // node with largest residual
};

NewtonResult newton_solve(Circuit& ckt, NeoSolver& solver,
                          std::vector<double>& solution,
                          const SimOptions& opts);

} // namespace neospice
