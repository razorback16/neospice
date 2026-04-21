#pragma once
#include "core/transient.hpp"
#include "core/dc.hpp"
#include "core/ac.hpp"
#include "core/noise.hpp"
#include "framework/ngspice_runner.hpp"
#include <string>

namespace neospice {

struct Tolerance {
    double relative = 1e-3;
    double absolute = 1e-9;
};

struct CompareResult {
    bool passed;
    std::string worst_signal;
    double worst_error;
    int num_points_compared;
};

struct OscillatorTolerance {
    double period_relative = 1e-2;    // <=1% period error
    double amplitude_relative = 5e-2; // <=5% peak-to-peak amplitude error
    double dc_absolute = 5e-2;        // 50 mV absolute for DC-tied nodes
    double mid_absolute = 1e-1;       // 100 mV: max allowed |mid_expected - mid_actual| for oscillating signals
    int min_periods = 3;              // require >= this many periods in the window
};

CompareResult compare_dc(const DCResult& expected, const DCResult& actual, Tolerance tol = {});
CompareResult compare_transient(const TransientResult& expected, const TransientResult& actual, Tolerance tol = {});
CompareResult compare_ac(const ACResult& expected, const ACResult& actual, Tolerance tol = {});

// Phase-insensitive comparator for free-running oscillators.  Instead of
// comparing values sample-by-sample (which fails when the two simulators'
// integrators phase-drift), we extract period and peak-to-peak amplitude
// from each signal via rising-edge zero-crossings about the midpoint, and
// compare those scalar metrics.  DC-tied nodes are detected automatically
// (peak-to-peak < 0.1 V) and compared with an absolute tolerance on a few
// late-window samples.  See compare_transient_oscillator implementation.
CompareResult compare_transient_oscillator(
    const TransientResult& expected,
    const TransientResult& actual,
    OscillatorTolerance tol = {});

/// Compare noise results. neospice stores V^2/Hz; ngspice stores V/sqrt(Hz).
/// The comparator converts neospice to V/sqrt(Hz) before comparing.
CompareResult compare_noise(const NgspiceNoiseResult& expected,
                            const NoiseResult& actual, Tolerance tol = {});

// ---------------------------------------------------------------------------
// Timing-based transient comparison
// ---------------------------------------------------------------------------
// Instead of point-wise sample matching (which is dominated by interpolation
// artifacts at switching edges), extract physically meaningful timing metrics
// from each waveform and compare those.  This gives sub-percent tolerances
// that reflect actual simulator accuracy.

struct EdgeMetrics {
    double cross_time;    // 50% crossing time (interpolated)
    double rise_time;     // 10%-90% transition time (negative for falling)
    double settled_value; // mean value in the settled window after the edge
    double overshoot;     // peak beyond settled value (absolute)
};

struct EdgeTolerance {
    double crossing_relative = 1e-3;   // 0.1% on 50% crossing time
    double rise_fall_relative = 5e-2;  // 5% on rise/fall time
    double settled_absolute = 1e-3;    // 1mV on settled value
    double overshoot_absolute = 5e-3;  // 5mV on overshoot
};

struct EdgeCompareResult {
    bool passed;
    std::string detail;       // human-readable description of worst mismatch
    double worst_error;       // worst relative or absolute error found
    int num_edges_compared;
};

/// Extract edges from a single-signal waveform.  A "falling edge" crosses
/// v_mid from above; a "rising edge" from below.  v_low/v_high define the
/// 10%/90% thresholds.  settle_window is the time after an edge to average
/// for the settled value.
std::vector<EdgeMetrics> extract_edges(
    const std::vector<double>& time,
    const std::vector<double>& signal,
    double v_low, double v_high,
    double settle_window);

/// Compare edges extracted from two waveforms.
EdgeCompareResult compare_edges(
    const std::vector<EdgeMetrics>& expected,
    const std::vector<EdgeMetrics>& actual,
    EdgeTolerance tol = {});

} // namespace neospice
