#include "api/neospice.hpp"
#include "core/fourier.hpp"
#include "output/output.hpp"
#include "parser/netlist_parser.hpp"
#include "devices/vsource.hpp"
#include "devices/isource.hpp"
#include <unordered_set>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace neospice {

namespace {
std::string lower(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return r;
}
} // anonymous namespace

// ---------------------------------------------------------------------------
// .save filtering helpers
// ---------------------------------------------------------------------------

template<typename Result>
static void apply_save_filter(Result& r, const std::vector<std::string>& sigs) {
    if (sigs.empty()) return;
    std::unordered_set<std::string> keep(sigs.begin(), sigs.end());
    auto erase_missing = [&keep](auto& map) {
        for (auto it = map.begin(); it != map.end(); ) {
            if (keep.count(it->first) == 0)
                it = map.erase(it);
            else
                ++it;
        }
    };
    if constexpr (requires { r.node_voltages; }) {
        erase_missing(r.node_voltages);
        erase_missing(r.branch_currents);
    } else {
        erase_missing(r.voltages);
        erase_missing(r.currents);
    }
}

// ---------------------------------------------------------------------------

Circuit Simulator::load(const std::string& filepath) {
    NetlistParser parser;
    auto ckt = parser.parse_file(filepath);
    ckt.set_source_path(filepath);
    // Read file contents for potential re-parsing (.step param)
    std::ifstream ifs(filepath);
    if (ifs) {
        std::ostringstream ss;
        ss << ifs.rdbuf();
        ckt.set_source_text(ss.str());
    }
    return ckt;
}

Circuit Simulator::parse(const std::string& netlist_text) {
    NetlistParser parser;
    auto ckt = parser.parse(netlist_text);
    ckt.set_source_text(netlist_text);
    return ckt;
}

DCResult Simulator::run_dc(Circuit& ckt) {
    ckt.finalize_if_needed();
    auto result = solve_dc(ckt);
    apply_save_filter(result, ckt.save_signals);
    return result;
}

DCSweepResult Simulator::run_dc_sweep(Circuit& ckt,
                                      const std::vector<DCSweepParam>& params) {
    ckt.finalize_if_needed();
    auto result = solve_dc_sweep(ckt, params);
    apply_save_filter(result, ckt.save_signals);
    return result;
}

TransientResult Simulator::run_transient(Circuit& ckt, double tstep, double tstop) {
    ckt.finalize_if_needed();
    auto result = solve_transient(ckt, tstep, tstop);
    apply_save_filter(result, ckt.save_signals);
    return result;
}

ACResult Simulator::run_ac(Circuit& ckt, ACMode mode,
                           int npoints, double fstart, double fstop) {
    ckt.finalize_if_needed();
    auto result = solve_ac(ckt, mode, npoints, fstart, fstop);
    apply_save_filter(result, ckt.save_signals);
    return result;
}

TransientResult Simulator::run_transient(Circuit& ckt, double tstep, double tstop,
                                         const TransientOptions& opts) {
    ckt.finalize_if_needed();
    auto result = solve_transient(ckt, tstep, tstop, opts);
    apply_save_filter(result, ckt.save_signals);
    return result;
}

ACResult Simulator::run_ac(Circuit& ckt, ACMode mode,
                           int npoints, double fstart, double fstop,
                           const ACOptions& opts) {
    ckt.finalize_if_needed();
    auto result = solve_ac(ckt, mode, npoints, fstart, fstop, opts);
    apply_save_filter(result, ckt.save_signals);
    return result;
}

NoiseResult Simulator::run_noise(Circuit& ckt, const std::string& output_node,
                                 const std::string& input_src,
                                 ACMode mode,
                                 int npoints, double fstart, double fstop) {
    ckt.finalize_if_needed();
    return solve_noise(ckt, output_node, input_src, mode, npoints, fstart, fstop);
}

TFResult Simulator::run_tf(Circuit& ckt, const std::string& output_var,
                           const std::string& input_src) {
    ckt.finalize_if_needed();
    return solve_tf(ckt, output_var, input_src);
}

