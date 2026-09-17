#include "framework/comparator.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <iterator>
#include <stdexcept>

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

CompareResult invalid_comparison(const std::string& detail) {
    return {false, detail, std::numeric_limits<double>::infinity(), 0};
}

bool finite_value(double value) { return std::isfinite(value); }
bool finite_value(std::complex<double> value) {
    return std::isfinite(value.real()) && std::isfinite(value.imag());
}

bool valid_tolerance(Tolerance tol) {
    return std::isfinite(tol.relative) && tol.relative >= 0.0 &&
           std::isfinite(tol.absolute) && tol.absolute > 0.0;
}

bool valid_axis(const std::vector<double>& axis) {
    if (axis.empty()) return false;
    for (size_t i = 0; i < axis.size(); ++i) {
        if (!std::isfinite(axis[i]) || axis[i] < 0.0 ||
            (i > 0 && axis[i] <= axis[i - 1])) return false;
    }
    return true;
}

// Axis agreement permits floating-point accumulation only, independently of
// the waveform tolerance. Time endpoints retain the existing 1e-18 s snap.
bool same_coordinate(double a, double b, double snap = 0.0) {
    return std::abs(a - b) <= std::max(snap,
        64 * std::numeric_limits<double>::epsilon() * std::max(std::abs(a), std::abs(b)));
}

bool matching_frequencies(const std::vector<double>& expected,
                          const std::vector<double>& actual) {
    if (!valid_axis(expected) || !valid_axis(actual) || expected.size() != actual.size())
        return false;
    for (size_t i = 0; i < expected.size(); ++i)
        if (!same_coordinate(expected[i], actual[i])) return false;
    return true;
}

template<typename Map>
bool finite_scalars(const Map& signals) {
    for (const auto& [name, value] : signals)
        if (!finite_value(value)) return false;
    return true;
}

template<typename Map>
bool valid_vectors(const Map& signals, size_t count) {
    for (const auto& [name, values] : signals) {
        if (values.size() != count) return false;
        for (const auto& value : values)
            if (!finite_value(value)) return false;
    }
    return true;
}

bool valid_transient(const TransientResult& value) {
    return value.status.converged && value.time.size() >= 2 && valid_axis(value.time) &&
        !(value.voltages.empty() && value.currents.empty()) &&
        valid_vectors(value.voltages, value.time.size()) &&
        valid_vectors(value.currents, value.time.size());
}

bool covers_reference(const std::vector<double>& expected,
                      const std::vector<double>& actual) {
    return (actual.front() <= expected.front() ||
            same_coordinate(actual.front(), expected.front(), 1e-18)) &&
           (actual.back() >= expected.back() ||
            same_coordinate(actual.back(), expected.back(), 1e-18));
}

double relative_error(double expected, double actual, double abstol) {
    if (!std::isfinite(expected) || !std::isfinite(actual))
        return std::numeric_limits<double>::infinity();
    double denom = std::max(std::abs(expected), abstol);
    return std::abs(expected - actual) / denom;
}

double complex_relative_error(std::complex<double> expected,
                              std::complex<double> actual, double abstol) {
    const double error = std::abs(expected - actual) /
        std::max(std::abs(expected), abstol);
    return std::isfinite(error) ? error : std::numeric_limits<double>::infinity();
}

