#include "core/convergence.hpp"
#include "core/circuit.hpp"
#include "core/neo_solver.hpp"
#include "devices/vsource.hpp"
#include "devices/isource.hpp"
#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace neospice {

namespace {

struct StateCheckpoint {
    std::vector<double> state0;
    std::vector<double> state1;
    std::vector<double> state2;
};

StateCheckpoint save_state(Circuit& ckt) {
    StateCheckpoint saved;
    const int32_t n = ckt.num_states();
    if (n > 0) {
        saved.state0.assign(ckt.state0(), ckt.state0() + n);
        saved.state1.assign(ckt.state1(), ckt.state1() + n);
        saved.state2.assign(ckt.state2(), ckt.state2() + n);
    }
    return saved;
}

void restore_state(Circuit& ckt, const StateCheckpoint& saved) {
    const int32_t n = ckt.num_states();
    if (n <= 0) return;
    std::copy_n(saved.state0.data(), n, ckt.state0());
    std::copy_n(saved.state1.data(), n, ckt.state1());
    std::copy_n(saved.state2.data(), n, ckt.state2());
}

void clear_state(Circuit& ckt) {
    const int32_t n = ckt.num_states();
    if (n <= 0) return;
    std::fill_n(ckt.state0(), n, 0.0);
    std::fill_n(ckt.state1(), n, 0.0);
    std::fill_n(ckt.state2(), n, 0.0);
}

} // namespace

NewtonResult gmin_stepping(Circuit& ckt, NeoSolver& solver,
                           std::vector<double>& solution,
                           const SimOptions& opts,
                           int firstmode, int continuemode) {
    const std::vector<double> entry_solution = solution;
    const StateCheckpoint entry_state = save_state(ckt);

    const double gmin_factor = 10.0;
    double factor = gmin_factor;
    double OldGmin = 1e-2;
    double diag_gmin = OldGmin / factor;  // starts at 1e-3
    const double gtarget = std::max(opts.gmin, opts.gshunt);

    int total_iterations = 0;
    double last_residual = 0.0;
    int32_t last_worst_idx = -1;
    bool success = false;
    bool failed = false;

    SimOptions step_opts = opts;
    step_opts.max_iter = std::min(opts.max_iter, 100);

    // Start from zero initial guess (matching ngspice)
    solution.assign(solution.size(), 0.0);
    clear_state(ckt);

    std::vector<double> saved_solution = solution;
    StateCheckpoint saved_state = save_state(ckt);

    // Set first mode (e.g. MODETRANOP|MODEINITJCT)
    ckt.integrator_ctx.mode = firstmode;

    while (!success && !failed) {
        step_opts.diag_gmin = diag_gmin;

        NewtonResult result;
        try {
            result = newton_solve(ckt, solver, solution, step_opts);
        } catch (const std::runtime_error&) {
            result.converged = false;
        }

        last_residual = result.residual;
        last_worst_idx = result.worst_node_idx;
        int iters = result.iterations;
        total_iterations += iters;

        if (result.converged) {
            // Switch to continuation mode (e.g. MODETRANOP|MODEINITFLOAT)
            ckt.integrator_ctx.mode = continuemode;

            if (diag_gmin <= gtarget) {
                success = true;
            } else {
                // Save accepted solution and state
                saved_solution = solution;
                saved_state = save_state(ckt);

                // Adapt factor based on iteration count (ngspice cktop.c:187-194)
                if (iters <= step_opts.max_iter / 4) {
                    factor *= std::sqrt(factor);
                    if (factor > gmin_factor)
                        factor = gmin_factor;
                }
                if (iters > (3 * step_opts.max_iter / 4)) {
                    factor = std::sqrt(factor);
                }

                OldGmin = diag_gmin;

                // Reduce diag_gmin, clamping to gtarget (ngspice cktop.c:198-203)
                if (diag_gmin < factor * gtarget) {
                    factor = diag_gmin / gtarget;
                    diag_gmin = gtarget;
                } else {
                    diag_gmin /= factor;
                }
            }
        } else {
            // Convergence failure at this gmin level
            if (factor < 1.00005) {
                failed = true;
            } else {
                // Reduce factor aggressiveness and retry (ngspice cktop.c:213-214)
                factor = std::sqrt(std::sqrt(factor));
                diag_gmin = OldGmin / factor;

                // Restore last accepted solution
                solution = saved_solution;
                restore_state(ckt, saved_state);
            }
        }
    }

    if (success) {
        // ngspice cktop.c:229+242 — final solve at true gmin (no artificial diagonal)
        // After gmin stepping converges, ngspice sets CKTdiagGmin = CKTgshunt
        // and runs one more NIiter to confirm the solution holds without the
        // artificial diagonal conductance.
        SimOptions final_opts = opts;
        final_opts.diag_gmin = std::max(opts.gshunt, 0.0);
        NewtonResult final_result;
        try {
            final_result = newton_solve(ckt, solver, solution, final_opts);
        } catch (const std::runtime_error&) {
            final_result.converged = false;
        }
        if (!final_result.converged) {
            // Final solve failed — gmin stepping didn't truly converge.
            // Restore entry state so the caller can try the next method.
            solution = entry_solution;
            restore_state(ckt, entry_state);
            return {false, total_iterations + final_result.iterations, final_result.residual, final_result.worst_node_idx};
        }
        total_iterations += final_result.iterations;
        return {true, total_iterations, last_residual, last_worst_idx};
    }

    // Failed — restore entry state
    solution = entry_solution;
    restore_state(ckt, entry_state);
    return {false, total_iterations, last_residual, last_worst_idx};
}

