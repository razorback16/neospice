#pragma once
#include "core/circuit.hpp"
#include <vector>
#include <string>
#include <unordered_map>

namespace cudaspice {

struct TransientResult {
    std::vector<double> time;
    std::unordered_map<std::string, std::vector<double>> voltages;
    std::unordered_map<std::string, std::vector<double>> currents;
};

TransientResult solve_transient(Circuit& ckt, double tstep, double tstop);

} // namespace cudaspice
