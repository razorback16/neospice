// Profile where time is spent in THS4131 DC operating point.
// Instruments newton_solve phases: device eval, matrix factor, solve, convergence.

#include "api/neospice.hpp"
#include "core/circuit.hpp"
#include "core/neo_solver.hpp"
#include "core/newton.hpp"
#include "core/dc.hpp"

#include <chrono>
#include <cstdio>
#include <vector>

using namespace neospice;
using Clock = std::chrono::high_resolution_clock;

int main() {
    std::string cir_path = std::string(TEST_CIRCUITS_DIR) + "/ths4131_diff_amp.cir";

    Simulator sim;
    auto ckt = sim.load(cir_path);

    std::printf("=== THS4131 Newton Profile ===\n");
    std::printf("Nodes: %d, MNA vars: %d, Devices: %zu, NNZ: %d\n\n",
                ckt.num_nodes(), ckt.num_vars(), ckt.devices().size(),
                static_cast<int>(ckt.pattern().nnz()));

    // Warm up
    for (int w = 0; w < 3; w++) {
        auto c = sim.load(cir_path);
        sim.run_dc(c);
    }

    // Profile: run DC 100 times and accumulate phase timings
    const int RUNS = 100;
    double total_parse_us = 0, total_dc_us = 0;

    for (int r = 0; r < RUNS; r++) {
        auto t0 = Clock::now();
        auto c = sim.load(cir_path);
        auto t1 = Clock::now();
        sim.run_dc(c);
        auto t2 = Clock::now();

        total_parse_us += std::chrono::duration<double, std::micro>(t1 - t0).count();
        total_dc_us += std::chrono::duration<double, std::micro>(t2 - t1).count();
    }

    std::printf("Averaged over %d runs:\n", RUNS);
    std::printf("  Parse:    %8.1f µs\n", total_parse_us / RUNS);
    std::printf("  DC OP:    %8.1f µs\n", total_dc_us / RUNS);
    std::printf("  Total:    %8.1f µs\n", (total_parse_us + total_dc_us) / RUNS);

    // Now do a single instrumented DC run with verbose to count iterations
    std::printf("\n--- Single verbose DC run ---\n");
    {
        auto c = sim.load(cir_path);
        c.options.verbose = true;
        sim.run_dc(c);
    }

    // Profile the hot inner loop: device evaluation vs solver
    std::printf("\n--- Device evaluation vs Solver timing (single DC OP) ---\n");
    {
        auto c = sim.load(cir_path);
        const int32_t n = c.num_vars();
        const int32_t num_nodes = c.num_nodes();
        const auto& pattern = c.pattern();

        NeoSolver solver;
        solver.symbolic(pattern);

        NumericMatrix mat(pattern);
        std::vector<double> rhs(n, 0.0);
        std::vector<double> solution(n, 0.0);
        std::vector<double> old_solution(n, 0.0);

        // Set DC OP mode with junction init
        c.integrator_ctx.mode = 0x10 | 0x200;  // MODEDCOP | MODEINITJCT

        double eval_us = 0, stamp_gmin_us = 0, factor_us = 0, solve_us = 0;
        double clear_us = 0, limit_us = 0, conv_us = 0, copy_us = 0;
        int iters = 0;

        for (int iter = 0; iter < 100; iter++) {
            iters++;
            auto tc0 = Clock::now();
            old_solution = solution;
            mat.clear();
            std::fill(rhs.begin(), rhs.end(), 0.0);
            auto tc1 = Clock::now();

            // Evaluate devices
            struct IntegratorCtxGuard {
                IntegratorCtxGuard(const IntegratorCtx& ctx) { tls_integrator_ctx = &ctx; }
                ~IntegratorCtxGuard() { tls_integrator_ctx = nullptr; }
            } guard(c.integrator_ctx);

            auto te0 = Clock::now();
            for (auto& dev : c.devices()) {
                dev->evaluate(solution, mat, rhs);
            }
            auto te1 = Clock::now();

            // Gmin
            auto tg0 = Clock::now();
            double gmin = c.options.gmin;
            if (gmin != 0.0) {
                for (int32_t i = 0; i < num_nodes; i++) {
                    MatrixOffset off = pattern.offset(i, i);
                    mat.add(off, gmin);
                }
            }
            auto tg1 = Clock::now();

            // Factorize
            auto tf0 = Clock::now();
            if (iter == 0) {
                solver.numeric(pattern, mat);
            } else {
                solver.refactorize(mat);
            }
            auto tf1 = Clock::now();

            // Solve
            auto ts0 = Clock::now();
            solver.solve(rhs);
            auto ts1 = Clock::now();

            // Limit + update
            auto tl0 = Clock::now();
            for (auto& dev : c.devices()) {
                dev->limit_voltages(old_solution, rhs);
            }
            solution = rhs;
            auto tl1 = Clock::now();

            // Convergence check
            auto tk0 = Clock::now();
            bool converged = true;
            for (int32_t i = 0; i < n; i++) {
                double diff = std::abs(solution[i] - old_solution[i]);
                double tol = (i < num_nodes)
                    ? c.options.reltol * std::max(std::abs(solution[i]), std::abs(old_solution[i])) + c.options.vntol
                    : c.options.reltol * std::max(std::abs(solution[i]), std::abs(old_solution[i])) + c.options.abstol;
                if (diff > tol) { converged = false; break; }
            }
            auto tk1 = Clock::now();

            clear_us   += std::chrono::duration<double, std::micro>(tc1 - tc0).count();
            eval_us    += std::chrono::duration<double, std::micro>(te1 - te0).count();
            stamp_gmin_us += std::chrono::duration<double, std::micro>(tg1 - tg0).count();
            factor_us  += std::chrono::duration<double, std::micro>(tf1 - tf0).count();
            solve_us   += std::chrono::duration<double, std::micro>(ts1 - ts0).count();
            limit_us   += std::chrono::duration<double, std::micro>(tl1 - tl0).count();
            conv_us    += std::chrono::duration<double, std::micro>(tk1 - tk0).count();

            // Simplified: skip init-phase transitions for profiling
            if (converged && iter > 2) break;
        }

        double total = clear_us + eval_us + stamp_gmin_us + factor_us + solve_us + limit_us + conv_us;
        std::printf("\nIterations: %d\n", iters);
        std::printf("  %-20s %8.1f µs  (%4.1f%%)\n", "Clear mat+rhs", clear_us, 100.0*clear_us/total);
        std::printf("  %-20s %8.1f µs  (%4.1f%%)\n", "Device evaluate", eval_us, 100.0*eval_us/total);
        std::printf("  %-20s %8.1f µs  (%4.1f%%)\n", "Gmin stamp", stamp_gmin_us, 100.0*stamp_gmin_us/total);
        std::printf("  %-20s %8.1f µs  (%4.1f%%)\n", "Factor", factor_us, 100.0*factor_us/total);
        std::printf("  %-20s %8.1f µs  (%4.1f%%)\n", "Solve", solve_us, 100.0*solve_us/total);
        std::printf("  %-20s %8.1f µs  (%4.1f%%)\n", "Limit voltages", limit_us, 100.0*limit_us/total);
        std::printf("  %-20s %8.1f µs  (%4.1f%%)\n", "Convergence check", conv_us, 100.0*conv_us/total);
        std::printf("  %-20s %8.1f µs\n", "TOTAL", total);
        std::printf("\n  Per iteration:\n");
        std::printf("  %-20s %8.1f µs\n", "Device evaluate", eval_us / iters);
        std::printf("  %-20s %8.1f µs\n", "Factor", factor_us / iters);
        std::printf("  %-20s %8.1f µs\n", "Solve", solve_us / iters);
    }

    return 0;
}