NewtonResult source_stepping(Circuit& ckt, NeoSolver& solver,
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

    // Source stepping with adaptive refinement and backtracking.
    double fraction = 0.0;
    double step = 0.001;          // ngspice gillespie_src starts at 0.001
    const double min_step = 1e-7;  // ngspice: raise >= 1e-7
    int total_iterations = 0;
    double last_residual = 0.0;
    int32_t last_worst_idx = -1;

    // Start with all sources at zero and solve
    constexpr int MODEINITJCT_BIT   = 0x200;
    constexpr int MODEINITFLOAT_BIT = 0x100;
    constexpr int INITF_MASK        = 0x3F00;
    int base_mode = ckt.integrator_ctx.mode & ~INITF_MASK;
    ckt.integrator_ctx.mode = base_mode | MODEINITJCT_BIT;

    scale_sources(0.0);
    solution.assign(solution.size(), 0.0);
    clear_state(ckt);
    NewtonResult result;
    try {
        result = newton_solve(ckt, solver, solution, opts);
    } catch (const std::runtime_error&) {
        result.converged = false;
    }
    if (!result.converged) {
        // ngspice gillespie_src: if zero-source solve fails, try gmin
        // stepping at zero sources before giving up (cktop.c:510-548).
        double zg = std::max(opts.gmin, opts.gshunt);
        if (zg == 0.0) zg = opts.gmin;
        double diag = zg;
        for (int i = 0; i < 10; ++i) diag *= 10.0;
        solution.assign(solution.size(), 0.0);
        clear_state(ckt);
        ckt.integrator_ctx.mode = base_mode | MODEINITJCT_BIT;
        SimOptions zg_opts = opts;
        bool zg_ok = false;
        for (int i = 0; i <= 10; ++i) {
            zg_opts.diag_gmin = diag;
            NewtonResult zr;
            try { zr = newton_solve(ckt, solver, solution, zg_opts); }
            catch (const std::runtime_error&) { zr.converged = false; }
            if (!zr.converged) break;
            total_iterations += zr.iterations;
            diag /= 10.0;
            ckt.integrator_ctx.mode = base_mode | MODEINITFLOAT_BIT;
            if (i == 10) zg_ok = true;
        }
        if (!zg_ok) {
            return {false, total_iterations, result.residual, result.worst_node_idx};
        }
    }
    total_iterations += result.iterations;
    std::vector<double> accepted_solution = solution;
    StateCheckpoint accepted_state = save_state(ckt);

    // Use continuation mode (MODEINITFLOAT) during the ramp steps
    ckt.integrator_ctx.mode = base_mode | MODEINITFLOAT_BIT;

    while (fraction < 1.0) {
        double next_frac = fraction + step;
        if (next_frac > 1.0) next_frac = 1.0;

        solution = accepted_solution;
        restore_state(ckt, accepted_state);
        scale_sources(next_frac);
        try {
            result = newton_solve(ckt, solver, solution, opts);
        } catch (const std::runtime_error&) {
            result.converged = false;
        }

        last_residual = result.residual;
        last_worst_idx = result.worst_node_idx;

        if (result.converged) {
            total_iterations += result.iterations;
            fraction = next_frac;
            accepted_solution = solution;
            accepted_state = save_state(ckt);
            if (result.iterations < opts.max_iter / 4) {
                step = std::min(0.1, step * 1.5);
            } else if (result.iterations > opts.max_iter / 2) {
                step = std::max(min_step, step * 0.5);
            }
        } else {
            if (step * (1.0 - fraction) < 1e-8)
                break;
            solution = accepted_solution;
            restore_state(ckt, accepted_state);
            scale_sources(fraction);
            step *= 0.1;
            if (step > 0.01) step = 0.01;
            if (step < min_step) {
                return {false, total_iterations, last_residual, last_worst_idx};
            }
        }
    }

    // fraction == 1.0 and sources are at their original values; the restorer
    // will write them again on exit, which is harmless.
    result.iterations = total_iterations;
    return result;
}

