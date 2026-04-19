#pragma once
#include "core/circuit.hpp"
#include "core/transient.hpp"
#include "core/ac.hpp"
#include "core/dc.hpp"
#include <string>
#include <unordered_map>
#include <vector>

namespace neospice {

struct MeasureResult {
    std::unordered_map<std::string, double> values;  // measure_name -> result
};

/// Execute all measure commands against available simulation results.
/// Returns a MeasureResult with computed values for each command.
MeasureResult execute_measures(const std::vector<MeasureCommand>& measures,
                               const TransientResult* tran,
                               const ACResult* ac,
                               const DCSweepResult* dc_sweep);

} // namespace neospice
