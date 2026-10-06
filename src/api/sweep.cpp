#include "api/neospice.hpp"
#include "parser/netlist_parser.hpp"
#include "parser/expression.hpp"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <limits>
#include <random>
#include <set>
#ifndef __EMSCRIPTEN__
#include <thread>
#endif

namespace neospice {
namespace {
std::uint64_t job_seed(std::uint64_t seed, std::size_t index) {
    // SplitMix64: each job gets a seed independent of scheduling.
    auto x = seed + 0x9e3779b97f4a7c15ULL * (index + 1);
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

void require_converged(const SimulationResult& result) {
    std::visit([](const auto& analysis) {
        using T = std::decay_t<decltype(analysis)>;
        if constexpr (std::is_same_v<T, std::monostate>)
            throw std::runtime_error("Sweep requires an analysis directive");
        else if (!analysis.status.converged)
            throw SimulationError("Sweep analysis did not converge", analysis.status);
    }, result.analysis);
}
} // namespace

SweepResult Simulator::run_sweep(const std::string& netlist,
    const std::vector<SweepPoint>& points, const SweepOptions& options, bool from_file) const {
#ifdef __EMSCRIPTEN__
    throw std::invalid_argument("Parallel studies require a native build; this WASM module is single-threaded");
#else
    SweepResult batch;
    batch.samples.resize(points.size());
    if (points.empty()) return batch;
    unsigned workers = options.workers ? options.workers : std::thread::hardware_concurrency();
    workers = static_cast<unsigned>(std::min<std::size_t>(std::max(1u, workers), points.size()));
    batch.workers_used = workers;
    std::atomic<std::size_t> next{0};
    auto work = [&] {
        for (;;) {
            const auto index = next.fetch_add(1, std::memory_order_relaxed);
            if (index >= points.size()) return;
            auto& sample = batch.samples[index];
            try {
                sample.point = points[index];
                seed_expression_rng(job_seed(options.seed, index));
                NetlistParser parser;
                parser.set_force_pspice_compat(pspice_compat_override_);
                parser.set_parameter_overrides(sample.point.parameters);
                auto circuit = from_file ? parser.parse_file(netlist) : parser.parse(netlist);
                if (!circuit.step_commands.empty())
                    throw std::invalid_argument("Batch jobs cannot contain .step; supply sweep points instead");
                // run() retains only the last analysis. Reject ambiguous decks
                // so an earlier failure cannot disappear behind a later result.
                if (circuit.analyses.size() != 1)
                    throw std::invalid_argument("Batch jobs require exactly one analysis directive");
                for (const auto& [name, value] : sample.point.device_values) {
                    if (!std::isfinite(value) || !circuit.set_param(name, value))
                        throw std::invalid_argument("Invalid or unsupported device value: " + name);
                }
                if (sample.point.temperature_celsius) {
                    const auto temp = *sample.point.temperature_celsius;
                    if (!std::isfinite(temp) || temp <= -273.15)
                        throw std::invalid_argument("Temperature must be finite and above absolute zero");
                    circuit.options.temp = temp + 273.15;
                }
                Simulator sim;
                sample.result = sim.run(circuit);
                require_converged(*sample.result);
            } catch (const std::exception& error) {
                sample.error = error.what();
            }
        }
    };
    // Join during unwinding if launching a later worker fails. Use std::thread
    // because the macOS 14 standard library does not provide std::jthread.
    // Even workers=1 uses an isolated thread, preserving the caller's RNG.
    {
        struct WorkerPool {
            std::vector<std::thread> threads;
            ~WorkerPool() {
                for (auto& thread : threads)
                    if (thread.joinable()) thread.join();
            }
        } pool;
        pool.threads.reserve(workers);
        for (unsigned i = 0; i < workers; ++i) pool.threads.emplace_back(work);
    }
    return batch;
#endif
}

SweepResult Simulator::monte_carlo(const std::string& netlist,
    const std::vector<ParameterVariation>& variations, const MonteCarloOptions& options,
    bool from_file) const {
    const auto n = variations.size();
    if (!n) throw std::invalid_argument("Monte Carlo requires at least one variation");
    std::set<std::string> names;
    for (const auto& v : variations) {
        auto name = v.parameter;
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::tolower(c); });
        if (name.empty() || !names.insert(name).second ||
            !std::isfinite(v.nominal) || !std::isfinite(v.spread) || v.spread < 0 ||
            (v.distribution != VariationDistribution::Gaussian && v.distribution != VariationDistribution::Uniform))
            throw std::invalid_argument("Invalid or duplicate Monte Carlo parameter: " + v.parameter);
    }
    std::vector<std::vector<double>> factor(n, std::vector<double>(n));
    if (options.correlation.empty()) {
        for (std::size_t i = 0; i < n; ++i) factor[i][i] = 1;
    } else {
        const auto& correlation = options.correlation;
        if (correlation.size() != n)
            throw std::invalid_argument("Correlation matrix has wrong size");
        for (const auto& row : correlation)
            if (row.size() != n) throw std::invalid_argument("Correlation matrix has wrong size");
        for (std::size_t i = 0; i < n; ++i) {
            if (variations[i].distribution != VariationDistribution::Gaussian)
                throw std::invalid_argument("Correlation requires Gaussian variations");
            for (std::size_t j = 0; j < n; ++j)
                if (!std::isfinite(correlation[i][j]) || std::abs(correlation[i][j]) > 1 ||
                    std::abs(correlation[i][j] - correlation[j][i]) > 1e-12 ||
                    (i == j && correlation[i][j] != 1))
                    throw std::invalid_argument("Invalid correlation matrix");
            for (std::size_t j = 0; j <= i; ++j) {
                double value = correlation[i][j];
                for (std::size_t k = 0; k < j; ++k) value -= factor[i][k] * factor[j][k];
                if (i == j) {
                    if (value < -1e-12) throw std::invalid_argument("Correlation matrix is not positive semidefinite");
                    factor[i][j] = std::sqrt(std::max(0.0, value));
                } else if (factor[j][j] > 1e-14) {
                    factor[i][j] = value / factor[j][j];
                } else if (std::abs(value) > 1e-12) {
                    throw std::invalid_argument("Correlation matrix is not positive semidefinite");
                }
            }
        }
    }
    std::vector<SweepPoint> points(options.samples);
    for (std::size_t sample = 0; sample < points.size(); ++sample) {
        std::mt19937_64 random(job_seed(options.execution.seed, sample));
        std::normal_distribution<double> gaussian;
        std::uniform_real_distribution<double> uniform(-1, 1);
        std::vector<double> z(n);
        for (std::size_t i = 0; i < n; ++i)
            z[i] = variations[i].distribution == VariationDistribution::Gaussian ? gaussian(random) : uniform(random);
        for (std::size_t i = 0; i < n; ++i) {
            double deviation = 0;
            for (std::size_t j = 0; j <= i; ++j) deviation += factor[i][j] * z[j];
            points[sample].parameters[variations[i].parameter] = variations[i].nominal + variations[i].spread * deviation;
        }
    }
    return run_sweep(netlist, points, options.execution, from_file);
}

