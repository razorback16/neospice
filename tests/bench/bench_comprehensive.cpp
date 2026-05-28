// Comprehensive benchmark: neospice vs ngspice across all analysis types.
// Both simulators run in-process via shared library linkage.

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

void print_comparison(const char* label, TimingStats neo, TimingStats ng) {
    double ratio = ng.median_us / neo.median_us;
    const char* winner = (ratio >= 1.0) ? "neo" : " ng";
    double factor = (ratio >= 1.0) ? ratio : 1.0 / ratio;
    std::printf("  %-40s  %s  %s  %4.1fx %s\n",
                label, fmt_time(neo.median_us).c_str(),
                fmt_time(ng.median_us).c_str(), factor, winner);
}

// Run neospice timing
template<typename F>
TimingStats bench_neo(F&& fn, int warmup, int runs) {
    for (int i = 0; i < warmup; ++i) fn();
    std::vector<double> times;
    for (int i = 0; i < runs; ++i) {
        auto t0 = Clock::now();
        fn();
        auto t1 = Clock::now();
        times.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
    }
    return compute_stats(times);
}

// Run ngspice timing
template<typename F>
TimingStats bench_ng(NgspiceLib& ng, F&& fn, int warmup, int runs) {
    for (int i = 0; i < warmup; ++i) { fn(); ng.reset(); }
    std::vector<double> times;
    for (int i = 0; i < runs; ++i) {
        auto t0 = Clock::now();
        fn();
        auto t1 = Clock::now();
        ng.reset();
        times.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
    }
    return compute_stats(times);
}

