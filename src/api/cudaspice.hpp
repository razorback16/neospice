#pragma once
#include "core/circuit.hpp"
#include "core/dc.hpp"
#include "core/transient.hpp"
#include "core/ac.hpp"
#include <string>
#include <optional>

namespace cudaspice {

struct SimulationResult {
    std::optional<DCResult> dc;
    std::optional<TransientResult> transient;
    std::optional<ACResult> ac;
};

struct SimulatorOptions {
    double abstol = 1e-12;
    double reltol = 1e-3;
    double vntol  = 1e-6;
    double trtol  = 7.0;
    double gmin   = 1e-12;
};

class Simulator {
public:
    using Options = SimulatorOptions;

    explicit Simulator(Options opts = Options{});

    Circuit load(const std::string& filepath);
    Circuit parse(const std::string& netlist_text);

    DCResult run_dc(Circuit& ckt);
    TransientResult run_transient(Circuit& ckt, double tstep, double tstop);
    ACResult run_ac(Circuit& ckt, AnalysisCommand::ACMode mode,
                    int npoints, double fstart, double fstop);

    SimulationResult run(Circuit& ckt);

private:
    Options opts_;
};

} // namespace cudaspice
