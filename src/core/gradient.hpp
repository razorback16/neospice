#pragma once
#include "core/circuit.hpp"
#include "core/sim_status.hpp"
#include <complex>

namespace neospice {

struct GradientResult {
    std::vector<std::string> outputs, parameters;
    std::vector<double> values;
    std::vector<std::vector<double>> jacobian; // [output][parameter]
    std::size_t adjoint_solves = 0;
    SimStatus status;
};

struct ACGradientResult {
    std::vector<std::string> outputs, parameters;
    std::vector<double> frequency;
    std::vector<std::vector<std::complex<double>>> values; // [frequency][output]
    std::vector<std::vector<std::vector<std::complex<double>>>> jacobian;
    std::size_t adjoint_solves = 0;
    SimStatus status;
};

// Outputs: v(node), v(pos,neg), i(device with an MNA branch).
// Parameters: R/C/L nominal values; V/I DC values. Empty list selects these.
// Device model parameters and derivatives at switching boundaries are excluded.
GradientResult solve_gradient(Circuit& circuit, const std::vector<std::string>& outputs,
                              const std::vector<std::string>& parameters = {});

// Linear circuits only. R/C/uncoupled-L plus V/I :dc, :ac_mag and :ac_phase
// (degrees). Unsuffixed V/I names select :ac_mag. Plain complex derivatives,
// not magnitude/dB derivatives. Shape: [frequency][output][parameter].
ACGradientResult solve_ac_gradient(Circuit& circuit, const std::vector<std::string>& outputs,
    const std::vector<std::string>& parameters, const std::vector<double>& frequencies);

} // namespace neospice
