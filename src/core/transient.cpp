#include "core/transient.hpp"
#include "core/newton.hpp"
#include "core/convergence.hpp"
#include "core/klu_solver.hpp"
#include "core/timestep.hpp"
#include "devices/vsource.hpp"
#include "devices/isource.hpp"
#include "devices/capacitor.hpp"
#include "devices/inductor.hpp"
#include "devices/coupled_inductor.hpp"
#include "devices/vcvs.hpp"
#include "devices/ccvs.hpp"
#include "devices/vcvs_nonlinear.hpp"
#include "devices/tline.hpp"
#include "devices/ltra.hpp"
#include "devices/asrc/asrc_device.hpp"
#include <algorithm>
#include <cmath>

namespace neospice {

static std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

static std::string make_branch_key(const std::string& dname) {
    std::string lower = to_lower(dname);
    auto dot = lower.rfind('.');
    if (dot != std::string::npos && dot + 1 < lower.size()) {
        char type_letter = lower[dot + 1];
        return "i(" + std::string(1, type_letter) + "." + lower + ")";
    }
    return "i(" + lower + ")";
}

// Collect PULSE/SIN breakpoints from sources
static void collect_breakpoints(Circuit& ckt, TimeStepController& ctrl, double tstop) {
    for (auto& dev : ckt.devices()) {
        if (auto* vs = dynamic_cast<VSource*>(dev.get())) {
            auto bps = vs->get_breakpoints(0.0, tstop);
            for (double bp : bps) ctrl.add_source_breakpoint(bp);
        } else if (auto* is = dynamic_cast<ISource*>(dev.get())) {
            auto bps = is->get_breakpoints(0.0, tstop);
            for (double bp : bps) ctrl.add_source_breakpoint(bp);
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

    const bool use_gear = (ckt.options.method == "gear");

    // DC preamble — the transient initial operating point uses MODETRANOP
    // (0x20), NOT the full MODEDC mask (0x70) or MODEDCOP (0x10).  ngspice
    // uses MODETRANOP so that BSIM4v7's load function can distinguish a
    // transient-initial DC from a standalone DC operating point.
    constexpr int MODETRANOP_BIT  = 0x20;
    constexpr int MODEINITJCT_BIT = 0x200;
    constexpr int MODEINITFIX_BIT = 0x400;

    ckt.integrator_ctx.mode = MODETRANOP_BIT | MODEINITJCT_BIT;
    auto result = newton_solve(ckt, solver, solution, ckt.options);
    if (result.converged) {
        solution = result.solution;
    } else {
        ckt.integrator_ctx.mode = MODETRANOP_BIT | MODEINITFIX_BIT;
        result = gmin_stepping(ckt, solver, solution, ckt.options);
        if (result.converged) {
            solution = result.solution;
        } else {
            ckt.integrator_ctx.mode = MODETRANOP_BIT | MODEINITFIX_BIT;
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
    // Result storage: pre-compute extraction slots once
    // ---------------------------------------------------------------
    TransientResult tran_result;

    struct TranSlot { std::string key; int32_t idx; };
    std::vector<TranSlot> v_slots, c_slots;

    for (int32_t i = 0; i < num_nodes; ++i) {
        if (ckt.is_internal_node(i)) continue;
        v_slots.push_back({"v(" + to_lower(ckt.node_name(i)) + ")", i});
    }
    auto add_tran_slot = [&](int32_t br, const std::string& dname) {
        if (br >= 0 && br < n)
            c_slots.push_back({make_branch_key(dname), br});
    };
    for (const auto& dev : ckt.devices()) {
        if (auto* vs = dynamic_cast<const VSource*>(dev.get()))
            add_tran_slot(vs->branch_index(), dev->name());
        else if (auto* ind = dynamic_cast<const Inductor*>(dev.get()))
            add_tran_slot(ind->branch_index(), dev->name());
        else if (auto* e = dynamic_cast<const VCVS*>(dev.get()))
            add_tran_slot(e->branch_index(), dev->name());
        else if (auto* h = dynamic_cast<const CCVS*>(dev.get()))
            add_tran_slot(h->branch_index(), dev->name());
        else if (auto* enl = dynamic_cast<const NonlinearVCVS*>(dev.get()))
            add_tran_slot(enl->branch_index(), dev->name());
        else if (auto* etbl = dynamic_cast<const TableVCVS*>(dev.get()))
            add_tran_slot(etbl->branch_index(), dev->name());
        else if (auto* bs = dynamic_cast<const ASRCDevice*>(dev.get()))
            if (bs->mode() == ASRCDevice::Mode::VOLTAGE)
                add_tran_slot(bs->branch_index(), dev->name());
    }

    std::vector<std::vector<double>*> v_ptrs, c_ptrs;
    for (auto& s : v_slots)
        v_ptrs.push_back(&tran_result.voltages[s.key]);
    for (auto& s : c_slots)
        c_ptrs.push_back(&tran_result.currents[s.key]);

    auto store_point = [&](double t, const std::vector<double>& sol) {
        tran_result.time.push_back(t);
        for (std::size_t k = 0; k < v_slots.size(); ++k)
            v_ptrs[k]->push_back(sol[v_slots[k].idx]);
        for (std::size_t k = 0; k < c_slots.size(); ++k)
            c_ptrs[k]->push_back(sol[c_slots[k].idx]);
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
            cap->set_integration_method(use_gear ? 1 : 0);
        } else if (auto* ind = dynamic_cast<Inductor*>(dev.get())) {
            ind->set_transient(tstep);
            ind->set_integration_method(use_gear ? 1 : 0);
        } else if (auto* ki = dynamic_cast<CoupledInductor*>(dev.get())) {
            ki->set_transient(tstep);
            ki->set_integration_method(use_gear ? 1 : 0);
        } else if (auto* tl = dynamic_cast<TransmissionLine*>(dev.get())) {
            tl->set_transient(true);
        } else if (auto* ltl = dynamic_cast<LossyTransmissionLine*>(dev.get())) {
            ltl->set_transient(true);
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
        } else if (auto* ki = dynamic_cast<CoupledInductor*>(dev.get())) {
            ki->init_dc_state(solution);
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
    // MODETRAN=0x1, MODEINITTRAN=0x1000, MODEINITPRED=0x2000
    // (source: ngspice cktdefs.h; mirrored in bsim4v7_shim.hpp)
    constexpr int MODETRAN_BIT     = 0x1;
    constexpr int MODEINITTRAN_BIT = 0x1000;
    constexpr int MODEINITPRED_BIT = 0x2000;

    // ---------------------------------------------------------------
    // 8. Adaptive time-stepping loop
    // ---------------------------------------------------------------
    // ngspice: delta = MIN(finalTime/100, step)/10  (dctran.c ~line 112)
    // Then at the first breakpoint (t=0): delta /= 10  (dctran.c ~line 569)
    // Combined: initial dt = MIN(tstop/100, tstep) / 100
    double dt = std::min(tstop / 100.0, tstep) / 100.0;
    ckt.integrator_ctx.integrate_method = use_gear ? 1 : 0;

    // prev_prev_dt: the dt of the step before the current one, needed for
    // quadratic interpolation (sol_prev2 lives at t - dt - prev_prev_dt).
    double prev_prev_dt = dt;

    // Counter: accepted steps since last source breakpoint crossing.
    // Used to hold order at 1 (BE) for a few steps after a discontinuity.
    int steps_after_bp = 1000;

    int total_iterations = 0;
    constexpr int MAX_ITERATIONS = 500000;

    while (ctrl.current_time() < tstop - 1e-18) {
        if (++total_iterations > MAX_ITERATIONS) {
            throw ConvergenceError(
                "Transient exceeded " + std::to_string(MAX_ITERATIONS) +
                " iterations at t=" + std::to_string(ctrl.current_time()) +
                " step_count=" + std::to_string(step_count) +
                " dt=" + std::to_string(dt) +
                " order=" + std::to_string(ctrl.order()) +
                " steps_after_bp=" + std::to_string(steps_after_bp));
        }
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
            } else if (auto* ki = dynamic_cast<CoupledInductor*>(dev.get())) {
                ki->set_transient(dt);
            }
            // TransmissionLine does not need set_transient(dt) each step — it
            // reads tls_integrator_ctx->current_time to know what time it is.
            // BSIM4 reads dt from integrator_ctx (CKTdelta).
        }

        // Update time on sources and behavioral elements
        for (auto& dev : ckt.devices()) {
            if (auto* vs = dynamic_cast<VSource*>(dev.get())) {
                vs->set_time(t);
            } else if (auto* is = dynamic_cast<ISource*>(dev.get())) {
                is->set_time(t);
            } else if (auto* bs = dynamic_cast<ASRCDevice*>(dev.get())) {
                bs->set_time(t);
            }
        }

        // Fill integrator context before Newton load
        {
            bool first_step = (step_count == 0);
            int cur_order = ctrl.order();
            ckt.integrator_ctx.order = cur_order;
            ckt.integrator_ctx.delta = dt;
            ckt.integrator_ctx.current_time = t;
            ckt.integrator_ctx.delta_old[1] = first_step ? dt : ctrl.prev_dt();
            ckt.integrator_ctx.mode = MODETRAN_BIT | (first_step ? MODEINITTRAN_BIT : MODEINITPRED_BIT);

            // Determine effective method: trap unless user chose gear or
            // ringing detection temporarily switched to gear.
            bool eff_gear = use_gear;

            if (cur_order == 1) {
                // Backward Euler: y'(t_n) ≈ (y_n - y_{n-1}) / dt
                ckt.integrator_ctx.ag[0] =  1.0 / dt;
                ckt.integrator_ctx.ag[1] = -1.0 / dt;
                ckt.integrator_ctx.ag[2] =  0.0;
            } else {
                if (!eff_gear) {
                    // Trapezoidal: i_n = (2/h)(q_n - q_{n-1}) - i_{n-1}
                    ckt.integrator_ctx.ag[0] =  2.0 / dt;
                    ckt.integrator_ctx.ag[1] = -2.0 / dt;
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
                    double h_old = ctrl.prev_dt();
                    if (h_old > 0.0) {
                        double sum = dt + h_old;
                        ckt.integrator_ctx.ag[0] =  (2.0 * dt + h_old) / (dt    * sum  );
                        ckt.integrator_ctx.ag[1] = -(dt + h_old)       / (dt    * h_old);
                        ckt.integrator_ctx.ag[2] =  dt                 / (h_old * sum  );
                    } else {
                        // Fallback to BE if prev_dt not yet set
                        ckt.integrator_ctx.ag[0] =  1.0 / dt;
                        ckt.integrator_ctx.ag[1] = -1.0 / dt;
                        ckt.integrator_ctx.ag[2] =  0.0;
                    }
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
                    else if (auto* ki = dynamic_cast<CoupledInductor*>(dev.get()))
                        ki->clear_transient();
                    else if (auto* tl = dynamic_cast<TransmissionLine*>(dev.get()))
                        tl->set_transient(false);
                    else if (auto* ltl = dynamic_cast<LossyTransmissionLine*>(dev.get()))
                        ltl->set_transient(false);
                }
                throw ConvergenceError("Transient failed to converge at t=" + std::to_string(t));
            }
            continue;
        }
        solution = nr.solution;

        // Global node-voltage LTE check — skip first 2 steps (need 3 points)
        // and skip 3 steps after a source breakpoint so the second-difference
        // history is fully populated from uniform post-edge steps.
        bool accepted = true;
        if (step_count >= 2 && steps_after_bp >= 3) {
            ctrl.set_dt(dt);
            accepted = ctrl.evaluate_step(solution, sol_prev, sol_prev2, num_nodes, ckt.options);
            if (!accepted && dt <= dt_min * 1.01) {
                accepted = true;
            }
        }

        // Device-specific LTE — compute minimum suggested dt from charge
        // truncation error across all devices (BSIM4, MOS1, BJT, etc.).
        // This always runs (matching ngspice's CKTtrunc default path).
        double device_dt = 1e30;
        if (step_count >= 2) {
            for (const auto& dev : ckt.devices()) {
                device_dt = std::min(device_dt,
                    dev->compute_trunc(ckt.integrator_ctx, ckt.options));
            }
            if (accepted && device_dt < dt * 0.9 && dt > dt_min * 1.01) {
                accepted = false;
                ctrl.record_rejection();
                ctrl.set_dt(dt);
            }
        }

        if (!accepted) {
            solution = sol_prev;
            double proposed = ctrl.proposed_dt();
            if (device_dt < proposed)
                proposed = device_dt;
            dt = std::max(proposed, dt_min);
            continue;
        }

        // Accept step
        prev_prev_dt = ctrl.prev_dt();  // save before overwriting
        ctrl.set_prev_dt(dt);
        ctrl.advance(dt);
        ctrl.set_dt(dt);
        step_count++;
        if (steps_after_bp < 1000) ++steps_after_bp;

        // After crossing a source breakpoint, reset dt to tstep.
        // Note: ngspice additionally resets order to 1 and reduces dt to
        // 0.1 * min(saveDelta, bp_gap), but that interacts with device-specific
        // charge integration differences. Keep the simpler approach.
        if (ctrl.crossed_source_breakpoint() && step_count > 2) {
            steps_after_bp = 0;
            dt = tstep;
        }

        // Advance to order 2 once two steps have been accepted
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
            } else if (auto* ki = dynamic_cast<CoupledInductor*>(dev.get())) {
                ki->accept_step_from_solution(solution);
            } else if (auto* tl = dynamic_cast<TransmissionLine*>(dev.get())) {
                tl->accept_step(t, solution);
            } else if (auto* ltl = dynamic_cast<LossyTransmissionLine*>(dev.get())) {
                ltl->accept_step(t, solution);
            }
            // BSIM4 state advance happens via ckt.rotate_state() on the next step.
        }

        // Store output at tstep intervals (interpolate if we overshot)
        while (next_output_time <= ctrl.current_time() + 1e-18 && next_output_time <= tstop + 1e-18) {
            if (std::abs(ctrl.current_time() - next_output_time) < 1e-18) {
                // Landed exactly on output point
                store_point(next_output_time, solution);
            } else if (step_count >= 2 && prev_prev_dt > 1e-20) {
                // Quadratic (Lagrange) interpolation using 3 history points:
                //   sol_prev2 at t0, sol_prev at t1, solution at t2
                double t2 = ctrl.current_time();
                double t1 = t2 - dt;
                double t0 = t1 - prev_prev_dt;  // dt of step before this one
                double t_out = next_output_time;
                // Lagrange basis values (same for all variables)
                double L0 = ((t_out - t1) * (t_out - t2)) / ((t0 - t1) * (t0 - t2));
                double L1 = ((t_out - t0) * (t_out - t2)) / ((t1 - t0) * (t1 - t2));
                double L2 = ((t_out - t0) * (t_out - t1)) / ((t2 - t0) * (t2 - t1));
                std::vector<double> interp(n);
                for (int32_t i = 0; i < n; ++i) {
                    interp[i] = L0 * sol_prev2[i] + L1 * sol_prev[i] + L2 * solution[i];
                }
                store_point(next_output_time, interp);
            } else {
                // Linear interpolation for first 2 steps (only 2 history points)
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

        // Propose next dt (constrained by LTE and device LTE).
        // Require sab >= 4: LTE first evaluates at sab==3, so the
        // proposed_dt from that evaluation is available at sab==4.
        // ngspice: *timeStep = MIN(2 * *timeStep, timetemp) — cap growth at 2x.
        if (step_count >= 2 && steps_after_bp >= 4) {
            double proposed = ctrl.proposed_dt();
            // Cap to 2x current dt (ngspice ckttrunc.c line 53)
            proposed = std::min(proposed, 2.0 * dt);
            if (device_dt < proposed)
                proposed = std::min(proposed, device_dt);
            dt = std::max(proposed, dt_min);
        }
        // else keep dt = tstep for the first few steps post-breakpoint
    }

    // ---------------------------------------------------------------
    // 9. Clean up
    // ---------------------------------------------------------------
    for (auto& dev : ckt.devices()) {
        if (auto* cap = dynamic_cast<Capacitor*>(dev.get()))
            cap->clear_transient();
        else if (auto* ind = dynamic_cast<Inductor*>(dev.get()))
            ind->clear_transient();
        else if (auto* ki = dynamic_cast<CoupledInductor*>(dev.get()))
            ki->clear_transient();
        else if (auto* tl = dynamic_cast<TransmissionLine*>(dev.get()))
            tl->set_transient(false);
        else if (auto* ltl = dynamic_cast<LossyTransmissionLine*>(dev.get()))
            ltl->set_transient(false);
    }

    tran_result.rejected_steps = ctrl.rejected_count();
    return tran_result;
}

} // namespace neospice
