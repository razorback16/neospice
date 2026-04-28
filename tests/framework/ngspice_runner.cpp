#include "framework/ngspice_runner.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>

namespace neospice {

NgspiceRunner::NgspiceRunner() = default;

std::string NgspiceRunner::find_plot(const std::string& prefix) {
    char** plots = ng_.all_plots();
    if (!plots) throw std::runtime_error("ngSpice_AllPlots returned null");

    std::string best;
    for (int i = 0; plots[i]; ++i) {
        std::string p = plots[i];
        std::string lower = p;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if (lower.find(prefix) == 0) {
            if (best.empty() || p > best)
                best = p;
        }
    }
    if (best.empty())
        throw std::runtime_error("No plot found with prefix '" + prefix + "'");
    return best;
}

std::vector<std::string> NgspiceRunner::vec_names(const std::string& plot) {
    std::vector<std::string> names;
    char** vecs = ng_.all_vecs(plot);
    if (!vecs) return names;
    for (int i = 0; vecs[i]; ++i)
        names.emplace_back(vecs[i]);
    return names;
}

std::string NgspiceRunner::normalize_name(const std::string& name) {
    std::string n = name;
    std::transform(n.begin(), n.end(), n.begin(), ::tolower);

    // The shared library API returns bare internal names while the .raw file
    // format wraps them: node "mid" -> "v(mid)", "v1#branch" -> "i(v1)".
    // Convert to .raw-compatible names so the comparator sees matching keys.

    // Branch current: "v1#branch" -> "i(v1)"
    auto hash_pos = n.find("#branch");
    if (hash_pos != std::string::npos) {
        std::string dev = n.substr(0, hash_pos);
        n = "i(" + dev + ")";
    }

    // Strip XSPICE POLY wrapper: i(a$poly$e.x1.eos) -> i(e.x1.eos)
    if (n.size() > 2 && n[0] == 'i' && n[1] == '(') {
        const std::string poly_prefix = "a$poly$";
        size_t pos = n.find(poly_prefix, 2);
        if (pos != std::string::npos) {
            n = "i(" + n.substr(pos + poly_prefix.size());
        }
    }
    // Scale/sweep vectors and special names — keep bare
    else if (n == "time" || n == "frequency" || n == "v-sweep" ||
             n == "onoise_spectrum" || n == "inoise_spectrum") {
        // no wrapping
    }
    // Bare node name — wrap as voltage: "mid" -> "v(mid)"
    else {
        n = "v(" + n + ")";
    }
    return n;
}

DCResult NgspiceRunner::run_dc(const std::string& cir_path) {
    ng_.reset();
    ng_.load_circuit(cir_path);
    ng_.command("run");

    std::string plot = find_plot("op");
    auto names = vec_names(plot);

    DCResult result;
    for (const auto& raw_name : names) {
        std::string qname = plot + "." + raw_name;
        pvector_info vi = ng_.get_vec_info(qname);
        if (!vi || vi->v_length == 0) continue;

        std::string name = normalize_name(raw_name);
        int last = vi->v_length - 1;
        double value = vi->v_compdata
            ? vi->v_compdata[last].cx_real
            : vi->v_realdata[last];

        if (name.find("v(") == 0 || name == "v-sweep") {
            result.node_voltages[name] = value;
        } else if (name.find("i(") == 0 || name.find("#branch") != std::string::npos) {
            result.branch_currents[name] = value;
        } else {
            result.node_voltages[name] = value;
        }
    }
    return result;
}

DCSweepResult NgspiceRunner::run_dc_sweep(const std::string& cir_path) {
    ng_.reset();
    ng_.load_circuit(cir_path);
    ng_.command("run");

    std::string plot = find_plot("dc");
    auto names = vec_names(plot);

    DCSweepResult result;
    for (const auto& raw_name : names) {
        std::string qname = plot + "." + raw_name;
        pvector_info vi = ng_.get_vec_info(qname);
        if (!vi || vi->v_length == 0) continue;

        std::string name = normalize_name(raw_name);
        std::vector<double> data(vi->v_length);
        for (int i = 0; i < vi->v_length; ++i)
            data[i] = vi->v_realdata[i];

        if (name.find("v-sweep") != std::string::npos) {
            result.sweep_var = name;
            result.sweep_values = std::move(data);
        } else if (name.find("v(") == 0) {
            result.voltages[name] = std::move(data);
        } else if (name.find("i(") == 0 || name.find("#branch") != std::string::npos) {
            result.currents[name] = std::move(data);
        } else {
            result.voltages[name] = std::move(data);
        }
    }
    return result;
}

TransientResult NgspiceRunner::run_transient(const std::string& cir_path) {
    ng_.reset();
    ng_.load_circuit(cir_path);
    ng_.command("run");

    std::string plot = find_plot("tran");
    auto names = vec_names(plot);

    TransientResult result;
    for (const auto& raw_name : names) {
        std::string qname = plot + "." + raw_name;
        pvector_info vi = ng_.get_vec_info(qname);
        if (!vi || vi->v_length == 0) continue;

        std::string name = normalize_name(raw_name);
        std::vector<double> data(vi->v_length);
        for (int i = 0; i < vi->v_length; ++i)
            data[i] = vi->v_realdata[i];

        if (name == "time") {
            result.time = std::move(data);
        } else if (name.find("v(") == 0) {
            result.voltages[name] = std::move(data);
        } else if (name.find("i(") == 0 || name.find("#branch") != std::string::npos) {
            result.currents[name] = std::move(data);
        } else {
            result.voltages[name] = std::move(data);
        }
    }
    return result;
}

ACResult NgspiceRunner::run_ac(const std::string& cir_path) {
    ng_.reset();
    ng_.load_circuit(cir_path);
    ng_.command("run");

    std::string plot = find_plot("ac");
    auto names = vec_names(plot);

    ACResult result;
    for (const auto& raw_name : names) {
        std::string qname = plot + "." + raw_name;
        pvector_info vi = ng_.get_vec_info(qname);
        if (!vi || vi->v_length == 0) continue;

        std::string name = normalize_name(raw_name);

        if (name == "frequency") {
            result.frequency.resize(vi->v_length);
            for (int i = 0; i < vi->v_length; ++i)
                result.frequency[i] = vi->v_compdata
                    ? vi->v_compdata[i].cx_real
                    : vi->v_realdata[i];
        } else {
            std::vector<std::complex<double>> cdata(vi->v_length);
            if (vi->v_compdata) {
                for (int i = 0; i < vi->v_length; ++i)
                    cdata[i] = {vi->v_compdata[i].cx_real, vi->v_compdata[i].cx_imag};
            } else {
                for (int i = 0; i < vi->v_length; ++i)
                    cdata[i] = {vi->v_realdata[i], 0.0};
            }
            if (name.find("v(") == 0) {
                result.voltages[name] = std::move(cdata);
            } else if (name.find("i(") == 0 || name.find("#branch") != std::string::npos) {
                result.currents[name] = std::move(cdata);
            } else {
                result.voltages[name] = std::move(cdata);
            }
        }
    }
    return result;
}

NgspiceNoiseResult NgspiceRunner::run_noise(const std::string& cir_path) {
    ng_.reset();
    ng_.load_circuit(cir_path);
    ng_.command("run");

    // Noise produces two plots: spectral density (many points) and integrated
    // noise (single point). Find the spectral density plot.
    char** plots = ng_.all_plots();
    if (!plots) throw std::runtime_error("ngSpice_AllPlots returned null");

    std::string spectral_plot;
    for (int i = 0; plots[i]; ++i) {
        std::string p = plots[i];
        std::string lower = p;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if (lower.find("noise") != 0) continue;

        auto vnames = vec_names(p);
        for (const auto& vn : vnames) {
            std::string qname = p + "." + vn;
            pvector_info vi = ng_.get_vec_info(qname);
            if (vi && vi->v_length > 1) {
                spectral_plot = p;
                break;
            }
        }
        if (!spectral_plot.empty()) break;
    }
    if (spectral_plot.empty())
        throw std::runtime_error("No noise spectral density plot found");

    auto names = vec_names(spectral_plot);
    NgspiceNoiseResult result;

    for (const auto& raw_name : names) {
        std::string qname = spectral_plot + "." + raw_name;
        pvector_info vi = ng_.get_vec_info(qname);
        if (!vi || vi->v_length == 0) continue;

        std::string name = normalize_name(raw_name);
        std::vector<double> data(vi->v_length);
        for (int i = 0; i < vi->v_length; ++i)
            data[i] = vi->v_realdata[i];

        if (name == "frequency")
            result.frequency = std::move(data);
        else if (name == "onoise_spectrum")
            result.onoise_spectrum = std::move(data);
        else if (name == "inoise_spectrum")
            result.inoise_spectrum = std::move(data);
    }

    if (result.frequency.empty() || result.onoise_spectrum.empty() || result.inoise_spectrum.empty())
        throw std::runtime_error("Noise spectral plot missing expected variables");

    return result;
}

} // namespace neospice
