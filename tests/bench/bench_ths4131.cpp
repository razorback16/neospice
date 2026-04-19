// Performance benchmark: neospice vs ngspice on THS4131 diff amp.
// Measures parse, DC operating point, and AC analysis wall-clock time.

#include "api/neospice.hpp"
#include "framework/ngspice_runner.hpp"

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>
#include <numeric>
#include <cmath>

using namespace neospice;
using Clock = std::chrono::high_resolution_clock;

struct TimingStats {
    double median_us;
    double min_us;
    double max_us;
    double mean_us;
    int runs;
};

TimingStats compute_stats(std::vector<double>& samples) {
    std::sort(samples.begin(), samples.end());
    int n = static_cast<int>(samples.size());
    double med = (n % 2 == 0)
        ? (samples[n/2 - 1] + samples[n/2]) / 2.0
        : samples[n/2];
    double sum = std::accumulate(samples.begin(), samples.end(), 0.0);
    return {med, samples.front(), samples.back(), sum / n, n};
}

void print_row(const char* label, TimingStats s) {
    if (s.median_us < 1000.0) {
        std::printf("  %-35s %8.0f µs  (min=%6.0f  max=%6.0f  n=%d)\n",
                    label, s.median_us, s.min_us, s.max_us, s.runs);
    } else {
        std::printf("  %-35s %8.2f ms  (min=%6.2f  max=%6.2f  n=%d)\n",
                    label, s.median_us / 1000.0, s.min_us / 1000.0,
                    s.max_us / 1000.0, s.runs);
    }
}

