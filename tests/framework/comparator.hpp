#pragma once
#include "core/transient.hpp"
#include "core/dc.hpp"
#include "core/ac.hpp"
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

} // namespace neospice
