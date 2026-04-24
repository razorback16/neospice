#pragma once
#include "core/circuit.hpp"
#include "core/sim_status.hpp"
#include <string>
#include <map>
#include <vector>
#include <cmath>
#include <algorithm>
#include <stdexcept>

namespace neospice {

struct NoiseResult {
    std::vector<double> frequency;
    std::vector<double> output_noise_density;  // V^2/Hz (output-referred, total)
    std::vector<double> input_noise_density;   // V^2/Hz (input-referred, total)
    // Per-device breakdown: device_name -> V^2/Hz contribution at each frequency
    std::map<std::string, std::vector<double>> device_noise;

    std::vector<double> output_noise_sqrt() const {
        std::vector<double> result(output_noise_density.size());
        for (std::size_t i = 0; i < result.size(); ++i)
            result[i] = std::sqrt(std::max(output_noise_density[i], 0.0));
        return result;
    }

    std::vector<double> input_noise_sqrt() const {
        std::vector<double> result(input_noise_density.size());
        for (std::size_t i = 0; i < result.size(); ++i)
            result[i] = std::sqrt(std::max(input_noise_density[i], 0.0));
        return result;
    }

    double integrated_output_noise(double fmin, double fmax) const {
        if (frequency.size() < 2) return 0.0;
        double sum = 0.0;
        for (std::size_t i = 1; i < frequency.size(); ++i) {
            if (frequency[i] < fmin) continue;
            if (frequency[i - 1] > fmax) break;
            double f0 = std::max(frequency[i - 1], fmin);
            double f1 = std::min(frequency[i], fmax);
            sum += 0.5 * (output_noise_density[i - 1] + output_noise_density[i]) * (f1 - f0);
        }
        return std::sqrt(std::max(sum, 0.0));
    }

    double integrated_input_noise(double fmin, double fmax) const {
        if (frequency.size() < 2) return 0.0;
        double sum = 0.0;
        for (std::size_t i = 1; i < frequency.size(); ++i) {
            if (frequency[i] < fmin) continue;
            if (frequency[i - 1] > fmax) break;
            double f0 = std::max(frequency[i - 1], fmin);
            double f1 = std::min(frequency[i], fmax);
            sum += 0.5 * (input_noise_density[i - 1] + input_noise_density[i]) * (f1 - f0);
        }
        return std::sqrt(std::max(sum, 0.0));
    }

    std::vector<std::string> device_names() const {
        std::vector<std::string> names;
        names.reserve(device_noise.size());
        for (const auto& [k, v] : device_noise) names.push_back(k);
        return names;
    }

    const std::vector<double>& device_noise_density(const std::string& name) const {
        auto it = device_noise.find(name);
        if (it != device_noise.end()) return it->second;
        throw std::out_of_range("Noise device not found: " + name);
    }

    std::vector<std::string> signal_names() const {
        std::vector<std::string> names = {"onoise", "inoise"};
        for (const auto& [k, v] : device_noise) names.push_back(k);
        return names;
    }

    SimStatus status;
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
                        ACMode mode,
                        int npoints, double fstart, double fstop);

} // namespace neospice
