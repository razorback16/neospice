#pragma once
#include "core/circuit.hpp"
#include <string>
#include <map>
#include <vector>
#include <stdexcept>

namespace neospice {

struct DCResult {
    std::map<std::string, double> node_voltages;
    std::map<std::string, double> branch_currents;

    double voltage(const std::string& node) const {
        std::string key = "v(" + node + ")";
        auto it = node_voltages.find(key);
        if (it != node_voltages.end()) return it->second;
        it = node_voltages.find(node);
        if (it != node_voltages.end()) return it->second;
        throw std::out_of_range("DC voltage not found: " + node);
    }

    double current(const std::string& dev) const {
        std::string key = "i(" + dev + ")";
        auto it = branch_currents.find(key);
        if (it != branch_currents.end()) return it->second;
        it = branch_currents.find(dev);
        if (it != branch_currents.end()) return it->second;
        throw std::out_of_range("DC current not found: " + dev);
    }

    double diff(const std::string& node_p, const std::string& node_n) const {
        return voltage(node_p) - voltage(node_n);
    }
};

struct DCSweepResult {
    std::string sweep_var;
    std::vector<double> sweep_values;
    std::map<std::string, std::vector<double>> voltages;
    std::map<std::string, std::vector<double>> currents;

    const std::vector<double>& voltage(const std::string& node) const {
        std::string key = "v(" + node + ")";
        auto it = voltages.find(key);
        if (it != voltages.end()) return it->second;
        it = voltages.find(node);
        if (it != voltages.end()) return it->second;
        throw std::out_of_range("DC sweep voltage not found: " + node);
    }

    const std::vector<double>& current(const std::string& dev) const {
        std::string key = "i(" + dev + ")";
        auto it = currents.find(key);
        if (it != currents.end()) return it->second;
        it = currents.find(dev);
        if (it != currents.end()) return it->second;
        throw std::out_of_range("DC sweep current not found: " + dev);
    }
};

DCResult solve_dc(Circuit& ckt);

DCSweepResult solve_dc_sweep(Circuit& ckt, const std::vector<DCSweepParam>& params);

} // namespace neospice
