#include "api/neospice.hpp"
#include "parser/netlist_parser.hpp"
#include <unordered_set>

namespace neospice {

// ---------------------------------------------------------------------------
// .save filtering helpers
// ---------------------------------------------------------------------------

static void apply_save_filter(DCResult& r, const std::vector<std::string>& sigs) {
    if (sigs.empty()) return;
    std::unordered_set<std::string> keep(sigs.begin(), sigs.end());
    for (auto it = r.node_voltages.begin(); it != r.node_voltages.end(); ) {
        if (keep.count(it->first) == 0)
            it = r.node_voltages.erase(it);
        else
            ++it;
    }
    for (auto it = r.branch_currents.begin(); it != r.branch_currents.end(); ) {
        if (keep.count(it->first) == 0)
            it = r.branch_currents.erase(it);
        else
            ++it;
    }
}

static void apply_save_filter(DCSweepResult& r, const std::vector<std::string>& sigs) {
    if (sigs.empty()) return;
    std::unordered_set<std::string> keep(sigs.begin(), sigs.end());
    for (auto it = r.voltages.begin(); it != r.voltages.end(); ) {
        if (keep.count(it->first) == 0)
            it = r.voltages.erase(it);
        else
            ++it;
    }
    for (auto it = r.currents.begin(); it != r.currents.end(); ) {
        if (keep.count(it->first) == 0)
            it = r.currents.erase(it);
        else
            ++it;
    }
}

static void apply_save_filter(TransientResult& r, const std::vector<std::string>& sigs) {
    if (sigs.empty()) return;
    std::unordered_set<std::string> keep(sigs.begin(), sigs.end());
    for (auto it = r.voltages.begin(); it != r.voltages.end(); ) {
        if (keep.count(it->first) == 0)
            it = r.voltages.erase(it);
        else
            ++it;
    }
    for (auto it = r.currents.begin(); it != r.currents.end(); ) {
        if (keep.count(it->first) == 0)
            it = r.currents.erase(it);
        else
            ++it;
    }
}

static void apply_save_filter(ACResult& r, const std::vector<std::string>& sigs) {
    if (sigs.empty()) return;
    std::unordered_set<std::string> keep(sigs.begin(), sigs.end());
    for (auto it = r.voltages.begin(); it != r.voltages.end(); ) {
        if (keep.count(it->first) == 0)
            it = r.voltages.erase(it);
        else
            ++it;
    }
    for (auto it = r.currents.begin(); it != r.currents.end(); ) {
        if (keep.count(it->first) == 0)
            it = r.currents.erase(it);
        else
            ++it;
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

ACResult Simulator::run_ac(Circuit& ckt, AnalysisCommand::ACMode mode,
                           int npoints, double fstart, double fstop) {
    auto result = solve_ac(ckt, mode, npoints, fstart, fstop);
    apply_save_filter(result, ckt.save_signals);
    return result;
}

SimulationResult Simulator::run(Circuit& ckt) {
    SimulationResult result;
    for (auto& cmd : ckt.analyses) {
        switch (cmd.type) {
        case AnalysisCommand::OP: {
            auto dc = solve_dc(ckt);
            apply_save_filter(dc, ckt.save_signals);
            result.dc = std::move(dc);
            break;
        }
        case AnalysisCommand::TRAN: {
            auto tran = solve_transient(ckt, cmd.tran_tstep, cmd.tran_tstop);
            apply_save_filter(tran, ckt.save_signals);
            result.transient = std::move(tran);
            break;
        }
        case AnalysisCommand::AC: {
            auto ac = solve_ac(ckt, cmd.ac_mode, cmd.ac_npoints,
                               cmd.ac_fstart, cmd.ac_fstop);
            apply_save_filter(ac, ckt.save_signals);
            result.ac = std::move(ac);
            break;
        }
        case AnalysisCommand::DC_SWEEP: {
            auto sw = solve_dc_sweep(ckt, cmd.dc_sweep_params);
            apply_save_filter(sw, ckt.save_signals);
            result.dc_sweep = std::move(sw);
            break;
        }
        default:
            break;
        }
    }
    return result;
}

} // namespace neospice