int main() {
    const std::string circuits = std::string(TEST_CIRCUITS_DIR);
    const int W = 3;   // warmup
    const int R = 30;  // runs

    std::printf("=== Comprehensive neospice vs ngspice Benchmark ===\n");
    std::printf("Both simulators in-process. Warmup=%d, Runs=%d\n", W, R);
    std::printf("ngspice: system libngspice (Sparse 1.3 default; .options klu not used)\n");
    std::printf("neospice: NeoSolver (self-contained Sparse 1.3-compatible solver; no KLU)\n\n");

    NgspiceLib ng;
    Simulator sim;

    std::printf("  %-40s  %9s  %9s  %s\n", "Benchmark", "neospice", "ngspice", "Winner");
    std::printf("  %-40s  %9s  %9s  %s\n",
                "----------------------------------------",
                "---------", "---------", "----------");

    // =====================================================================
    // 1. PARSE + FINALIZE
    // =====================================================================
    {
        std::string cir = circuits + "/ths4131_diff_amp.cir";
        auto neo = bench_neo([&]{ sim.load(cir); }, W, R);
        auto ngs = bench_ng(ng, [&]{ ng.load_circuit(cir); }, W, R);
        print_comparison("Parse: THS4131 (77 nodes, 58 devs)", neo, ngs);
    }
    {
        std::string cir = circuits + "/resistor_divider.cir";
        auto neo = bench_neo([&]{ sim.load(cir); }, W, R);
        auto ngs = bench_ng(ng, [&]{ ng.load_circuit(cir); }, W, R);
        print_comparison("Parse: resistor divider (3 devs)", neo, ngs);
    }

    std::printf("\n");

    // =====================================================================
    // 2. DC OPERATING POINT
    // =====================================================================
    {
        std::string cir = circuits + "/ths4131_diff_amp.cir";
        auto neo = bench_neo([&]{
            auto ckt = sim.load(cir);
            sim.run_dc(ckt);
        }, W, R);
        auto ngs = bench_ng(ng, [&]{
            ng.load_circuit(cir);
            ng.op();
        }, W, R);
        print_comparison("DC OP: THS4131 (14 BJTs)", neo, ngs);
    }
    {
        std::string cir = circuits + "/resistor_divider.cir";
        auto neo = bench_neo([&]{
            auto ckt = sim.load(cir);
            sim.run_dc(ckt);
        }, W, R);
        auto ngs = bench_ng(ng, [&]{
            ng.load_circuit(cir);
            ng.op();
        }, W, R);
        print_comparison("DC OP: resistor divider", neo, ngs);
    }

    std::printf("\n");

    // =====================================================================
    // 3. AC ANALYSIS
    // =====================================================================
    {
        std::string cir = circuits + "/ths4131_diff_amp.cir";
        auto neo = bench_neo([&]{
            auto ckt = sim.load(cir);
            sim.run_dc(ckt);
            sim.run_ac(ckt, ACMode::DEC, 10, 1.0, 100e6);
        }, W, R);
        auto ngs = bench_ng(ng, [&]{
            ng.load_circuit(cir);
            ng.op();
            ng.ac("dec", 10, 1.0, 100e6);
        }, W, R);
        print_comparison("AC: THS4131 DEC10 (81 pts)", neo, ngs);
    }
    {
        std::string cir = circuits + "/ths4131_diff_amp.cir";
        auto neo = bench_neo([&]{
            auto ckt = sim.load(cir);
            sim.run_dc(ckt);
            sim.run_ac(ckt, ACMode::DEC, 1000, 1.0, 100e6);
        }, W, R);
        auto ngs = bench_ng(ng, [&]{
            ng.load_circuit(cir);
            ng.op();
            ng.ac("dec", 1000, 1.0, 100e6);
        }, W, 10);
        print_comparison("AC: THS4131 DEC1000 (8001 pts)", neo, ngs);
    }
    {
        std::string cir = circuits + "/rc_ac.cir";
        auto neo = bench_neo([&]{
            auto ckt = sim.load(cir);
            sim.run_dc(ckt);
            sim.run_ac(ckt, ACMode::DEC, 10, 1.0, 1e9);
        }, W, R);
        auto ngs = bench_ng(ng, [&]{
            ng.load_circuit(cir);
            ng.op();
            ng.ac("dec", 10, 1.0, 1e9);
        }, W, R);
        print_comparison("AC: RC lowpass DEC10 (91 pts)", neo, ngs);
    }

    std::printf("\n");

    // =====================================================================
    // 4. TRANSIENT ANALYSIS
    // =====================================================================
    {
        std::string cir = circuits + "/rc_lowpass.cir";
        auto neo = bench_neo([&]{
            auto ckt = sim.load(cir);
            sim.run_transient(ckt, 1e-6, 5e-4);
        }, W, R);
        auto ngs = bench_ng(ng, [&]{
            ng.load_circuit(cir);
            ng.tran(1e-6, 5e-4);
        }, W, R);
        print_comparison("Tran: RC lowpass 500µs", neo, ngs);
    }
    {
        std::string cir = circuits + "/rlc_series.cir";
        auto neo = bench_neo([&]{
            auto ckt = sim.load(cir);
            sim.run_transient(ckt, 1e-7, 1e-4);
        }, W, R);
        auto ngs = bench_ng(ng, [&]{
            ng.load_circuit(cir);
            ng.tran(1e-7, 1e-4);
        }, W, R);
        print_comparison("Tran: RLC series 100µs", neo, ngs);
    }
    {
        std::string cir = circuits + "/pulse_defaults.cir";
        auto neo = bench_neo([&]{
            auto ckt = sim.load(cir);
            sim.run_transient(ckt, 1e-7, 1e-4);
        }, W, R);
        auto ngs = bench_ng(ng, [&]{
            ng.load_circuit(cir);
            ng.tran(1e-7, 1e-4);
        }, W, R);
        print_comparison("Tran: pulse source 100µs", neo, ngs);
    }

    std::printf("\n");

    // =====================================================================
    // 5. NOISE ANALYSIS
    // =====================================================================
    {
        std::string cir = circuits + "/resistor_divider_noise.cir";
        auto neo = bench_neo([&]{
            auto ckt = sim.load(cir);
            sim.run_noise(ckt, "out", "v1", ACMode::DEC, 10, 1.0, 1e9);
        }, W, R);
        auto ngs = bench_ng(ng, [&]{
            ng.load_circuit(cir);
            ng.command("run");
        }, W, R);
        print_comparison("Noise: R divider DEC10 (91 pts)", neo, ngs);
    }

    std::printf("\n");

    // =====================================================================
    // 6. DC SWEEP
    // =====================================================================
    {
        std::string cir = circuits + "/resistor_divider.cir";
        auto neo = bench_neo([&]{
            auto ckt = sim.load(cir);
            std::vector<DCSweepParam> params = {{"v1", -5, 5, 0.01}};
            sim.run_dc_sweep(ckt, params);
        }, W, R);
        auto ngs = bench_ng(ng, [&]{
            ng.load_circuit(cir);
            ng.command("dc v1 -5 5 0.01");
        }, W, R);
        print_comparison("DC Sweep: V1 -5..5 (1001 pts)", neo, ngs);
    }

    std::printf("\n");

    // =====================================================================
    // 7. END-TO-END (parse + all analyses in netlist)
    // =====================================================================
    {
        std::string cir = circuits + "/ths4131_diff_amp.cir";
        auto neo = bench_neo([&]{
            auto ckt = sim.load(cir);
            sim.run(ckt);
        }, W, R);
        auto ngs = bench_ng(ng, [&]{
            ng.load_circuit(cir);
            ng.command("run");
        }, W, R);
        print_comparison("E2E: THS4131 (.op + .ac dec 10)", neo, ngs);
    }
    {
        std::string cir = circuits + "/opa1632_test.cir";
        auto neo = bench_neo([&]{
            auto ckt = sim.load(cir);
            sim.run(ckt);
        }, W, R);
        auto ngs = bench_ng(ng, [&]{
            ng.load_circuit(cir);
            ng.command("run");
        }, W, R);
        print_comparison("E2E: OPA1632 (.op + .ac dec 10)", neo, ngs);
    }

    std::printf("\n  Legend: 'neo' = neospice faster, 'ng' = ngspice faster\n");

    return 0;
}
