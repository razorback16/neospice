#pragma once
#include "core/circuit.hpp"
#include "core/sim_status.hpp"
#include <stdexcept>
#include <string>
#include <vector>

namespace neospice {

struct SensResult {
    std::string output_var;   // e.g., "v(out)"
    double output_value;      // DC value of output variable

    struct Entry {
        std::string element;    // e.g., "r1", "v1"
        std::string parameter;  // e.g., "resistance", "dc"
        double sensitivity;     // dV(out)/dParam (e.g., V/Ohm, V/V, V/A)
        double normalized;      // sensitivity * param_value / output_value (dimensionless)
    };
    std::vector<Entry> entries;

    const Entry& find(const std::string& element) const {
        for (const auto& e : entries)
            if (e.element == element) return e;
        throw std::out_of_range("Sensitivity entry not found: " + element);
    }

    double sensitivity(const std::string& element) const {
        return find(element).sensitivity;
    }

    double normalized_sensitivity(const std::string& element) const {
        return find(element).normalized;
    }

    std::vector<std::string> signal_names() const {
        std::vector<std::string> names;
        names.reserve(entries.size());
        for (const auto& e : entries) names.push_back(e.element);
        return names;
    }

    SimStatus status;
};

SensResult solve_sens(Circuit& ckt, const std::string& output_var);

} // namespace neospice
