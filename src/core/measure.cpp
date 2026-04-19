#include "core/measure.hpp"
#include "parser/expression.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace neospice {

namespace {

// ---------------------------------------------------------------------------
// Helper: get a signal vector from transient results.
// Handles v(node) and i(source) formats.
// ---------------------------------------------------------------------------
const std::vector<double>* get_tran_signal(const TransientResult& tran,
                                            const std::string& signal_name) {
    // Try voltages first
    auto it = tran.voltages.find(signal_name);
    if (it != tran.voltages.end()) return &it->second;
    // Try currents
    it = tran.currents.find(signal_name);
    if (it != tran.currents.end()) return &it->second;
    return nullptr;
}

// ---------------------------------------------------------------------------
// Helper: get a signal vector from DC sweep results.
// ---------------------------------------------------------------------------
const std::vector<double>* get_dc_signal(const DCSweepResult& dc,
                                          const std::string& signal_name) {
    auto it = dc.voltages.find(signal_name);
    if (it != dc.voltages.end()) return &it->second;
    it = dc.currents.find(signal_name);
    if (it != dc.currents.end()) return &it->second;
    return nullptr;
}

// ---------------------------------------------------------------------------
// Helper: get AC signal magnitude vector.
// ---------------------------------------------------------------------------
std::vector<double> get_ac_magnitude(const ACResult& ac,
                                      const std::string& signal_name) {
    std::vector<double> mag;
    auto it = ac.voltages.find(signal_name);
    if (it != ac.voltages.end()) {
        mag.reserve(it->second.size());
        for (const auto& c : it->second) mag.push_back(std::abs(c));
        return mag;
    }
    it = ac.currents.find(signal_name);
    if (it != ac.currents.end()) {
        mag.reserve(it->second.size());
        for (const auto& c : it->second) mag.push_back(std::abs(c));
        return mag;
    }
    return mag;
}

// ---------------------------------------------------------------------------
// Find the time/freq/sweep index range for [from, to].
// Returns (start_idx, end_idx) inclusive.
// ---------------------------------------------------------------------------
std::pair<size_t, size_t> find_range(const std::vector<double>& xvec,
                                      double from_val, double to_val) {
    size_t start = 0;
    size_t end = xvec.empty() ? 0 : xvec.size() - 1;

    if (from_val > -1e29) {
        // Find first index >= from_val
        auto it = std::lower_bound(xvec.begin(), xvec.end(), from_val);
        if (it != xvec.end()) start = static_cast<size_t>(it - xvec.begin());
    }
    if (to_val < 1e29) {
        // Find last index <= to_val
        auto it = std::upper_bound(xvec.begin(), xvec.end(), to_val);
        if (it != xvec.begin()) end = static_cast<size_t>((it - 1) - xvec.begin());
    }
    return {start, end};
}

// ---------------------------------------------------------------------------
// Find a threshold crossing in a signal vector.
// direction: "rise", "fall", "cross"
// count: which crossing (1-based)
// Returns the interpolated x-value at the crossing, or NaN if not found.
// ---------------------------------------------------------------------------
double find_crossing(const std::vector<double>& xvec,
                     const std::vector<double>& signal,
                     double threshold,
                     const std::string& direction,
                     int count) {
    if (signal.size() < 2 || xvec.size() < 2) return std::nan("");
    int crossings_found = 0;
    size_t n = std::min(xvec.size(), signal.size());
    for (size_t i = 1; i < n; ++i) {
        bool is_rise = (signal[i - 1] < threshold && signal[i] >= threshold);
        bool is_fall = (signal[i - 1] > threshold && signal[i] <= threshold);
        // Also handle exact equality at point i-1
        bool is_rise2 = (signal[i - 1] <= threshold && signal[i] > threshold);
        bool is_fall2 = (signal[i - 1] >= threshold && signal[i] < threshold);
        is_rise = is_rise || is_rise2;
        is_fall = is_fall || is_fall2;

        bool match = false;
        if (direction == "rise" && is_rise) match = true;
        else if (direction == "fall" && is_fall) match = true;
        else if (direction == "cross" && (is_rise || is_fall)) match = true;

        if (match) {
            crossings_found++;
            if (crossings_found == count) {
                // Linear interpolation
                double y0 = signal[i - 1] - threshold;
                double y1 = signal[i] - threshold;
                double frac = (y1 != y0) ? (-y0 / (y1 - y0)) : 0.0;
                return xvec[i - 1] + frac * (xvec[i] - xvec[i - 1]);
            }
        }
    }
    return std::nan("");
}

// ---------------------------------------------------------------------------
// Interpolate a signal value at a given x-point (linear interpolation).
// ---------------------------------------------------------------------------
double interpolate_at(const std::vector<double>& xvec,
                      const std::vector<double>& signal,
                      double x_target) {
    if (xvec.empty() || signal.empty()) return std::nan("");
    if (x_target <= xvec.front()) return signal.front();
    if (x_target >= xvec.back()) return signal.back();

    // Binary search for the interval
    auto it = std::lower_bound(xvec.begin(), xvec.end(), x_target);
    if (it == xvec.begin()) return signal.front();
    size_t idx = static_cast<size_t>(it - xvec.begin());
    if (idx >= signal.size()) idx = signal.size() - 1;

    double x0 = xvec[idx - 1], x1 = xvec[idx];
    double y0 = signal[idx - 1], y1 = signal[idx];
    double frac = (x1 != x0) ? ((x_target - x0) / (x1 - x0)) : 0.0;
    return y0 + frac * (y1 - y0);
}

// ---------------------------------------------------------------------------
// Statistical measures on a signal within [from, to].
// ---------------------------------------------------------------------------
double compute_min(const std::vector<double>& signal, size_t start, size_t end) {
    double m = signal[start];
    for (size_t i = start + 1; i <= end; ++i) m = std::min(m, signal[i]);
    return m;
}

double compute_max(const std::vector<double>& signal, size_t start, size_t end) {
    double m = signal[start];
    for (size_t i = start + 1; i <= end; ++i) m = std::max(m, signal[i]);
    return m;
}

double compute_avg(const std::vector<double>& xvec, const std::vector<double>& signal,
                   size_t start, size_t end) {
    // Trapezoidal average: integral / (x_end - x_start)
    if (start >= end) return signal[start];
    double integral = 0.0;
    for (size_t i = start; i < end; ++i) {
        double dx = xvec[i + 1] - xvec[i];
        integral += 0.5 * (signal[i] + signal[i + 1]) * dx;
    }
    double span = xvec[end] - xvec[start];
    return (span > 0) ? integral / span : signal[start];
}

double compute_rms(const std::vector<double>& xvec, const std::vector<double>& signal,
                   size_t start, size_t end) {
    // RMS via trapezoidal integration of signal^2
    if (start >= end) return std::abs(signal[start]);
    double integral = 0.0;
    for (size_t i = start; i < end; ++i) {
        double dx = xvec[i + 1] - xvec[i];
        integral += 0.5 * (signal[i] * signal[i] + signal[i + 1] * signal[i + 1]) * dx;
    }
    double span = xvec[end] - xvec[start];
    return (span > 0) ? std::sqrt(integral / span) : std::abs(signal[start]);
}

double compute_integ(const std::vector<double>& xvec, const std::vector<double>& signal,
                     size_t start, size_t end) {
    if (start >= end) return 0.0;
    double integral = 0.0;
    for (size_t i = start; i < end; ++i) {
        double dx = xvec[i + 1] - xvec[i];
        integral += 0.5 * (signal[i] + signal[i + 1]) * dx;
    }
    return integral;
}

// ---------------------------------------------------------------------------
// Execute a single measure command.
// ---------------------------------------------------------------------------
double execute_one_measure(const MeasureCommand& cmd,
                           const TransientResult* tran,
                           const ACResult* ac,
                           const DCSweepResult* dc_sweep,
                           const std::unordered_map<std::string, double>& prev_results) {
    std::string atype = cmd.analysis_type;

    // PARAM: evaluate expression using previously computed measures
    if (cmd.measure_type == "param") {
        return eval_expression(cmd.param_expr, prev_results);
    }

    // Determine the x-axis vector and signal accessor based on analysis type
    if (atype == "tran") {
        if (!tran) throw std::runtime_error("Measure '" + cmd.name + "': no transient result available");
        const auto& xvec = tran->time;

        if (cmd.measure_type == "trig_targ") {
            const auto* trig_sig = get_tran_signal(*tran, cmd.trig_signal);
            const auto* targ_sig = get_tran_signal(*tran, cmd.targ_signal);
            if (!trig_sig) throw std::runtime_error("Measure '" + cmd.name + "': trig signal '" + cmd.trig_signal + "' not found");
            if (!targ_sig) throw std::runtime_error("Measure '" + cmd.name + "': targ signal '" + cmd.targ_signal + "' not found");

            double t_trig = find_crossing(xvec, *trig_sig, cmd.trig_val, cmd.trig_direction, cmd.trig_td_count);
            double t_targ = find_crossing(xvec, *targ_sig, cmd.targ_val, cmd.targ_direction, cmd.targ_td_count);
            return t_targ - t_trig;
        }

        if (cmd.measure_type == "find_when") {
            if (cmd.at_given) {
                // FIND signal AT=val
                const auto* sig = get_tran_signal(*tran, cmd.find_signal);
                if (!sig) throw std::runtime_error("Measure '" + cmd.name + "': find signal '" + cmd.find_signal + "' not found");
                return interpolate_at(xvec, *sig, cmd.at_val);
            }
            // FIND signal WHEN signal2=val
            const auto* when_sig = get_tran_signal(*tran, cmd.when_signal);
            if (!when_sig) throw std::runtime_error("Measure '" + cmd.name + "': when signal '" + cmd.when_signal + "' not found");
            double t_when = find_crossing(xvec, *when_sig, cmd.when_val, cmd.when_direction, cmd.when_td_count);
            if (std::isnan(t_when)) return std::nan("");
            const auto* find_sig = get_tran_signal(*tran, cmd.find_signal);
            if (!find_sig) throw std::runtime_error("Measure '" + cmd.name + "': find signal '" + cmd.find_signal + "' not found");
            return interpolate_at(xvec, *find_sig, t_when);
        }

        // Statistical measures
        const auto* sig = get_tran_signal(*tran, cmd.signal);
        if (!sig) throw std::runtime_error("Measure '" + cmd.name + "': signal '" + cmd.signal + "' not found");
        auto [start, end] = find_range(xvec, cmd.from_val, cmd.to_val);
        if (start > end || end >= sig->size()) return std::nan("");

        if (cmd.measure_type == "min") return compute_min(*sig, start, end);
        if (cmd.measure_type == "max") return compute_max(*sig, start, end);
        if (cmd.measure_type == "pp") return compute_max(*sig, start, end) - compute_min(*sig, start, end);
        if (cmd.measure_type == "avg") return compute_avg(xvec, *sig, start, end);
        if (cmd.measure_type == "rms") return compute_rms(xvec, *sig, start, end);
        if (cmd.measure_type == "integ") return compute_integ(xvec, *sig, start, end);

    } else if (atype == "ac") {
        if (!ac) throw std::runtime_error("Measure '" + cmd.name + "': no AC result available");
        const auto& xvec = ac->frequency;

        if (cmd.measure_type == "trig_targ") {
            auto trig_mag = get_ac_magnitude(*ac, cmd.trig_signal);
            auto targ_mag = get_ac_magnitude(*ac, cmd.targ_signal);
            if (trig_mag.empty()) throw std::runtime_error("Measure '" + cmd.name + "': trig signal '" + cmd.trig_signal + "' not found");
            if (targ_mag.empty()) throw std::runtime_error("Measure '" + cmd.name + "': targ signal '" + cmd.targ_signal + "' not found");

            double f_trig = find_crossing(xvec, trig_mag, cmd.trig_val, cmd.trig_direction, cmd.trig_td_count);
            double f_targ = find_crossing(xvec, targ_mag, cmd.targ_val, cmd.targ_direction, cmd.targ_td_count);
            return f_targ - f_trig;
        }

        if (cmd.measure_type == "find_when") {
            if (cmd.at_given) {
                auto find_mag = get_ac_magnitude(*ac, cmd.find_signal);
                if (find_mag.empty()) throw std::runtime_error("Measure '" + cmd.name + "': find signal '" + cmd.find_signal + "' not found");
                return interpolate_at(xvec, find_mag, cmd.at_val);
            }
            auto when_mag = get_ac_magnitude(*ac, cmd.when_signal);
            if (when_mag.empty()) throw std::runtime_error("Measure '" + cmd.name + "': when signal '" + cmd.when_signal + "' not found");
            double f_when = find_crossing(xvec, when_mag, cmd.when_val, cmd.when_direction, cmd.when_td_count);
            if (std::isnan(f_when)) return std::nan("");
            auto find_mag = get_ac_magnitude(*ac, cmd.find_signal);
            if (find_mag.empty()) throw std::runtime_error("Measure '" + cmd.name + "': find signal '" + cmd.find_signal + "' not found");
            return interpolate_at(xvec, find_mag, f_when);
        }

        // Statistical measures on AC magnitude
        auto mag = get_ac_magnitude(*ac, cmd.signal);
        if (mag.empty()) throw std::runtime_error("Measure '" + cmd.name + "': signal '" + cmd.signal + "' not found");
        auto [start, end] = find_range(xvec, cmd.from_val, cmd.to_val);
        if (start > end || end >= mag.size()) return std::nan("");

        if (cmd.measure_type == "min") return compute_min(mag, start, end);
        if (cmd.measure_type == "max") return compute_max(mag, start, end);
        if (cmd.measure_type == "pp") return compute_max(mag, start, end) - compute_min(mag, start, end);
        if (cmd.measure_type == "avg") return compute_avg(xvec, mag, start, end);
        if (cmd.measure_type == "rms") return compute_rms(xvec, mag, start, end);
        if (cmd.measure_type == "integ") return compute_integ(xvec, mag, start, end);

    } else if (atype == "dc") {
        if (!dc_sweep) throw std::runtime_error("Measure '" + cmd.name + "': no DC sweep result available");
        const auto& xvec = dc_sweep->sweep_values;

        if (cmd.measure_type == "find_when") {
            if (cmd.at_given) {
                const auto* sig = get_dc_signal(*dc_sweep, cmd.find_signal);
                if (!sig) throw std::runtime_error("Measure '" + cmd.name + "': find signal '" + cmd.find_signal + "' not found");
                return interpolate_at(xvec, *sig, cmd.at_val);
            }
            const auto* when_sig = get_dc_signal(*dc_sweep, cmd.when_signal);
            if (!when_sig) throw std::runtime_error("Measure '" + cmd.name + "': when signal '" + cmd.when_signal + "' not found");
            double x_when = find_crossing(xvec, *when_sig, cmd.when_val, cmd.when_direction, cmd.when_td_count);
            if (std::isnan(x_when)) return std::nan("");
            const auto* find_sig = get_dc_signal(*dc_sweep, cmd.find_signal);
            if (!find_sig) throw std::runtime_error("Measure '" + cmd.name + "': find signal '" + cmd.find_signal + "' not found");
            return interpolate_at(xvec, *find_sig, x_when);
        }

        if (cmd.measure_type == "trig_targ") {
            const auto* trig_sig = get_dc_signal(*dc_sweep, cmd.trig_signal);
            const auto* targ_sig = get_dc_signal(*dc_sweep, cmd.targ_signal);
            if (!trig_sig) throw std::runtime_error("Measure '" + cmd.name + "': trig signal '" + cmd.trig_signal + "' not found");
            if (!targ_sig) throw std::runtime_error("Measure '" + cmd.name + "': targ signal '" + cmd.targ_signal + "' not found");

            double x_trig = find_crossing(xvec, *trig_sig, cmd.trig_val, cmd.trig_direction, cmd.trig_td_count);
            double x_targ = find_crossing(xvec, *targ_sig, cmd.targ_val, cmd.targ_direction, cmd.targ_td_count);
            return x_targ - x_trig;
        }

        // Statistical measures
        const auto* sig = get_dc_signal(*dc_sweep, cmd.signal);
        if (!sig) throw std::runtime_error("Measure '" + cmd.name + "': signal '" + cmd.signal + "' not found");
        auto [start, end] = find_range(xvec, cmd.from_val, cmd.to_val);
        if (start > end || end >= sig->size()) return std::nan("");

        if (cmd.measure_type == "min") return compute_min(*sig, start, end);
        if (cmd.measure_type == "max") return compute_max(*sig, start, end);
        if (cmd.measure_type == "pp") return compute_max(*sig, start, end) - compute_min(*sig, start, end);
        if (cmd.measure_type == "avg") return compute_avg(xvec, *sig, start, end);
        if (cmd.measure_type == "rms") return compute_rms(xvec, *sig, start, end);
        if (cmd.measure_type == "integ") return compute_integ(xvec, *sig, start, end);
    }

    throw std::runtime_error("Measure '" + cmd.name + "': unsupported measure_type '" + cmd.measure_type + "' for analysis '" + atype + "'");
}

} // anonymous namespace

MeasureResult execute_measures(const std::vector<MeasureCommand>& measures,
                               const TransientResult* tran,
                               const ACResult* ac,
                               const DCSweepResult* dc_sweep) {
    MeasureResult result;
    for (const auto& cmd : measures) {
        double val = execute_one_measure(cmd, tran, ac, dc_sweep, result.values);
        result.values[cmd.name] = val;
    }
    return result;
}

} // namespace neospice
