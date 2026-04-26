#pragma once
#include "core/newton.hpp"
#include <vector>

namespace neospice {

class Circuit;
class LinearSolver;

NewtonResult gmin_stepping(Circuit& ckt, LinearSolver& solver,
                           std::vector<double>& solution,
                           const SimOptions& opts);

NewtonResult source_stepping(Circuit& ckt, LinearSolver& solver,
                             std::vector<double>& solution,
                             const SimOptions& opts);

NewtonResult pseudo_transient(Circuit& ckt, LinearSolver& solver,
                              std::vector<double>& solution,
                              const SimOptions& opts);

} // namespace neospice
