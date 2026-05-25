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

NewtonResult true_gmin_stepping(Circuit& ckt, NeoSolver& solver,
                                std::vector<double>& solution,
                                const SimOptions& opts,
                                int firstmode, int continuemode) {
    const std::vector<double> entry_solution = solution;
    const StateCheckpoint entry_state = save_state(ckt);
    const double original_gmin = ckt.options.gmin;

    const double gmin_factor = 10.0;
    double factor = gmin_factor;
    double OldGmin = 1e-2;
    double current_gmin = OldGmin / factor;
    const double gtarget = original_gmin;

    int total_iterations = 0;
    double last_residual = 0.0;
    int32_t last_worst_idx = -1;
    bool success = false;
    bool failed = false;

    SimOptions step_opts = opts;
    step_opts.max_iter = std::min(opts.max_iter, 100);

    solution.assign(solution.size(), 0.0);
    clear_state(ckt);

    std::vector<double> saved_solution = solution;
    StateCheckpoint saved_state = save_state(ckt);

    ckt.integrator_ctx.mode = firstmode;

    while (!success && !failed) {
        // Publish the stepping gmin to ckt.options so devices see it
        // via tls_integrator_ctx->options->gmin.
        ckt.options.gmin = current_gmin;
        step_opts.gmin = current_gmin;

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
            ckt.integrator_ctx.mode = continuemode;

            if (current_gmin <= gtarget) {
                success = true;
            } else {
                saved_solution = solution;
                saved_state = save_state(ckt);

                if (iters <= step_opts.max_iter / 4) {
                    factor *= std::sqrt(factor);
                    if (factor > gmin_factor)
                        factor = gmin_factor;
                }
                if (iters > (3 * step_opts.max_iter / 4)) {
                    factor = std::sqrt(factor);
                }

                OldGmin = current_gmin;

                if (current_gmin < factor * gtarget) {
                    factor = current_gmin / gtarget;
                    current_gmin = gtarget;
                } else {
                    current_gmin /= factor;
                }
            }
        } else {
            if (factor < 1.00005) {
                failed = true;
            } else {
                factor = std::sqrt(std::sqrt(factor));
                current_gmin = OldGmin / factor;
                solution = saved_solution;
                restore_state(ckt, saved_state);
            }
        }
    }

    // Restore original gmin regardless of outcome
    ckt.options.gmin = original_gmin;

    if (success) {
        // Final verification at target gmin (already restored above)
        SimOptions final_opts = opts;
        NewtonResult final_result;
        try {
            final_result = newton_solve(ckt, solver, solution, final_opts);
        } catch (const std::runtime_error&) {
            final_result.converged = false;
        }
        if (!final_result.converged) {
            solution = entry_solution;
            restore_state(ckt, entry_state);
            return {false, total_iterations + final_result.iterations,
                    final_result.residual, final_result.worst_node_idx};
        }
        total_iterations += final_result.iterations;
        return {true, total_iterations, last_residual, last_worst_idx};
    }

    solution = entry_solution;
    restore_state(ckt, entry_state);
    return {false, total_iterations, last_residual, last_worst_idx};
}

