#pragma once
#include "core/circuit.hpp"
#include "core/dc.hpp"
#include "core/transient.hpp"
#include "core/ac.hpp"
#include "core/noise.hpp"
#include "core/tf.hpp"
#include "core/sens.hpp"
#include "core/measure.hpp"
#include <string>
#include <optional>
#include <vector>

namespace neospice {

struct SimulationResult {
    std::optional<DCResult> dc;
    std::optional<TransientResult> transient;
    std::optional<ACResult> ac;
    std::optional<DCSweepResult> dc_sweep;
    std::optional<NoiseResult> noise;
    std::optional<TFResult> tf;
    std::optional<SensResult> sens;
    std::optional<MeasureResult> measures;
    std::vector<std::string> print_output;  // formatted .print/.plot output
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
    NoiseResult run_noise(Circuit& ckt, const std::string& output_node,
                          const std::string& input_src,
                          AnalysisCommand::ACMode mode,
                          int npoints, double fstart, double fstop);
    DCSweepResult run_dc_sweep(Circuit& ckt, const std::vector<DCSweepParam>& params);
    TFResult run_tf(Circuit& ckt, const std::string& output_var,
                    const std::string& input_src);
    SensResult run_sens(Circuit& ckt, const std::string& output_var);

    SimulationResult run(Circuit& ckt);

private:
    Options opts_;
};

} // namespace neospice