double interpolate(const std::vector<double>& xs, const std::vector<double>& ys, double x) {
    if (xs.empty()) return 0.0;
    size_t n = xs.size();
    if (x <= xs.front()) return ys.front();
    if (x >= xs.back()) return ys.back();

    auto it = std::lower_bound(xs.begin(), xs.end(), x);
    if (it == xs.begin()) return ys.front();
    size_t idx = static_cast<size_t>(std::distance(xs.begin(), it));

    // Return a sample only at its actual coordinate. A fixed time snap can
    // turn a valid steep ramp into a mismatch (or conceal a narrow feature).
    // Distinct coordinates require interpolation even when very close.
    if (x == xs[idx]) return ys[idx];

    size_t ia = idx - 1, ib = idx;
    double ya = ys[ia], yb = ys[ib];
    double t = (x - xs[ia]) / (xs[ib] - xs[ia]);
    double linear = ya + t * (yb - ya);

    // Preserve flat source segments and their corners. A polynomial spanning
    // a flat endpoint can bend an otherwise exact piecewise-linear ramp.
    const auto flat = [](double a, double b) {
        return std::abs(a - b) <= 8 * std::numeric_limits<double>::epsilon()
            * std::max(std::abs(a), std::abs(b));
    };
    if ((ia > 0 && flat(ys[ia - 1], ya)) || flat(ya, yb) ||
        (ib + 1 < n && flat(yb, ys[ib + 1]))) return linear;

    // Consider every contiguous four-point stencil containing the bracket,
    // choosing the smallest absolute third divided difference. This allows a
    // one-sided stencil at a curvature corner instead of forcing a centered
    // fit across it. Smooth extrema retain polynomial interpolation instead
    // of being replaced by a linear chord. With fewer points, lower the degree.
    // This is a local interpolation estimate, not an error bound on the signal.
    const long double scale = static_cast<long double>(xs[ib]) - xs[ia];
    const auto coordinate = [&](size_t i) {
        return (static_cast<long double>(xs[i]) - xs[ia]) / scale;
    };
    const auto coefficients = [&](size_t left, size_t count) {
        std::array<long double, 4> d{};
        for (size_t j = 0; j < count; ++j) d[j] = ys[left + j];
        for (size_t order = 1; order < count; ++order)
            for (size_t j = count - 1; j >= order; --j)
                d[j] = (d[j] - d[j - 1]) /
                    (coordinate(left + j) - coordinate(left + j - order));
        return d;
    };
    const size_t count = std::min(size_t{4}, n);
    const size_t first = ib >= count - 1 ? ib - (count - 1) : 0;
    const size_t last = std::min(ia, n - count);
    size_t left = first;
    auto d = coefficients(left, count);
    for (size_t candidate = first + 1; candidate <= last; ++candidate) {
        auto next = coefficients(candidate, count);
        if (std::abs(next[count - 1]) < std::abs(d[count - 1])) {
            left = candidate;
            d = next;
        }
    }
    const long double query = (static_cast<long double>(x) - xs[ia]) / scale;
    long double value = d[count - 1];
    for (size_t j = count - 1; j > 0; --j)
        value = d[j - 1] + (query - coordinate(left + j - 1)) * value;
    return static_cast<double>(value);
}

} // anonymous namespace

