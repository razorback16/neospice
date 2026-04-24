#pragma once
#include "core/circuit.hpp"
#include "core/sim_status.hpp"
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
    SimStatus status;
};

SensResult solve_sens(Circuit& ckt, const std::string& output_var);

} // namespace neospice
