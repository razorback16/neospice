#pragma once
#include "core/newton.hpp"
#include <vector>

namespace neospice {

class Circuit;
class KLUSolver;

NewtonResult gmin_stepping(Circuit& ckt, KLUSolver& solver,
                           std::vector<double>& solution,
                           const SimOptions& opts);

NewtonResult source_stepping(Circuit& ckt, KLUSolver& solver,
                             std::vector<double>& solution,
                             const SimOptions& opts);

NewtonResult pseudo_transient(Circuit& ckt, KLUSolver& solver,
                              std::vector<double>& solution,
                              const SimOptions& opts);

} // namespace neospice
