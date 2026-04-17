#include "framework/comparator.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace neospice {

namespace {

double relative_error(double expected, double actual, double abstol) {
    double denom = std::max(std::abs(expected), abstol);
    return std::abs(expected - actual) / denom;
}

double interpolate(const std::vector<double>& xs, const std::vector<double>& ys, double x) {
    if (xs.empty()) return 0.0;
    if (x <= xs.front()) return ys.front();
    if (x >= xs.back()) return ys.back();

    // Binary search for the interval
    auto it = std::lower_bound(xs.begin(), xs.end(), x);
    if (it == xs.begin()) return ys.front();

    size_t idx = static_cast<size_t>(std::distance(xs.begin(), it));
    size_t i0 = idx - 1;
    size_t i1 = idx;

    double x0 = xs[i0], x1 = xs[i1];
    double y0 = ys[i0], y1 = ys[i1];

    double t = (x - x0) / (x1 - x0);
    return y0 + t * (y1 - y0);
}

} // anonymous namespace

CompareResult compare_dc(const DCResult& expected, const DCResult& actual, Tolerance tol) {
    CompareResult result{true, "", 0.0, 0};

    // Compare node voltages
    for (const auto& [name, exp_val] : expected.node_voltages) {
        auto it = actual.node_voltages.find(name);
        if (it == actual.node_voltages.end()) {
            result.passed = false;
            result.worst_signal = name;
            result.worst_error = std::numeric_limits<double>::infinity();
            return result;
        }

        double err = relative_error(exp_val, it->second, tol.absolute);
        result.num_points_compared++;
        if (err > result.worst_error) {
            result.worst_error = err;
            result.worst_signal = name;
        }
        if (err > tol.relative) {
            result.passed = false;
        }
    }

    // Compare branch currents
    for (const auto& [name, exp_val] : expected.branch_currents) {
        auto it = actual.branch_currents.find(name);
        if (it == actual.branch_currents.end()) {
            result.passed = false;
            result.worst_signal = name;
            result.worst_error = std::numeric_limits<double>::infinity();
            return result;
        }

        double err = relative_error(exp_val, it->second, tol.absolute);
        result.num_points_compared++;
        if (err > result.worst_error) {
            result.worst_error = err;
            result.worst_signal = name;
        }
        if (err > tol.relative) {
            result.passed = false;
        }
    }

    return result;
}

CompareResult compare_transient(const TransientResult& expected, const TransientResult& actual, Tolerance tol) {
    CompareResult result{true, "", 0.0, 0};

    // Compare voltages
    for (const auto& [name, exp_vec] : expected.voltages) {
        auto it = actual.voltages.find(name);
        if (it == actual.voltages.end()) {
            result.passed = false;
            result.worst_signal = name;
            result.worst_error = std::numeric_limits<double>::infinity();
            return result;
        }

        const auto& act_vec = it->second;

        // For each expected time point, interpolate actual value
        for (size_t i = 0; i < expected.time.size(); ++i) {
            double t = expected.time[i];
            double exp_val = exp_vec[i];
            double act_val = interpolate(actual.time, act_vec, t);

            double err = relative_error(exp_val, act_val, tol.absolute);
            result.num_points_compared++;
            if (err > result.worst_error) {
                result.worst_error = err;
                result.worst_signal = name;
            }
            if (err > tol.relative) {
                result.passed = false;
            }
        }
    }

    // Compare currents
    for (const auto& [name, exp_vec] : expected.currents) {
        auto it = actual.currents.find(name);
        if (it == actual.currents.end()) {
            result.passed = false;
            result.worst_signal = name;
            result.worst_error = std::numeric_limits<double>::infinity();
            return result;
        }

        const auto& act_vec = it->second;

        for (size_t i = 0; i < expected.time.size(); ++i) {
            double t = expected.time[i];
            double exp_val = exp_vec[i];
            double act_val = interpolate(actual.time, act_vec, t);

            double err = relative_error(exp_val, act_val, tol.absolute);
            result.num_points_compared++;
            if (err > result.worst_error) {
                result.worst_error = err;
                result.worst_signal = name;
            }
            if (err > tol.relative) {
                result.passed = false;
            }
        }
    }

    return result;
}

