#pragma once
#include "core/transient.hpp"
#include <string>
#include <vector>

namespace neospice {

struct FourierComponent {
    int    harmonic;              // 0=DC, 1=fundamental, 2=2nd harmonic, etc.
    double frequency;             // Hz
    double magnitude;             // absolute magnitude
    double phase_deg;             // phase in degrees
    double normalized_mag;        // relative to fundamental (fundamental=1.0)
    double normalized_phase_deg;  // phase relative to fundamental phase
};

struct FourierResult {
    std::string signal_name;
    double fundamental_freq;
    std::vector<FourierComponent> components;  // DC (k=0) + harmonics 1..9
    double thd;  // Total Harmonic Distortion (%)
};

/// Compute Fourier decomposition from transient simulation data.
///
/// Uses a DFT (not FFT) over the last complete period [tstop-T, tstop] where
/// T = 1/fundamental_freq.  Trapezoidal integration is applied on the actual
/// simulation time-points with linear interpolation at the boundaries.
///
/// Returns one FourierResult per signal name.  Signals that are not found in
/// tran_result produce a result with all-zero components.
std::vector<FourierResult> compute_fourier(
    double fundamental_freq,
    const std::vector<std::string>& signal_names,
    const TransientResult& tran_result);

} // namespace neospice
