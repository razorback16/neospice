// Performance benchmark: neospice vs ngspice (shared library) on THS4131.
// Both simulators run in-process — no subprocess fork, no file I/O.

#include "api/neospice.hpp"
#include "framework/ngspice_lib.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <string>
#include <vector>

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
    std::printf("Both simulators run in-process (no fork, no file I/O)\n");
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

    // Initialize ngspice shared library (system libngspice)
    NgspiceLib ng;

    // =====================================================================
    // Phase-by-phase: neospice
    // =====================================================================
    std::printf("--- neospice (native C++) ---\n");

    double neo_parse_us = 0, neo_dc_us = 0, neo_ac_us = 0, neo_full_us = 0;

    // Parse only
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
        print_row("parse + finalize", s);
    }

    // DC only
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
        print_row("DC operating point", s);
    }

    // AC only (after DC)
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
        print_row("AC sweep (81 points)", s);
    }

    // Full end-to-end
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
        print_row("TOTAL (parse+DC+AC)", s);
    }

    // =====================================================================
    // Phase-by-phase: ngspice (shared library, in-process)
    // =====================================================================
    std::printf("\n--- ngspice (shared library, in-process) ---\n");

    double ng_parse_us = 0, ng_dc_us = 0, ng_ac_us = 0, ng_full_us = 0;

    // Parse only
    {
        std::vector<double> times;
        for (int i = 0; i < WARMUP; ++i) {
            ng.load_circuit(cir_path);
            ng.reset();
        }
        for (int i = 0; i < RUNS; ++i) {
            auto t0 = Clock::now();
            ng.load_circuit(cir_path);
            auto t1 = Clock::now();
            ng.reset();
            times.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
        }
        auto s = compute_stats(times);
        ng_parse_us = s.median_us;
        print_row("parse + expand", s);
    }

    // DC only (source, then op)
    {
        std::vector<double> times;
        for (int i = 0; i < WARMUP; ++i) {
            ng.load_circuit(cir_path);
            ng.op();
            ng.reset();
        }
        for (int i = 0; i < RUNS; ++i) {
            ng.load_circuit(cir_path);
            auto t0 = Clock::now();
            ng.op();
            auto t1 = Clock::now();
            ng.reset();
            times.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
        }
        auto s = compute_stats(times);
        ng_dc_us = s.median_us;
        print_row("DC operating point", s);
    }

    // AC only (after DC)
    {
        std::vector<double> times;
        for (int i = 0; i < WARMUP; ++i) {
            ng.load_circuit(cir_path);
            ng.op();
            ng.ac("dec", 10, 1.0, 100e6);
            ng.reset();
        }
        for (int i = 0; i < RUNS; ++i) {
            ng.load_circuit(cir_path);
            ng.op();
            auto t0 = Clock::now();
            ng.ac("dec", 10, 1.0, 100e6);
            auto t1 = Clock::now();
            ng.reset();
            times.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
        }
        auto s = compute_stats(times);
        ng_ac_us = s.median_us;
        print_row("AC sweep (81 points)", s);
    }

    // Full end-to-end (source + run)
    {
        std::vector<double> times;
        for (int i = 0; i < WARMUP; ++i) {
            ng.load_circuit(cir_path);
            ng.command("run");
            ng.reset();
        }
        for (int i = 0; i < RUNS; ++i) {
            auto t0 = Clock::now();
            ng.load_circuit(cir_path);
            ng.command("run");
            auto t1 = Clock::now();
            ng.reset();
            times.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
        }
        auto s = compute_stats(times);
        ng_full_us = s.median_us;
        print_row("TOTAL (parse+DC+AC)", s);
    }

    // =====================================================================
    // Summary
    // =====================================================================
    std::printf("\n=== Summary ===\n");
    std::printf("  %-25s %12s %12s %8s\n", "Phase", "neospice", "ngspice", "Speedup");
    std::printf("  %-25s %12s %12s %8s\n", "-----", "--------", "-------", "-------");

    auto print_summary = [](const char* phase, double neo_us, double ng_us) {
        auto fmt_time = [](double us, char* buf, int sz) {
            if (us < 1000.0)
                std::snprintf(buf, sz, "%.0f µs", us);
            else
                std::snprintf(buf, sz, "%.2f ms", us / 1000.0);
        };
        char neo_buf[32], ng_buf[32];
        fmt_time(neo_us, neo_buf, sizeof(neo_buf));
        fmt_time(ng_us, ng_buf, sizeof(ng_buf));
        double speedup = ng_us / neo_us;
        if (speedup >= 1.0)
            std::printf("  %-25s %12s %12s %7.1fx\n", phase, neo_buf, ng_buf, speedup);
        else
            std::printf("  %-25s %12s %12s %6.1fx *\n", phase, neo_buf, ng_buf, 1.0/speedup);
    };

    print_summary("Parse", neo_parse_us, ng_parse_us);
    print_summary("DC (.op)", neo_dc_us, ng_dc_us);
    print_summary("AC (81 pts)", neo_ac_us, ng_ac_us);
    print_summary("End-to-end", neo_full_us, ng_full_us);

    std::printf("\n  (* = ngspice faster by that factor)\n");
    std::printf("  Both simulators run in-process. No fork/exec, no file I/O.\n");

    // =====================================================================
    // Multi-density AC sweep comparison
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

    std::printf("  %-36s %12s %12s %8s %12s %12s\n",
                "Sweep", "neospice", "ngspice", "Speedup", "neo/pt", "ng/pt");
    std::printf("  %-36s %12s %12s %8s %12s %12s\n",
                "-----", "--------", "-------", "-------", "------", "-----");

    for (auto& sw : sweeps) {
        // neospice: parse + DC + AC
        double neo_median_us;
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
            auto s = compute_stats(times);
            neo_median_us = s.median_us;
        }

        // ngspice: source + op + ac (in-process)
        double ng_median_us;
        {
            char ac_cmd[128];
            std::snprintf(ac_cmd, sizeof(ac_cmd), "ac dec %d %g %g",
                          sw.npoints, sw.fstart, sw.fstop);
            std::vector<double> times;
            for (int i = 0; i < WARMUP; ++i) {
                ng.load_circuit(cir_path);
                ng.op();
                ng.command(ac_cmd);
                ng.reset();
            }
            for (int i = 0; i < sw.runs; ++i) {
                auto t0 = Clock::now();
                ng.load_circuit(cir_path);
                ng.op();
                ng.command(ac_cmd);
                auto t1 = Clock::now();
                ng.reset();
                times.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
            }
            auto s = compute_stats(times);
            ng_median_us = s.median_us;
        }

        double neo_ms = neo_median_us / 1000.0;
        double ng_ms = ng_median_us / 1000.0;
        double neo_per_pt = neo_median_us / sw.freq_count;
        double ng_per_pt = ng_median_us / sw.freq_count;
        double speedup = ng_median_us / neo_median_us;

        if (speedup >= 1.0) {
            std::printf("  %-36s %9.2f ms %9.2f ms %7.1fx %9.1f µs %9.1f µs\n",
                        sw.label, neo_ms, ng_ms, speedup, neo_per_pt, ng_per_pt);
        } else {
            std::printf("  %-36s %9.2f ms %9.2f ms %6.1fx * %9.1f µs %9.1f µs\n",
                        sw.label, neo_ms, ng_ms, 1.0/speedup, neo_per_pt, ng_per_pt);
        }
    }

    std::printf("\n  Both simulators in-process. Timing includes parse + DC + AC.\n");
    std::printf("  (* = ngspice faster by that factor)\n");

    return 0;
}
