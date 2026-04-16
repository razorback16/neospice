#include "core/convergence.hpp"
#include "core/circuit.hpp"
#include "core/klu_solver.hpp"

namespace neospice {

NewtonResult gmin_stepping(Circuit& ckt, KLUSolver& solver,
                           std::vector<double>& solution,
                           const SimOptions& opts) {
    // Start with a large gmin and reduce by 10x each step
    double gmin = 1e-2;
    const double target_gmin = opts.gmin;

    SimOptions step_opts = opts;

    while (gmin >= target_gmin) {
        step_opts.gmin = gmin;
        auto result = newton_solve(ckt, solver, solution, step_opts);
        if (!result.converged) {
            return {false, 0, solution};
        }
        solution = result.solution;

        if (gmin <= target_gmin) break;
        gmin *= 0.1;
        if (gmin < target_gmin) gmin = target_gmin;
    }

    // Final solve with the target gmin
    step_opts.gmin = target_gmin;
    auto result = newton_solve(ckt, solver, solution, step_opts);
    if (result.converged) {
        return result;
    }
    return {false, 0, solution};
}

NewtonResult source_stepping(Circuit& ckt, KLUSolver& solver,
                             std::vector<double>& solution,
                             const SimOptions& opts) {
    // Simple proxy: use gmin stepping with a different schedule
    // Full source scaling would require source value modification
    double gmin = 1e-1;
    const double target_gmin = opts.gmin;

    SimOptions step_opts = opts;

    while (gmin >= target_gmin) {
        step_opts.gmin = gmin;
        auto result = newton_solve(ckt, solver, solution, step_opts);
        if (!result.converged) {
            return {false, 0, solution};
        }
        solution = result.solution;

        if (gmin <= target_gmin) break;
        gmin *= 0.1;
        if (gmin < target_gmin) gmin = target_gmin;
    }

    step_opts.gmin = target_gmin;
    auto result = newton_solve(ckt, solver, solution, step_opts);
    if (result.converged) {
        return result;
    }
    return {false, 0, solution};
}

} // namespace neospice
