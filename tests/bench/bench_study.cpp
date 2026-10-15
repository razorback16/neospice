// neospice side of the many-sample study benchmark (tools/run_study_benchmark.py).
// Runs Simulator::run_sweep over evenly spaced values and prints wall time.
// usage: bench_study netlist mode(param|device) names(comma) lo hi N workers probe out_values
#include "api/neospice.hpp"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <variant>
#include <vector>
using namespace neospice;
using clk = std::chrono::steady_clock;

static double probe_value(const SimulationResult& r, const std::string& probe) {
    if (auto* dc = std::get_if<DCResult>(&r.analysis)) return dc->voltage(probe);
    if (auto* ac = std::get_if<ACResult>(&r.analysis)) {
        const auto& v = ac->voltages.at("v(" + probe + ")");
        return std::abs(v.back());
    }
    if (auto* tr = std::get_if<TransientResult>(&r.analysis)) return tr->voltage(probe).back();
    return NAN;
}

int main(int argc, char** argv) {
    std::string file = argv[1], mode = argv[2], names = argv[3];
    double lo = std::atof(argv[4]), hi = std::atof(argv[5]);
    int n = std::atoi(argv[6]);
    unsigned workers = std::atoi(argv[7]);
    std::string probe = argv[8], out = argv[9];
    std::vector<std::string> keys;
    std::stringstream ss(names);
    for (std::string k; std::getline(ss, k, ',');) keys.push_back(k);

    std::vector<SweepPoint> pts(n);
    for (int i = 0; i < n; ++i) {
        double v = lo + (hi - lo) * i / std::max(1, n - 1);
        for (auto& k : keys)
            (mode == "param" ? pts[i].parameters : pts[i].device_values)[k] = v;
    }
    SweepOptions o;
    o.workers = workers;
    Simulator sim;
    auto a = clk::now();
    auto res = sim.run_sweep(file, pts, o, true);
    auto b = clk::now();
    double ms = std::chrono::duration<double, std::milli>(b - a).count();
    int fails = 0;
    std::ofstream f(out);
    f.precision(17);
    for (auto& s : res.samples) {
        if (!s.error.empty()) { ++fails; f << "nan\n"; continue; }
        f << probe_value(*s.result, probe) << "\n";
    }
    std::printf("{\"wall_ms\": %.3f, \"samples\": %d, \"workers\": %u, \"fails\": %d}\n",
                ms, n, res.workers_used, fails);
}