SensResult Simulator::run_sens(Circuit& ckt, const std::string& output_var) {
    ckt.finalize_if_needed();
    return solve_sens(ckt, output_var);
}

SimulationResult Simulator::run(Circuit& ckt) {
    ckt.finalize_if_needed();
    if (!ckt.step_commands.empty()) {
        return run_step_sweep(ckt);
    }
    SimulationResult result;
    for (auto& cmd : ckt.analyses) {
        std::visit(overloaded{
            [&](const OpCmd&) {
                auto dc = solve_dc(ckt);
                apply_save_filter(dc, ckt.save_signals);
                result.analysis = std::move(dc);
            },
            [&](const TranCmd& c) {
                TransientOptions topts;
                topts.uic = c.uic;
                topts.tstart = c.tstart;
                auto tran = solve_transient(ckt, c.tstep, c.tstop, topts);
                apply_save_filter(tran, ckt.save_signals);
                result.analysis = std::move(tran);
            },
            [&](const ACCmd& c) {
                auto ac = solve_ac(ckt, c.mode, c.npoints, c.fstart, c.fstop);
                apply_save_filter(ac, ckt.save_signals);
                result.analysis = std::move(ac);
            },
            [&](const DCSweepCmd& c) {
                auto sw = solve_dc_sweep(ckt, c.params);
                apply_save_filter(sw, ckt.save_signals);
                result.analysis = std::move(sw);
            },
            [&](const NoiseCmd& c) {
                auto nr = solve_noise(ckt, c.output, c.input_src,
                                      c.mode, c.npoints, c.fstart, c.fstop);
                result.analysis = std::move(nr);
            },
            [&](const TFCmd& c) {
                result.analysis = solve_tf(ckt, c.output, c.input_src);
            },
            [&](const SensCmd& c) {
                result.analysis = solve_sens(ckt, c.output);
            },
            [&](const PZCmd& c) {
                result.analysis = solve_pz(ckt, c.in_pos, c.in_neg,
                                           c.out_pos, c.out_neg,
                                           c.transfer, c.type);
            }
        }, cmd);
    }

    // Execute .meas commands after all analyses complete
    if (!ckt.measures.empty()) {
        const auto* tran_ptr = std::get_if<TransientResult>(&result.analysis);
        const auto* ac_ptr = std::get_if<ACResult>(&result.analysis);
        const auto* dc_sweep_ptr = std::get_if<DCSweepResult>(&result.analysis);
        result.measures = execute_measures(ckt.measures, tran_ptr, ac_ptr, dc_sweep_ptr);
    }

    // Execute .print / .plot commands
    if (!ckt.prints.empty()) {
        const auto* tran_ptr = std::get_if<TransientResult>(&result.analysis);
        const auto* ac_ptr = std::get_if<ACResult>(&result.analysis);
        const auto* dc_sweep_ptr = std::get_if<DCSweepResult>(&result.analysis);
        const auto* noise_ptr = std::get_if<NoiseResult>(&result.analysis);
        const auto* dc_op_ptr = std::get_if<DCResult>(&result.analysis);
        for (const auto& pcmd : ckt.prints) {
            std::string formatted;
            if (pcmd.is_plot) {
                formatted = format_plot(pcmd, tran_ptr, ac_ptr, dc_sweep_ptr, noise_ptr, dc_op_ptr);
            } else {
                formatted = format_print(pcmd, tran_ptr, ac_ptr, dc_sweep_ptr, noise_ptr, dc_op_ptr);
            }
            result.print_output.push_back(std::move(formatted));
        }
    }

    // Execute .four / .fourier commands after transient completes
    if (auto* tran_ptr = std::get_if<TransientResult>(&result.analysis);
        !ckt.fourier_commands.empty() && tran_ptr) {
        for (const auto& fcmd : ckt.fourier_commands) {
            auto four_results = compute_fourier(fcmd.fundamental_freq,
                                                fcmd.signals,
                                                *tran_ptr);
            std::string formatted = format_fourier(four_results);
            result.print_output.push_back(std::move(formatted));
        }
    }

    return result;
}

