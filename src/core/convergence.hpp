#pragma once
#include "core/newton.hpp"
#include <vector>

namespace cudaspice {

class Circuit;
class KLUSolver;

NewtonResult gmin_stepping(Circuit& ckt, KLUSolver& solver,
                           std::vector<double>& solution,
                           const SimOptions& opts);

NewtonResult source_stepping(Circuit& ckt, KLUSolver& solver,
                             std::vector<double>& solution,
                             const SimOptions& opts);

} // namespace cudaspice