CompareResult compare_dc(const DCResult& expected, const DCResult& actual, Tolerance tol) {
    if (!valid_tolerance(tol) || !expected.status.converged || !actual.status.converged ||
        (expected.node_voltages.empty() && expected.branch_currents.empty()) ||
        !finite_scalars(expected.node_voltages) || !finite_scalars(expected.branch_currents) ||
        !finite_scalars(actual.node_voltages) || !finite_scalars(actual.branch_currents))
        return invalid_comparison("DC (invalid, empty, or unconverged result)");
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
        result.signals.push_back({name, 1, std::abs(exp_val - it->second), err,
                                  0, exp_val, it->second, "OP"});
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
        result.signals.push_back({name, 1, std::abs(exp_val - it->second), err,
                                  0, exp_val, it->second, "OP"});
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

std::string validate_dc_sweep_data(const DCSweepResult& expected, const DCSweepResult& actual) {
    for (const auto* result : {&expected, &actual}) {
        if (!result->status.converged || result->sweep_var.empty() || result->sweep_values.empty() ||
            (result->voltages.empty() && result->currents.empty()))
            return "DC sweep: empty or unsuccessful result";
        for (double x : result->sweep_values)
            if (!std::isfinite(x)) return "DC sweep: nonfinite coordinate";
        if (!valid_vectors(result->voltages, result->sweep_values.size()) ||
            !valid_vectors(result->currents, result->sweep_values.size()))
            return "DC sweep: nonfinite or malformed signal vector";
    }
    if (expected.sweep_values.size() != actual.sweep_values.size())
        return "DC sweep: coordinate counts differ";
    // Negative and descending axes are valid; nested sweeps repeat the inner
    // axis. Compare coordinates in order. Absolute roundoff uses the whole
    // range so accumulated error near a zero crossing is not treated as a
    // different requested source value. This is independent of signal limits.
    double scale = 0;
    for (double x : expected.sweep_values) scale = std::max(scale, std::abs(x));
    for (double x : actual.sweep_values) scale = std::max(scale, std::abs(x));
    const double rounding = 64 * std::numeric_limits<double>::epsilon() * scale;
    for (size_t i = 0; i < expected.sweep_values.size(); ++i)
        if (std::abs(expected.sweep_values[i] - actual.sweep_values[i]) > rounding)
            return "DC sweep: coordinates differ at index " + std::to_string(i);
    for (const auto& [name, values] : expected.voltages)
        if (!actual.voltages.contains(name)) return "DC sweep: missing voltage " + name;
    for (const auto& [name, values] : expected.currents)
        if (!actual.currents.contains(name)) return "DC sweep: missing current " + name;
    return {};
}

CompareResult compare_dc_sweep(const DCSweepResult& expected, const DCSweepResult& actual, Tolerance tol) {
    const auto invalid = validate_dc_sweep_data(expected, actual);
    if (!invalid.empty() || !valid_tolerance(tol))
        return invalid_comparison(invalid.empty() ? "DC sweep: invalid tolerance" : invalid);
    CompareResult result{true, "", 0, 0};
    for (size_t j = 0; j < expected.sweep_values.size(); ++j) {
        DCResult rp, np;
        for (const auto& [name, values] : expected.voltages) rp.node_voltages[name] = values[j];
        for (const auto& [name, values] : expected.currents) rp.branch_currents[name] = values[j];
        for (const auto& [name, values] : actual.voltages) np.node_voltages[name] = values[j];
        for (const auto& [name, values] : actual.currents) np.branch_currents[name] = values[j];
        const auto point = compare_dc(rp, np, tol);
        result.passed &= point.passed;
        result.num_points_compared += point.num_points_compared;
        if (point.worst_error > result.worst_error) {
            result.worst_error = point.worst_error;
            result.worst_signal = point.worst_signal;
        }
        if (j == 0) result.signals = point.signals;
        for (size_t k = 0; k < point.signals.size(); ++k) {
            auto& aggregate = result.signals[k];
            const auto& current = point.signals[k];
            if (j) aggregate.points += current.points;
            aggregate.max_absolute_error = std::max(aggregate.max_absolute_error, current.max_absolute_error);
            if (j == 0 || current.max_normalized_error > aggregate.max_normalized_error) {
                aggregate.max_normalized_error = current.max_normalized_error;
                aggregate.worst_coordinate = expected.sweep_values[j];
                aggregate.reference_at_worst = current.reference_at_worst;
                aggregate.actual_at_worst = current.actual_at_worst;
            }
            aggregate.coordinate_unit = expected.sweep_var.starts_with("i") ? "A" :
                expected.sweep_var.starts_with("v") ? "V" : expected.sweep_var;
        }
    }
    return result;
}

std::string validate_transient_data(const TransientResult& expected, const TransientResult& actual) {
    if (!valid_transient(expected) || !valid_transient(actual))
        return "transient (invalid, empty, or unconverged result)";
    if (!covers_reference(expected.time, actual.time))
        return "time (actual does not cover reference interval)";
    return {};
}

CompareResult compare_transient(const TransientResult& expected, const TransientResult& actual, Tolerance tol) {
    if (!valid_tolerance(tol) || !valid_transient(expected) || !valid_transient(actual))
        return invalid_comparison("transient (invalid, empty, or unconverged result)");
    if (!covers_reference(expected.time, actual.time))
        return invalid_comparison("time (actual does not cover reference interval)");
    CompareResult result{true, "", 0.0, 0};

    // Check every sampled time in either result. Restrict to the reference
    // interval, but never discard a sample merely because its grid is denser.
    std::vector<double> times;
    times.reserve(expected.time.size() + actual.time.size());
    std::set_union(expected.time.begin(), expected.time.end(),
                   actual.time.begin(), actual.time.end(), std::back_inserter(times));

    const auto compare_signals = [&](const auto& expected_signals,
                                     const auto& actual_signals) {
        for (const auto& [name, expected_values] : expected_signals) {
            auto found = actual_signals.find(name);
            if (found == actual_signals.end()) {
                result.passed = false;
                result.worst_signal = name;
                result.worst_error = std::numeric_limits<double>::infinity();
                return false;
            }
            SignalComparison signal;
            signal.name = name;
            for (double time : times) {
                if (time < expected.time.front() || time > expected.time.back()) continue;
                const double reference = interpolate(expected.time, expected_values, time);
                const double observed = interpolate(actual.time, found->second, time);
                const double error = relative_error(reference, observed, tol.absolute);
                const double absolute_error = std::isfinite(reference) && std::isfinite(observed)
                    ? std::abs(reference - observed) : std::numeric_limits<double>::infinity();
                signal.max_absolute_error = std::max(signal.max_absolute_error, absolute_error);
                if (signal.points == 0 || error > signal.max_normalized_error) {
                    signal.max_normalized_error = error;
                    signal.worst_coordinate = time;
                    signal.reference_at_worst = reference;
                    signal.actual_at_worst = observed;
                }
                ++signal.points;
                ++result.num_points_compared;
                if (error > result.worst_error) {
                    result.worst_error = error;
                    result.worst_signal = name;
                }
                if (error > tol.relative) result.passed = false;
            }
            result.signals.push_back(std::move(signal));
        }
        return true;
    };
    if (!compare_signals(expected.voltages, actual.voltages) ||
        !compare_signals(expected.currents, actual.currents)) return result;

    if (result.num_points_compared == 0)
        return invalid_comparison("transient (no points compared)");
#ifdef NEOSPICE_DEBUG_COMPARE
    for (const auto& signal : result.signals)
        std::fprintf(stderr, "DETAIL_TRAN|%s|%d|%.17g|%.17g|%.17g|%.17g|%.17g\n",
            signal.name.c_str(), signal.points, signal.max_absolute_error,
            signal.max_normalized_error, signal.worst_coordinate,
            signal.reference_at_worst, signal.actual_at_worst);
#endif
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
    for (double limit : {tol.period_relative, tol.amplitude_relative, tol.dc_absolute, tol.mid_absolute})
        if (!std::isfinite(limit) || limit < 0.0)
            return invalid_comparison("oscillator (invalid tolerance)");
    if (tol.min_periods < 1)
        return invalid_comparison("oscillator (min_periods must be positive)");
    if (!valid_transient(expected) || !valid_transient(actual) || expected.voltages.empty())
        return invalid_comparison("oscillator (invalid, empty, or unconverged result)");
    if (!covers_reference(expected.time, actual.time))
        return invalid_comparison("time (actual does not cover reference interval)");
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
        for (const auto& stats : {se, sa})
            for (double value : {stats.vmin, stats.vmax, stats.mid,
                                 stats.amplitude, stats.period, stats.dc_level})
                if (!std::isfinite(value))
                    return invalid_comparison(name + " (nonfinite oscillator statistic)");
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
    if (!valid_tolerance(tol) || !expected.status.converged || !actual.status.converged ||
        (expected.voltages.empty() && expected.currents.empty()) ||
        !matching_frequencies(expected.frequency, actual.frequency) ||
        !valid_vectors(expected.voltages, expected.frequency.size()) ||
        !valid_vectors(expected.currents, expected.frequency.size()) ||
        !valid_vectors(actual.voltages, actual.frequency.size()) ||
        !valid_vectors(actual.currents, actual.frequency.size()))
        return invalid_comparison("AC (invalid data, status, or frequency grid)");
    CompareResult result{true, "", 0.0, 0};

    // Compare full complex responses on the verified grid. Record every
    // required signal, including exact matches and other passing signals.
    const auto compare_signals = [&](const auto& reference, const auto& observed) {
        for (const auto& [name, exp_vec] : reference) {
            const auto it = observed.find(name);
            if (it == observed.end()) {
                result.passed = false;
                result.worst_signal = name;
                result.worst_error = std::numeric_limits<double>::infinity();
                return false;
            }
            SignalComparison signal;
            signal.name = name;
            signal.coordinate_unit = "Hz";
            for (size_t i = 0; i < expected.frequency.size(); ++i) {
                const auto actual_value = it->second[i];
                const double err = complex_relative_error(exp_vec[i], actual_value, tol.absolute);
                signal.max_absolute_error = std::max(signal.max_absolute_error,
                    std::abs(exp_vec[i] - actual_value));
                if (signal.points == 0 || err > signal.max_normalized_error) {
                    signal.max_normalized_error = err;
                    signal.worst_coordinate = expected.frequency[i];
                    signal.reference_at_worst = exp_vec[i].real();
                    signal.reference_imag_at_worst = exp_vec[i].imag();
                    signal.actual_at_worst = actual_value.real();
                    signal.actual_imag_at_worst = actual_value.imag();
                }
                ++signal.points;
                ++result.num_points_compared;
                if (err > result.worst_error) {
                    result.worst_error = err;
                    result.worst_signal = name;
                }
                if (err > tol.relative) result.passed = false;
            }
            result.signals.push_back(signal);
        }
        return true;
    };
    if (!compare_signals(expected.voltages, actual.voltages) ||
        !compare_signals(expected.currents, actual.currents)) return result;
#ifdef NEOSPICE_DEBUG_COMPARE
    for (const auto& signal : result.signals)
        std::fprintf(stderr, "DETAIL_AC|%s|%d|%.17g|%.17g|%.17g|%.17g|%.17g|%.17g|%.17g\n",
            signal.name.c_str(), signal.points, signal.max_absolute_error,
            signal.max_normalized_error, signal.worst_coordinate,
            signal.reference_at_worst, signal.reference_imag_at_worst,
            signal.actual_at_worst, signal.actual_imag_at_worst);
#endif

    CMP_MARGIN("AC", result, tol.relative);
    return result;
}

CompareResult compare_noise(const NgspiceNoiseResult& expected,
                            const NoiseResult& actual, Tolerance tol) {
    const auto valid_density = [](const std::vector<double>& values, size_t count) {
        return values.size() == count && std::all_of(values.begin(), values.end(),
            [](double value) { return std::isfinite(value) && value >= 0.0; });
    };
    if (!valid_tolerance(tol) || !actual.status.converged ||
        !matching_frequencies(expected.frequency, actual.frequency) ||
        !valid_density(expected.onoise_spectrum, expected.frequency.size()) ||
        !valid_density(expected.inoise_spectrum, expected.frequency.size()) ||
        !valid_density(actual.output_noise_density, actual.frequency.size()) ||
        !valid_density(actual.input_noise_density, actual.frequency.size()))
        return invalid_comparison("noise (invalid density, status, or frequency grid)");
    CompareResult result{true, "", 0.0, 0};

    size_t n = std::min(expected.frequency.size(), actual.frequency.size());
    if (n == 0) {
        result.passed = false;
        result.worst_signal = "frequency (empty)";
        result.worst_error = std::numeric_limits<double>::infinity();
        return result;
    }

    result.signals.resize(2);
    result.signals[0].name = "onoise_spectrum";
    result.signals[1].name = "inoise_spectrum";
    for (auto& signal : result.signals) signal.coordinate_unit = "Hz";
    const auto compare_point = [&](size_t signal_index, size_t i, double reference,
                                   double observed) {
        auto& signal = result.signals[signal_index];
        const double err = relative_error(reference, observed, tol.absolute);
        signal.max_absolute_error = std::max(signal.max_absolute_error,
                                             std::abs(reference - observed));
        if (signal.points == 0 || err > signal.max_normalized_error) {
            signal.max_normalized_error = err;
            signal.worst_coordinate = expected.frequency[i];
            signal.reference_at_worst = reference;
            signal.actual_at_worst = observed;
        }
        ++signal.points;
        ++result.num_points_compared;
        if (err > result.worst_error) {
            result.worst_error = err;
            result.worst_signal = signal.name;
        }
        if (err > tol.relative) result.passed = false;
    };
    for (size_t i = 0; i < n; ++i) {
        // Convert neospice PSD (V^2/Hz) to the reference amplitude density.
        compare_point(0, i, expected.onoise_spectrum[i],
                      std::sqrt(actual.output_noise_density[i]));
        compare_point(1, i, expected.inoise_spectrum[i],
                      std::sqrt(actual.input_noise_density[i]));
    }
#ifdef NEOSPICE_DEBUG_COMPARE
    for (const auto& signal : result.signals)
        std::fprintf(stderr, "DETAIL_NOISE|%s|%d|%.17g|%.17g|%.17g|%.17g|%.17g\n",
            signal.name.c_str(), signal.points, signal.max_absolute_error,
            signal.max_normalized_error, signal.worst_coordinate,
            signal.reference_at_worst, signal.actual_at_worst);
#endif

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
    return count > 0 ? sum / count : std::numeric_limits<double>::quiet_NaN();
}

double peak_in_window(const std::vector<double>& t, const std::vector<double>& y,
                      double t_start, double t_end, bool find_max) {
    double val = find_max ? -std::numeric_limits<double>::infinity()
                         : std::numeric_limits<double>::infinity();
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
    if (!valid_axis(time) || signal.size() != time.size() ||
        !std::all_of(signal.begin(), signal.end(), [](double v) { return std::isfinite(v); }) ||
        !std::isfinite(v_low) || !std::isfinite(v_high) || v_high <= v_low ||
        !std::isfinite(settle_window) || settle_window <= 0.0)
        throw std::invalid_argument("extract_edges: invalid waveform or thresholds");
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

        const double missing = std::numeric_limits<double>::quiet_NaN();
        EdgeMetrics em{missing, missing, missing, missing};
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
            em.rise_time = (t_10 >= 0 && t_90 >= 0) ? (t_90 - t_10) : missing;

            // Settled value and overshoot in window after transition
            double settle_start = em.cross_time + 2.0 * std::abs(em.rise_time);
            double settle_end = settle_start + settle_window;
            if (std::isfinite(settle_start) && settle_end <= t_end) {
                em.settled_value = mean_in_window(time, signal, settle_start, settle_end);
                double peak = peak_in_window(time, signal, em.cross_time, settle_start, true);
                if (std::isfinite(peak) && std::isfinite(em.settled_value))
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
            em.rise_time = (t_90 >= 0 && t_10 >= 0) ? -(t_10 - t_90) : missing;

            double settle_start = em.cross_time + 2.0 * std::abs(em.rise_time);
            double settle_end = settle_start + settle_window;
            if (std::isfinite(settle_start) && settle_end <= t_end) {
                em.settled_value = mean_in_window(time, signal, settle_start, settle_end);
                double peak = peak_in_window(time, signal, em.cross_time, settle_start, false);
                if (std::isfinite(peak) && std::isfinite(em.settled_value))
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
    for (double limit : {tol.crossing_relative, tol.rise_fall_relative,
                         tol.settled_absolute, tol.overshoot_absolute})
        if (!std::isfinite(limit) || limit < 0.0)
            return {false, "invalid edge tolerance", std::numeric_limits<double>::infinity(), 0};

    if (expected.empty() || expected.size() != actual.size()) {
        result.passed = false;
        result.detail = "edge count mismatch: expected " +
            std::to_string(expected.size()) + ", actual " +
            std::to_string(actual.size());
        result.worst_error = std::numeric_limits<double>::infinity();
        return result;
    }

    double worst_margin = 0.0;  // err / tol ratio — >1 means violation

    for (size_t i = 0; i < expected.size(); ++i) {
        const auto& e = expected[i];
        const auto& a = actual[i];
        std::string prefix = "edge[" + std::to_string(i) + "]";
        for (const auto& edge : {e, a}) {
            if (!std::isfinite(edge.cross_time) || !std::isfinite(edge.rise_time) ||
                !std::isfinite(edge.settled_value) || !std::isfinite(edge.overshoot) ||
                edge.cross_time < 0 || edge.rise_time == 0 || edge.overshoot < 0)
                return {false, prefix + " (invalid or incomplete metric)",
                        std::numeric_limits<double>::infinity(), 0};
        }
        if ((e.rise_time > 0) != (a.rise_time > 0) ||
            (i > 0 && (e.cross_time <= expected[i - 1].cross_time ||
                       a.cross_time <= actual[i - 1].cross_time)))
            return {false, prefix + " (edge direction or order mismatch)",
                    std::numeric_limits<double>::infinity(), 0};
        result.num_edges_compared++;

        auto check = [&](double err, double limit, const std::string& msg) {
            double margin = (limit > 0) ? err / limit :
                (err > 0 ? std::numeric_limits<double>::infinity() : 0.0);
            if (margin > worst_margin) {
                worst_margin = margin;
                result.worst_error = err;
                result.detail = msg;
            }
            if (err > limit) result.passed = false;
        };

        // 50% crossing time
        {
            double ref = std::max(std::abs(e.cross_time), 1e-18);
            double err = std::abs(e.cross_time - a.cross_time) / ref;
            check(err, tol.crossing_relative,
                  prefix + " crossing: " +
                  std::to_string(e.cross_time) + " vs " + std::to_string(a.cross_time) +
                  " (" + std::to_string(err * 100) + "%)");
        }

        // Rise/fall time
        {
            double ref = std::max(std::abs(e.rise_time), 1e-18);
            double err = std::abs(std::abs(e.rise_time) - std::abs(a.rise_time)) / ref;
            check(err, tol.rise_fall_relative,
                  prefix + (e.rise_time > 0 ? " rise_time: " : " fall_time: ") +
                  std::to_string(std::abs(e.rise_time)) + " vs " + std::to_string(std::abs(a.rise_time)) +
                  " (" + std::to_string(err * 100) + "%)");
        }

        // Settled value
        {
            double err = std::abs(e.settled_value - a.settled_value);
            check(err, tol.settled_absolute,
                  prefix + " settled: " +
                  std::to_string(e.settled_value) + " vs " + std::to_string(a.settled_value) +
                  " (delta=" + std::to_string(err) + "V)");
        }

        // Overshoot
        {
            double err = std::abs(e.overshoot - a.overshoot);
            check(err, tol.overshoot_absolute,
                  prefix + " overshoot: " +
                  std::to_string(e.overshoot) + " vs " + std::to_string(a.overshoot) +
                  " (delta=" + std::to_string(err) + "V)");
        }
    }

    CMP_MARGIN_EDGE("EDGE", result, (worst_margin > 0 ? result.worst_error / worst_margin : 0));
    return result;
}

} // namespace neospice
