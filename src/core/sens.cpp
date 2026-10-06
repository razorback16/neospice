/**********
Portions of this file are translated from UC Berkeley SPICE3F5
(sensan.c) and derive from the earlier SPICE2G6 (1983) Fortran,
tracing to L. Nagel's SPICE, UC Berkeley ERL Memorandum M382 (1973).

Copyright 1990 Regents of the University of California. All rights reserved.
(Permissive BSD-style "Berkeley Spice3" license.)
See NOTICE and CREDITS.md for full attribution.
**********/

#include "core/sens.hpp"
#include "core/dc.hpp"
#include "devices/resistor.hpp"
#include "devices/vsource.hpp"
#include "devices/isource.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>

namespace neospice {

static std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

/// Parse an output variable string like "v(out)", "v(a,b)", or "i(v1)" and
/// extract its value from the given DCResult.
static double extract_output(const DCResult& dc, const std::string& output_var) {
    std::string out = to_lower(output_var);

    if (out.size() > 2 && out[0] == 'v' && out[1] == '(') {
        std::string inner = out.substr(2, out.size() - 3);
        auto comma = inner.find(',');
        if (comma != std::string::npos) {
            // Differential voltage v(a,b)
            std::string np = inner.substr(0, comma);
            std::string nn = inner.substr(comma + 1);
            while (!np.empty() && np.back() == ' ') np.pop_back();
            while (!nn.empty() && nn.front() == ' ') nn.erase(nn.begin());
            double vp = (np == "0" || np == "gnd") ? 0.0 : dc.voltage(np);
            double vn = (nn == "0" || nn == "gnd") ? 0.0 : dc.voltage(nn);
            return vp - vn;
        } else {
            // Single node voltage v(out)
            if (inner == "0" || inner == "gnd") return 0.0;
            return dc.voltage(inner);
        }
    } else if (out.size() > 2 && out[0] == 'i' && out[1] == '(') {
        std::string dev = out.substr(2, out.size() - 3);
        return dc.current(dev);
    }
    throw std::invalid_argument("SENS: unrecognized output variable '" + output_var + "'");
}

namespace {

template<class T>
struct ParameterRestorer {
    T& device;
    void (T::*setter)(double);
    double original;
    ~ParameterRestorer() { (device.*setter)(original); }
};

// Apple Clang on macOS 14 needs an explicit guide for aggregate deduction.
template<class T>
ParameterRestorer(T&, void (T::*)(double), double) -> ParameterRestorer<T>;

struct SensitivityCacheGuard {
    Circuit& circuit;
    // Device states may describe the final perturbation. A subsequent analysis
    // must solve the restored circuit again instead of reusing that DC cache.
    ~SensitivityCacheGuard() { circuit.clear_operating_point(); }
};

} // namespace

SensResult solve_sens(Circuit& ckt, const std::string& output_var) {
    const auto t_start = std::chrono::steady_clock::now();
    SensitivityCacheGuard cache_guard{ckt};
    SensResult result;
    result.output_var = to_lower(output_var);
    int total_iterations = 0;
    const auto elapsed = [&] {
        return std::chrono::duration<double>(
            std::chrono::steady_clock::now() - t_start).count();
    };
    const auto fail = [&](SimStatus status, const std::string& context) {
        status.converged = false;
        status.iterations = total_iterations;
        status.elapsed_seconds = elapsed();
        status.warnings.push_back(context);
        result.status = status;
        if (!ckt.options.no_throw) throw SimulationError(context, status);
    };
    const auto solve_point = [&](const std::string& context) -> std::optional<DCResult> {
        DCResult dc;
        try {
            dc = solve_dc(ckt);
        } catch (const SimulationError& error) {
            total_iterations += error.status().iterations;
            fail(error.status(), context + ": " + error.what());
            return std::nullopt;
        }
        total_iterations += dc.status.iterations;
        if (!dc.status.converged) {
            fail(dc.status, context + ": DC solve did not converge");
            return std::nullopt;
        }
        return dc;
    };

    const auto baseline = solve_point("Sensitivity baseline");
    if (!baseline) return result;
    const double out_baseline = extract_output(*baseline, output_var);
    if (!std::isfinite(out_baseline)) {
        fail(baseline->status, "Sensitivity baseline output is not finite");
        return result;
    }
    result.output_value = out_baseline;
    result.status = baseline->status;

    // Preserve the existing forward-difference step and normalization. These
    // parameters define the measurement, independently of failure handling.
    constexpr double REL_DELTA = 1e-4;
    constexpr double ABS_DELTA = 1e-10;
    const auto perturb = [&](auto& device, auto setter, double original,
                             const std::string& parameter) {
        const std::string context = "Sensitivity perturbation " + device.name() +
                                    ":" + parameter;
        const double delta = std::max(std::abs(original) * REL_DELTA, ABS_DELTA);
        const double changed = original + delta;
        if (!std::isfinite(original) || !std::isfinite(changed) || changed == original) {
            fail(result.status, context + ": invalid parameter perturbation");
            return false;
        }
        ParameterRestorer restore{device, setter, original};
        (device.*setter)(changed);
        const auto dc = solve_point(context);
        if (!dc) return false;
        const double output = extract_output(*dc, output_var);
        const double sensitivity = (output - out_baseline) / delta;
        const double normalized = std::abs(out_baseline) > 1e-30
            ? sensitivity * original / out_baseline : 0.0;
        if (!std::isfinite(output) || !std::isfinite(sensitivity) ||
            !std::isfinite(normalized)) {
            fail(dc->status, context + ": nonfinite output or derivative");
            return false;
        }
        result.entries.push_back({to_lower(device.name()), parameter,
                                  sensitivity, normalized});
        return true;
    };

    for (auto& dev : ckt.devices())
        if (auto* r = dynamic_cast<Resistor*>(dev.get()))
            if (!perturb(*r, &Resistor::set_resistance, r->resistance(), "resistance"))
                return result;
    for (auto& dev : ckt.devices())
        if (auto* vs = dynamic_cast<VSource*>(dev.get()))
            if (!perturb(*vs, &VSource::set_dc_value, vs->dc_value(), "dc"))
                return result;
    for (auto& dev : ckt.devices())
        if (auto* is = dynamic_cast<ISource*>(dev.get()))
            if (!perturb(*is, &ISource::set_dc_value, is->dc_value(), "dc"))
                return result;

    result.status.iterations = total_iterations;
    result.status.elapsed_seconds = elapsed();
    return result;
}

} // namespace neospice
