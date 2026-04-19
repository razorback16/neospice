#pragma once
#include "core/circuit.hpp"
#include <string>
#include <unordered_map>
#include <vector>

namespace neospice {

struct NoiseResult {
    std::vector<double> frequency;
    std::vector<double> output_noise_density;  // V^2/Hz (output-referred, total)
    std::vector<double> input_noise_density;   // V^2/Hz (input-referred, total)
    // Per-device breakdown: device_name -> V^2/Hz contribution at each frequency
    std::unordered_map<std::string, std::vector<double>> device_noise;
};

/// Run noise analysis on the circuit.
///
/// Algorithm (adjoint method):
///  1. DC operating point (reuses convergence aids from AC)
///  2. Build G and C small-signal matrices
///  3. For each frequency point:
///     a. Form Y = G + jwC as a 2n x 2n real system
///     b. Solve the adjoint: Y^T * adj = e_out  (unit vector at output node)
///     c. For each device noise source (i,j,S):
///        output_noise += S * |adj[i] - adj[j]|^2
///     d. Compute gain from input source to output for input-referred noise
///  4. Return frequency-dependent noise spectral densities
///
/// @param ckt           The circuit (must be finalized)
/// @param output_node   Name of the output voltage node (lowercase)
/// @param input_src     Name of the input voltage source (lowercase)
/// @param mode          Frequency sweep mode (DEC/OCT/LIN)
/// @param npoints       Number of points per decade/octave, or total for LIN
/// @param fstart        Start frequency (Hz)
/// @param fstop         Stop frequency (Hz)
NoiseResult solve_noise(Circuit& ckt,
                        const std::string& output_node,
                        const std::string& input_src,
                        AnalysisCommand::ACMode mode,
                        int npoints, double fstart, double fstop);

} // namespace neospice
