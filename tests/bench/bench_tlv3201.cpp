// TLV3201 comparator: head-to-head neospice vs ngspice
// Performance timing + accuracy comparison

#include "api/neospice.hpp"
#include "framework/ngspice_lib.hpp"
#include "framework/ngspice_runner.hpp"
#include "framework/comparator.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace neospice;
using Clock = std::chrono::high_resolution_clock;

struct TimingStats {
    double median_us;
    double min_us;
    double max_us;
    int runs;
};

TimingStats compute_stats(std::vector<double>& samples) {
    std::sort(samples.begin(), samples.end());
    int n = static_cast<int>(samples.size());
    double med = (n % 2 == 0)
        ? (samples[n/2 - 1] + samples[n/2]) / 2.0
        : samples[n/2];
    return {med, samples.front(), samples.back(), n};
}

std::string fmt_time(double us) {
    static char buf[32];
    if (us < 1000.0)
        std::snprintf(buf, sizeof(buf), "%6.0f µs", us);
    else if (us < 1e6)
        std::snprintf(buf, sizeof(buf), "%6.2f ms", us / 1e3);
    else
        std::snprintf(buf, sizeof(buf), "%6.2f  s", us / 1e6);
    return buf;
}

int main() {
    const std::string circuits = std::string(TEST_CIRCUITS_DIR);
    const std::string cir = circuits + "/tlv3201_switching.cir";

    std::printf("=== TLV3201 Comparator: neospice vs ngspice ===\n\n");

    // ---- 1. ngspice run ----
    std::printf("Running ngspice...\n");
    NgspiceRunner ng_runner;
    TransientResult ng_result;
    {
        auto t0 = Clock::now();
        ng_result = ng_runner.run_transient(cir);
        auto t1 = Clock::now();
        double us = std::chrono::duration<double, std::micro>(t1 - t0).count();
        std::printf("  ngspice: %s  (%zu time points)\n",
                    fmt_time(us).c_str(),
                    ng_result.time.size());
    }

    // Print available signals
    std::printf("  ngspice signals:");
    for (auto& [k, v] : ng_result.voltages)
        std::printf(" %s(%zu)", k.c_str(), v.size());
    for (auto& [k, v] : ng_result.currents)
        std::printf(" %s(%zu)", k.c_str(), v.size());
    std::printf("\n\n");

    // ---- 2. neospice run ----
    std::printf("Running neospice...\n");
    Simulator sim;
    try {
        auto t0 = Clock::now();
        auto ckt = sim.load(cir);
        auto cs_result = sim.run(ckt);
        auto t1 = Clock::now();
        double us = std::chrono::duration<double, std::micro>(t1 - t0).count();

        if (!std::holds_alternative<TransientResult>(cs_result.analysis)) {
            std::printf("  neospice: FAILED (wrong analysis type)\n");
            return 1;
        }
        auto& neo_result = std::get<TransientResult>(cs_result.analysis);
        std::printf("  neospice: %s  (%zu time points)\n",
                    fmt_time(us).c_str(),
                    neo_result.time.size());

        // Print available signals
        std::printf("  neospice signals:");
        for (auto& [k, v] : neo_result.voltages)
            std::printf(" %s(%zu)", k.c_str(), v.size());
        for (auto& [k, v] : neo_result.currents)
            std::printf(" %s(%zu)", k.c_str(), v.size());
        std::printf("\n\n");

        // ---- 3. Performance benchmark (multiple runs) ----
        std::printf("=== Performance Benchmark ===\n");
        const int W = 3, R = 20;
        std::printf("Warmup=%d, Runs=%d\n\n", W, R);

        // neospice timing
        std::vector<double> neo_times;
        for (int i = 0; i < W; ++i) {
            auto c = sim.load(cir);
            sim.run(c);
        }
        for (int i = 0; i < R; ++i) {
            auto ta = Clock::now();
            auto c = sim.load(cir);
            sim.run(c);
            auto tb = Clock::now();
            neo_times.push_back(std::chrono::duration<double, std::micro>(tb - ta).count());
        }
        auto neo_stats = compute_stats(neo_times);

        // ngspice timing
        NgspiceLib ng;
        std::vector<double> ng_times;
        for (int i = 0; i < W; ++i) {
            ng.load_circuit(cir);
            ng.command("run");
            ng.reset();
        }
        for (int i = 0; i < R; ++i) {
            auto ta = Clock::now();
            ng.load_circuit(cir);
            ng.command("run");
            auto tb = Clock::now();
            ng.reset();
            ng_times.push_back(std::chrono::duration<double, std::micro>(tb - ta).count());
        }
        auto ng_stats = compute_stats(ng_times);

        double ratio = ng_stats.median_us / neo_stats.median_us;
        const char* winner = (ratio >= 1.0) ? "neospice" : "ngspice";
        double factor = (ratio >= 1.0) ? ratio : 1.0 / ratio;

        std::printf("  %-20s  %s\n", "neospice median:", fmt_time(neo_stats.median_us).c_str());
        std::printf("  %-20s  %s\n", "ngspice median:", fmt_time(ng_stats.median_us).c_str());
        std::printf("  Winner: %s (%.1fx faster)\n\n", winner, factor);

        // ---- 4. Accuracy comparison ----
        std::printf("=== Accuracy Comparison ===\n\n");

        // Separate port-level signals from internal subcircuit nodes.
        // For a comparator benchmark, accuracy is defined by port behavior,
        // not internal model nodes which can differ due to integration
        // method differences (e.g. ringing suppression via GEAR switching).
        std::vector<std::string> port_signals;
        std::vector<std::string> internal_signals;
        for (auto& [k, v] : neo_result.voltages) {
            if (!ng_result.voltages.count(k)) continue;
            if (k.find("x1.") != std::string::npos)
                internal_signals.push_back(k);
            else
                port_signals.push_back(k);
        }

        std::printf("  Port-level signals: %zu, Internal nodes: %zu\n\n",
                    port_signals.size(), internal_signals.size());

        // --- Primary metric: v(out) edge timing ---
        std::printf("  [1] Output edge timing (primary accuracy metric):\n");
        bool edge_pass = false;
        double worst_edge_ns = 0.0;
        if (neo_result.voltages.count("v(out)") && ng_result.voltages.count("v(out)")) {
            auto edges_ng = extract_edges(ng_result.time, ng_result.voltages.at("v(out)"),
                                           0.3, 3.0, 1e-6);
            auto edges_neo = extract_edges(neo_result.time, neo_result.voltages.at("v(out)"),
                                            0.3, 3.0, 1e-6);
            std::printf("      ngspice edges: %zu, neospice edges: %zu\n",
                        edges_ng.size(), edges_neo.size());

            edge_pass = (edges_ng.size() == edges_neo.size());
            size_t ne = std::min(edges_ng.size(), edges_neo.size());
            for (size_t i = 0; i < ne; ++i) {
                double t_diff = edges_neo[i].cross_time - edges_ng[i].cross_time;
                worst_edge_ns = std::max(worst_edge_ns, std::abs(t_diff * 1e9));
                const char* dir = (edges_ng[i].rise_time >= 0) ? "RISING" : "FALLING";
                std::printf("      Edge %zu: ng=%.3f µs  neo=%.3f µs  delta=%+.3f ns  %s\n",
                            i,
                            edges_ng[i].cross_time * 1e6,
                            edges_neo[i].cross_time * 1e6,
                            t_diff * 1e9,
                            dir);
            }
            if (edge_pass) edge_pass = (worst_edge_ns < 50.0);
            std::printf("      Worst edge delta: %.1f ns — %s (threshold: 50 ns)\n\n",
                        worst_edge_ns, edge_pass ? "PASS" : "FAIL");
        } else {
            std::printf("      v(out) not found in both results\n\n");
        }

        // --- Secondary: DC port signals (tolerance comparison) ---
        // v(out) and v(inp) are switching signals — point-wise comparison
        // produces artifacts from sub-ns timing differences.
        std::printf("  [2] DC port signals:\n");
        TransientResult neo_ports, ng_ports;
        neo_ports.time = neo_result.time;
        ng_ports.time = ng_result.time;
        for (auto& sig : port_signals) {
            if (sig == "v(out)" || sig == "v(inp)") continue;
            neo_ports.voltages[sig] = neo_result.voltages.at(sig);
            ng_ports.voltages[sig] = ng_result.voltages.at(sig);
        }

        struct ToleranceLevel {
            const char* name;
            double rel;
            double abs;
        };
        ToleranceLevel levels[] = {
            {"Loose  (1%%, 10mV)",   1e-2, 1e-2},
            {"Medium (0.1%%, 1mV)",  1e-3, 1e-3},
            {"Tight  (0.01%%, 0.1mV)", 1e-4, 1e-4},
        };

        bool non_switch_pass = true;
        for (auto& tol : levels) {
            auto cmp = compare_transient(neo_ports, ng_ports, {tol.rel, tol.abs});
            std::printf("      %s: %s", tol.name, cmp.passed ? "PASS" : "FAIL");
            if (!cmp.passed)
                std::printf("  (worst: %s, error=%.6g)", cmp.worst_signal.c_str(), cmp.worst_error);
            std::printf("\n");
        }
        non_switch_pass = compare_transient(neo_ports, ng_ports, {1e-2, 1e-2}).passed;

        // --- Diagnostic: per-signal error table for port signals ---
        std::printf("\n  [3] Per-signal detail (port-level):\n");
        std::printf("      %-25s  %12s  %12s\n", "Signal", "Max Error", "Rel Error");
        std::printf("      %-25s  %12s  %12s\n", "-------------------------", "------------", "------------");

        for (auto& sig : port_signals) {
            auto& ng_v = ng_result.voltages.at(sig);
            double ng_max = 0.0;
            for (auto v : ng_v) ng_max = std::max(ng_max, std::abs(v));

            TransientResult neo_single, ng_single;
            neo_single.time = neo_result.time;
            neo_single.voltages[sig] = neo_result.voltages.at(sig);
            ng_single.time = ng_result.time;
            ng_single.voltages[sig] = ng_v;
            auto sig_cmp = compare_transient(neo_single, ng_single, {1e-10, 1e-15});

            std::printf("      %-25s  %12.6g  %12.6g\n",
                        sig.c_str(), sig_cmp.worst_error,
                        ng_max > 0 ? sig_cmp.worst_error / ng_max : 0.0);
        }

        // --- Informational: worst internal nodes (top 10) ---
        std::printf("\n  [4] Internal node errors (top 10, informational):\n");
        std::printf("      These differ due to ringing suppression (GEAR switching).\n");
        std::printf("      %-25s  %12s\n", "Signal", "Max Error");
        std::printf("      %-25s  %12s\n", "-------------------------", "------------");

        std::vector<std::pair<double, std::string>> internal_errors;
        for (auto& sig : internal_signals) {
            TransientResult neo_single, ng_single;
            neo_single.time = neo_result.time;
            neo_single.voltages[sig] = neo_result.voltages.at(sig);
            ng_single.time = ng_result.time;
            ng_single.voltages[sig] = ng_result.voltages.at(sig);
            auto sig_cmp = compare_transient(neo_single, ng_single, {1e-10, 1e-15});
            internal_errors.push_back({sig_cmp.worst_error, sig});
        }
        std::sort(internal_errors.rbegin(), internal_errors.rend());
        for (size_t i = 0; i < std::min<size_t>(10, internal_errors.size()); ++i) {
            std::printf("      %-25s  %12.6g\n",
                        internal_errors[i].second.c_str(),
                        internal_errors[i].first);
        }

        // --- Overall verdict ---
        std::printf("\n  === VERDICT ===\n");
        std::printf("  Edge timing (<50ns):        %s\n", edge_pass ? "PASS" : "FAIL");
        std::printf("  DC ports (1%%, 10mV):        %s\n", non_switch_pass ? "PASS" : "FAIL");

    } catch (const std::exception& e) {
        std::printf("  neospice: FAILED (%s)\n", e.what());
        return 1;
    }

    std::printf("\nDone.\n");
    return 0;
}
