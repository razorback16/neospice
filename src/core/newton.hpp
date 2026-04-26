#pragma once
#include "core/types.hpp"
#include <vector>

namespace neospice {

class Circuit;
class LinearSolver;

struct NewtonResult {
    bool converged = false;
    int iterations = 0;
    std::vector<double> solution;
};

NewtonResult newton_solve(Circuit& ckt, LinearSolver& solver,
                          std::vector<double>& solution,
                          const SimOptions& opts);

} // namespace neospice
