#include "output/output.hpp"
#include <algorithm>
#include <cmath>
#include <complex>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace neospice {

// ---------------------------------------------------------------------------
// Helper: parse a signal specifier into (function, base_name)
// For AC signals:
//   VM(x) or V(x)  -> magnitude
//   VP(x)          -> phase in degrees
//   VDB(x)         -> 20*log10(|v|)
//   VR(x)          -> real part
//   VI(x)          -> imaginary part
//   I(x)           -> magnitude of current
// For transient/dc signals:
//   V(x) / I(x)
// ---------------------------------------------------------------------------

enum class ACSigFunc { MAG, PHASE, DB, REAL, IMAG };

struct SigSpec {
    bool is_current = false;      // I(...) vs V(...)
    ACSigFunc ac_func = ACSigFunc::MAG;
    std::string base_name;        // node/source name inside parens
    std::string original;         // original token (for header)
};

static SigSpec parse_signal(const std::string& sig) {
    SigSpec s;
    s.original = sig;
    // sig is already lower-cased by the parser
    // Determine function prefix: vm, vp, vdb, vr, vi, v, i
    auto open = sig.find('(');
    auto close = sig.rfind(')');
    if (open == std::string::npos || close == std::string::npos || close <= open) {
        // Not a standard signal — return as-is with base_name = sig
        s.base_name = sig;
        return s;
    }
    std::string func = sig.substr(0, open);
    s.base_name = sig.substr(open + 1, close - open - 1);

    if (func == "i") {
        s.is_current = true;
        s.ac_func = ACSigFunc::MAG;
    } else if (func == "vm" || func == "v") {
        s.is_current = false;
        s.ac_func = ACSigFunc::MAG;
    } else if (func == "vp") {
        s.is_current = false;
        s.ac_func = ACSigFunc::PHASE;
    } else if (func == "vdb") {
        s.is_current = false;
        s.ac_func = ACSigFunc::DB;
    } else if (func == "vr") {
        s.is_current = false;
        s.ac_func = ACSigFunc::REAL;
    } else if (func == "vi") {
        s.is_current = false;
        s.ac_func = ACSigFunc::IMAG;
    }
    return s;
}

// ---------------------------------------------------------------------------
// Lookup helpers: get a data vector for the given signal and result type
//
// The result maps use the FULL signal string as the key (e.g. "v(out)",
// "i(v1)"), which is exactly what the parser stores in SigSpec::original.
// AC variants like "vm(out)", "vp(out)", "vdb(out)" are NOT stored in the
// ACResult map; the map contains the base V/I key "v(out)" and we apply
// the function (mag/phase/dB) ourselves.  So for AC we look up using the
// canonical "v(<name>)" or "i(<name>)" key derived from base_name.
// ---------------------------------------------------------------------------

static std::vector<double> get_tran_values(const SigSpec& s, const TransientResult* tran) {
    if (!tran) return {};
    // Keys in the transient result are "v(<node>)" and "i(<device>)"
    // The original signal already matches (both lower-cased by parser)
    if (s.is_current) {
        auto it = tran->currents.find(s.original);
        if (it != tran->currents.end()) return it->second;
    } else {
        auto it = tran->voltages.find(s.original);
        if (it != tran->voltages.end()) return it->second;
    }
    return {};
}

static std::vector<double> get_dc_values(const SigSpec& s, const DCSweepResult* dc) {
    if (!dc) return {};
    if (s.is_current) {
        auto it = dc->currents.find(s.original);
        if (it != dc->currents.end()) return it->second;
    } else {
        auto it = dc->voltages.find(s.original);
        if (it != dc->voltages.end()) return it->second;
    }
    return {};
}

static std::vector<double> get_ac_values(const SigSpec& s, const ACResult* ac) {
    if (!ac) return {};
    // AC result keys are "v(<node>)" and "i(<device>)".
    // Build the canonical lookup key from base_name:
    std::string lookup_key = (s.is_current ? "i(" : "v(") + s.base_name + ")";
    std::vector<std::complex<double>> raw;
    if (s.is_current) {
        auto it = ac->currents.find(lookup_key);
        if (it != ac->currents.end()) raw = it->second;
    } else {
        auto it = ac->voltages.find(lookup_key);
        if (it != ac->voltages.end()) raw = it->second;
    }
    if (raw.empty()) return {};

    std::vector<double> result;
    result.reserve(raw.size());
    for (const auto& c : raw) {
        switch (s.ac_func) {
        case ACSigFunc::MAG:
            result.push_back(std::abs(c));
            break;
        case ACSigFunc::PHASE:
            result.push_back(std::arg(c) * 180.0 / M_PI);
            break;
        case ACSigFunc::DB:
            result.push_back(20.0 * std::log10(std::max(std::abs(c), 1e-300)));
            break;
        case ACSigFunc::REAL:
            result.push_back(c.real());
            break;
        case ACSigFunc::IMAG:
            result.push_back(c.imag());
            break;
        }
    }
    return result;
}

