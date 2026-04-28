#include "framework/comparator.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

#ifdef NEOSPICE_DEBUG_COMPARE
#include <cstdio>
#define CMP_MARGIN(tag, result, tol_val) \
    do { if ((result).worst_error > 0) \
        std::fprintf(stderr, "MARGIN_%s|%s|%.3e|%.3e|%.1fx\n", \
            (tag), (result).worst_signal.c_str(), \
            (result).worst_error, (tol_val), \
            (tol_val) / (result).worst_error); \
    } while (0)
#define CMP_MARGIN_EDGE(tag, result, tol_val) \
    do { if ((result).worst_error > 0) \
        std::fprintf(stderr, "MARGIN_%s|%s|%.3e|%.3e|%.1fx\n", \
            (tag), (result).detail.c_str(), \
            (result).worst_error, (tol_val), \
            (tol_val) / (result).worst_error); \
    } while (0)
#else
#define CMP_MARGIN(tag, result, tol_val) ((void)0)
#define CMP_MARGIN_EDGE(tag, result, tol_val) ((void)0)
#endif

namespace neospice {

namespace {

double relative_error(double expected, double actual, double abstol) {
    double denom = std::max(std::abs(expected), abstol);
    return std::abs(expected - actual) / denom;
}

double interpolate(const std::vector<double>& xs, const std::vector<double>& ys, double x) {
    if (xs.empty()) return 0.0;
    size_t n = xs.size();
    if (x <= xs.front()) return ys.front();
    if (x >= xs.back()) return ys.back();

    auto it = std::lower_bound(xs.begin(), xs.end(), x);
    if (it == xs.begin()) return ys.front();
    size_t idx = static_cast<size_t>(std::distance(xs.begin(), it));

    // Avoid inventing error at source breakpoints when the two simulators
    // hit the same nominal time but differ by a few floating-point ulps.
    constexpr double kTimeSnapAbs = 1e-18;
    if (std::abs(x - xs[idx]) <= kTimeSnapAbs) return ys[idx];
    if (std::abs(x - xs[idx - 1]) <= kTimeSnapAbs) return ys[idx - 1];

    size_t ia = idx - 1, ib = idx;
    double ya = ys[ia], yb = ys[ib];
    double t = (x - xs[ia]) / (xs[ib] - xs[ia]);
    double linear = ya + t * (yb - ya);

    // Cubic Lagrange using 4 surrounding points, with two guards:
    // 1. Data must be monotonic across the 4 points (avoids inflection issues).
    // 2. Result must stay within [min(ya,yb), max(ya,yb)] (avoids overshoot
    //    even on monotonic data with strong curvature, e.g. exponential decay).
    // Falls back to linear when either guard fails.
    if (n >= 4 && idx >= 2 && idx + 1 < n) {
        double y0 = ys[idx - 2], y1 = ya, y2 = yb, y3 = ys[idx + 1];
        double d01 = y1 - y0, d12 = y2 - y1, d23 = y3 - y2;
        bool monotone = (d01 >= 0 && d12 >= 0 && d23 >= 0) ||
                        (d01 <= 0 && d12 <= 0 && d23 <= 0);
        if (monotone) {
            double x0 = xs[idx - 2], x1 = xs[ia], x2 = xs[ib], x3 = xs[idx + 1];
            double L0 = ((x-x1)*(x-x2)*(x-x3)) / ((x0-x1)*(x0-x2)*(x0-x3));
            double L1 = ((x-x0)*(x-x2)*(x-x3)) / ((x1-x0)*(x1-x2)*(x1-x3));
            double L2 = ((x-x0)*(x-x1)*(x-x3)) / ((x2-x0)*(x2-x1)*(x2-x3));
            double L3 = ((x-x0)*(x-x1)*(x-x2)) / ((x3-x0)*(x3-x1)*(x3-x2));
            double cubic = L0*y0 + L1*y1 + L2*y2 + L3*y3;
            double lo = std::min(ya, yb);
            double hi = std::max(ya, yb);
            if (cubic >= lo && cubic <= hi)
                return cubic;
        }
    }

    return linear;
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

    CMP_MARGIN("DC", result, tol.relative);
    return result;
}

CompareResult compare_transient(const TransientResult& expected, const TransientResult& actual, Tolerance tol) {
    CompareResult result{true, "", 0.0, 0};

    // Determine which time grid is denser.  Cubic interpolation is accurate
    // on a dense grid but overshoots on a sparse one, so always interpolate
    // FROM the denser grid.  Signal-presence checks still use expected's
    // signal set (all expected signals must exist in actual).
    bool dense_is_actual = (actual.time.size() >= expected.time.size());
    const auto& iter_time   = dense_is_actual ? expected.time : actual.time;
    const auto& interp_time = dense_is_actual ? actual.time   : expected.time;

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
        const auto& iter_vec   = dense_is_actual ? exp_vec  : act_vec;
        const auto& interp_vec = dense_is_actual ? act_vec  : exp_vec;

        for (size_t i = 0; i < iter_time.size(); ++i) {
            double t = iter_time[i];
            double iter_val = iter_vec[i];
            double interp_val = interpolate(interp_time, interp_vec, t);

            double err = relative_error(iter_val, interp_val, tol.absolute);
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
        const auto& iter_vec   = dense_is_actual ? exp_vec  : act_vec;
        const auto& interp_vec = dense_is_actual ? act_vec  : exp_vec;

        for (size_t i = 0; i < iter_time.size(); ++i) {
            double t = iter_time[i];
            double iter_val = iter_vec[i];
            double interp_val = interpolate(interp_time, interp_vec, t);

            double err = relative_error(iter_val, interp_val, tol.absolute);
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

    CMP_MARGIN("TRAN", result, tol.relative);
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

        // Mid-level (DC bias) offset check — catches a bug where the waveform
        // oscillates at the right frequency/amplitude but is shifted up/down.
        double mid_err = std::abs(se.mid - sa.mid);
        if (mid_err > result.worst_error) {
            result.worst_error = mid_err;
            result.worst_signal = name + " (mid-offset)";
        }
        if (mid_err > tol.mid_absolute) {
            result.passed = false;
        }
    }

    CMP_MARGIN("OSC", result, tol.period_relative);
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

    CMP_MARGIN("AC", result, tol.relative);
    return result;
}

CompareResult compare_noise(const NgspiceNoiseResult& expected,
                            const NoiseResult& actual, Tolerance tol) {
    CompareResult result{true, "", 0.0, 0};

    size_t n = std::min(expected.frequency.size(), actual.frequency.size());
    if (n == 0) {
        result.passed = false;
        result.worst_signal = "frequency (empty)";
        result.worst_error = std::numeric_limits<double>::infinity();
        return result;
    }

    for (size_t i = 0; i < n; ++i) {
        // neospice: V^2/Hz -> convert to V/sqrt(Hz) for comparison
        double neo_onoise = std::sqrt(actual.output_noise_density[i]);
        double neo_inoise = std::sqrt(actual.input_noise_density[i]);

        double ng_onoise = expected.onoise_spectrum[i];
        double ng_inoise = expected.inoise_spectrum[i];

        double oerr = relative_error(ng_onoise, neo_onoise, tol.absolute);
        result.num_points_compared++;
        if (oerr > result.worst_error) {
            result.worst_error = oerr;
            result.worst_signal = "onoise_spectrum";
        }
        if (oerr > tol.relative) {
            result.passed = false;
        }

        double ierr = relative_error(ng_inoise, neo_inoise, tol.absolute);
        result.num_points_compared++;
        if (ierr > result.worst_error) {
            result.worst_error = ierr;
            result.worst_signal = "inoise_spectrum";
        }
        if (ierr > tol.relative) {
            result.passed = false;
        }
    }

    CMP_MARGIN("NOISE", result, tol.relative);
    return result;
}

// ---------------------------------------------------------------------------
// Timing-based edge extraction and comparison
// ---------------------------------------------------------------------------

namespace {

double mean_in_window(const std::vector<double>& t, const std::vector<double>& y,
                      double t_start, double t_end) {
    double sum = 0.0;
    int count = 0;
    for (size_t i = 0; i < t.size(); ++i) {
        if (t[i] >= t_start && t[i] <= t_end) {
            sum += y[i];
            ++count;
        }
    }
    return count > 0 ? sum / count : 0.0;
}

double peak_in_window(const std::vector<double>& t, const std::vector<double>& y,
                      double t_start, double t_end, bool find_max) {
    double val = find_max ? -1e30 : 1e30;
    for (size_t i = 0; i < t.size(); ++i) {
        if (t[i] >= t_start && t[i] <= t_end) {
            val = find_max ? std::max(val, y[i]) : std::min(val, y[i]);
        }
    }
    return val;
}

} // anonymous namespace

std::vector<EdgeMetrics> extract_edges(
    const std::vector<double>& time,
    const std::vector<double>& signal,
    double v_low, double v_high,
    double settle_window) {

    std::vector<EdgeMetrics> edges;
    if (time.size() < 2) return edges;

    double v_mid = 0.5 * (v_low + v_high);
    double v_10 = v_low + 0.1 * (v_high - v_low);
    double v_90 = v_low + 0.9 * (v_high - v_low);
    double t_end = time.back();

    size_t i = 1;
    while (i < time.size()) {
        bool rising_cross = (signal[i-1] < v_mid && signal[i] >= v_mid);
        bool falling_cross = (signal[i-1] > v_mid && signal[i] <= v_mid);

        if (!rising_cross && !falling_cross) { ++i; continue; }

        EdgeMetrics em{};
        double dy = signal[i] - signal[i-1];
        double frac = (std::abs(dy) > 1e-30) ? (v_mid - signal[i-1]) / dy : 0.5;
        em.cross_time = time[i-1] + frac * (time[i] - time[i-1]);

        if (rising_cross) {
            // Rising edge: find 10% and 90% crossings around this point
            // Search backwards for 10% crossing
            double t_10 = -1;
            for (size_t j = i; j > 0; --j) {
                if (signal[j-1] < v_10 && signal[j] >= v_10) {
                    double d = signal[j] - signal[j-1];
                    double f = (std::abs(d) > 1e-30) ? (v_10 - signal[j-1]) / d : 0.5;
                    t_10 = time[j-1] + f * (time[j] - time[j-1]);
                    break;
                }
            }
            // Search forward for 90% crossing
            double t_90 = -1;
            for (size_t j = i; j < time.size(); ++j) {
                if (signal[j-1] < v_90 && signal[j] >= v_90) {
                    double d = signal[j] - signal[j-1];
                    double f = (std::abs(d) > 1e-30) ? (v_90 - signal[j-1]) / d : 0.5;
                    t_90 = time[j-1] + f * (time[j] - time[j-1]);
                    break;
                }
            }
            em.rise_time = (t_10 >= 0 && t_90 >= 0) ? (t_90 - t_10) : -1.0;

            // Settled value and overshoot in window after transition
            double settle_start = em.cross_time + 2.0 * std::abs(em.rise_time > 0 ? em.rise_time : settle_window);
            double settle_end = std::min(settle_start + settle_window, t_end);
            if (settle_start < t_end) {
                em.settled_value = mean_in_window(time, signal, settle_start, settle_end);
                double peak = peak_in_window(time, signal, em.cross_time, settle_start, true);
                em.overshoot = std::max(0.0, peak - em.settled_value);
            }
        } else {
            // Falling edge: find 90% and 10% crossings
            double t_90 = -1;
            for (size_t j = i; j > 0; --j) {
                if (signal[j-1] > v_90 && signal[j] <= v_90) {
                    double d = signal[j] - signal[j-1];
                    double f = (std::abs(d) > 1e-30) ? (v_90 - signal[j-1]) / d : 0.5;
                    t_90 = time[j-1] + f * (time[j] - time[j-1]);
                    break;
                }
            }
            double t_10 = -1;
            for (size_t j = i; j < time.size(); ++j) {
                if (signal[j-1] > v_10 && signal[j] <= v_10) {
                    double d = signal[j] - signal[j-1];
                    double f = (std::abs(d) > 1e-30) ? (v_10 - signal[j-1]) / d : 0.5;
                    t_10 = time[j-1] + f * (time[j] - time[j-1]);
                    break;
                }
            }
            em.rise_time = (t_90 >= 0 && t_10 >= 0) ? -(t_10 - t_90) : -1.0;

            double settle_start = em.cross_time + 2.0 * std::abs(em.rise_time > 0 ? em.rise_time : settle_window);
            double settle_end = std::min(settle_start + settle_window, t_end);
            if (settle_start < t_end) {
                em.settled_value = mean_in_window(time, signal, settle_start, settle_end);
                double peak = peak_in_window(time, signal, em.cross_time, settle_start, false);
                em.overshoot = std::max(0.0, em.settled_value - peak);
            }
        }

        edges.push_back(em);

        // Skip past this transition to avoid double-counting
        while (i < time.size() && std::abs(time[i] - em.cross_time) < settle_window * 0.5)
            ++i;
        ++i;
    }

    return edges;
}

EdgeCompareResult compare_edges(
    const std::vector<EdgeMetrics>& expected,
    const std::vector<EdgeMetrics>& actual,
    EdgeTolerance tol) {

    EdgeCompareResult result{true, "", 0.0, 0};
    double worst_tol = tol.crossing_relative;

    if (expected.size() != actual.size()) {
        result.passed = false;
        result.detail = "edge count mismatch: expected " +
            std::to_string(expected.size()) + ", actual " +
            std::to_string(actual.size());
        result.worst_error = std::numeric_limits<double>::infinity();
        return result;
    }

    for (size_t i = 0; i < expected.size(); ++i) {
        const auto& e = expected[i];
        const auto& a = actual[i];
        std::string prefix = "edge[" + std::to_string(i) + "]";

        // 50% crossing time
        if (e.cross_time > 0 && a.cross_time > 0) {
            double ref = std::max(std::abs(e.cross_time), 1e-18);
            double err = std::abs(e.cross_time - a.cross_time) / ref;
            result.num_edges_compared++;
            if (err > result.worst_error) {
                result.worst_error = err;
                worst_tol = tol.crossing_relative;
                result.detail = prefix + " crossing: " +
                    std::to_string(e.cross_time) + " vs " + std::to_string(a.cross_time) +
                    " (" + std::to_string(err * 100) + "%)";
            }
            if (err > tol.crossing_relative) result.passed = false;
        }

        // Rise/fall time
        if (e.rise_time != -1.0 && a.rise_time != -1.0) {
            double ref = std::max(std::abs(e.rise_time), 1e-18);
            double err = std::abs(std::abs(e.rise_time) - std::abs(a.rise_time)) / ref;
            result.num_edges_compared++;
            if (err > result.worst_error) {
                result.worst_error = err;
                worst_tol = tol.rise_fall_relative;
                result.detail = prefix + (e.rise_time > 0 ? " rise_time: " : " fall_time: ") +
                    std::to_string(std::abs(e.rise_time)) + " vs " + std::to_string(std::abs(a.rise_time)) +
                    " (" + std::to_string(err * 100) + "%)";
            }
            if (err > tol.rise_fall_relative) result.passed = false;
        }

        // Settled value
        {
            double err = std::abs(e.settled_value - a.settled_value);
            result.num_edges_compared++;
            if (err > result.worst_error) {
                result.worst_error = err;
                worst_tol = tol.settled_absolute;
                result.detail = prefix + " settled: " +
                    std::to_string(e.settled_value) + " vs " + std::to_string(a.settled_value) +
                    " (delta=" + std::to_string(err) + "V)";
            }
            if (err > tol.settled_absolute) result.passed = false;
        }

        // Overshoot
        {
            double err = std::abs(e.overshoot - a.overshoot);
            result.num_edges_compared++;
            if (err > result.worst_error) {
                result.worst_error = err;
                worst_tol = tol.overshoot_absolute;
                result.detail = prefix + " overshoot: " +
                    std::to_string(e.overshoot) + " vs " + std::to_string(a.overshoot) +
                    " (delta=" + std::to_string(err) + "V)";
            }
            if (err > tol.overshoot_absolute) result.passed = false;
        }
    }

    CMP_MARGIN_EDGE("EDGE", result, worst_tol);
    return result;
}

} // namespace neospice
