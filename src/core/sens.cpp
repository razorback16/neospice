#include "core/sens.hpp"
#include "core/dc.hpp"
#include "devices/resistor.hpp"
#include "devices/vsource.hpp"
#include "devices/isource.hpp"
#include <algorithm>
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

SensResult solve_sens(Circuit& ckt, const std::string& output_var) {
    SensResult result;
    result.output_var = to_lower(output_var);

    // 1. Baseline DC operating point
    DCResult baseline = solve_dc(ckt);
    double out_baseline = extract_output(baseline, output_var);
    result.output_value = out_baseline;

    // Relative perturbation factor (ngspice uses 1e-4 in sensetup.c)
    constexpr double REL_DELTA = 1e-4;
    constexpr double ABS_DELTA = 1e-10;

    // 2. Perturb each Resistor
    for (auto& dev : ckt.devices()) {
        if (auto* r = dynamic_cast<Resistor*>(dev.get())) {
            double orig = r->resistance();
            double delta = std::abs(orig) * REL_DELTA;
            if (delta < ABS_DELTA) delta = ABS_DELTA;

            r->set_resistance(orig + delta);
            DCResult perturbed = solve_dc(ckt);
            double out_perturbed = extract_output(perturbed, output_var);

            double sens = (out_perturbed - out_baseline) / delta;
            double norm = (std::abs(out_baseline) > 1e-30)
                              ? sens * orig / out_baseline
                              : 0.0;
            result.entries.push_back(
                {to_lower(r->name()), "resistance", sens, norm});
            r->set_resistance(orig);  // restore
        }
    }

    // 3. Perturb each VSource DC value
    for (auto& dev : ckt.devices()) {
        if (auto* vs = dynamic_cast<VSource*>(dev.get())) {
            double orig = vs->dc_value();
            double delta = std::abs(orig) * REL_DELTA;
            if (delta < ABS_DELTA) delta = ABS_DELTA;

            vs->set_dc_value(orig + delta);
            DCResult perturbed = solve_dc(ckt);
            double out_perturbed = extract_output(perturbed, output_var);

            double sens = (out_perturbed - out_baseline) / delta;
            double norm = (std::abs(out_baseline) > 1e-30)
                              ? sens * orig / out_baseline
                              : 0.0;
            result.entries.push_back(
                {to_lower(vs->name()), "dc", sens, norm});
            vs->set_dc_value(orig);  // restore
        }
    }

    // 4. Perturb each ISource DC value
    for (auto& dev : ckt.devices()) {
        if (auto* is = dynamic_cast<ISource*>(dev.get())) {
            double orig = is->dc_value();
            double delta = std::abs(orig) * REL_DELTA;
            if (delta < ABS_DELTA) delta = ABS_DELTA;

            is->set_dc_value(orig + delta);
            DCResult perturbed = solve_dc(ckt);
            double out_perturbed = extract_output(perturbed, output_var);

            double sens = (out_perturbed - out_baseline) / delta;
            double norm = (std::abs(out_baseline) > 1e-30)
                              ? sens * orig / out_baseline
                              : 0.0;
            result.entries.push_back(
                {to_lower(is->name()), "dc", sens, norm});
            is->set_dc_value(orig);  // restore
        }
    }

    return result;
}

} // namespace neospice
