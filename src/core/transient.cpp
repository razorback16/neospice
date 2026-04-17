#include "core/transient.hpp"
#include "core/newton.hpp"
#include "core/convergence.hpp"
#include "core/klu_solver.hpp"
#include "core/timestep.hpp"
#include "devices/vsource.hpp"
#include "devices/isource.hpp"
#include "devices/capacitor.hpp"
#include "devices/inductor.hpp"
#include <algorithm>
#include <cmath>

namespace neospice {

static std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

// Collect PULSE/SIN breakpoints from sources
static void collect_breakpoints(Circuit& ckt, TimeStepController& ctrl, double tstop) {
    for (auto& dev : ckt.devices()) {
        if (auto* vs = dynamic_cast<VSource*>(dev.get())) {
            auto bps = vs->get_breakpoints(0.0, tstop);
            for (double bp : bps) ctrl.add_breakpoint(bp);
        } else if (auto* is = dynamic_cast<ISource*>(dev.get())) {
            auto bps = is->get_breakpoints(0.0, tstop);
            for (double bp : bps) ctrl.add_breakpoint(bp);
        }
    }
}

TransientResult solve_transient(Circuit& ckt, double tstep, double tstop) {
    const int32_t n = ckt.num_vars();
    const int32_t num_nodes = ckt.num_nodes();

    // ---------------------------------------------------------------
    // 1. DC operating point
    // ---------------------------------------------------------------
    // Initial guess: zeros + .nodeset hints; .ic as fallback for unpinned nodes.
    // .nodeset wins when both are set.  .ic doubles as a Newton seed hint here
    // so circuits that ship .ic (ring oscillators, bistable latches) start DC
    // from a feasible point instead of all-zero (where subthreshold gm/gds
    // vanish).  The .ic overrides applied post-DC at line ~70 still take
    // effect as transient initial conditions — that loop is independent.
    std::vector<double> solution(n, 0.0);
    std::vector<char> pinned(n, 0);
    for (auto& [node_idx, value] : ckt.nodeset) {
        if (node_idx >= 0 && node_idx < n) {
            solution[node_idx] = value;
            pinned[node_idx] = 1;
        }
    }
    for (auto& [node_idx, value] : ckt.ic) {
        if (node_idx >= 0 && node_idx < n && !pinned[node_idx]) {
            solution[node_idx] = value;
        }
    }

    KLUSolver solver;
    solver.symbolic(ckt.pattern());

    // Publish SimOptions (temp, tolerances, gmin) so BSIM4v7Device can read
    // user-configured values from evaluate() via tls_integrator_ctx.
    ckt.integrator_ctx.options = &ckt.options;

    // DC preamble — mirror dc.cpp's integrator_ctx.mode sequence so BSIM4v7
    // (and any future state-storing device) sees MODEDC + MODEINITJCT/FIX
    // at the same phases of gmin/source stepping.  See dc.cpp for the bit
    // values and the ngspice cktdefs.h cross-reference.
    constexpr int MODEDC_BITS     = 0x70;
    constexpr int MODEINITJCT_BIT = 0x200;
    constexpr int MODEINITFIX_BIT = 0x400;

    ckt.integrator_ctx.mode = MODEDC_BITS | MODEINITJCT_BIT;
    auto result = newton_solve(ckt, solver, solution, ckt.options);
    if (result.converged) {
        solution = result.solution;
    } else {
        ckt.integrator_ctx.mode = MODEDC_BITS | MODEINITFIX_BIT;
        result = gmin_stepping(ckt, solver, solution, ckt.options);
        if (result.converged) {
            solution = result.solution;
        } else {
            ckt.integrator_ctx.mode = MODEDC_BITS | MODEINITFIX_BIT;
            result = source_stepping(ckt, solver, solution, ckt.options);
            if (result.converged) {
                solution = result.solution;
            } else {
                throw ConvergenceError("DC operating point failed to converge");
            }
        }
    }

    // ---------------------------------------------------------------
    // 2. Seed state1 with DC op-point
    // ---------------------------------------------------------------
    // BSIM4's MODEINITTRAN branch (bsim4v7_load.cpp ~line 264-276) reads
    // vds/vgs/vbs/... from CKTstate1 to seed the first transient Newton
    // iteration.  The DC preamble above wrote the converged op-point into
    // state0 — rotate it into state1 so the first transient step starts
    // from a valid operating point instead of zero.
    ckt.rotate_state();

    // ---------------------------------------------------------------
    // 3. Apply .ic overrides
    // ---------------------------------------------------------------
    for (auto& [node_idx, value] : ckt.ic) {
        if (node_idx >= 0 && node_idx < n)
            solution[node_idx] = value;
    }

    // ---------------------------------------------------------------
    // Result storage helper
    // ---------------------------------------------------------------
    TransientResult tran_result;

    auto store_point = [&](double t, const std::vector<double>& sol) {
        tran_result.time.push_back(t);
        for (int32_t i = 0; i < num_nodes; ++i) {
            std::string key = "v(" + to_lower(ckt.node_name(i)) + ")";
            tran_result.voltages[key].push_back(sol[i]);
        }
        for (const auto& dev : ckt.devices()) {
            if (auto* vs = dynamic_cast<const VSource*>(dev.get())) {
                int32_t br = vs->branch_index();
                if (br >= 0 && br < n) {
                    std::string key = "i(" + to_lower(dev->name()) + ")";
                    tran_result.currents[key].push_back(sol[br]);
                }
            } else if (auto* ind = dynamic_cast<const Inductor*>(dev.get())) {
                int32_t br = ind->branch_index();
                if (br >= 0 && br < n) {
                    std::string key = "i(" + to_lower(dev->name()) + ")";
                    tran_result.currents[key].push_back(sol[br]);
                }
            }
        }
    };

    // ---------------------------------------------------------------
    // 4. Store t=0
    // ---------------------------------------------------------------
    store_point(0.0, solution);

    // ---------------------------------------------------------------
    // 5. Enable transient and set integration method
    // ---------------------------------------------------------------
    for (auto& dev : ckt.devices()) {
        if (auto* cap = dynamic_cast<Capacitor*>(dev.get())) {
            cap->set_transient(tstep);
            cap->set_integration_method(1);  // Gear
        } else if (auto* ind = dynamic_cast<Inductor*>(dev.get())) {
            ind->set_transient(tstep);
            ind->set_integration_method(1);  // Gear
        }
        // TODO(Phase-1b): MOSFET set_transient/set_integration_method wired in Task 3+
    }

    // ---------------------------------------------------------------
    // 6. Initialize DC state
    // ---------------------------------------------------------------
    for (auto& dev : ckt.devices()) {
        if (auto* cap = dynamic_cast<Capacitor*>(dev.get())) {
            cap->init_dc_state(solution);
        } else if (auto* ind = dynamic_cast<Inductor*>(dev.get())) {
            ind->init_dc_state(solution);
        }
        // TODO(Phase-1b): MOSFET init_dc_state wired in Task 3+
    }

    // ---------------------------------------------------------------
    // 7. Set up time step controller
    // ---------------------------------------------------------------
    TimeStepController ctrl;
    ctrl.init(tstep, tstop);
    collect_breakpoints(ckt, ctrl, tstop);

    // Add output times as breakpoints so we land exactly on them
    {
        int num_output = static_cast<int>(std::round(tstop / tstep));
        for (int i = 1; i <= num_output; ++i) {
            ctrl.add_breakpoint(i * tstep);
        }
    }

    // Step bounds
    const double dt_min = tstep * 1e-6;
    const double dt_max = tstep * 100.0;

    // History for LTE (stores the two previous accepted solutions)
    std::vector<double> sol_prev = solution;
    std::vector<double> sol_prev2 = solution;
    double next_output_time = tstep;
    int step_count = 0;

    // CKTmode bits for transient
    // MODETRAN=0x1, MODEINITTRAN=0x1000 (source: ngspice cktdefs.h; mirrored in bsim4v7_shim.hpp)
    constexpr int MODETRAN_BIT     = 0x1;
    constexpr int MODEINITTRAN_BIT = 0x1000;

    // ---------------------------------------------------------------
    // 8. Adaptive time-stepping loop
    // ---------------------------------------------------------------
    double dt = tstep;

    while (ctrl.current_time() < tstop - 1e-18) {
        // Clamp dt to not exceed tstop, breakpoints, or next output point
        dt = std::min(dt, dt_max);
        dt = std::max(dt, dt_min);
        dt = ctrl.clamp_to_breakpoint(dt);
        dt = ctrl.clamp_to_end(dt);

        if (dt < 1e-20) break;

        double t = ctrl.current_time() + dt;

        // Update timestep on reactive devices
        for (auto& dev : ckt.devices()) {
            if (auto* cap = dynamic_cast<Capacitor*>(dev.get())) {
                cap->set_transient(dt);
            } else if (auto* ind = dynamic_cast<Inductor*>(dev.get())) {
                ind->set_transient(dt);
            }
            // TODO(Phase-1b): MOSFET set_transient(dt) wired in Task 3+
        }

        // Update time on sources
        for (auto& dev : ckt.devices()) {
            if (auto* vs = dynamic_cast<VSource*>(dev.get())) {
                vs->set_time(t);
            } else if (auto* is = dynamic_cast<ISource*>(dev.get())) {
                is->set_time(t);
            }
        }

        // Fill integrator context before Newton load
        {
            bool first_step = (step_count == 0);
            int cur_order = ctrl.order();
            ckt.integrator_ctx.order = cur_order;
            ckt.integrator_ctx.delta = dt;
            ckt.integrator_ctx.delta_old[1] = ctrl.prev_dt();
            ckt.integrator_ctx.mode = MODETRAN_BIT | (first_step ? MODEINITTRAN_BIT : 0);

            if (cur_order == 1) {
                // Backward Euler
                ckt.integrator_ctx.ag[0] =  1.0 / dt;
                ckt.integrator_ctx.ag[1] = -1.0 / dt;
            } else {
                // Gear-2
                double h_old = ctrl.prev_dt();
                if (h_old > 0.0) {
                    double r = dt / h_old;
                    ckt.integrator_ctx.ag[0] = (1.0 + 2.0 * r) / (dt * (1.0 + r));
                    ckt.integrator_ctx.ag[1] = -(1.0 + r) / (dt * r);
                } else {
                    // Fallback to BE if prev_dt not yet set
                    ckt.integrator_ctx.ag[0] =  1.0 / dt;
                    ckt.integrator_ctx.ag[1] = -1.0 / dt;
                }
            }
        }

        // Newton-Raphson solve
        auto nr = newton_solve(ckt, solver, solution, ckt.options);
        if (!nr.converged) {
            // Newton failed — halve dt and retry
            dt *= 0.5;
            if (dt < dt_min) {
                // Clean up and throw
                for (auto& dev : ckt.devices()) {
                    if (auto* cap = dynamic_cast<Capacitor*>(dev.get()))
                        cap->clear_transient();
                    else if (auto* ind = dynamic_cast<Inductor*>(dev.get()))
                        ind->clear_transient();
                    // TODO(Phase-1b): MOSFET clear_transient wired in Task 3+
                }
                throw ConvergenceError("Transient failed to converge at t=" + std::to_string(t));
            }
            continue;
        }
        solution = nr.solution;

        // LTE check (skip first two steps — need three points)
        bool accepted = true;
        if (step_count >= 2) {
            ctrl.set_dt(dt);  // set before evaluate so proposed_dt is relative to current dt
            accepted = ctrl.evaluate_step(solution, sol_prev, sol_prev2, num_nodes, ckt.options);
            // If rejected but dt is already at minimum, force acceptance
            if (!accepted && dt <= dt_min * 1.01) {
                accepted = true;
            }
        }

        if (!accepted) {
            // Reject: restore solution and retry with smaller dt
            solution = sol_prev;
            dt = std::max(ctrl.proposed_dt(), dt_min);
            continue;
        }

        // Accept step
        ctrl.set_prev_dt(dt);
        ctrl.advance(dt);
        ctrl.set_dt(dt);
        step_count++;

        // Advance to Gear-2 once two steps have been accepted
        if (step_count == 2) {
            ctrl.set_order(2);
        }

        // Rotate state history ring for BSIM4-style state-storing devices
        ckt.rotate_state();

        // Accept on reactive devices
        for (auto& dev : ckt.devices()) {
            if (auto* cap = dynamic_cast<Capacitor*>(dev.get())) {
                cap->accept_step_from_solution(solution);
            } else if (auto* ind = dynamic_cast<Inductor*>(dev.get())) {
                ind->accept_step_from_solution(solution);
            }
            // TODO(Phase-1b): MOSFET accept_step_from_solution wired in Task 3+
        }

        // Store output at tstep intervals (interpolate if we overshot)
        while (next_output_time <= ctrl.current_time() + 1e-18 && next_output_time <= tstop + 1e-18) {
            if (std::abs(ctrl.current_time() - next_output_time) < 1e-18) {
                // Landed exactly on output point
                store_point(next_output_time, solution);
            } else {
                // Interpolate between sol_prev (at t-dt) and solution (at t)
                double alpha = (next_output_time - (ctrl.current_time() - dt)) / dt;
                alpha = std::max(0.0, std::min(1.0, alpha));
                std::vector<double> interp(n);
                for (int32_t i = 0; i < n; ++i) {
                    interp[i] = sol_prev[i] + alpha * (solution[i] - sol_prev[i]);
                }
                store_point(next_output_time, interp);
            }
            next_output_time += tstep;
        }

        // Shift history
        sol_prev2 = sol_prev;
        sol_prev = solution;

        // Propose next dt
        if (step_count >= 2) {
            dt = std::max(ctrl.proposed_dt(), dt_min);
        }
        // else keep dt = tstep for the first few steps
    }

    // ---------------------------------------------------------------
    // 9. Clean up
    // ---------------------------------------------------------------
    for (auto& dev : ckt.devices()) {
        if (auto* cap = dynamic_cast<Capacitor*>(dev.get()))
            cap->clear_transient();
        else if (auto* ind = dynamic_cast<Inductor*>(dev.get()))
            ind->clear_transient();
        // TODO(Phase-1b): MOSFET clear_transient wired in Task 3+
    }

    tran_result.rejected_steps = ctrl.rejected_count();
    return tran_result;
}

} // namespace neospice
