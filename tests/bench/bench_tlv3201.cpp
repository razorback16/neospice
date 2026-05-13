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

        // Find common voltage signals
        std::vector<std::string> common_signals;
        for (auto& [k, v] : neo_result.voltages) {
            if (ng_result.voltages.count(k))
                common_signals.push_back(k);
        }

        std::printf("  Common voltage signals: %zu\n", common_signals.size());

        // Compare with relaxed tolerance first, then tighten
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

        for (auto& tol : levels) {
            auto cmp = compare_transient(neo_result, ng_result, {tol.rel, tol.abs});
            std::printf("  %s: %s", tol.name, cmp.passed ? "PASS" : "FAIL");
            if (!cmp.passed)
                std::printf("  (worst: %s, error=%.6g)", cmp.worst_signal.c_str(), cmp.worst_error);
            std::printf("\n");
        }

        // Per-signal breakdown at medium tolerance
        std::printf("\n  Per-signal worst error (vs ngspice):\n");
        std::printf("  %-25s  %12s  %12s\n", "Signal", "Max Error", "Rel Error");
        std::printf("  %-25s  %12s  %12s\n", "-------------------------", "------------", "------------");

        for (auto& sig : common_signals) {
            auto& neo_v = neo_result.voltages.at(sig);
            auto& ng_v = ng_result.voltages.at(sig);

            // Simple max error over common time range
            size_t n = std::min(neo_v.size(), ng_v.size());
            n = std::min(n, neo_result.time.size());
            n = std::min(n, ng_result.time.size());

            double max_abs_err = 0.0;
            double max_rel_err = 0.0;
            double ng_max = 0.0;
            for (size_t i = 0; i < n; ++i)
                ng_max = std::max(ng_max, std::abs(ng_v[i]));

            // Use interpolation for fair comparison (different time grids)
            // Just use the comparator which already handles this
            CompareResult sig_cmp;
            TransientResult neo_single, ng_single;
            neo_single.time = neo_result.time;
            neo_single.voltages[sig] = neo_v;
            ng_single.time = ng_result.time;
            ng_single.voltages[sig] = ng_v;
            sig_cmp = compare_transient(neo_single, ng_single, {1e-10, 1e-15});

            std::printf("  %-25s  %12.6g  %12.6g\n",
                        sig.c_str(), sig_cmp.worst_error,
                        ng_max > 0 ? sig_cmp.worst_error / ng_max : 0.0);
        }

        // Edge comparison for output signal
        std::printf("\n  Output switching analysis:\n");
        if (neo_result.voltages.count("v(out)") && ng_result.voltages.count("v(out)")) {
            auto edges_ng = extract_edges(ng_result.time, ng_result.voltages.at("v(out)"),
                                           0.3, 3.0, 1e-6);
            auto edges_neo = extract_edges(neo_result.time, neo_result.voltages.at("v(out)"),
                                            0.3, 3.0, 1e-6);
            std::printf("  ngspice edges: %zu, neospice edges: %zu\n",
                        edges_ng.size(), edges_neo.size());

            size_t ne = std::min(edges_ng.size(), edges_neo.size());
            for (size_t i = 0; i < ne; ++i) {
                double t_diff = edges_neo[i].cross_time - edges_ng[i].cross_time;
                const char* dir = (edges_ng[i].rise_time >= 0) ? "RISING" : "FALLING";
                std::printf("    Edge %zu: ng=%.3f µs  neo=%.3f µs  delta=%.3f ns  %s\n",
                            i,
                            edges_ng[i].cross_time * 1e6,
                            edges_neo[i].cross_time * 1e6,
                            t_diff * 1e9,
                            dir);
            }
        } else {
            std::printf("  v(out) not found in both results\n");
        }

    } catch (const std::exception& e) {
        std::printf("  neospice: FAILED (%s)\n", e.what());
        return 1;
    }

    std::printf("\nDone.\n");
    return 0;
}
