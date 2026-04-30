#include "neospice/measure.hpp"
#include "core/transient.hpp"
#include "core/ac.hpp"
#include "core/noise.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace neospice::measure {

double rise_time(const TransientResult& r, NodeId node,
                 double low, double high) {
    auto v = r.voltage(node);
    auto t = r.time_span();
    double t_low = -1, t_high = -1;
    for (std::size_t i = 1; i < v.size(); ++i) {
        if (t_low < 0 && v[i-1] <= low && v[i] >= low) {
            double frac = (low - v[i-1]) / (v[i] - v[i-1]);
            t_low = t[i-1] + frac * (t[i] - t[i-1]);
        }
        if (t_high < 0 && v[i-1] <= high && v[i] >= high) {
            double frac = (high - v[i-1]) / (v[i] - v[i-1]);
            t_high = t[i-1] + frac * (t[i] - t[i-1]);
        }
    }
    if (t_low < 0 || t_high < 0)
        throw std::runtime_error("rise_time: thresholds not crossed");
    return t_high - t_low;
}

double settling_time(const TransientResult& r, NodeId node,
                     double final_val, double tolerance) {
    auto v = r.voltage(node);
    auto t = r.time_span();
    for (std::size_t i = v.size(); i > 0; --i) {
        if (std::abs(v[i-1] - final_val) > tolerance) {
            return (i < v.size()) ? t[i] : t.back();
        }
    }
    return 0.0;  // always within tolerance
}

double overshoot(const TransientResult& r, NodeId node, double final_val) {
    auto v = r.voltage(node);
    double peak = *std::max_element(v.begin(), v.end());
    if (final_val == 0.0) return peak;
    return (peak - final_val) / std::abs(final_val) * 100.0;
}

double rms(const TransientResult& r, NodeId node,
           double tstart, double tstop) {
    auto v = r.voltage(node);
    auto t = r.time_span();
    double sum = 0.0, total_dt = 0.0;
    for (std::size_t i = 1; i < v.size(); ++i) {
        if (t[i] < tstart) continue;
        if (t[i-1] > tstop) break;
        double dt = t[i] - t[i-1];
        double avg_sq = 0.5 * (v[i]*v[i] + v[i-1]*v[i-1]);
        sum += avg_sq * dt;
        total_dt += dt;
    }
    if (total_dt <= 0.0) return 0.0;
    return std::sqrt(sum / total_dt);
}

double bandwidth_3db(const ACResult& r, NodeId node) {
    auto db = r.magnitude_db(node);
    double dc_gain = db[0];
    double target = dc_gain - 3.0;
    for (std::size_t i = 1; i < db.size(); ++i) {
        if (db[i] <= target) {
            double frac = (target - db[i-1]) / (db[i] - db[i-1]);
            return r.frequency[i-1] + frac * (r.frequency[i] - r.frequency[i-1]);
        }
    }
    return r.frequency.back();
}

std::pair<double,double> phase_margin(const ACResult& r, NodeId node) {
    auto db = r.magnitude_db(node);
    auto ph = r.phase_deg(node);
    for (std::size_t i = 1; i < db.size(); ++i) {
        if (db[i-1] >= 0.0 && db[i] < 0.0) {
            double frac = (0.0 - db[i-1]) / (db[i] - db[i-1]);
            double f_gc = r.frequency[i-1] + frac * (r.frequency[i] - r.frequency[i-1]);
            double phase_at_gc = ph[i-1] + frac * (ph[i] - ph[i-1]);
            return {f_gc, 180.0 + phase_at_gc};
        }
    }
    return {0.0, 0.0};
}

std::pair<double,double> gain_margin(const ACResult& r, NodeId node) {
    auto db = r.magnitude_db(node);
    auto ph = r.phase_deg(node);
    for (std::size_t i = 1; i < ph.size(); ++i) {
        if (ph[i-1] >= -180.0 && ph[i] < -180.0) {
            double frac = (-180.0 - ph[i-1]) / (ph[i] - ph[i-1]);
            double f_pc = r.frequency[i-1] + frac * (r.frequency[i] - r.frequency[i-1]);
            double gain_at_pc = db[i-1] + frac * (db[i] - db[i-1]);
            return {f_pc, -gain_at_pc};
        }
    }
    return {0.0, 0.0};
}

double spot_noise(const NoiseResult& r, double freq) {
    for (std::size_t i = 1; i < r.frequency.size(); ++i) {
        if (r.frequency[i] >= freq) {
            double frac = (freq - r.frequency[i-1]) / (r.frequency[i] - r.frequency[i-1]);
            return r.output_noise_density[i-1] +
                   frac * (r.output_noise_density[i] - r.output_noise_density[i-1]);
        }
    }
    return r.output_noise_density.back();
}

} // namespace neospice::measure
