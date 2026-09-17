#pragma once
// Shared frequency-point generation for AC and noise analyses.
// AC and noise have distinct DEC and small LIN grid rules in ngspice 47.

#include "core/circuit.hpp"
#include <cmath>
#include <vector>

namespace neospice {

enum class FrequencyAnalysis { AC, Noise };

/// Generate the ngspice 47 AC or noise sweep grid, including its endpoint
/// tolerance and incremental floating-point arithmetic. AC DEC redistributes
/// intervals only for ranges of at least one decade; narrower ranges and noise
/// use a fixed exp(log(10)/npoints) ratio. See acan.c/noisean.c.
/// Returns empty on invalid input.
inline std::vector<double> generate_frequencies(ACMode mode, int npoints,
                                                double fstart, double fstop,
                                                double reltol = 1e-3,
                                                FrequencyAnalysis analysis = FrequencyAnalysis::AC) {
    std::vector<double> freqs;
    if (!std::isfinite(fstart) || !std::isfinite(fstop) || !std::isfinite(reltol) ||
        fstart <= 0 || fstop < fstart || npoints < 1 || reltol < 0)
        return freqs;
    double delta = 0.0;
    bool logarithmic = mode != ACMode::LIN;
    switch (mode) {
    case ACMode::DEC:
        if (analysis == FrequencyAnalysis::AC) {
            if (fstop / 10.0 < fstart) {
                delta = fstop == fstart ? 1.0 : std::exp(std::log(10.0) / npoints);
            } else {
                const double intervals = std::floor(std::abs(std::log10(fstop / fstart)) * npoints);
                if (intervals < 1 || !std::isfinite(intervals)) return freqs;
                delta = std::exp(std::log(fstop / fstart) / intervals);
            }
        } else {
            delta = std::exp(std::log(10.0) / npoints);
        }
        break;
    case ACMode::OCT:
        delta = std::exp(std::log(2.0) / npoints);
        break;
    case ACMode::LIN:
        // ngspice AC uses one point for npoints=1 or 2; noise only for 1.
        if (npoints > (analysis == FrequencyAnalysis::AC ? 2 : 1))
            delta = (fstop - fstart) / (npoints - 1);
        break;
    default:
        return freqs;
    }
    if (!std::isfinite(delta)) return freqs;
    const double end_tolerance = logarithmic ? delta * fstop * reltol : delta * reltol;
    for (double frequency = fstart; frequency <= fstop + end_tolerance;) {
        freqs.push_back(frequency);
        const double next = logarithmic ? frequency * delta : frequency + delta;
        if (!std::isfinite(next) || next <= frequency) break;
        frequency = next;
    }
    return freqs;
}

} // namespace neospice
