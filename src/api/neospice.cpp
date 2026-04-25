#include "api/neospice.hpp"
#include "core/fourier.hpp"
#include "output/output.hpp"
#include "parser/netlist_parser.hpp"
#include "devices/vsource.hpp"
#include "devices/isource.hpp"
#include <unordered_set>
#include <algorithm>
#include <cctype>

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

Simulator::Simulator(Options opts) : opts_(opts) {}

Circuit Simulator::load(const std::string& filepath) {
    NetlistParser parser;
    return parser.parse_file(filepath);
}

Circuit Simulator::parse(const std::string& netlist_text) {
    NetlistParser parser;
    return parser.parse(netlist_text);
}

DCResult Simulator::run_dc(Circuit& ckt) {
    auto result = solve_dc(ckt);
    apply_save_filter(result, ckt.save_signals);
    return result;
}

DCSweepResult Simulator::run_dc_sweep(Circuit& ckt,
                                      const std::vector<DCSweepParam>& params) {
    auto result = solve_dc_sweep(ckt, params);
    apply_save_filter(result, ckt.save_signals);
    return result;
}

TransientResult Simulator::run_transient(Circuit& ckt, double tstep, double tstop) {
    auto result = solve_transient(ckt, tstep, tstop);
    apply_save_filter(result, ckt.save_signals);
    return result;
}

ACResult Simulator::run_ac(Circuit& ckt, ACMode mode,
                           int npoints, double fstart, double fstop) {
    auto result = solve_ac(ckt, mode, npoints, fstart, fstop);
    apply_save_filter(result, ckt.save_signals);
    return result;
}

TransientResult Simulator::run_transient(Circuit& ckt, double tstep, double tstop,
                                         const TransientOptions& opts) {
    auto result = solve_transient(ckt, tstep, tstop, opts);
    apply_save_filter(result, ckt.save_signals);
    return result;
}

ACResult Simulator::run_ac(Circuit& ckt, ACMode mode,
                           int npoints, double fstart, double fstop,
                           const ACOptions& opts) {
    auto result = solve_ac(ckt, mode, npoints, fstart, fstop, opts);
    apply_save_filter(result, ckt.save_signals);
    return result;
}

NoiseResult Simulator::run_noise(Circuit& ckt, const std::string& output_node,
                                 const std::string& input_src,
                                 ACMode mode,
                                 int npoints, double fstart, double fstop) {
    return solve_noise(ckt, output_node, input_src, mode, npoints, fstart, fstop);
}

TFResult Simulator::run_tf(Circuit& ckt, const std::string& output_var,
                           const std::string& input_src) {
    return solve_tf(ckt, output_var, input_src);
}

SensResult Simulator::run_sens(Circuit& ckt, const std::string& output_var) {
    return solve_sens(ckt, output_var);
}

SimulationResult Simulator::run(Circuit& ckt) {
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
                auto tran = solve_transient(ckt, c.tstep, c.tstop, c.uic);
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
        for (const auto& pcmd : ckt.prints) {
            std::string formatted;
            if (pcmd.is_plot) {
                formatted = format_plot(pcmd, tran_ptr, ac_ptr, dc_sweep_ptr, noise_ptr);
            } else {
                formatted = format_print(pcmd, tran_ptr, ac_ptr, dc_sweep_ptr, noise_ptr);
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
        case StepCommand::PARAM:
            break;
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

} // namespace neospice
