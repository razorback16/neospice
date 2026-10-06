#pragma once
#include "core/circuit.hpp"
#include "core/dc.hpp"
#include "core/transient.hpp"
#include "core/ac.hpp"
#include "core/noise.hpp"
#include "core/tf.hpp"
#include "core/sens.hpp"
#include "core/gradient.hpp"
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

// Each point runs a fresh circuit. Values override declared top-level .param
// definitions; device_values changes supported primitive values after parsing.
struct SweepPoint {
    std::map<std::string, double> parameters;
    std::map<std::string, double> device_values;
    std::optional<double> temperature_celsius;
};

struct SweepOptions {
    unsigned workers = 0; // 0: hardware concurrency, bounded by job count
    std::uint64_t seed = 0;
};

struct SweepSample {
    SweepPoint point;
    std::optional<SimulationResult> result;
    std::string error; // nonempty on parse, simulation, or convergence failure
};

struct SweepResult {
    std::vector<SweepSample> samples; // always in input order, including failures
    unsigned workers_used = 0;
};

enum class VariationDistribution { Gaussian, Uniform };

struct ParameterVariation {
    std::string parameter;
    double nominal = 0;
    double spread = 0; // absolute standard deviation (Gaussian) or half-width (uniform)
    VariationDistribution distribution = VariationDistribution::Gaussian;
};

struct MonteCarloOptions {
    std::size_t samples = 100;
    SweepOptions execution;
    // Optional symmetric positive-semidefinite correlation matrix. Correlated
    // variation currently requires all parameters to be Gaussian.
    std::vector<std::vector<double>> correlation;
};

struct SampleStatistics {
    std::size_t count = 0;
    double mean = 0, standard_deviation = 0, minimum = 0, maximum = 0;
    double yield = 0; // fraction inside inclusive limits
    std::vector<std::size_t> histogram;
    std::vector<double> bin_edges;
};

// Rejects empty/nonfinite data; reports sample standard deviation (n-1).
SampleStatistics summarize_samples(const std::vector<double>& values,
    double lower, double upper, std::size_t bins = 10);

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

    /// Force PSpice/LTspice compatibility mode for subsequent load()/parse()
    /// calls, overriding netlist-dialect auto-detection. Mirrors ngspice's
    /// `-D ngbehavior=ps/lt/psa`. std::nullopt (default) = auto-detect.
    void set_pspice_compat(std::optional<bool> v) { pspice_compat_override_ = v; }

    DCResult run_dc(Circuit& ckt);
    DCResult re_solve(Circuit& ckt);
    ACResult re_solve_ac(Circuit& ckt, ACMode mode, int npoints, double fstart, double fstop);
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
    GradientResult sensitivity(Circuit& ckt, const std::vector<std::string>& outputs,
                               const std::vector<std::string>& parameters = {});
    ACGradientResult sensitivity_ac(Circuit& ckt, const std::vector<std::string>& outputs,
                                    const std::vector<std::string>& parameters,
                                    const std::vector<double>& frequencies);

    SimulationResult run(Circuit& ckt);
    SimulationResult run_step_sweep(Circuit& ckt);

    // netlist is text by default; from_file preserves relative .include paths.
    SweepResult run_sweep(const std::string& netlist,
                         const std::vector<SweepPoint>& points,
                         const SweepOptions& options = {}, bool from_file = false) const;
    SweepResult monte_carlo(const std::string& netlist,
                           const std::vector<ParameterVariation>& variations,
                           const MonteCarloOptions& options = {}, bool from_file = false) const;

    void register_device(std::string_view prefix, ModelMatcher matcher,
                         DeviceFactory factory);

private:
    struct RegistryEntry {
        std::string prefix;
        ModelMatcher matcher;
        DeviceFactory factory;
    };
    std::vector<RegistryEntry> registry_;
    std::optional<bool> pspice_compat_override_;
};

} // namespace neospice
