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
                           const SimOptions& opts) {
    const std::vector<double> entry_solution = solution;
    const StateCheckpoint entry_state = save_state(ckt);

    // ngspice targets MAX(CKTgmin, CKTgshunt) -- step diag_gmin down to device
    // gmin level, then clear it (final solve uses gshunt).
    const double target_gmin = std::max(opts.gmin, opts.gshunt);
    double gmin = std::max(1.0, target_gmin);
    double factor = 10.0;
    double accepted_gmin = gmin;
    int total_iterations = 0;
    bool have_accepted = false;
    double last_residual = 0.0;
    int32_t last_worst_idx = -1;
    SimOptions step_opts = opts;
    step_opts.max_iter = std::min(opts.max_iter, 50);

    solution.assign(solution.size(), 0.0);
    clear_state(ckt);

    std::vector<double> accepted_solution = solution;
    StateCheckpoint accepted_state = save_state(ckt);

    for (int step = 0; step < 80 && gmin >= target_gmin; ++step) {
        step_opts.diag_gmin = gmin;
        NewtonResult result;
        try {
            result = newton_solve(ckt, solver, solution, step_opts);
        } catch (const std::runtime_error&) {
            result.converged = false;
        }

        last_residual = result.residual;
        last_worst_idx = result.worst_node_idx;

        if (!result.converged) {
            if (!have_accepted) {
                if (gmin < 1e6) {
                    gmin *= 10.0;
                    solution.assign(solution.size(), 0.0);
                    clear_state(ckt);
                    continue;
                }
                solution = entry_solution;
                restore_state(ckt, entry_state);
                return {false, total_iterations, solution, last_residual, last_worst_idx};
            }

            solution = accepted_solution;
            restore_state(ckt, accepted_state);
            // A failed continuation point usually means the previous gmin
            // drop crossed a sharp nonlinear transition. Retry near the last
            // accepted point instead of spending full Newton solves on the
            // lower half of that interval.
            factor = std::min(1.25, std::sqrt(factor));
            if (factor < 1.05) {
                return {false, total_iterations, solution, last_residual, last_worst_idx};
            }
            gmin = std::max(target_gmin, accepted_gmin / factor);
            continue;
        }

        total_iterations += result.iterations;
        solution = result.solution;
        accepted_solution = solution;
        accepted_state = save_state(ckt);
        accepted_gmin = gmin;
        have_accepted = true;

        if (gmin <= target_gmin) {
            result.iterations = total_iterations;
            result.solution = solution;
            return result;
        }

        if (result.iterations < opts.max_iter / 4) {
            factor = std::min(100.0, factor * 1.5);
        } else if (result.iterations > opts.max_iter / 2) {
            factor = std::max(1.05, std::sqrt(factor));
        }
        gmin = std::max(target_gmin, gmin / factor);
    }

    if (have_accepted) {
        solution = accepted_solution;
        restore_state(ckt, accepted_state);
    } else {
        solution = entry_solution;
        restore_state(ckt, entry_state);
    }
    return {false, total_iterations, solution, last_residual, last_worst_idx};
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
    double step = 0.05;
    const double min_step = 1e-4;  // minimum step size before giving up
    int total_iterations = 0;
    double last_residual = 0.0;
    int32_t last_worst_idx = -1;

    // Start with all sources at zero and solve
    scale_sources(0.0);
    solution.assign(solution.size(), 0.0);
    clear_state(ckt);
    NewtonResult result;
    try {
        result = newton_solve(ckt, solver, solution, opts);
    } catch (const std::runtime_error&) {
        return {false, 0, solution, 0.0, -1};
    }
    if (!result.converged) {
        return {false, 0, solution, result.residual, result.worst_node_idx};
    }
    total_iterations += result.iterations;
    solution = result.solution;
    std::vector<double> accepted_solution = solution;
    StateCheckpoint accepted_state = save_state(ckt);

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
            solution = result.solution;
            fraction = next_frac;
            accepted_solution = solution;
            accepted_state = save_state(ckt);
            if (result.iterations < opts.max_iter / 4) {
                step = std::min(0.1, step * 1.5);
            } else if (result.iterations > opts.max_iter / 2) {
                step = std::max(min_step, step * 0.5);
            }
        } else {
            solution = accepted_solution;
            restore_state(ckt, accepted_state);
            scale_sources(fraction);
            step *= 0.1;
            if (step < min_step) {
                return {false, total_iterations, solution, last_residual, last_worst_idx};
            }
        }
    }

    // fraction == 1.0 and sources are at their original values; the restorer
    // will write them again on exit, which is harmless.
    result.iterations = total_iterations;
    result.solution = solution;
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
            solution = result.solution;
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
                return {false, 0, solution, result.residual, result.worst_node_idx};
            }
        }
    }

    // Final solve with the true diag_gmin (no pseudo-capacitor contribution)
    step_opts.diag_gmin = target_gmin;
    NewtonResult final_result;
    try {
        final_result = newton_solve(ckt, solver, solution, step_opts);
    } catch (const std::runtime_error&) {
        return {false, 0, solution, 0.0, -1};
    }
    if (final_result.converged) {
        return final_result;
    }
    return {false, 0, solution, final_result.residual, final_result.worst_node_idx};
}

} // namespace neospice
