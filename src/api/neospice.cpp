#include "api/neospice.hpp"
#include "parser/netlist_parser.hpp"

namespace neospice {

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
    return solve_dc(ckt);
}

TransientResult Simulator::run_transient(Circuit& ckt, double tstep, double tstop) {
    return solve_transient(ckt, tstep, tstop);
}

ACResult Simulator::run_ac(Circuit& ckt, AnalysisCommand::ACMode mode,
                           int npoints, double fstart, double fstop) {
    return solve_ac(ckt, mode, npoints, fstart, fstop);
}

SimulationResult Simulator::run(Circuit& ckt) {
    SimulationResult result;
    for (auto& cmd : ckt.analyses) {
        switch (cmd.type) {
        case AnalysisCommand::OP:
            result.dc = solve_dc(ckt);
            break;
        case AnalysisCommand::TRAN:
            result.transient = solve_transient(ckt, cmd.tran_tstep, cmd.tran_tstop);
            break;
        case AnalysisCommand::AC:
            result.ac = solve_ac(ckt, cmd.ac_mode, cmd.ac_npoints,
                                 cmd.ac_fstart, cmd.ac_fstop);
            break;
        default:
            break;
        }
    }
    return result;
}

} // namespace neospice