static std::vector<double> get_noise_values(const SigSpec& s, const NoiseResult* noise) {
    if (!noise) return {};
    // Noise signals: "inoise" -> input_noise_density, "onoise" -> output_noise_density
    // Support both "inoise" (bare) and "v(inoise)" forms
    const std::string& key = s.base_name;
    if (key == "inoise") return noise->input_noise_density;
    if (key == "onoise") return noise->output_noise_density;
    // Also support original signal form (in case user writes "inoise" directly)
    if (s.original == "inoise") return noise->input_noise_density;
    if (s.original == "onoise") return noise->output_noise_density;
    auto it = noise->device_noise.find(key);
    if (it != noise->device_noise.end()) return it->second;
    return {};
}

// ---------------------------------------------------------------------------
// format_print — tabular ASCII output
// ---------------------------------------------------------------------------

std::string format_print(const PrintCommand& cmd,
                          const TransientResult* tran,
                          const ACResult* ac,
                          const DCSweepResult* dc_sweep,
                          const NoiseResult* noise) {
    std::ostringstream out;
    const std::string& atype = cmd.analysis_type;

    // Determine x-axis values and name
    std::vector<double> x_vals;
    std::string x_name;
    if (atype == "tran" && tran) {
        x_vals = tran->time;
        x_name = "time";
    } else if (atype == "ac" && ac) {
        x_vals = ac->frequency;
        x_name = "frequency";
    } else if ((atype == "dc" || atype == "dc_sweep") && dc_sweep) {
        x_vals = dc_sweep->sweep_values;
        x_name = dc_sweep->sweep_var;
        if (x_name.empty()) x_name = "sweep";
    } else if (atype == "noise" && noise) {
        x_vals = noise->frequency;
        x_name = "frequency";
    } else {
        out << "No data available for analysis type: " << atype << "\n";
        return out.str();
    }

    // Parse signal specs and gather data columns
    std::vector<SigSpec> specs;
    std::vector<std::vector<double>> data_cols;
    for (const auto& sig : cmd.signals) {
        SigSpec s = parse_signal(sig);
        std::vector<double> vals;
        if (atype == "tran") {
            vals = get_tran_values(s, tran);
        } else if (atype == "ac") {
            vals = get_ac_values(s, ac);
        } else if (atype == "dc" || atype == "dc_sweep") {
            vals = get_dc_values(s, dc_sweep);
        } else if (atype == "noise") {
            vals = get_noise_values(s, noise);
        }
        specs.push_back(s);
        data_cols.push_back(std::move(vals));
    }

    const int COL_W = 16;

    // Header row
    out << std::left << std::setw(8) << "Index"
        << std::setw(COL_W) << x_name;
    for (const auto& s : specs) {
        out << std::setw(COL_W) << s.original;
    }
    out << "\n";

    // Data rows
    size_t npoints = x_vals.size();
    for (size_t i = 0; i < npoints; ++i) {
        out << std::left << std::setw(8) << i;
        out << std::setw(COL_W) << std::scientific << std::setprecision(6) << x_vals[i];
        for (const auto& col : data_cols) {
            if (i < col.size()) {
                out << std::setw(COL_W) << std::scientific << std::setprecision(6) << col[i];
            } else {
                out << std::setw(COL_W) << "N/A";
            }
        }
        out << "\n";
    }

    return out.str();
}

// ---------------------------------------------------------------------------
// format_plot — ASCII waveform plot
// ---------------------------------------------------------------------------

