#pragma once
#include "core/circuit.hpp"
#include "core/sim_status.hpp"
#include "neospice/types.hpp"
#include <span>
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

    std::vector<std::string> signal_names() const {
        std::vector<std::string> names;
        names.reserve(node_voltages.size() + branch_currents.size());
        for (const auto& [k, v] : node_voltages) names.push_back(k);
        for (const auto& [k, v] : branch_currents) names.push_back(k);
        return names;
    }

    // Dense storage indexed by node/device ordinal
    std::vector<double> node_voltages_dense;
    std::vector<double> branch_currents_dense;

    // Handle-based O(1) access
    double voltage(NodeId node) const {
        auto idx = static_cast<int32_t>(node);
        if (idx < 0 || idx >= static_cast<int32_t>(node_voltages_dense.size()))
            throw std::out_of_range("Invalid NodeId for voltage access");
        return node_voltages_dense[idx];
    }

    double current(DevId dev) const {
        auto idx = static_cast<int32_t>(dev);
        if (idx < 0 || idx >= static_cast<int32_t>(branch_currents_dense.size()))
            throw std::out_of_range("Invalid DevId for current access");
        return branch_currents_dense[idx];
    }

    double diff(NodeId p, NodeId n) const {
        return voltage(p) - voltage(n);
    }

    SimStatus status;
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

    std::vector<double> diff(const std::string& node_p, const std::string& node_n) const {
        const auto& vp = voltage(node_p);
        const auto& vn = voltage(node_n);
        std::vector<double> result(vp.size());
        for (std::size_t i = 0; i < vp.size(); ++i)
            result[i] = vp[i] - vn[i];
        return result;
    }

    std::vector<std::string> signal_names() const {
        std::vector<std::string> names;
        names.reserve(voltages.size() + currents.size());
        for (const auto& [k, v] : voltages) names.push_back(k);
        for (const auto& [k, v] : currents) names.push_back(k);
        return names;
    }

    // Dense storage indexed by node/device ordinal
    std::vector<std::vector<double>> voltages_dense;   // [node_idx][sweep_point]
    std::vector<std::vector<double>> currents_dense;    // [dev_idx][sweep_point]

    std::span<const double> voltage(NodeId node) const {
        auto idx = static_cast<int32_t>(node);
        if (idx < 0 || idx >= static_cast<int32_t>(voltages_dense.size()))
            throw std::out_of_range("Invalid NodeId for sweep voltage");
        return voltages_dense[idx];
    }

    std::span<const double> current(DevId dev) const {
        auto idx = static_cast<int32_t>(dev);
        if (idx < 0 || idx >= static_cast<int32_t>(currents_dense.size()))
            throw std::out_of_range("Invalid DevId for sweep current");
        return currents_dense[idx];
    }

    std::vector<double> diff(NodeId p, NodeId n) const {
        auto vp = voltage(p);
        auto vn = voltage(n);
        std::vector<double> result(vp.size());
        for (std::size_t i = 0; i < vp.size(); ++i)
            result[i] = vp[i] - vn[i];
        return result;
    }

    SimStatus status;
};

DCResult solve_dc(Circuit& ckt);

DCSweepResult solve_dc_sweep(Circuit& ckt, const std::vector<DCSweepParam>& params);

} // namespace neospice