SimulationResult Simulator::run_step_sweep(Circuit& ckt) {
    SimulationResult outer;
    const auto& sc = ckt.step_commands[0];

    StepResult step_result;
    step_result.step_variable = sc.name.empty() ? "temp" : sc.name;

    double val = sc.start;
    int direction = (sc.step > 0) ? 1 : -1;
    while (direction > 0 ? val <= sc.stop + std::abs(sc.step) * 0.001
                         : val >= sc.stop - std::abs(sc.step) * 0.001) {
        step_result.step_values.push_back(val);

        switch (sc.kind) {
        case StepCommand::SOURCE: {
            std::string target = lower(sc.name);
            for (auto& dev : ckt.devices()) {
                if (lower(dev->name()) == target) {
                    if (auto* vs = dynamic_cast<VSource*>(dev.get()))
                        vs->set_dc_value(val);
                    else if (auto* is = dynamic_cast<ISource*>(dev.get()))
                        is->set_dc_value(val);
                }
            }
            break;
        }
        case StepCommand::TEMP:
            ckt.options.temp = val + 273.15;
            break;
        case StepCommand::PARAM: {
            std::string source = ckt.source_text();
            if (source.empty() && !ckt.source_path().empty()) {
                std::ifstream f(ckt.source_path());
                if (f) {
                    std::ostringstream ss;
                    ss << f.rdbuf();
                    source = ss.str();
                }
            }
            if (!source.empty()) {
                // Replace or append .param override.
                // We search for existing .param <name> lines and replace
                // them. If none found, inject after the title line.
                std::string val_str = std::to_string(val);
                std::string name_lower = lower(sc.name);
                std::string modified;
                bool replaced = false;
                std::istringstream iss(source);
                std::string line;
                while (std::getline(iss, line)) {
                    // Check if this line is a .param defining our variable
                    std::string line_lower = lower(line);
                    // Match lines like ".param RVAL=..." or ".param RVAL =..."
                    auto pos = line_lower.find(".param");
                    if (pos != std::string::npos) {
                        // Find the param name after .param
                        auto npos = pos + 6;
                        while (npos < line_lower.size() && std::isspace(static_cast<unsigned char>(line_lower[npos])))
                            ++npos;
                        auto nstart = npos;
                        while (npos < line_lower.size() &&
                               (std::isalnum(static_cast<unsigned char>(line_lower[npos])) || line_lower[npos] == '_'))
                            ++npos;
                        std::string pname = line_lower.substr(nstart, npos - nstart);
                        if (pname == name_lower) {
                            modified += ".param " + sc.name + "=" + val_str + "\n";
                            replaced = true;
                            continue;
                        }
                    }
                    modified += line + "\n";
                }
                if (!replaced) {
                    // Inject after title line
                    auto nl = modified.find('\n');
                    std::string override_line = ".param " + sc.name + "=" + val_str + "\n";
                    if (nl != std::string::npos) {
                        modified.insert(nl + 1, override_line);
                    } else {
                        modified += "\n" + override_line;
                    }
                }
                Circuit new_ckt = parse(modified);
                new_ckt.step_commands.clear();
                auto inner = run(new_ckt);
                step_result.results.push_back(std::move(inner));
            }
            val += sc.step;
            continue;  // skip the shared run(ckt) below
        }
        }

        ckt.reset_state();

        auto saved_steps = std::move(ckt.step_commands);
        ckt.step_commands.clear();
        auto result = run(ckt);
        ckt.step_commands = std::move(saved_steps);

        step_result.results.push_back(std::move(result));
        val += sc.step;
    }

    outer.step = std::make_unique<StepResult>(std::move(step_result));
    return outer;
}

void Simulator::register_device(std::string_view prefix, ModelMatcher matcher,
                                DeviceFactory factory) {
    registry_.push_back({std::string(prefix), std::move(matcher), std::move(factory)});
}

} // namespace neospice
