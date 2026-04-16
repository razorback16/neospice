#pragma once
#include "core/circuit.hpp"
#include <string>
#include <unordered_map>

namespace cudaspice {

struct DCResult {
    std::unordered_map<std::string, double> node_voltages;
    std::unordered_map<std::string, double> branch_currents;
};

DCResult solve_dc(Circuit& ckt);

} // namespace cudaspice
