#pragma once
#include "core/circuit.hpp"
#include <complex>
#include <vector>
#include <string>
#include <unordered_map>

namespace neospice {

struct ACResult {
    std::vector<double> frequency;
    std::unordered_map<std::string, std::vector<std::complex<double>>> voltages;
    std::unordered_map<std::string, std::vector<std::complex<double>>> currents;
};

ACResult solve_ac(Circuit& ckt, AnalysisCommand::ACMode mode,
                  int npoints, double fstart, double fstop);

} // namespace neospice
