#pragma once
#include "core/types.hpp"
#include <vector>

namespace neospice {

class Circuit;
class NeoSolver;

struct NewtonResult {
    bool converged = false;
    int iterations = 0;
    std::vector<double> solution;
};

NewtonResult newton_solve(Circuit& ckt, NeoSolver& solver,
                          std::vector<double>& solution,
                          const SimOptions& opts);

} // namespace neospice
