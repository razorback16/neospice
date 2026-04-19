#pragma once
#include "core/circuit.hpp"
#include <string>
#include <unordered_map>
#include <vector>

namespace neospice {

struct DCResult {
    std::unordered_map<std::string, double> node_voltages;
    std::unordered_map<std::string, double> branch_currents;
};

struct DCSweepResult {
    std::string sweep_var;                                              // inner sweep source name, e.g. "v1"
    std::vector<double> sweep_values;                                   // inner sweep values (x-axis)
    std::unordered_map<std::string, std::vector<double>> voltages;      // node voltages at each point
    std::unordered_map<std::string, std::vector<double>> currents;      // branch currents at each point
};

DCResult solve_dc(Circuit& ckt);

DCSweepResult solve_dc_sweep(Circuit& ckt, const std::vector<DCSweepParam>& params);

} // namespace neospice
