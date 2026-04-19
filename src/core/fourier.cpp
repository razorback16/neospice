#include "core/fourier.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace neospice {

namespace {

// ---------------------------------------------------------------------------
// Linear interpolation of a time-domain signal at time t.
// time[] and values[] must have equal length >= 2, time[] sorted ascending.
// ---------------------------------------------------------------------------
static double interp(const std::vector<double>& time,
                     const std::vector<double>& values,
                     double t) {
    if (time.empty()) return 0.0;
    if (t <= time.front()) return values.front();
    if (t >= time.back())  return values.back();

    // Binary search for the interval [i, i+1] containing t
    auto it = std::lower_bound(time.begin(), time.end(), t);
    size_t hi = static_cast<size_t>(it - time.begin());
    if (hi == 0) return values.front();
    size_t lo = hi - 1;

    double dt = time[hi] - time[lo];
    if (dt == 0.0) return values[lo];
    double alpha = (t - time[lo]) / dt;
    return values[lo] + alpha * (values[hi] - values[lo]);
}

// ---------------------------------------------------------------------------
// Trapezoidal integration of f(t)*cos(2*pi*k*f0*t) and
//                           f(t)*sin(2*pi*k*f0*t)
// over [t_start, t_end] using the data points in time[]/values[] that fall
// in that interval, plus exact boundary values via interpolation.
//
// Returns (a_k, b_k) where:
//   a_k = (2/T) * integral(signal * cos(k*omega*t), t_start, t_end)
//   b_k = (2/T) * integral(signal * sin(k*omega*t), t_start, t_end)
//
// For k==0:  DC magnitude = a_0/2  (factor of 2 cancelled later)
// ---------------------------------------------------------------------------
static std::pair<double, double>
dft_coeff(int k,
          double f0,
          double t_start,
          double t_end,
          const std::vector<double>& time,
          const std::vector<double>& values) {
    const double omega = 2.0 * M_PI * f0;
    const double T     = 1.0 / f0;

    // Build a working array of (t, v) that covers [t_start, t_end]:
    // 1. Interpolated point at t_start
    // 2. All original points strictly inside [t_start, t_end]
    // 3. Interpolated point at t_end

    std::vector<std::pair<double,double>> pts;
    pts.reserve(256);

    // Left boundary
    pts.emplace_back(t_start, interp(time, values, t_start));

    // Interior points
    for (size_t i = 0; i < time.size(); ++i) {
        if (time[i] > t_start && time[i] < t_end) {
            pts.emplace_back(time[i], values[i]);
        }
    }

    // Right boundary
    pts.emplace_back(t_end, interp(time, values, t_end));

    // Trapezoidal integration
    double sum_a = 0.0;
    double sum_b = 0.0;
    for (size_t i = 1; i < pts.size(); ++i) {
        double ta = pts[i-1].first;
        double tb = pts[i  ].first;
        double va = pts[i-1].second;
        double vb = pts[i  ].second;
        double dt = tb - ta;
        if (dt <= 0.0) continue;

        // Midpoint argument for phase — use exact trapezoidal evaluation:
        // integral(v(t)*cos(k*omega*t), ta, tb) ≈ (dt/2)*(fa + fb)
        // where fa = va*cos(k*omega*ta), fb = vb*cos(k*omega*tb)
        double cos_a = (k == 0) ? 1.0 : std::cos(k * omega * ta);
        double cos_b = (k == 0) ? 1.0 : std::cos(k * omega * tb);
        double sin_a = (k == 0) ? 0.0 : std::sin(k * omega * ta);
        double sin_b = (k == 0) ? 0.0 : std::sin(k * omega * tb);

        sum_a += (dt / 2.0) * (va * cos_a + vb * cos_b);
        sum_b += (dt / 2.0) * (va * sin_a + vb * sin_b);
    }

    // Scale: (2/T) factor
    double scale = 2.0 / T;
    return { scale * sum_a, scale * sum_b };
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// compute_fourier
// ---------------------------------------------------------------------------
std::vector<FourierResult> compute_fourier(
    double fundamental_freq,
    const std::vector<std::string>& signal_names,
    const TransientResult& tran_result) {

    if (fundamental_freq <= 0.0) {
        throw std::invalid_argument("compute_fourier: fundamental_freq must be positive");
    }
    if (tran_result.time.empty()) {
        // Return empty results
        std::vector<FourierResult> out;
        for (const auto& name : signal_names) {
            FourierResult fr;
            fr.signal_name      = name;
            fr.fundamental_freq = fundamental_freq;
            fr.thd              = 0.0;
            out.push_back(std::move(fr));
        }
        return out;
    }

    const double T      = 1.0 / fundamental_freq;
    const double t_end  = tran_result.time.back();
    const double t_start = t_end - T;

    std::vector<FourierResult> results;
    results.reserve(signal_names.size());

    for (const auto& name : signal_names) {
        FourierResult fr;
        fr.signal_name      = name;
        fr.fundamental_freq = fundamental_freq;

        // Locate the data vector (try voltages then currents)
        const std::vector<double>* pvals = nullptr;
        {
            auto it = tran_result.voltages.find(name);
            if (it != tran_result.voltages.end()) {
                pvals = &it->second;
            } else {
                auto it2 = tran_result.currents.find(name);
                if (it2 != tran_result.currents.end()) {
                    pvals = &it2->second;
                }
            }
        }

        const int N_HARMONICS = 9;  // 1..9
        double mag[N_HARMONICS + 1] = {};  // index 0=DC, 1..9=harmonics
        double phase[N_HARMONICS + 1] = {};

        if (pvals && !pvals->empty()) {
            const std::vector<double>& tv = tran_result.time;
            const std::vector<double>& vv = *pvals;

            for (int k = 0; k <= N_HARMONICS; ++k) {
                auto [ak, bk] = dft_coeff(k, fundamental_freq, t_start, t_end, tv, vv);

                if (k == 0) {
                    // DC: a_0/2 (the (2/T) factor is already applied, so divide by 2)
                    mag[0]   = ak / 2.0;
                    phase[0] = 0.0;
                } else {
                    mag[k]   = std::sqrt(ak * ak + bk * bk);
                    phase[k] = std::atan2(bk, ak) * (180.0 / M_PI);
                }
            }
        }

        // Build component list
        double fund_mag   = mag[1];
        double fund_phase = phase[1];

        fr.components.resize(N_HARMONICS + 1);
        for (int k = 0; k <= N_HARMONICS; ++k) {
            FourierComponent& fc = fr.components[k];
            fc.harmonic   = k;
            fc.frequency  = k * fundamental_freq;
            fc.magnitude  = mag[k];
            fc.phase_deg  = phase[k];
            if (fund_mag > 0.0) {
                fc.normalized_mag         = mag[k] / fund_mag;
                fc.normalized_phase_deg   = phase[k] - fund_phase;
            } else {
                fc.normalized_mag         = (k == 0 ? 0.0 : 0.0);
                fc.normalized_phase_deg   = 0.0;
            }
        }

        // THD = sqrt(sum(mag_k^2, k=2..9)) / mag_1 * 100%
        if (fund_mag > 0.0) {
            double sum_sq = 0.0;
            for (int k = 2; k <= N_HARMONICS; ++k) {
                sum_sq += mag[k] * mag[k];
            }
            fr.thd = std::sqrt(sum_sq) / fund_mag * 100.0;
        } else {
            fr.thd = 0.0;
        }

        results.push_back(std::move(fr));
    }

    return results;
}

} // namespace neospice