NewtonResult pseudo_transient(Circuit& ckt, NeoSolver& solver,
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
    const double target_gmin = opts.diag_gmin;
    double final_probe_g = std::max(1e-6, target_gmin * 1e6);

    SimOptions step_opts = opts;

    for (int step = 0; step < max_steps; ++step) {
        double G_pseudo = C_pseudo / dt_pseudo;
        step_opts.diag_gmin = target_gmin + G_pseudo;

        NewtonResult result;
        try {
            result = newton_solve(ckt, solver, solution, step_opts);
        } catch (const std::runtime_error&) {
            result.converged = false;
        }

        if (result.converged) {
            StateCheckpoint accepted_state = save_state(ckt);
            dt_pseudo *= 2.0;  // grow pseudo-timestep (decay G_pseudo)

            if (G_pseudo <= final_probe_g) {
                std::vector<double> accepted_solution = solution;
                step_opts.diag_gmin = target_gmin;
                NewtonResult final_result;
                try {
                    final_result = newton_solve(ckt, solver, solution, step_opts);
                } catch (const std::runtime_error&) {
                    final_result.converged = false;
                }
                if (final_result.converged) {
                    return final_result;
                }
                solution = accepted_solution;
                restore_state(ckt, accepted_state);
                final_probe_g *= 1e-3;
            }

            // When the pseudo-capacitor conductance is negligible compared
            // to the baseline gmin, the solution is essentially the true DC
            // operating point — break and confirm with a final solve.
            if (G_pseudo < target_gmin * 0.01) {
                break;
            }
        } else {
            dt_pseudo *= 0.5;  // shrink on failure
            if (dt_pseudo < 1e-15) {
                return {false, 0, result.residual, result.worst_node_idx};
            }
        }
    }

    // Final solve with the true diag_gmin (no pseudo-capacitor contribution)
    step_opts.diag_gmin = target_gmin;
    NewtonResult final_result;
    try {
        final_result = newton_solve(ckt, solver, solution, step_opts);
    } catch (const std::runtime_error&) {
        return {false, 0, 0.0, -1};
    }
    if (final_result.converged) {
        return final_result;
    }
    return {false, 0, final_result.residual, final_result.worst_node_idx};
}

} // namespace neospice
