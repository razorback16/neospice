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
        // BSIM4 uses the circuit state ring (rotate_state + set_state_ptrs)
        // rather than these per-device hooks.
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
        // BSIM4 state1 is seeded by rotate_state() below.
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
            // BSIM4 reads dt from integrator_ctx (CKTdelta).
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
                // Backward Euler: y'(t_n) ≈ (y_n - y_{n-1}) / dt
                ckt.integrator_ctx.ag[0] =  1.0 / dt;
                ckt.integrator_ctx.ag[1] = -1.0 / dt;
                ckt.integrator_ctx.ag[2] =  0.0;
            } else {
                // Variable-step Gear-2 (BDF2).  Derived from the Lagrange
                // polynomial through (t_{n-2}, y_{n-2}), (t_{n-1}, y_{n-1}),
                // (t_n, y_n) differentiated at t_n.  With h = dt = t_n -
                // t_{n-1} and k = h_old = t_{n-1} - t_{n-2}:
                //
                //   ag[0] =  (2h + k) / ( h * (h + k) )
                //   ag[1] = -(h  + k) / ( h * k       )
                //   ag[2] =   h       / ( k * (h + k) )
                //
                // Uniform-step sanity: h = k → (1.5, -2, 0.5)/h ✓.
                // Shim::NIintegrate (bsim4v7_shim.cpp) sums all three terms
                // over CKTstate0/1/2 for CKTorder==2, so all three must be
                // populated — the previous code populated only ag[0]/ag[1]
                // and additionally used an ag[1] expression that was only
                // correct for uniform h=k.
                double h_old = ctrl.prev_dt();
                if (h_old > 0.0) {
                    double sum = dt + h_old;
                    ckt.integrator_ctx.ag[0] =  (2.0 * dt + h_old) / (dt    * sum  );
                    ckt.integrator_ctx.ag[1] = -(dt + h_old)       / (dt    * h_old);
                    ckt.integrator_ctx.ag[2] =  dt                 / (h_old * sum  );
                } else {
                    // Fallback to BE if prev_dt not yet set (shouldn't happen
                    // once step_count ≥ 2, but keeps us safe).
                    ckt.integrator_ctx.ag[0] =  1.0 / dt;
                    ckt.integrator_ctx.ag[1] = -1.0 / dt;
                    ckt.integrator_ctx.ag[2] =  0.0;
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

        // Device-specific LTE — compute minimum suggested dt from charge
        // truncation error across all devices (BSIM4v7 MOSFETs, etc.).
        // This mirrors ngspice's CKTtrunc / BSIM4v7trunc path.
        double device_dt = 1e30;
        if (step_count >= 2) {
            for (const auto& dev : ckt.devices()) {
                device_dt = std::min(device_dt,
                    dev->compute_trunc(ckt.integrator_ctx, ckt.options));
            }
            // If device LTE suggests significantly smaller step, reject
            if (accepted && device_dt < dt * 0.9) {
                if (dt > dt_min * 1.01) {
                    accepted = false;
                    ctrl.set_dt(dt);  // for proposed_dt bookkeeping
                }
            }
        }

        if (!accepted) {
            // Reject: restore solution and retry with smaller dt
            solution = sol_prev;
            double proposed = ctrl.proposed_dt();
            // If device LTE gave a tighter bound, use it
            if (device_dt < proposed)
                proposed = device_dt;
            dt = std::max(proposed, dt_min);
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
            // BSIM4 state advance happens via ckt.rotate_state() on the next step.
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

        // Propose next dt (constrained by device LTE if applicable)
        if (step_count >= 2) {
            double proposed = ctrl.proposed_dt();
            if (device_dt < proposed)
                proposed = device_dt;
            dt = std::max(proposed, dt_min);
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
    }

    tran_result.rejected_steps = ctrl.rejected_count();
    return tran_result;
}

} // namespace neospice
