#pragma once
#include "bench/paired_benchmark.hpp"
#include <array>

namespace neospice::bench::driver {

// Original TLV3201 qualification: crossing deltas <50ns; non-switching ports
// use relative1e-2 with denominator floor1e-2V. Errors below are expressed as
// fractions of those allowances, never as interchangeable voltage/time errors.
struct Tlv3201Validation {
    static constexpr double threshold = 1;
    static constexpr double crossing_limit = 50e-9;
    static constexpr Tolerance port_tolerance{1e-2, 1e-2};
    static const char* denominator_floor() { return "null"; }
    static const char* policy_json() {
        return R"POLICY({"kind":"tlv3201-v1","crossing_absolute_s":5e-8,"crossing_strict":true,"port_relative":0.01,"port_denominator_floor_v":0.01,"edge_low_v":0.3,"edge_high_v":3.0,"settle_window_s":1e-6,"required_voltages":["v(vcc)","v(inm)","v(inp)","v(out)"],"dc_ports":["v(vcc)","v(inm)"],"start_s":0,"stop_s":3e-5,"error_scale":"fraction_of_allowance"})POLICY";
    }
    CompareResult waveform = invalid("Waveform diagnostics not evaluated");
    std::vector<EdgeMetrics> expected_edges, actual_edges;
    void reset() {
        waveform = invalid("Waveform diagnostics not evaluated");
        expected_edges.clear(); actual_edges.clear();
    }
    CompareResult compare(const TransientResult& ref, const TransientResult& neo) {
        reset();
        if (const auto problem = validate_transient_data(ref, neo); !problem.empty())
            return invalid(problem);
        if (ref.time.front() != 0 || neo.time.front() != 0 ||
            !endpoint(ref.time.back(), 30e-6) || !endpoint(neo.time.back(), 30e-6))
            return invalid("TLV3201 transient request incomplete");
        for (const auto* name : {"v(vcc)", "v(inm)", "v(inp)", "v(out)"})
            if (!ref.voltages.count(name) || !neo.voltages.count(name))
                return invalid(std::string("Missing required TLV3201 voltage: ") + name);
        // Full waveform comparison remains informational. Preserve internal
        // voltage/current diagnostics without using their tolerance to qualify
        // the original edge/DC-port experiment.
        waveform = compare_transient(ref, neo, {1e-3, 1e-9});
        TransientResult rp, np;
        rp.time = ref.time; np.time = neo.time;
        rp.status = ref.status; np.status = neo.status;
        for (const auto* name : {"v(vcc)", "v(inm)"}) {
            rp.voltages[name] = ref.voltages.at(name);
            np.voltages[name] = neo.voltages.at(name);
        }
        CompareResult result = compare_transient(rp, np, port_tolerance);
        result.worst_error /= port_tolerance.relative;
        for (auto& signal : result.signals)
            signal.max_normalized_error /= port_tolerance.relative;
        expected_edges = extract_edges(ref.time, ref.voltages.at("v(out)"), 0.3, 3.0, 1e-6);
        actual_edges = extract_edges(neo.time, neo.voltages.at("v(out)"), 0.3, 3.0, 1e-6);
        if (expected_edges.empty() || expected_edges.size() != actual_edges.size()) {
            result.passed = false;
            result.worst_signal = "Missing or unequal TLV3201 output edge count";
            result.worst_error = std::numeric_limits<double>::infinity();
            return result;
        }
        for (size_t i = 0; i < expected_edges.size(); ++i) {
            const auto& r = expected_edges[i]; const auto& n = actual_edges[i];
            if (!std::isfinite(r.cross_time) || !std::isfinite(n.cross_time) ||
                !std::isfinite(r.rise_time) || !std::isfinite(n.rise_time) ||
                r.rise_time == 0 || n.rise_time == 0 ||
                std::signbit(r.rise_time) != std::signbit(n.rise_time)) {
                result.passed = false;
                result.worst_signal = "Incomplete or opposite-direction TLV3201 output edge";
                result.worst_error = std::numeric_limits<double>::infinity();
                return result;
            }
            const double error = std::abs(r.cross_time-n.cross_time);
            const double fraction = error/crossing_limit;
            const auto name = std::string("v(out):") + (r.rise_time > 0 ? "rising:" : "falling:") + std::to_string(i);
            result.signals.push_back({name, 1, error, fraction, r.cross_time,
                                      r.cross_time, n.cross_time, "s"});
            ++result.num_points_compared;
            if (!(error < crossing_limit)) result.passed = false;
            if (fraction > result.worst_error) {
                result.worst_error = fraction; result.worst_signal = name;
            }
        }
        return result;
    }
    CompareResult operator()(const Outputs& ref, const Outputs& neo, const Workload& w) {
        if (w.kind != Workload::TRAN || ref.size() != 1 || neo.size() != 1 ||
            !std::holds_alternative<TransientResult>(ref[0]) ||
            !std::holds_alternative<TransientResult>(neo[0]))
            return invalid("Missing TLV3201 transient result");
        return compare(std::get<TransientResult>(ref[0]), std::get<TransientResult>(neo[0]));
    }
    void write_extra(std::ostream& out) const {
        out << ",\"informational_waveform_comparison\":"; comparison(out, waveform);
        auto edges = [&](const std::vector<EdgeMetrics>& values) {
            out << '[';
            for (size_t i = 0; i < values.size(); ++i) {
                if (i) out << ',';
                const auto& e = values[i];
                out << "{\"cross_time_s\":"; number(out, e.cross_time);
                out << ",\"rise_time_s\":"; number(out, e.rise_time);
                out << ",\"settled_value_v\":"; number(out, e.settled_value);
                out << ",\"overshoot_v\":"; number(out, e.overshoot); out << '}';
            }
            out << ']';
        };
        out << ",\"reference_edges\":"; edges(expected_edges);
        out << ",\"actual_edges\":"; edges(actual_edges);
    }
};
} // namespace neospice::bench::driver