NewtonResult source_stepping(Circuit& ckt, NeoSolver& solver,
                             std::vector<double>& solution,
                             const SimOptions& opts) {
    double fraction = 0.0;
    double step = 0.001;
    const double min_step = 1e-7;
    int total_iterations = 0;
    double last_residual = 0.0;
    int32_t last_worst_idx = -1;

    constexpr int MODEINITJCT_BIT   = 0x200;
    constexpr int MODEINITFLOAT_BIT = 0x100;
    constexpr int INITF_MASK        = 0x3F00;
    int base_mode = ckt.integrator_ctx.mode & ~INITF_MASK;
    ckt.integrator_ctx.mode = base_mode | MODEINITJCT_BIT;

    // Start with all sources at zero
    solution.assign(solution.size(), 0.0);
    clear_state(ckt);

    SimOptions step_opts = opts;
    step_opts.src_fact = 0.0;

    NewtonResult result;
    try {
        result = newton_solve(ckt, solver, solution, step_opts);
    } catch (const std::runtime_error&) {
        result.converged = false;
    }
    if (!result.converged) {
        // Try gmin stepping at zero sources
        double zg = std::max(opts.gmin, opts.gshunt);
        if (zg == 0.0) zg = opts.gmin;
        double diag = zg;
        for (int i = 0; i < 10; ++i) diag *= 10.0;
        solution.assign(solution.size(), 0.0);
        clear_state(ckt);
        ckt.integrator_ctx.mode = base_mode | MODEINITJCT_BIT;
        SimOptions zg_opts = step_opts;
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
            step_opts.src_fact = 1.0;
            return {false, total_iterations, result.residual, result.worst_node_idx};
        }
    }
    total_iterations += result.iterations;
    std::vector<double> accepted_solution = solution;
    StateCheckpoint accepted_state = save_state(ckt);

    ckt.integrator_ctx.mode = base_mode | MODEINITFLOAT_BIT;

    while (fraction < 1.0) {
        double next_frac = fraction + step;
        if (next_frac > 1.0) next_frac = 1.0;

        solution = accepted_solution;
        restore_state(ckt, accepted_state);

        step_opts.src_fact = next_frac;
        try {
            result = newton_solve(ckt, solver, solution, step_opts);
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
            step_opts.src_fact = fraction;
            step *= 0.1;
            if (step > 0.01) step = 0.01;
            if (step < min_step) {
                step_opts.src_fact = 1.0;
                return {false, total_iterations, last_residual, last_worst_idx};
            }
        }
    }

    step_opts.src_fact = 1.0;
    result.iterations = total_iterations;
    return result;
}

NewtonResult gain_stepping(Circuit& ckt, NeoSolver& solver,
                           std::vector<double>& solution,
                           const SimOptions& opts) {
    const double original_dep_src_fact = ckt.options.dep_src_fact;

    double fraction = 0.0;
    double step = 0.001;
    const double min_step = 1e-7;
    int total_iterations = 0;
    double last_residual = 0.0;
    int32_t last_worst_idx = -1;

    constexpr int MODEINITJCT_BIT   = 0x200;
    constexpr int MODEINITFLOAT_BIT = 0x100;
    constexpr int INITF_MASK        = 0x3F00;
    int base_mode = ckt.integrator_ctx.mode & ~INITF_MASK;
    ckt.integrator_ctx.mode = base_mode | MODEINITJCT_BIT;

    // Start with all dependent source gains at zero
    solution.assign(solution.size(), 0.0);
    clear_state(ckt);

    SimOptions step_opts = opts;
    step_opts.dep_src_fact = 0.0;
    ckt.options.dep_src_fact = 0.0;

    NewtonResult result;
    try {
        result = newton_solve(ckt, solver, solution, step_opts);
    } catch (const std::runtime_error&) {
        result.converged = false;
    }
    if (!result.converged) {
        // Try gmin stepping at zero gains
        double zg = std::max(opts.gmin, opts.gshunt);
        if (zg == 0.0) zg = opts.gmin;
        double diag = zg;
        for (int i = 0; i < 10; ++i) diag *= 10.0;
        solution.assign(solution.size(), 0.0);
        clear_state(ckt);
        ckt.integrator_ctx.mode = base_mode | MODEINITJCT_BIT;
        SimOptions zg_opts = step_opts;
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
            ckt.options.dep_src_fact = original_dep_src_fact;
            return {false, total_iterations, result.residual, result.worst_node_idx};
        }
    }
    total_iterations += result.iterations;
    std::vector<double> accepted_solution = solution;
    StateCheckpoint accepted_state = save_state(ckt);

    ckt.integrator_ctx.mode = base_mode | MODEINITFLOAT_BIT;

    while (fraction < 1.0) {
        double next_frac = fraction + step;
        if (next_frac > 1.0) next_frac = 1.0;

        solution = accepted_solution;
        restore_state(ckt, accepted_state);

        step_opts.dep_src_fact = next_frac;
        ckt.options.dep_src_fact = next_frac;
        try {
            result = newton_solve(ckt, solver, solution, step_opts);
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
            step_opts.dep_src_fact = fraction;
            ckt.options.dep_src_fact = fraction;
            step *= 0.1;
            if (step > 0.01) step = 0.01;
            if (step < min_step) {
                ckt.options.dep_src_fact = original_dep_src_fact;
                return {false, total_iterations, last_residual, last_worst_idx};
            }
        }
    }

    ckt.options.dep_src_fact = original_dep_src_fact;
    result.iterations = total_iterations;
    return result;
}

