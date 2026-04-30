#pragma once
#include "core/circuit.hpp"
#include "core/dc.hpp"
#include "core/sim_status.hpp"
#include "neospice/types.hpp"
#include <complex>
#include <span>
#include <vector>
#include <string>
#include <map>
#include <cmath>
#include <stdexcept>

namespace neospice {

struct ACResult {
    std::vector<double> frequency;
    std::map<std::string, std::vector<std::complex<double>>> voltages;
    std::map<std::string, std::vector<std::complex<double>>> currents;

    const std::vector<std::complex<double>>& voltage(const std::string& node) const {
        std::string key = "v(" + node + ")";
        auto it = voltages.find(key);
        if (it != voltages.end()) return it->second;
        it = voltages.find(node);
        if (it != voltages.end()) return it->second;
        throw std::out_of_range("AC voltage not found: " + node);
    }

    const std::vector<std::complex<double>>& current(const std::string& dev) const {
        std::string key = "i(" + dev + ")";
        auto it = currents.find(key);
        if (it != currents.end()) return it->second;
        it = currents.find(dev);
        if (it != currents.end()) return it->second;
        throw std::out_of_range("AC current not found: " + dev);
    }

    std::vector<double> magnitude_db(const std::string& node) const {
        const auto& v = voltage(node);
        std::vector<double> result(v.size());
        for (std::size_t i = 0; i < v.size(); ++i)
            result[i] = 20.0 * std::log10(std::max(std::abs(v[i]), 1e-30));
        return result;
    }

    std::vector<double> phase_deg(const std::string& node) const {
        const auto& v = voltage(node);
        std::vector<double> result(v.size());
        for (std::size_t i = 0; i < v.size(); ++i)
            result[i] = std::atan2(v[i].imag(), v[i].real()) * (180.0 / M_PI);
        return result;
    }

    std::vector<double> magnitude(const std::string& node) const {
        const auto& v = voltage(node);
        std::vector<double> result(v.size());
        for (std::size_t i = 0; i < v.size(); ++i)
            result[i] = std::abs(v[i]);
        return result;
    }

    std::vector<std::complex<double>> diff(const std::string& node_p,
                                           const std::string& node_n) const {
        const auto& vp = voltage(node_p);
        const auto& vn = voltage(node_n);
        std::vector<std::complex<double>> result(vp.size());
        for (std::size_t i = 0; i < vp.size(); ++i)
            result[i] = vp[i] - vn[i];
        return result;
    }

    std::vector<double> diff_magnitude_db(const std::string& node_p,
                                          const std::string& node_n) const {
        auto d = diff(node_p, node_n);
        std::vector<double> result(d.size());
        for (std::size_t i = 0; i < d.size(); ++i)
            result[i] = 20.0 * std::log10(std::max(std::abs(d[i]), 1e-30));
        return result;
    }

    std::vector<double> current_magnitude_db(const std::string& dev) const {
        const auto& c = current(dev);
        std::vector<double> result(c.size());
        for (std::size_t i = 0; i < c.size(); ++i)
            result[i] = 20.0 * std::log10(std::max(std::abs(c[i]), 1e-30));
        return result;
    }

    std::vector<double> current_phase_deg(const std::string& dev) const {
        const auto& c = current(dev);
        std::vector<double> result(c.size());
        for (std::size_t i = 0; i < c.size(); ++i)
            result[i] = std::atan2(c[i].imag(), c[i].real()) * (180.0 / M_PI);
        return result;
    }

    std::vector<double> current_magnitude(const std::string& dev) const {
        const auto& c = current(dev);
        std::vector<double> result(c.size());
        for (std::size_t i = 0; i < c.size(); ++i)
            result[i] = std::abs(c[i]);
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
    std::vector<std::vector<std::complex<double>>> voltages_dense;
    std::vector<std::vector<std::complex<double>>> currents_dense;

    std::span<const std::complex<double>> voltage(NodeId node) const {
        auto idx = static_cast<int32_t>(node);
        if (idx < 0 || idx >= static_cast<int32_t>(voltages_dense.size()))
            throw std::out_of_range("Invalid NodeId for voltage access");
        return voltages_dense[idx];
    }

    std::span<const std::complex<double>> current(DevId dev) const {
        auto idx = static_cast<int32_t>(dev);
        if (idx < 0 || idx >= static_cast<int32_t>(currents_dense.size()))
            throw std::out_of_range("Invalid DevId for current access");
        return currents_dense[idx];
    }

    // Derived quantities — return vector (computed on access)
    std::vector<double> magnitude_db(NodeId node) const {
        auto v = voltage(node);
        std::vector<double> result(v.size());
        for (std::size_t i = 0; i < v.size(); ++i)
            result[i] = 20.0 * std::log10(std::max(std::abs(v[i]), 1e-30));
        return result;
    }

    std::vector<double> phase_deg(NodeId node) const {
        auto v = voltage(node);
        std::vector<double> result(v.size());
        for (std::size_t i = 0; i < v.size(); ++i)
            result[i] = std::atan2(v[i].imag(), v[i].real()) * (180.0 / M_PI);
        return result;
    }

    std::vector<double> magnitude(NodeId node) const {
        auto v = voltage(node);
        std::vector<double> result(v.size());
        for (std::size_t i = 0; i < v.size(); ++i)
            result[i] = std::abs(v[i]);
        return result;
    }

    std::vector<std::complex<double>> diff(NodeId p, NodeId n) const {
        auto vp = voltage(p);
        auto vn = voltage(n);
        std::vector<std::complex<double>> result(vp.size());
        for (std::size_t i = 0; i < vp.size(); ++i)
            result[i] = vp[i] - vn[i];
        return result;
    }

    std::vector<double> diff_magnitude_db(NodeId p, NodeId n) const {
        auto d = diff(p, n);
        std::vector<double> result(d.size());
        for (std::size_t i = 0; i < d.size(); ++i)
            result[i] = 20.0 * std::log10(std::max(std::abs(d[i]), 1e-30));
        return result;
    }

    SimStatus status;
};

struct ACOptions {
    const DCResult* op_from = nullptr;
};

ACResult solve_ac(Circuit& ckt, ACMode mode,
                  int npoints, double fstart, double fstop);
ACResult solve_ac(Circuit& ckt, ACMode mode,
                  int npoints, double fstart, double fstop,
                  const ACOptions& opts);

} // namespace neospice