int main() {
    const std::string cir_path = std::string(TEST_CIRCUITS_DIR) + "/ths4131_diff_amp.cir";
    const int WARMUP = 5;
    const int RUNS = 50;

    std::printf("=== THS4131 Fully-Differential Op-Amp Benchmark ===\n");
    std::printf("Circuit: ths4131_diff_amp.cir (14 BJTs, 3 model types)\n");
    std::printf("Warmup: %d, Measured runs: %d\n\n", WARMUP, RUNS);

    // Print circuit stats from a single load
    {
        Simulator sim;
        auto ckt = sim.load(cir_path);
        std::printf("Circuit stats:\n");
        std::printf("  Nodes: %d, MNA variables: %d, Devices: %zu\n",
                    ckt.num_nodes(), ckt.num_vars(), ckt.devices().size());
        std::printf("  Matrix NNZ: %d (sparsity pattern)\n",
                    static_cast<int>(ckt.pattern().nnz()));
        std::printf("  AC sweep: DEC 10, 1 Hz - 100 MHz (81 points)\n\n");
    }

    double neo_parse_us = 0, neo_dc_us = 0, neo_ac_us = 0, neo_full_us = 0;
    double ng_full_us = 0;

    // --- neospice: parse/load only ---
    {
        Simulator sim;
        std::vector<double> times;
        for (int i = 0; i < WARMUP; ++i) sim.load(cir_path);
        for (int i = 0; i < RUNS; ++i) {
            auto t0 = Clock::now();
            auto ckt = sim.load(cir_path);
            auto t1 = Clock::now();
            times.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
        }
        auto s = compute_stats(times);
        neo_parse_us = s.median_us;
        print_row("neospice parse+expand", s);
    }

    // --- neospice: DC only ---
    {
        Simulator sim;
        std::vector<double> times;
        for (int i = 0; i < WARMUP; ++i) {
            auto ckt = sim.load(cir_path);
            sim.run_dc(ckt);
        }
        for (int i = 0; i < RUNS; ++i) {
            auto ckt = sim.load(cir_path);
            auto t0 = Clock::now();
            sim.run_dc(ckt);
            auto t1 = Clock::now();
            times.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
        }
        auto s = compute_stats(times);
        neo_dc_us = s.median_us;
        print_row("neospice DC (.op)", s);
    }

    // --- neospice: AC only (after DC) ---
    {
        Simulator sim;
        std::vector<double> times;
        for (int i = 0; i < WARMUP; ++i) {
            auto ckt = sim.load(cir_path);
            sim.run_dc(ckt);
            sim.run_ac(ckt, AnalysisCommand::DEC, 10, 1.0, 100e6);
        }
        for (int i = 0; i < RUNS; ++i) {
            auto ckt = sim.load(cir_path);
            sim.run_dc(ckt);
            auto t0 = Clock::now();
            sim.run_ac(ckt, AnalysisCommand::DEC, 10, 1.0, 100e6);
            auto t1 = Clock::now();
            times.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
        }
        auto s = compute_stats(times);
        neo_ac_us = s.median_us;
        print_row("neospice AC (81 freq points)", s);
    }

    // --- neospice: full end-to-end ---
    {
        Simulator sim;
        std::vector<double> times;
        for (int i = 0; i < WARMUP; ++i) {
            auto ckt = sim.load(cir_path);
            sim.run(ckt);
        }
        for (int i = 0; i < RUNS; ++i) {
            auto t0 = Clock::now();
            auto ckt = sim.load(cir_path);
            sim.run(ckt);
            auto t1 = Clock::now();
            times.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
        }
        auto s = compute_stats(times);
        neo_full_us = s.median_us;
        print_row("neospice TOTAL (parse+DC+AC)", s);
    }

    std::printf("\n");

    // --- ngspice: full batch ---
    {
        NgspiceRunner ng(NGSPICE_BINARY);
        std::vector<double> times;
        for (int i = 0; i < WARMUP; ++i) {
            try { ng.run_dc(cir_path); } catch (...) {}
        }
        for (int i = 0; i < RUNS; ++i) {
            auto t0 = Clock::now();
            try {
                ng.run_dc(cir_path);
            } catch (...) {
                continue;
            }
            auto t1 = Clock::now();
            times.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
        }
        if (!times.empty()) {
            auto s = compute_stats(times);
            ng_full_us = s.median_us;
            print_row("ngspice TOTAL (batch: fork+parse+sim+raw)", s);
        } else {
            std::printf("  ngspice: all runs failed\n");
        }
    }

    // --- neospice AC phase breakdown ---
    {
        Simulator sim;
        auto ckt = sim.load(cir_path);
        sim.run_dc(ckt);

        auto t_total_start = Clock::now();
        auto ac = sim.run_ac(ckt, AnalysisCommand::DEC, 10, 1.0, 100e6);
        auto t_total_end = Clock::now();

        double total_us = std::chrono::duration<double, std::micro>(
            t_total_end - t_total_start).count();
        int nfreq = static_cast<int>(ac.frequency.size());
        std::printf("\n  AC phase breakdown (single run):\n");
        std::printf("    Total AC: %.0f µs for %d freq points (%.1f µs/point)\n",
                    total_us, nfreq, total_us / nfreq);
    }

    // --- Summary ---
    std::printf("\n--- Summary ---\n");
    std::printf("  neospice breakdown:  parse %.0f µs + DC %.0f µs + AC %.0f µs = %.2f ms\n",
                neo_parse_us, neo_dc_us, neo_ac_us,
                (neo_parse_us + neo_dc_us + neo_ac_us) / 1000.0);
    std::printf("  neospice end-to-end: %.2f ms\n", neo_full_us / 1000.0);
    std::printf("  ngspice  end-to-end: %.2f ms\n", ng_full_us / 1000.0);
    if (ng_full_us > 0 && neo_full_us > 0) {
        double ratio = ng_full_us / neo_full_us;
        if (ratio >= 1.0) {
            std::printf("  neospice is %.1fx faster than ngspice\n", ratio);
        } else {
            std::printf("  ngspice is %.1fx faster than neospice\n", 1.0 / ratio);
        }
    }
    std::printf("\n  Note: ngspice timing includes process fork/exec + .raw file I/O.\n");
    std::printf("        Both include netlist parsing and subcircuit expansion.\n");

    // =====================================================================
    // Multi-density sweep comparison
    // =====================================================================
    std::printf("\n=== Multi-Density AC Sweep Comparison ===\n\n");

    struct SweepConfig {
        int npoints;
        double fstart;
        double fstop;
        const char* label;
        int freq_count;
        int runs;
    };
    SweepConfig sweeps[] = {
        {10,   1.0, 100e6,  "DEC 10,   1 Hz - 100 MHz",    81, 50},
        {100,  1.0, 100e6,  "DEC 100,  1 Hz - 100 MHz",   801, 50},
        {1000, 1.0, 100e6,  "DEC 1000, 1 Hz - 100 MHz",  8001, 20},
    };

    const char* ng_circuits[] = {
        "/ths4131_diff_amp.cir",
        "/ths4131_bench_medium.cir",
        "/ths4131_bench_large.cir",
    };

    std::printf("  %-36s %12s %12s %8s %12s %12s\n",
                "Sweep", "neospice", "ngspice", "Speedup", "neo/pt", "ng/pt");
    std::printf("  %-36s %12s %12s %8s %12s %12s\n",
                "-----", "--------", "-------", "-------", "------", "-----");

    for (int si = 0; si < 3; ++si) {
        auto& sw = sweeps[si];

        // --- neospice end-to-end (parse + DC + AC) ---
        {
            Simulator sim;
            std::vector<double> times;
            for (int i = 0; i < WARMUP; ++i) {
                auto ckt = sim.load(cir_path);
                sim.run_dc(ckt);
                sim.run_ac(ckt, AnalysisCommand::DEC, sw.npoints, sw.fstart, sw.fstop);
            }
            for (int i = 0; i < sw.runs; ++i) {
                auto t0 = Clock::now();
                auto ckt = sim.load(cir_path);
                sim.run_dc(ckt);
                sim.run_ac(ckt, AnalysisCommand::DEC, sw.npoints, sw.fstart, sw.fstop);
                auto t1 = Clock::now();
                times.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
            }
            auto neo_s = compute_stats(times);

            // --- ngspice end-to-end (fork + parse + sim + raw) ---
            double ng_median = 0;
            bool ng_ok = false;
            if (ng_circuits[si]) {
                std::string ng_cir = std::string(TEST_CIRCUITS_DIR) + ng_circuits[si];
                NgspiceRunner ng(NGSPICE_BINARY);
                std::vector<double> ng_times;
                for (int i = 0; i < WARMUP; ++i) {
                    try { ng.run_dc(ng_cir); } catch (...) {}
                }
                for (int i = 0; i < sw.runs; ++i) {
                    auto t0 = Clock::now();
                    try { ng.run_dc(ng_cir); } catch (...) { continue; }
                    auto t1 = Clock::now();
                    ng_times.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
                }
                if (!ng_times.empty()) {
                    auto ng_s = compute_stats(ng_times);
                    ng_median = ng_s.median_us;
                    ng_ok = true;
                }
            }

            double neo_ms = neo_s.median_us / 1000.0;
            double neo_per_pt = neo_s.median_us / sw.freq_count;

            if (ng_ok) {
                double ng_ms = ng_median / 1000.0;
                double ng_per_pt = ng_median / sw.freq_count;
                double speedup = ng_median / neo_s.median_us;
                std::printf("  %-36s %9.2f ms %9.2f ms %7.1fx %9.1f µs %9.1f µs\n",
                            sw.label, neo_ms, ng_ms, speedup, neo_per_pt, ng_per_pt);
            } else {
                std::printf("  %-36s %9.2f ms %12s %8s %9.1f µs %12s\n",
                            sw.label, neo_ms, "N/A", "N/A", neo_per_pt, "N/A");
            }
        }
    }

    std::printf("\n  Note: neospice = in-process (parse+DC+AC). ngspice = subprocess (fork+parse+sim+RAW I/O).\n");

    return 0;
}
