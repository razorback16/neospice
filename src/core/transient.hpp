#pragma once
#include "core/circuit.hpp"
#include "core/dc.hpp"
#include "core/sim_status.hpp"
#include "core/timestep.hpp"
#include "neospice/types.hpp"
#include <span>
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
    std::vector<std::vector<double>> voltages_dense;   // [node_idx][timepoint]
    std::vector<std::vector<double>> currents_dense;    // [dev_idx][timepoint]

    std::span<const double> voltage(NodeId node) const {
        auto idx = static_cast<int32_t>(node);
        if (idx < 0 || idx >= static_cast<int32_t>(voltages_dense.size()))
            throw std::out_of_range("Invalid NodeId for voltage access");
        return voltages_dense[idx];
    }

    std::span<const double> current(DevId dev) const {
        auto idx = static_cast<int32_t>(dev);
        if (idx < 0 || idx >= static_cast<int32_t>(currents_dense.size()))
            throw std::out_of_range("Invalid DevId for current access");
        return currents_dense[idx];
    }

    std::span<const double> time_span() const {
        return std::span<const double>(time);
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

struct TransientOptions {
    const DCResult* ic_from = nullptr;
    bool uic = false;
};

TransientResult solve_transient(Circuit& ckt, double tstep, double tstop,
                                bool uic = false);
TransientResult solve_transient(Circuit& ckt, double tstep, double tstop,
                                const TransientOptions& opts);

} // namespace neospice
