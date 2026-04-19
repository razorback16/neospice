#pragma once
// Shared frequency-point generation for AC and noise analyses.
// Both analyses use identical DEC/OCT/LIN sweep logic — this header
// provides a single definition to avoid duplication.

#include "core/circuit.hpp"
#include <cmath>
#include <vector>

namespace neospice {

/// Generate frequency sample points for a sweep analysis.
///
/// @param mode    DEC (decades), OCT (octaves), or LIN (linear)
/// @param npoints Number of points per decade/octave, or total points for LIN
/// @param fstart  Start frequency (Hz) — must be > 0
/// @param fstop   Stop frequency  (Hz) — must be >= fstart > 0
/// @returns       Ordered vector of frequency values; empty on invalid input.
inline std::vector<double> generate_frequencies(AnalysisCommand::ACMode mode,
                                                int npoints,
                                                double fstart, double fstop) {
    std::vector<double> freqs;
    if (fstart <= 0 || fstop <= 0 || fstop < fstart || npoints < 1)
        return freqs;

    switch (mode) {
    case AnalysisCommand::DEC: {
        double decades = std::log10(fstop / fstart);
        int total = static_cast<int>(std::round(decades * npoints)) + 1;
        freqs.reserve(total);
        for (int i = 0; i < total; ++i) {
            double f = fstart * std::pow(10.0, static_cast<double>(i) / npoints);
            freqs.push_back(f);
        }
        break;
    }
    case AnalysisCommand::OCT: {
        double octaves = std::log2(fstop / fstart);
        int total = static_cast<int>(std::round(octaves * npoints)) + 1;
        freqs.reserve(total);
        for (int i = 0; i < total; ++i) {
            double f = fstart * std::pow(2.0, static_cast<double>(i) / npoints);
            freqs.push_back(f);
        }
        break;
    }
    case AnalysisCommand::LIN: {
        freqs.reserve(npoints);
        double step = (npoints > 1) ? (fstop - fstart) / (npoints - 1) : 0.0;
        for (int i = 0; i < npoints; ++i) {
            freqs.push_back(fstart + i * step);
        }
        break;
    }
    }
    return freqs;
}

} // namespace neospice
