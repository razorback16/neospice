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
#include <chrono>
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

// Classify a source function as HARD or SOFT breakpoint type.
// HARD: PULSE, EXP, PWL (sharp edges / corners)
// SOFT: SIN, AM, SFFM (smooth zero-crossings / envelope crossings)
static TimeStepController::BreakpointType classify_source(SourceFunction func) {
    switch (func) {
    case SourceFunction::SIN:
    case SourceFunction::AM:
    case SourceFunction::SFFM:
        return TimeStepController::BreakpointType::SOFT;
    default:
        return TimeStepController::BreakpointType::HARD;
    }
}

// Collect PULSE/SIN breakpoints from sources with type classification
static void collect_breakpoints(Circuit& ckt, TimeStepController& ctrl, double tstop) {
    for (auto& dev : ckt.devices()) {
        if (auto* vs = dynamic_cast<VSource*>(dev.get())) {
            auto type = classify_source(vs->source_function());
            auto bps = vs->get_breakpoints(0.0, tstop);
            for (double bp : bps) ctrl.add_source_breakpoint(bp, type);
        } else if (auto* is = dynamic_cast<ISource*>(dev.get())) {
            auto type = classify_source(is->source_function());
            auto bps = is->get_breakpoints(0.0, tstop);
            for (double bp : bps) ctrl.add_source_breakpoint(bp, type);
        } else if (auto* tl = dynamic_cast<TransmissionLine*>(dev.get())) {
            auto bps = tl->get_breakpoints(0.0, tstop);
            for (double bp : bps) ctrl.add_source_breakpoint(bp);  // TL breakpoints are HARD
        }
    }
}