SampleStatistics summarize_samples(const std::vector<double>& values,
    double lower, double upper, std::size_t bins) {
    if (values.empty() || !bins || std::isnan(lower) || std::isnan(upper) || lower > upper)
        throw std::invalid_argument("Statistics require data, bins, and ordered limits");
    SampleStatistics result;
    long double mean = 0, m2 = 0;
    std::size_t passed = 0;
    result.minimum = result.maximum = values.front();
    for (double value : values) {
        if (!std::isfinite(value)) throw std::invalid_argument("Statistics require finite data");
        ++result.count;
        long double delta = value - mean;
        mean += delta / result.count;
        m2 += delta * (value - mean);
        result.minimum = std::min(result.minimum, value);
        result.maximum = std::max(result.maximum, value);
        passed += value >= lower && value <= upper;
    }
    result.mean = static_cast<double>(mean);
    result.standard_deviation = result.count > 1 ? static_cast<double>(std::sqrt(m2 / (result.count - 1))) : 0;
    result.yield = static_cast<double>(passed) / result.count;
    result.histogram.resize(bins);
    result.bin_edges.resize(bins + 1);
    const long double range = static_cast<long double>(result.maximum) - result.minimum;
    for (std::size_t i = 0; i <= bins; ++i)
        result.bin_edges[i] = static_cast<double>(result.minimum + range * i / bins);
    for (double value : values) {
        auto index = range == 0 ? 0 : std::min(bins - 1,
            static_cast<std::size_t>((value - static_cast<long double>(result.minimum)) / range * bins));
        ++result.histogram[index];
    }
    return result;
}
} // namespace neospice