static std::string format_single_plot(const std::string& sig_name,
                                       const std::vector<double>& x_vals,
                                       const std::string& x_name,
                                       const std::vector<double>& y_vals,
                                       char marker = '*') {
    const int PLOT_WIDTH = 50;
    const int X_COL_W   = 12;

    std::ostringstream out;
    size_t npoints = x_vals.size();
    if (npoints == 0 || y_vals.empty()) {
        out << "No data for signal: " << sig_name << "\n";
        return out.str();
    }

    // Compute min/max of y
    double ymin = *std::min_element(y_vals.begin(), y_vals.end());
    double ymax = *std::max_element(y_vals.begin(), y_vals.end());
    if (ymax == ymin) {
        ymax = ymin + 1.0;
    }

    // Header: signal name centred
    {
        int pad = (X_COL_W + PLOT_WIDTH / 2) - (int)sig_name.size() / 2;
        out << std::string(std::max(0, pad), ' ') << sig_name << "\n";
    }

    // Scale line: x_name | ymin ... ymid ... ymax
    {
        double ymid = (ymin + ymax) / 2.0;
        std::ostringstream scale_ss;
        scale_ss << std::scientific << std::setprecision(1)
                 << ymin << "   " << ymid << "   " << ymax;
        std::string scale_str = scale_ss.str();
        out << std::left << std::setw(X_COL_W) << x_name << " " << scale_str << "\n";
    }

    // Separator line
    out << std::string(X_COL_W, '-') << " "
        << std::string(PLOT_WIDTH, '-') << "\n";

    // Data rows
    for (size_t i = 0; i < npoints; ++i) {
        double y = (i < y_vals.size()) ? y_vals[i] : ymin;
        // Map y to plot column [0, PLOT_WIDTH-1]
        int col = static_cast<int>((y - ymin) / (ymax - ymin) * (PLOT_WIDTH - 1) + 0.5);
        col = std::max(0, std::min(PLOT_WIDTH - 1, col));

        // X axis label
        std::ostringstream xss;
        xss << std::scientific << std::setprecision(3) << x_vals[i];
        out << std::left << std::setw(X_COL_W) << xss.str() << " ";

        // Plot line: place marker at computed column
        for (int c = 0; c < PLOT_WIDTH; ++c) {
            if (c == col) out << marker;
            else          out << ' ';
        }
        out << "\n";
    }
    out << "\n";
    return out.str();
}

std::string format_plot(const PrintCommand& cmd,
                         const TransientResult* tran,
                         const ACResult* ac,
                         const DCSweepResult* dc_sweep,
                         const NoiseResult* noise) {
    const std::string& atype = cmd.analysis_type;

    // Determine x-axis
    std::vector<double> x_vals;
    std::string x_name;
    if (atype == "tran" && tran) {
        x_vals = tran->time;
        x_name = "time";
    } else if (atype == "ac" && ac) {
        x_vals = ac->frequency;
        x_name = "frequency";
    } else if ((atype == "dc" || atype == "dc_sweep") && dc_sweep) {
        x_vals = dc_sweep->sweep_values;
        x_name = dc_sweep->sweep_var;
        if (x_name.empty()) x_name = "sweep";
    } else if (atype == "noise" && noise) {
        x_vals = noise->frequency;
        x_name = "frequency";
    } else {
        return "No data available for analysis type: " + atype + "\n";
    }

    const char markers[] = { '*', '+', 'o', 'x', '#', '@' };
    const int N_MARKERS = (int)(sizeof(markers) / sizeof(markers[0]));

    std::string result;
    for (size_t si = 0; si < cmd.signals.size(); ++si) {
        const auto& sig = cmd.signals[si];
        SigSpec s = parse_signal(sig);
        std::vector<double> y_vals;
        if (atype == "tran") {
            y_vals = get_tran_values(s, tran);
        } else if (atype == "ac") {
            y_vals = get_ac_values(s, ac);
        } else if (atype == "dc" || atype == "dc_sweep") {
            y_vals = get_dc_values(s, dc_sweep);
        } else if (atype == "noise") {
            y_vals = get_noise_values(s, noise);
        }
        char mk = markers[si % N_MARKERS];
        result += format_single_plot(sig, x_vals, x_name, y_vals, mk);
    }
    return result;
}

// ---------------------------------------------------------------------------
// format_fourier — ngspice-style Fourier output
// ---------------------------------------------------------------------------

std::string format_fourier(const std::vector<FourierResult>& results) {
    std::ostringstream out;

    for (const auto& fr : results) {
        out << "\nFourier analysis for " << fr.signal_name << ":\n";
        out << "  No. Harmonics: " << fr.components.size()
            << ", THD = " << std::fixed << std::setprecision(4) << fr.thd << " %\n\n";

        // Column headers
        out << std::setw(8)  << "Harmonic"
            << std::setw(13) << "Frequency"
            << std::setw(13) << "Fourier"
            << std::setw(13) << "Normalized"
            << std::setw(13) << "Phase"
            << std::setw(13) << "Normalized"
            << "\n";
        out << std::setw(8)  << "Number"
            << std::setw(13) << "[Hz]"
            << std::setw(13) << "Component"
            << std::setw(13) << "Component"
            << std::setw(13) << "[Degrees]"
            << std::setw(13) << "Phase [Deg]"
            << "\n";

        // Separator
        const std::string sep(8 + 5 * 13, '-');
        out << sep << "\n";

        // Data rows
        for (const auto& fc : fr.components) {
            out << std::setw(8) << fc.harmonic;
            out << std::scientific << std::setprecision(3);
            out << std::setw(13) << fc.frequency;
            out << std::setw(13) << fc.magnitude;
            out << std::setw(13) << fc.normalized_mag;
            out << std::setw(13) << fc.phase_deg;
            out << std::setw(13) << fc.normalized_phase_deg;
            out << "\n";
        }
        out << "\n";
    }

    return out.str();
}

} // namespace neospice
