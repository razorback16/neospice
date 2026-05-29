#pragma once
#include "core/newton.hpp"
#include <vector>

namespace neospice {

class Circuit;
class ISolver;

NewtonResult gmin_stepping(Circuit& ckt, ISolver& solver,
                           std::vector<double>& solution,
                           const SimOptions& opts,
                           int firstmode, int continuemode);

NewtonResult source_stepping(Circuit& ckt, ISolver& solver,
                             std::vector<double>& solution,
                             const SimOptions& opts);

NewtonResult gain_stepping(Circuit& ckt, ISolver& solver,
                           std::vector<double>& solution,
                           const SimOptions& opts);

NewtonResult pseudo_transient(Circuit& ckt, ISolver& solver,
                              std::vector<double>& solution,
                              const SimOptions& opts);

NewtonResult transient_operating_point(Circuit& ckt, ISolver& solver,
                                        std::vector<double>& solution,
                                        const SimOptions& opts);

NewtonResult true_gmin_stepping(Circuit& ckt, ISolver& solver,
                                std::vector<double>& solution,
                                const SimOptions& opts,
                                int firstmode, int continuemode);

} // namespace neospice
