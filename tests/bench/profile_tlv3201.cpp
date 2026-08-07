#include "api/neospice.hpp"
#include <chrono>
#include <cstdio>
using namespace neospice;
using Clock = std::chrono::high_resolution_clock;

int main() {
    const std::string cir = std::string(TEST_CIRCUITS_DIR) + "/tlv3201_switching.cir";
    Simulator sim;
    
    // Warmup
    for (int i = 0; i < 3; i++) { auto c = sim.load(cir); sim.run(c); }
    
    // Timed run: total
    double total_us = 0;
    int N = 10;
    for (int i = 0; i < N; i++) {
        auto t0 = Clock::now();
        auto c = sim.load(cir);
        auto r = sim.run(c);
        auto t1 = Clock::now();
        total_us += std::chrono::duration<double, std::micro>(t1 - t0).count();
    }
    printf("Total (load+run): %.2f ms\n", total_us / N / 1000.0);
    
    // Timed run: load only
    double load_us = 0;
    for (int i = 0; i < N; i++) {
        auto t0 = Clock::now();
        auto c = sim.load(cir);
        auto t1 = Clock::now();
        load_us += std::chrono::duration<double, std::micro>(t1 - t0).count();
    }
    printf("Load only: %.2f ms\n", load_us / N / 1000.0);
    
    // Timed run: run only (pre-loaded)
    double run_us = 0;
    for (int i = 0; i < N; i++) {
        auto c = sim.load(cir);
        auto t0 = Clock::now();
        auto r = sim.run(c);
        auto t1 = Clock::now();
        run_us += std::chrono::duration<double, std::micro>(t1 - t0).count();
        auto& tr = std::get<TransientResult>(r.analysis);
        if (i == 0) printf("  Steps: %zu\n", tr.time.size());
    }
    printf("Run only (DC+tran): %.2f ms\n", run_us / N / 1000.0);
    
    return 0;
}