namespace {

// Summary of a single-signal waveform over a given window [idx_lo, idx_hi).
struct SignalStats {
    bool is_dc = false;
    double vmin = 0.0;
    double vmax = 0.0;
    double mid = 0.0;
    double amplitude = 0.0; // peak-to-peak
    double period = 0.0;    // mean inter-crossing interval on rising edges
    int num_periods = 0;    // number of rising-edge intervals measured
    double dc_level = 0.0;  // mean value over window (used for DC classification)
};

// Extract min/max over samples in [lo, hi).
void minmax_over(const std::vector<double>& y, size_t lo, size_t hi,
                 double& vmin, double& vmax) {
    vmin = std::numeric_limits<double>::infinity();
    vmax = -std::numeric_limits<double>::infinity();
    for (size_t i = lo; i < hi; ++i) {
        vmin = std::min(vmin, y[i]);
        vmax = std::max(vmax, y[i]);
    }
}

// Classify a signal and extract its oscillation metrics over the second half
// of the time window (to skip startup transients).  If peak-to-peak on the
// full window is below dc_threshold, treat as DC and return is_dc=true with
// dc_level = mean over the second-half window.
SignalStats analyze_signal(const std::vector<double>& t,
                           const std::vector<double>& y,
                           double dc_threshold /* volts */) {
    SignalStats s;
    if (y.size() < 2 || t.size() != y.size()) {
        s.is_dc = true;
        return s;
    }

    // Peak-to-peak across full window for DC classification.
    double full_min, full_max;
    minmax_over(y, 0, y.size(), full_min, full_max);
    double full_pp = full_max - full_min;

    // Second-half window (skip startup).
    size_t lo = y.size() / 2;
    size_t hi = y.size();
    if (hi - lo < 4) { lo = 0; hi = y.size(); }

    double half_min, half_max;
    minmax_over(y, lo, hi, half_min, half_max);
    s.vmin = half_min;
    s.vmax = half_max;

    // Compute mean over second-half for DC level.
    double sum = 0.0;
    for (size_t i = lo; i < hi; ++i) sum += y[i];
    s.dc_level = sum / static_cast<double>(hi - lo);

    if (full_pp < dc_threshold) {
        s.is_dc = true;
        return s;
    }

    s.is_dc = false;
    s.mid = 0.5 * (half_min + half_max);
    s.amplitude = half_max - half_min;

    // Find rising-edge crossings of mid in the second-half window.
    // A rising edge = y[i] < mid && y[i+1] >= mid.  Linearly interpolate
    // to find the precise crossing time.
    std::vector<double> crossings;
    crossings.reserve(16);
    for (size_t i = lo; i + 1 < hi; ++i) {
        double y0 = y[i];
        double y1 = y[i + 1];
        if (y0 < s.mid && y1 >= s.mid) {
            double dy = y1 - y0;
            double frac = (dy != 0.0) ? (s.mid - y0) / dy : 0.0;
            double tc = t[i] + frac * (t[i + 1] - t[i]);
            crossings.push_back(tc);
        }
    }

    if (crossings.size() < 2) {
        s.period = 0.0;
        s.num_periods = 0;
        return s;
    }

    double total = crossings.back() - crossings.front();
    s.num_periods = static_cast<int>(crossings.size() - 1);
    s.period = total / static_cast<double>(s.num_periods);
    return s;
}

} // anonymous namespace

