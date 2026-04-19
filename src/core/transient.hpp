#pragma once
#include "core/circuit.hpp"
#include "core/timestep.hpp"
#include <vector>
#include <string>
#include <map>

namespace neospice {

enum class IntegrationMethod { TRAPEZOIDAL, GEAR2 };

struct TransientResult {
    std::vector<double> time;
    std::map<std::string, std::vector<double>> voltages;
    std::map<std::string, std::vector<double>> currents;
    int rejected_steps = 0;  // diagnostic
};

TransientResult solve_transient(Circuit& ckt, double tstep, double tstop);

} // namespace neospice
