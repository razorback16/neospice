#pragma once
#include "core/circuit.hpp"
#include <string>
#include <map>
#include <vector>

namespace neospice {

struct DCResult {
    std::map<std::string, double> node_voltages;
    std::map<std::string, double> branch_currents;
};

struct DCSweepResult {
    std::string sweep_var;
    std::vector<double> sweep_values;
    std::map<std::string, std::vector<double>> voltages;
    std::map<std::string, std::vector<double>> currents;
};

DCResult solve_dc(Circuit& ckt);

DCSweepResult solve_dc_sweep(Circuit& ckt, const std::vector<DCSweepParam>& params);

} // namespace neospice