CompareResult compare_transient_oscillator(const TransientResult& expected,
                                           const TransientResult& actual,
                                           OscillatorTolerance tol) {
    CompareResult result{true, "", 0.0, 0};

    constexpr double kDcThreshold = 0.1; // volts peak-to-peak

    // Iterate expected voltages; every signal present in expected must be in
    // actual too (same policy as compare_transient).
    for (const auto& [name, exp_vec] : expected.voltages) {
        auto it = actual.voltages.find(name);
        if (it == actual.voltages.end()) {
            result.passed = false;
            result.worst_signal = name + " (missing)";
            result.worst_error = std::numeric_limits<double>::infinity();
            return result;
        }
        const auto& act_vec = it->second;

        SignalStats se = analyze_signal(expected.time, exp_vec, kDcThreshold);
        SignalStats sa = analyze_signal(actual.time,  act_vec, kDcThreshold);
        result.num_points_compared++;

        // Classification mismatch: one DC, the other oscillating.
        if (se.is_dc != sa.is_dc) {
            result.passed = false;
            result.worst_signal = name + (se.is_dc
                ? " (expected DC, actual oscillates)"
                : " (expected oscillates, actual DC)");
            result.worst_error = std::numeric_limits<double>::infinity();
            return result;
        }

        if (se.is_dc) {
            // DC node: compare late-window mean values with absolute tolerance.
            double err = std::abs(se.dc_level - sa.dc_level);
            if (err > result.worst_error) {
                result.worst_error = err;
                result.worst_signal = name + " (dc)";
            }
            if (err > tol.dc_absolute) {
                result.passed = false;
            }
            continue;
        }

        // Oscillating node: require enough periods on both sides.
        if (se.num_periods < tol.min_periods || sa.num_periods < tol.min_periods) {
            result.passed = false;
            result.worst_signal = name + " (signal did not oscillate enough to measure)";
            result.worst_error = std::numeric_limits<double>::infinity();
            return result;
        }

        // Period check.
        double period_err = std::abs(se.period - sa.period) /
                            std::max(std::abs(se.period), 1e-18);
        if (period_err > result.worst_error) {
            result.worst_error = period_err;
            result.worst_signal = name + " (period)";
        }
        if (period_err > tol.period_relative) {
            result.passed = false;
        }

        // Amplitude check.
        double amp_err = std::abs(se.amplitude - sa.amplitude) /
                         std::max(std::abs(se.amplitude), 1e-18);
        if (amp_err > result.worst_error) {
            result.worst_error = amp_err;
            result.worst_signal = name + " (amplitude)";
        }
        if (amp_err > tol.amplitude_relative) {
            result.passed = false;
        }
    }

    return result;
}

CompareResult compare_ac(const ACResult& expected, const ACResult& actual, Tolerance tol) {
    CompareResult result{true, "", 0.0, 0};

    // Compare voltage magnitudes at matching frequency points
    for (const auto& [name, exp_vec] : expected.voltages) {
        auto it = actual.voltages.find(name);
        if (it == actual.voltages.end()) {
            result.passed = false;
            result.worst_signal = name;
            result.worst_error = std::numeric_limits<double>::infinity();
            return result;
        }

        const auto& act_vec = it->second;
        size_t n = std::min(exp_vec.size(), act_vec.size());

        for (size_t i = 0; i < n; ++i) {
            double exp_mag = std::abs(exp_vec[i]);
            double act_mag = std::abs(act_vec[i]);

            double err = relative_error(exp_mag, act_mag, tol.absolute);
            result.num_points_compared++;
            if (err > result.worst_error) {
                result.worst_error = err;
                result.worst_signal = name;
            }
            if (err > tol.relative) {
                result.passed = false;
            }
        }
    }

    // Compare current magnitudes
    for (const auto& [name, exp_vec] : expected.currents) {
        auto it = actual.currents.find(name);
        if (it == actual.currents.end()) {
            result.passed = false;
            result.worst_signal = name;
            result.worst_error = std::numeric_limits<double>::infinity();
            return result;
        }

        const auto& act_vec = it->second;
        size_t n = std::min(exp_vec.size(), act_vec.size());

        for (size_t i = 0; i < n; ++i) {
            double exp_mag = std::abs(exp_vec[i]);
            double act_mag = std::abs(act_vec[i]);

            double err = relative_error(exp_mag, act_mag, tol.absolute);
            result.num_points_compared++;
            if (err > result.worst_error) {
                result.worst_error = err;
                result.worst_signal = name;
            }
            if (err > tol.relative) {
                result.passed = false;
            }
        }
    }

    return result;
}

} // namespace neospice
