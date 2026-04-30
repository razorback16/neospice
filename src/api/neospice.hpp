#pragma once
#include "core/circuit.hpp"
#include "core/dc.hpp"
#include "core/transient.hpp"
#include "core/ac.hpp"
#include "core/noise.hpp"
#include "core/tf.hpp"
#include "core/sens.hpp"
#include "core/pz.hpp"
#include "core/measure.hpp"
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace neospice {

struct StepResult;  // forward declaration

using AnalysisResult = std::variant<std::monostate,
                                    DCResult, TransientResult, ACResult,
                                    DCSweepResult, NoiseResult, TFResult,
                                    SensResult, PZResult>;

struct SimulationResult {
    // Exactly one analysis result per run
    AnalysisResult analysis;

    // Orthogonal to the main analysis:
    std::optional<MeasureResult> measures;
    std::vector<std::string> print_output;  // formatted .print/.plot output
    std::unique_ptr<StepResult> step;       // non-null when .step sweep ran
};

struct StepResult {
    std::vector<double> step_values;
    std::string step_variable;
    std::vector<SimulationResult> results;
};

struct ModelMatcher {
    std::optional<std::string> model_type;
    std::optional<int> level;
};

class Simulator {
public:
    using DeviceFactory = std::function<
        std::unique_ptr<Device>(std::string_view name,
                                std::span<const int32_t> nodes,
                                const std::map<std::string, double>& params)>;

    Simulator() = default;

    Circuit load(const std::string& filepath);
    Circuit parse(const std::string& netlist_text);

    DCResult run_dc(Circuit& ckt);
    TransientResult run_transient(Circuit& ckt, double tstep, double tstop);
    TransientResult run_transient(Circuit& ckt, double tstep, double tstop,
                                  const TransientOptions& opts);
    ACResult run_ac(Circuit& ckt, ACMode mode,
                    int npoints, double fstart, double fstop);
    ACResult run_ac(Circuit& ckt, ACMode mode,
                    int npoints, double fstart, double fstop,
                    const ACOptions& opts);
    NoiseResult run_noise(Circuit& ckt, const std::string& output_node,
                          const std::string& input_src,
                          ACMode mode,
                          int npoints, double fstart, double fstop);
    DCSweepResult run_dc_sweep(Circuit& ckt, const std::vector<DCSweepParam>& params);
    TFResult run_tf(Circuit& ckt, const std::string& output_var,
                    const std::string& input_src);
    SensResult run_sens(Circuit& ckt, const std::string& output_var);

    SimulationResult run(Circuit& ckt);
    SimulationResult run_step_sweep(Circuit& ckt);

    void register_device(std::string_view prefix, ModelMatcher matcher,
                         DeviceFactory factory);

private:
    struct RegistryEntry {
        std::string prefix;
        ModelMatcher matcher;
        DeviceFactory factory;
    };
    std::vector<RegistryEntry> registry_;
};

} // namespace neospice
