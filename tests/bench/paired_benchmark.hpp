#pragma once
// Shared paired in-process driver. See docs/benchmark-methods.md.
#include "api/neospice.hpp"
#include "framework/comparator.hpp"
#include "bench/paired_measurement.hpp"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <variant>

namespace neospice::bench::driver {
using namespace neospice;
using Output = std::variant<DCResult, ACResult, TransientResult, NoiseResult,
                            NgspiceNoiseResult, DCSweepResult>;
using Outputs = std::vector<Output>;
struct Workload {
    std::string id, file, command;
    enum Kind { DC, AC, TRAN, NOISE, SWEEP, OP_AC } kind;
    int points = 10;
    double start = 1, stop = 1e8;
};

inline std::string quote(const std::string& text) {
    std::ostringstream out;
    out << '"';
    for (unsigned char c : text) {
        if (c == '"' || c == '\\') out << '\\' << c;
        else if (c < 32) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(c);
        else out << c;
    }
    out << '"';
    return out.str();
}
inline void number(std::ostream& out, double value) {
    if (std::isfinite(value)) out << value;
    else out << "null";
}
inline void phases(std::ostream& out, const bench::Phases& p) {
    out << "{\"load_us\":" << p.load_us << ",\"analysis_us\":" << p.analysis_us
        << ",\"cleanup_us\":" << p.cleanup_us << ",\"total_us\":" << p.total_us << '}';
}
inline void comparison(std::ostream& out, const CompareResult& c) {
    out << "{\"passed\":" << (c.passed ? "true" : "false")
        << ",\"points\":" << c.num_points_compared
        << ",\"worst_signal\":" << quote(c.worst_signal) << ",\"worst_error\":";
    number(out, c.worst_error);
    out << ",\"signals\":[";
    bool first = true;
    for (const auto& s : c.signals) {
        if (!first) out << ',';
        first = false;
        out << "{\"name\":" << quote(s.name) << ",\"points\":" << s.points
            << ",\"max_absolute_error\":"; number(out, s.max_absolute_error);
        out << ",\"max_normalized_error\":"; number(out, s.max_normalized_error);
        out << ",\"coordinate\":"; number(out, s.worst_coordinate);
        out << ",\"unit\":" << quote(s.coordinate_unit)
            << ",\"reference\":"; number(out, s.reference_at_worst);
        out << ",\"actual\":"; number(out, s.actual_at_worst);
        out << ",\"reference_imag\":"; number(out, s.reference_imag_at_worst);
        out << ",\"actual_imag\":"; number(out, s.actual_imag_at_worst);
        out << '}';
    }
    out << "]}";
}

inline Outputs neo_analysis(Simulator& sim, Circuit& ckt, const Workload& w) {
    Outputs result;
    if (w.kind == Workload::DC || w.kind == Workload::OP_AC)
        result.emplace_back(sim.run_dc(ckt));
    if (w.kind == Workload::AC || w.kind == Workload::OP_AC)
        result.emplace_back(sim.run_ac(ckt, ACMode::DEC, w.points, w.start, w.stop));
    if (w.kind == Workload::TRAN)
        result.emplace_back(sim.run_transient(ckt, w.start, w.stop));
    if (w.kind == Workload::NOISE)
        result.emplace_back(sim.run_noise(ckt, "out", "v1", ACMode::DEC, w.points, w.start, w.stop));
    if (w.kind == Workload::SWEEP)
        result.emplace_back(sim.run_dc_sweep(ckt, {{"v1", w.start, w.stop, 0.01}}));
    return result;
}
inline Outputs reference_analysis(NgspiceRunner& ng, const Workload& w, std::string& diagnostics) {
    Outputs result;
    auto command = [&](const std::string& cmd) {
        try { ng.command(cmd); }
        catch (...) { diagnostics += ng.diagnostics(); throw; }
        diagnostics += ng.diagnostics();
    };
    if (w.kind == Workload::OP_AC) {
        command("op"); result.emplace_back(ng.read_dc());
        std::ostringstream ac;
        ac << std::setprecision(17) << "ac dec " << w.points << ' ' << w.start << ' ' << w.stop;
        command(ac.str()); result.emplace_back(ng.read_ac());
        return result;
    }
    command(w.command);
    switch (w.kind) {
    case Workload::DC: result.emplace_back(ng.read_dc()); break;
    case Workload::AC: result.emplace_back(ng.read_ac()); break;
    case Workload::TRAN: result.emplace_back(ng.read_transient()); break;
    case Workload::NOISE: result.emplace_back(ng.read_noise()); break;
    case Workload::SWEEP: result.emplace_back(ng.read_dc_sweep()); break;
    default: throw std::logic_error("Unknown workload");
    }
    return result;
}

inline CompareResult invalid(const std::string& detail) {
    return {false, detail, std::numeric_limits<double>::infinity(), 0};
}
inline bool endpoint(double a, double b) {
    return std::isfinite(a) && std::abs(a-b) <=
        128 * std::numeric_limits<double>::epsilon() * std::max(std::abs(a), std::abs(b));
}
inline CompareResult validate(const Outputs& ref, const Outputs& neo, const Workload& w) {
    // Frozen screening threshold; not calibrated to obtain benchmark passes.
    constexpr Tolerance tolerance{1e-3, 1e-9};
    CompareResult combined{true, "", 0, 0};
    if (ref.empty() || ref.size() != neo.size()) return invalid("Missing analysis result");
    for (size_t i = 0; i < ref.size(); ++i) {
        CompareResult c = std::visit([&](const auto& r) -> CompareResult {
            using T = std::decay_t<decltype(r)>;
            if constexpr (std::is_same_v<T, DCResult>) {
                return compare_dc(r, std::get<DCResult>(neo[i]), tolerance);
            } else if constexpr (std::is_same_v<T, ACResult>) {
                if (!bench::complete_frequency_request(r.frequency, w.start, w.stop,
                    size_t(std::lround(std::log10(w.stop/w.start)*w.points)+1)))
                    return invalid("Reference AC request incomplete");
                return compare_ac(r, std::get<ACResult>(neo[i]), tolerance);
            } else if constexpr (std::is_same_v<T, TransientResult>) {
                const auto& n = std::get<TransientResult>(neo[i]);
                if (r.time.empty() || n.time.empty() || r.time.front() != 0 || n.time.front() != 0 ||
                    !endpoint(r.time.back(), w.stop) || !endpoint(n.time.back(), w.stop))
                    return invalid("Transient request incomplete");
                return compare_transient(r, n, tolerance);
            } else if constexpr (std::is_same_v<T, NgspiceNoiseResult>) {
                if (!bench::complete_frequency_request(r.frequency, w.start, w.stop,
                    size_t(std::lround(std::log10(w.stop/w.start)*w.points)+1)))
                    return invalid("Reference noise request incomplete");
                return compare_noise(r, std::get<NoiseResult>(neo[i]), tolerance);
            } else if constexpr (std::is_same_v<T, DCSweepResult>) {
                const auto& n = std::get<DCSweepResult>(neo[i]);
                auto detail = validate_dc_sweep_data(r, n);
                if (!detail.empty()) return invalid(detail);
                if (r.sweep_values.size() != size_t(w.points) || n.sweep_values.size() != size_t(w.points))
                    return invalid("DC sweep request incomplete");
                for (size_t j = 0; j < r.sweep_values.size(); ++j) {
                    // Repeated 0.01 additions accumulate roundoff near zero.
                    const double requested = w.start + j * 0.01;
                    const double grid_error = 128 * std::numeric_limits<double>::epsilon() * w.points;
                    if (std::abs(r.sweep_values[j]-requested) > grid_error ||
                        std::abs(n.sweep_values[j]-requested) > grid_error)
                        return invalid("DC sweep coordinates differ from request");
                }
                return compare_dc_sweep(r, n, tolerance);
            } else return invalid("Unexpected reference result type");
        }, ref[i]);
        combined.passed &= c.passed;
        combined.num_points_compared += c.num_points_compared;
        if (c.worst_error > combined.worst_error) {
            combined.worst_error = c.worst_error;
            combined.worst_signal = c.worst_signal;
        }
        combined.signals.insert(combined.signals.end(), c.signals.begin(), c.signals.end());
    }
    return combined;
}

struct StandardValidation {
    static constexpr double threshold = 1e-3;
    static const char* denominator_floor() { return "1e-9"; }
    static const char* policy_json() { return "null"; }
    void reset() {}
    void write_extra(std::ostream&) const {}
    CompareResult operator()(const Outputs& ref, const Outputs& neo, const Workload& w) {
        return validate(ref, neo, w);
    }
};

template<class Validation = StandardValidation>
int run(const std::vector<Workload>& workloads, int argc, char** argv,
        Validation validation = {}) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--describe") {
            std::cout << "{\"schema\":1,\"compiler\":" << quote(__VERSION__)
                << ",\"circuits_root\":" << quote(TEST_CIRCUITS_DIR) << ",\"workloads\":[";
            bool first = true;
            for (const auto& w : workloads) {
                if (!first) std::cout << ',';
                first = false;
                std::cout << "{\"id\":" << quote(w.id) << ",\"file\":" << quote(w.file)
                    << ",\"command\":" << quote(w.command) << '}';
            }
            std::cout << "],\"validation_policy\":" << validation.policy_json() << "}\n";
            return 0;
        }
        bool verify = false;
        int samples = 30, warmup = 3;
        std::string output, filter;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--verify-only") verify = true;
            else if (i + 1 < argc && arg == "--output") output = argv[++i];
            else if (i + 1 < argc && arg == "--samples") samples = std::stoi(argv[++i]);
            else if (i + 1 < argc && arg == "--warmup") warmup = std::stoi(argv[++i]);
            else if (i + 1 < argc && arg == "--case") filter = argv[++i];
            else throw std::runtime_error("Usage: paired benchmark --output FILE [--verify-only] [--samples N] [--warmup N] [--case ID]");
        }
        if (output.empty() || samples < 1 || warmup < 0)
            throw std::runtime_error("An output file and valid sample counts are required");
        if (!filter.empty() && std::none_of(workloads.begin(), workloads.end(), [&](const auto& w) { return w.id == filter; }))
            throw std::runtime_error("Unknown workload: " + filter);
        if (!verify) {
            for (const auto* key : {"OMP_NUM_THREADS", "OPENBLAS_NUM_THREADS"}) {
                const char* value = std::getenv(key);
                if (!value || std::string(value) != "1")
                    throw std::runtime_error(std::string(key) + " must be 1 for timing runs");
            }
        }
        // Never silently overwrite an earlier experiment.
        if (std::ifstream(output).good()) throw std::runtime_error("Output already exists: " + output);
        std::ofstream out(output);
        out.exceptions(std::ios::badbit | std::ios::failbit);
        out << std::setprecision(17);
        Simulator sim;
        NgspiceRunner ng;
        ng.command("version");
        const auto version = ng.diagnostics();
        // This is a separately labeled one-thread benchmark configuration.
        ng.command("set num_threads=1");
        ng.command("set");
        const auto settings = ng.diagnostics();
        out << "{\"type\":\"metadata\",\"schema\":1,\"verify_only\":" << (verify ? "true" : "false")
            << ",\"samples\":" << (verify ? 0 : samples) << ",\"warmup\":" << (verify ? 0 : warmup)
            << ",\"reference_version\":" << quote(version) << ",\"reference_settings\":" << quote(settings)
            << ",\"population\":[";
        bool first_workload = true;
        for (const auto& w : workloads) {
            if (!filter.empty() && w.id != filter) continue;
            if (!first_workload) out << ',';
            first_workload = false; out << quote(w.id);
        }
        out << "]" << ",\"relative_tolerance\":" << validation.threshold
            << ",\"denominator_floor\":" << validation.denominator_floor()
            << ",\"validation_policy\":" << validation.policy_json()
            << ",\"clock\":\"steady_clock\"}\n";
        bool success = true;
        for (const auto& w : workloads) {
            if (!filter.empty() && w.id != filter) continue;
            bool accepted = true;
            const int total = verify ? 1 : 1 + warmup + samples;
            out << "{\"type\":\"workload\",\"id\":" << quote(w.id)
                << ",\"file\":" << quote(w.file) << ",\"command\":" << quote(w.command) << "}\n";
            for (int iteration = 0; iteration < total; ++iteration) {
                validation.reset();
                std::optional<Circuit> circuit;
                std::string diagnostics;
                CompareResult evidence = invalid("Simulation or extraction failed before comparison");
                try {
                    auto neo = [&] { return bench::measure(
                        [&] { circuit.emplace(sim.load(std::string(TEST_CIRCUITS_DIR) + '/' + w.file)); },
                        [&] { return neo_analysis(sim, *circuit, w); },
                        [&] { circuit.reset(); }); };
                    auto reference = [&] { return bench::measure(
                        [&] {
                            try { ng.load(std::string(TEST_CIRCUITS_DIR) + '/' + w.file); }
                            catch (...) { diagnostics += ng.diagnostics(); throw; }
                            diagnostics += ng.diagnostics();
                        },
                        [&] { return reference_analysis(ng, w, diagnostics); },
                        [&] { ng.reset(); }); };
                    auto [n, r, c] = bench::paired(iteration % 2 == 0, neo, reference,
                        [&](const Outputs& r, const Outputs& n) { evidence = validation(r, n, w); return evidence; });
                    const std::string phase = iteration == 0 ? "validation" :
                        iteration <= warmup ? "warmup" : "sample";
                    out << "{\"type\":\"pair\",\"id\":" << quote(w.id) << ",\"iteration\":" << iteration
                        << ",\"phase\":" << quote(phase) << ",\"neo_first\":" << (iteration % 2 == 0 ? "true" : "false")
                        << ",\"comparison\":"; comparison(out, c);
                    validation.write_extra(out);
                    // Verification produces no performance evidence.
                    if (!verify) { out << ",\"neo\":"; phases(out, n); out << ",\"reference\":"; phases(out, r); }
                    out << ",\"reference_diagnostics\":" << quote(diagnostics) << "}\n";
                } catch (const std::exception& e) {
                    accepted = false;
                    out << "{\"type\":\"failure\",\"id\":" << quote(w.id) << ",\"iteration\":" << iteration
                        << ",\"error\":" << quote(e.what()) << ",\"comparison\":"; comparison(out, evidence);
                    validation.write_extra(out);
                    out << ",\"reference_diagnostics\":" << quote(diagnostics) << "}\n";
                    break;
                }
                out.flush();
            }
            out << "{\"type\":\"case_end\",\"id\":" << quote(w.id) << ",\"accepted\":" << (accepted ? "true" : "false") << "}\n";
            out.flush();
            std::cout << w.id << ": " << (accepted ? "PASS" : "FAIL (no accepted timings)") << '\n';
            success &= accepted;
        }
        out << "{\"type\":\"end\",\"success\":" << (success ? "true" : "false") << "}\n";
        return success ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 2;
    }
}

} // namespace neospice::bench::driver
