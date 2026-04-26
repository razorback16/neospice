#include "core/convergence.hpp"
#include "core/circuit.hpp"
#include "core/linear_solver.hpp"
#include "devices/vsource.hpp"
#include "devices/isource.hpp"
#include <utility>
#include <vector>

namespace neospice {

NewtonResult gmin_stepping(Circuit& ckt, LinearSolver& solver,
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

NewtonResult source_stepping(Circuit& ckt, LinearSolver& solver,
                             std::vector<double>& solution,
                             const SimOptions& opts) {
    // Collect all independent sources and save their original DC values
    struct SourceInfo {
        VSource* vs = nullptr;
        ISource* is = nullptr;
        double   original_dc = 0.0;
    };
    std::vector<SourceInfo> sources;

    for (auto& dev : ckt.devices()) {
        if (auto* vs = dynamic_cast<VSource*>(dev.get())) {
            sources.push_back({vs, nullptr, vs->dc_value()});
        } else if (auto* is = dynamic_cast<ISource*>(dev.get())) {
            sources.push_back({nullptr, is, is->dc_value()});
        }
    }

    // RAII guard: always restore original DC values on exit
    struct SourceRestorer {
        std::vector<SourceInfo>& srcs;
        ~SourceRestorer() {
            for (auto& s : srcs) {
                if (s.vs) s.vs->set_dc_value(s.original_dc);
                if (s.is) s.is->set_dc_value(s.original_dc);
            }
        }
    } restorer{sources};

    // Helper to scale all sources to a given fraction of their original values
    auto scale_sources = [&](double frac) {
        for (auto& s : sources) {
            if (s.vs) s.vs->set_dc_value(frac * s.original_dc);
            if (s.is) s.is->set_dc_value(frac * s.original_dc);
        }
    };

    // Source stepping with adaptive refinement
    double fraction = 0.0;
    double step = 0.1;
    const double min_step = 1e-4;  // minimum step size before giving up

    // Start with all sources at zero and solve
    scale_sources(0.0);
    auto result = newton_solve(ckt, solver, solution, opts);
    if (!result.converged) {
        return {false, 0, solution};
    }
    solution = result.solution;

    while (fraction < 1.0) {
        double next_frac = fraction + step;
        if (next_frac > 1.0) next_frac = 1.0;

        scale_sources(next_frac);
        result = newton_solve(ckt, solver, solution, opts);

        if (result.converged) {
            solution = result.solution;
            fraction = next_frac;
            // If we had reduced the step, try growing it back (up to 0.1)
            if (step < 0.1) step *= 2.0;
            if (step > 0.1) step = 0.1;
        } else {
            // Halve step and retry from last successful fraction
            step *= 0.5;
            if (step < min_step) {
                // Cannot refine further — source stepping failed
                return {false, 0, solution};
            }
        }
    }

    // fraction == 1.0 and sources are at their original values; the restorer
    // will write them again on exit, which is harmless.
    return result;
}

NewtonResult pseudo_transient(Circuit& ckt, LinearSolver& solver,
                              std::vector<double>& solution,
                              const SimOptions& opts) {
    // Pseudo-transient continuation: add a fictitious conductance
    // G_pseudo = C_pseudo / dt_pseudo to every diagonal, effectively turning
    // the DC problem into a transient that can be solved incrementally.
    // As dt_pseudo grows, G_pseudo decays to zero and the solution converges
    // to the true DC operating point.

    const double C_pseudo = 1e-3;       // fictitious capacitance (F)
    double dt_pseudo      = 1e-6;       // initial pseudo-timestep (s)
    const int max_steps   = 200;
    const double target_gmin = opts.gmin;

    SimOptions step_opts = opts;

    for (int step = 0; step < max_steps; ++step) {
        double G_pseudo = C_pseudo / dt_pseudo;
        step_opts.gmin = target_gmin + G_pseudo;

        auto result = newton_solve(ckt, solver, solution, step_opts);

        if (result.converged) {
            solution = result.solution;
            dt_pseudo *= 2.0;  // grow pseudo-timestep (decay G_pseudo)

            // When the pseudo-capacitor conductance is negligible compared
            // to the baseline gmin, the solution is essentially the true DC
            // operating point — break and confirm with a final solve.
            if (G_pseudo < target_gmin * 0.01) {
                break;
            }
        } else {
            dt_pseudo *= 0.5;  // shrink on failure
            if (dt_pseudo < 1e-15) {
                return {false, 0, solution};
            }
        }
    }

    // Final solve with the true gmin (no pseudo-capacitor contribution)
    step_opts.gmin = target_gmin;
    auto result = newton_solve(ckt, solver, solution, step_opts);
    if (result.converged) {
        return result;
    }
    return {false, 0, solution};
}

} // namespace neospice
