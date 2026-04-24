#pragma once
#include "core/circuit.hpp"
#include "core/sim_status.hpp"
#include <string>

namespace neospice {

struct TFResult {
    std::string output_var;    // e.g., "v(out)" or "i(vout)"
    std::string input_src;     // e.g., "vin"
    double transfer_function;  // output/input
    double input_impedance;    // Ohms
    double output_impedance;   // Ohms
    SimStatus status;
};

TFResult solve_tf(Circuit& ckt, const std::string& output_var, const std::string& input_src);

} // namespace neospice