TransientResult solve_transient(Circuit& ckt, double tstep, double tstop,
                                bool uic) {
    auto t_start = std::chrono::steady_clock::now();
    int total_newton_iters = 0;
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
        total_newton_iters += result.iterations;
    } else {
        ckt.integrator_ctx.mode = MODETRANOP_BIT | MODEINITFIX_BIT;
        result = gmin_stepping(ckt, solver, solution, ckt.options);
        if (result.converged) {
            solution = result.solution;
            total_newton_iters += result.iterations;
        } else {
            ckt.integrator_ctx.mode = MODETRANOP_BIT | MODEINITFIX_BIT;
            result = source_stepping(ckt, solver, solution, ckt.options);
            if (result.converged) {
                solution = result.solution;
                total_newton_iters += result.iterations;
            } else {
                ckt.integrator_ctx.mode = MODETRANOP_BIT | MODEINITFIX_BIT;
                result = pseudo_transient(ckt, solver, solution, ckt.options);
                if (result.converged) {
                    solution = result.solution;
                    total_newton_iters += result.iterations;
                } else {
                    throw ConvergenceError("DC operating point failed to converge");
                }
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
    // 4. Enable transient and set integration method
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
    // 5. Initialize DC state
    // ---------------------------------------------------------------
    for (auto& dev : ckt.devices()) {
        if (auto* cap = dynamic_cast<Capacitor*>(dev.get())) {
            cap->init_dc_state(solution);
        } else if (auto* ind = dynamic_cast<Inductor*>(dev.get())) {
            ind->init_dc_state(solution);
        } else if (auto* ki = dynamic_cast<CoupledInductor*>(dev.get())) {
            ki->init_dc_state(solution);
        } else if (auto* tl = dynamic_cast<TransmissionLine*>(dev.get())) {
            tl->init_dc_state(solution);
        } else if (auto* ltl = dynamic_cast<LossyTransmissionLine*>(dev.get())) {
            ltl->init_dc_state(solution);
        }
        // BSIM4 state1 is seeded by rotate_state() below.
    }

    // ---------------------------------------------------------------
    // 5a. Apply IC= overrides when UIC is active
    // ---------------------------------------------------------------
    if (uic) {
        for (auto& dev : ckt.devices()) {
            if (auto* cap = dynamic_cast<Capacitor*>(dev.get())) {
                cap->apply_ic_override();
            } else if (auto* ind = dynamic_cast<Inductor*>(dev.get())) {
                ind->apply_ic_override(solution);
            }
        }
    }

    // ---------------------------------------------------------------
    // 5b. Re-solve for consistent t=0 when TL has IC
    // ---------------------------------------------------------------
    // When a transmission line has IC= values, the delay-line history is seeded
    // with those values.  The DC solution (computed before transient mode) does
    // not reflect the TL's IC-driven wave sources.  Re-solve at t=0 with the
    // TL in transient mode so node voltages are consistent with IC.
    {
        bool tl_has_ic = false;
        for (auto& dev : ckt.devices()) {
            if (auto* tl = dynamic_cast<TransmissionLine*>(dev.get())) {
                if (tl->has_ic()) { tl_has_ic = true; break; }
            }
        }
        if (tl_has_ic) {
            ckt.integrator_ctx.current_time = 0.0;
            ckt.integrator_ctx.delta = tstep;
            ckt.integrator_ctx.mode = MODETRANOP_BIT | MODEINITFIX_BIT;
            auto ic_result = newton_solve(ckt, solver, solution, ckt.options);
            if (ic_result.converged) {
                solution = ic_result.solution;
            }
        }
    }

    // ---------------------------------------------------------------
    // 6. Store t=0
    // ---------------------------------------------------------------
    store_point(0.0, solution);

    // ---------------------------------------------------------------
    // 6b. Resolve PULSE/SIN default parameters
    // ---------------------------------------------------------------
    // ngspice resolves unspecified PULSE parameters (TR/TF → tstep, PW/PER →
    // tstop) and SIN freq (→ 1/tstop) at evaluation time.  We do it once here
    // before breakpoint collection so that get_breakpoints sees the resolved
    // values.
    for (auto& dev : ckt.devices()) {
        if (auto* vs = dynamic_cast<VSource*>(dev.get()))
            vs->resolve_defaults(tstep, tstop);
        else if (auto* is = dynamic_cast<ISource*>(dev.get()))
            is->resolve_defaults(tstep, tstop);
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

    // History for LTE (stores the previous accepted solutions)
    std::vector<double> sol_prev = solution;
    std::vector<double> sol_prev2 = solution;
    std::vector<double> sol_prev3 = solution;  // 4th history point for ringing detection
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

    // saved_delta: the "natural" dt the solver wanted before it was cut to
    // reach a breakpoint.  Set only when clamp_to_breakpoint shortens dt.
    // (ngspice: CKTsaveDelta, dctran.c ~line 584)
    double saved_delta = dt;

    // Counter: accepted steps since last source breakpoint crossing.
    // Used to guard global node-voltage LTE (which ngspice doesn't have).
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
        // Save "natural" dt before breakpoint clamping (ngspice CKTsaveDelta)
        double unclamped_dt = dt;
        dt = ctrl.clamp_to_breakpoint(dt);
        if (dt < unclamped_dt - 1e-18)
            saved_delta = unclamped_dt;
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
            bool eff_gear = (ckt.integrator_ctx.integrate_method == 1);

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
            // Newton failed — cut dt by 8× (ngspice dctran.c ~802)
            dt /= 8.0;
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
        total_newton_iters += nr.iterations;

        // Global LTE check — skip first 2 steps (need 3 history points)
        // and skip 3 steps after a source breakpoint so the second-difference
        // history is fully populated from uniform post-edge steps.
        bool accepted = true;
        if (step_count >= 2 && steps_after_bp >= 3) {
            ctrl.set_dt(dt);
            accepted = ctrl.evaluate_step(solution, sol_prev, sol_prev2,
                                          num_nodes, ckt.options);
            if (!accepted && dt <= dt_min * 1.01) {
                accepted = true;
            }
        }

        // Device-specific LTE — compute minimum suggested dt from charge
        // truncation error across all devices (BSIM4, MOS1, BJT, etc.).
        // ngspice CKTtrunc (ckttrunc.c): device_dt = MIN(2*dt, timetemp).
        double device_dt = 1e30;
        if (step_count >= 2) {
            for (const auto& dev : ckt.devices()) {
                device_dt = std::min(device_dt,
                    dev->compute_trunc(ckt.integrator_ctx, ckt.options));
            }
            device_dt = std::min(2.0 * dt, device_dt);
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

        // After crossing a source breakpoint: reduce dt by a configurable
        // scale factor (restart_step_scale, default 0.1×).  Soft breakpoints
        // (SIN/AM/SFFM zero-crossings) use a milder reduction (sqrt of the
        // hard scale) since their waveforms are smooth.
        // Note: ngspice also resets order to 1 here, but our device charge
        // integration diverges from ngspice at order 1 near switching edges,
        // so we keep order 2 to avoid tripling the voltage error.
        if (ctrl.crossed_source_breakpoint() && step_count > 2) {
            steps_after_bp = 0;
            double bp_gap = ctrl.next_breakpoint_gap();
            double scale = ckt.options.restart_step_scale;
            // Soft breakpoints use a milder reduction (sqrt of the scale)
            if (ctrl.last_bp_type() == TimeStepController::BreakpointType::SOFT) {
                scale = std::sqrt(scale);  // e.g., 0.1 -> ~0.316
            }
            dt = std::min(dt, scale * std::min(saved_delta, bp_gap));
            dt = std::max(dt, dt_min * 2.0);
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
            } else if (auto* asrc = dynamic_cast<ASRCDevice*>(dev.get())) {
                asrc->expression().accept_ddt();
                asrc->expression().accept_idt();
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

        // Shift history (sol_prev3 must shift before sol_prev2)
        sol_prev3 = sol_prev2;
        sol_prev2 = sol_prev;
        sol_prev = solution;

        // Ringing detection — check for sign-alternating second differences
        // that indicate trapezoidal integration overshoot.  Only when user
        // selected trap (not gear) and we have 4 history points (step >= 3).
        // Also skip near breakpoints (steps_after_bp < 5) where the step
        // reduction already handles transient artifacts from sharp edges.
        if (step_count >= 3 && !use_gear && steps_after_bp >= 5) {
            ctrl.check_ringing(solution, sol_prev, sol_prev2, sol_prev3,
                               num_nodes, ckt.options);
            ctrl.tick_cooldown();

            // Switch integration method based on ringing state
            int new_method = (ctrl.ringing_detected() || ctrl.ringing_cooldown() > 0) ? 1 : 0;
            if (new_method != ckt.integrator_ctx.integrate_method) {
                ckt.integrator_ctx.integrate_method = new_method;
                // Propagate to reactive devices so their companion models
                // use the correct integration coefficients on the next step.
                for (auto& dev : ckt.devices()) {
                    if (auto* cap = dynamic_cast<Capacitor*>(dev.get())) {
                        cap->set_integration_method(new_method);
                    } else if (auto* ind = dynamic_cast<Inductor*>(dev.get())) {
                        ind->set_integration_method(new_method);
                    } else if (auto* ki = dynamic_cast<CoupledInductor*>(dev.get())) {
                        ki->set_integration_method(new_method);
                    }
                }
            }
        }

        // Propose next dt from device LTE (ngspice: CKTdelta = newdelta
        // after CKTtrunc, dctran.c ~876).  Use device_dt which already
        // incorporates the 2× growth cap from CKTtrunc.
        // Also incorporate global LTE proposal when available.
        if (step_count >= 2) {
            double proposed = device_dt;
            if (steps_after_bp >= 4) {
                proposed = std::min(proposed, ctrl.proposed_dt());
            }
            proposed = std::min(proposed, 2.0 * dt);
            dt = std::max(proposed, dt_min);
        }
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

    auto t_end = std::chrono::steady_clock::now();
    tran_result.status.converged = true;
    tran_result.status.iterations = total_newton_iters;
    tran_result.status.elapsed_seconds = std::chrono::duration<double>(t_end - t_start).count();

    return tran_result;
}

} // namespace neospice
