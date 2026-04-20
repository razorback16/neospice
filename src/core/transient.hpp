#pragma once
#include "core/circuit.hpp"
#include "core/timestep.hpp"
#include <vector>
#include <string>
#include <map>
#include <stdexcept>

namespace neospice {

enum class IntegrationMethod { TRAPEZOIDAL, GEAR2 };

struct TransientResult {
    std::vector<double> time;
    std::map<std::string, std::vector<double>> voltages;
    std::map<std::string, std::vector<double>> currents;
    int rejected_steps = 0;  // diagnostic

    const std::vector<double>& voltage(const std::string& node) const {
        std::string key = "v(" + node + ")";
        auto it = voltages.find(key);
        if (it != voltages.end()) return it->second;
        it = voltages.find(node);
        if (it != voltages.end()) return it->second;
        throw std::out_of_range("Transient voltage not found: " + node);
    }

    const std::vector<double>& current(const std::string& dev) const {
        std::string key = "i(" + dev + ")";
        auto it = currents.find(key);
        if (it != currents.end()) return it->second;
        it = currents.find(dev);
        if (it != currents.end()) return it->second;
        throw std::out_of_range("Transient current not found: " + dev);
    }
};

TransientResult solve_transient(Circuit& ckt, double tstep, double tstop,
                                bool uic = false);

} // namespace neospice