NewtonResult pseudo_transient(Circuit& ckt, NeoSolver& solver,
                              std::vector<double>& solution,
                              const SimOptions& opts) {
    const std::vector<double> entry_solution = solution;
    const StateCheckpoint entry_state = save_state(ckt);

    const double C_pseudo = 1e-3;
    double dt_pseudo      = 1e-6;
    const int max_steps   = 200;
    const double target_gmin = std::max(opts.gshunt, 0.0);
    double final_probe_g = std::max(1e-6, target_gmin * 1e6);

    SimOptions step_opts = opts;
    step_opts.max_iter = std::min(opts.max_iter, 100);

    solution.assign(solution.size(), 0.0);
    clear_state(ckt);

    constexpr int INITF_MASK        = 0x3F00;
    constexpr int MODEINITJCT_BIT   = 0x200;
    constexpr int MODEINITFLOAT_BIT = 0x100;
    int base_mode = ckt.integrator_ctx.mode & ~INITF_MASK;
    ckt.integrator_ctx.mode = base_mode | MODEINITJCT_BIT;

    int total_iterations = 0;
    double prev_residual = -1.0;

    std::vector<double> accepted_solution = solution;
    StateCheckpoint accepted_state = save_state(ckt);

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
            total_iterations += result.iterations;
            accepted_solution = solution;
            accepted_state = save_state(ckt);
            double curr_residual = result.residual;

            ckt.integrator_ctx.mode = base_mode | MODEINITFLOAT_BIT;

            if (prev_residual > 0.0 && curr_residual > 0.0) {
                double ratio = prev_residual / curr_residual;
                ratio = std::min(ratio, 4.0);
                ratio = std::max(ratio, 0.25);
                dt_pseudo *= ratio;
            } else {
                dt_pseudo *= 2.0;
            }
            dt_pseudo = std::max(dt_pseudo, 1e-15);
            dt_pseudo = std::min(dt_pseudo, 1e6);

            prev_residual = curr_residual;

            if (G_pseudo <= final_probe_g) {
                std::vector<double> probe_solution = solution;
                StateCheckpoint probe_state = save_state(ckt);
                step_opts.diag_gmin = target_gmin;
                NewtonResult final_result;
                try {
                    final_result = newton_solve(ckt, solver, solution, step_opts);
                } catch (const std::runtime_error&) {
                    final_result.converged = false;
                }
                if (final_result.converged) {
                    total_iterations += final_result.iterations;
                    final_result.iterations = total_iterations;
                    return final_result;
                }
                solution = probe_solution;
                restore_state(ckt, probe_state);
                final_probe_g *= 1e-3;
            }
        } else {
            solution = accepted_solution;
            restore_state(ckt, accepted_state);
            dt_pseudo /= 4.0;
            prev_residual = -1.0;
            if (dt_pseudo < 1e-15) {
                break;
            }
        }
    }

    // Final attempt at target gmin
    step_opts.diag_gmin = target_gmin;
    NewtonResult final_result;
    try {
        final_result = newton_solve(ckt, solver, solution, step_opts);
    } catch (const std::runtime_error&) {
        final_result.converged = false;
    }
    if (final_result.converged) {
        total_iterations += final_result.iterations;
        final_result.iterations = total_iterations;
        return final_result;
    }

    solution = entry_solution;
    restore_state(ckt, entry_state);
    return {false, total_iterations, final_result.residual, final_result.worst_node_idx};
}

} // namespace neospice
