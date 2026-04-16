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
