/**********
Portions of this file are translated from UC Berkeley SPICE3F5
(niconv.c / niiter.c, convergence aids and GMIN/source stepping) and
derive from the earlier SPICE2G6 (1983) Fortran, tracing to
L. Nagel's SPICE, UC Berkeley ERL Memorandum M382 (1973).

Copyright 1990 Regents of the University of California. All rights reserved.
(Permissive BSD-style "Berkeley Spice3" license.)
See NOTICE and CREDITS.md for full attribution.
**********/

#include "core/convergence.hpp"
#include "core/circuit.hpp"
#include "core/solver_iface.hpp"
#include "devices/vsource.hpp"
#include "devices/isource.hpp"
#include "devices/capacitor.hpp"
#include "devices/inductor.hpp"
#include "devices/coupled_inductor.hpp"
#include "devices/tline.hpp"
#include "devices/ltra.hpp"
#include "devices/asrc/asrc_device.hpp"
#include "core/ckt_mode.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <utility>
#include <vector>

namespace neospice {

namespace {

struct StateCheckpoint {
    std::vector<double> state0;
    std::vector<double> state1;
    std::vector<double> state2;
    std::vector<double> state3;
};

StateCheckpoint save_state(Circuit& ckt) {
    StateCheckpoint saved;
    const int32_t n = ckt.num_states();
    if (n > 0) {
        saved.state0.assign(ckt.state0(), ckt.state0() + n);
        saved.state1.assign(ckt.state1(), ckt.state1() + n);
        saved.state2.assign(ckt.state2(), ckt.state2() + n);
        saved.state3.assign(ckt.state3(), ckt.state3() + n);
    }
    return saved;
}

void restore_state(Circuit& ckt, const StateCheckpoint& saved) {
    const int32_t n = ckt.num_states();
    if (n <= 0) return;
    std::copy_n(saved.state0.data(), n, ckt.state0());
    std::copy_n(saved.state1.data(), n, ckt.state1());
    std::copy_n(saved.state2.data(), n, ckt.state2());
    std::copy_n(saved.state3.data(), n, ckt.state3());
}

void clear_state(Circuit& ckt) {
    const int32_t n = ckt.num_states();
    if (n <= 0) return;
    std::fill_n(ckt.state0(), n, 0.0);
    std::fill_n(ckt.state1(), n, 0.0);
    std::fill_n(ckt.state2(), n, 0.0);
    std::fill_n(ckt.state3(), n, 0.0);
}

void clear_state0(Circuit& ckt) {
    const int32_t n = ckt.num_states();
    if (n > 0) std::fill_n(ckt.state0(), n, 0.0);
}

void restore_state0(Circuit& ckt, const StateCheckpoint& saved) {
    const int32_t n = ckt.num_states();
    if (n > 0) std::copy_n(saved.state0.data(), n, ckt.state0());
}

void enable_optran_devices(Circuit& ckt, double dt, int method) {
    for (auto& dev : ckt.devices()) {
        if (auto* cap = dynamic_cast<Capacitor*>(dev.get())) {
            cap->set_transient(dt);
            cap->set_integration_method(method);
            cap->set_integrator_order(1);
        } else if (auto* ind = dynamic_cast<Inductor*>(dev.get())) {
            ind->set_transient(dt);
            ind->set_integration_method(method);
            ind->set_integrator_order(1);
        } else if (auto* ki = dynamic_cast<CoupledInductor*>(dev.get())) {
            ki->set_transient(dt);
            ki->set_integration_method(method);
            ki->set_integrator_order(1);
        } else if (auto* tl = dynamic_cast<TransmissionLine*>(dev.get())) {
            tl->set_transient(true);
        } else if (auto* ltl = dynamic_cast<LossyTransmissionLine*>(dev.get())) {
            ltl->set_transient(true);
        }
    }
}

void update_optran_device_timestep(Circuit& ckt, double dt, int order) {
    for (auto& dev : ckt.devices()) {
        if (auto* cap = dynamic_cast<Capacitor*>(dev.get())) {
            cap->set_transient(dt);
            cap->set_integrator_order(order);
        } else if (auto* ind = dynamic_cast<Inductor*>(dev.get())) {
            ind->set_transient(dt);
            ind->set_integrator_order(order);
        } else if (auto* ki = dynamic_cast<CoupledInductor*>(dev.get())) {
            ki->set_transient(dt);
            ki->set_integrator_order(order);
        }
    }
}

void cleanup_optran_devices(Circuit& ckt) {
    for (auto& dev : ckt.devices()) {
        if (auto* cap = dynamic_cast<Capacitor*>(dev.get())) {
            cap->clear_transient();
        } else if (auto* ind = dynamic_cast<Inductor*>(dev.get())) {
            ind->clear_transient();
        } else if (auto* ki = dynamic_cast<CoupledInductor*>(dev.get())) {
            ki->clear_transient();
        } else if (auto* tl = dynamic_cast<TransmissionLine*>(dev.get())) {
            tl->set_transient(false);
        } else if (auto* ltl = dynamic_cast<LossyTransmissionLine*>(dev.get())) {
            ltl->set_transient(false);
        }
    }
}

void init_optran_device_state(Circuit& ckt, const std::vector<double>& solution) {
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
    }
}

void accept_optran_step(Circuit& ckt, double t, const std::vector<double>& solution) {
    for (auto& dev : ckt.devices()) {
        if (auto* cap = dynamic_cast<Capacitor*>(dev.get())) {
            cap->accept_step_from_solution(solution);
        } else if (auto* ind = dynamic_cast<Inductor*>(dev.get())) {
            ind->accept_step_from_solution(solution);
        } else if (auto* ki = dynamic_cast<CoupledInductor*>(dev.get())) {
            ki->accept_step_from_solution(solution);
        } else if (auto* tl = dynamic_cast<TransmissionLine*>(dev.get())) {
            // This continuation seeks a DC state; it does not emit a physical
            // transient trace or maintain the transient breakpoint queue.
            (void)tl->accept_step(t, solution, ckt.integrator_ctx, 0.0);
        } else if (auto* ltl = dynamic_cast<LossyTransmissionLine*>(dev.get())) {
            ltl->accept_step(t, solution);
        } else if (auto* asrc = dynamic_cast<ASRCDevice*>(dev.get())) {
            asrc->expression().accept_idt();
        }
    }
}

void update_optran_source_time(Circuit& ckt, double t) {
    for (auto& dev : ckt.devices()) {
        if (auto* vs = dynamic_cast<VSource*>(dev.get())) {
            vs->set_time(t);
        } else if (auto* is = dynamic_cast<ISource*>(dev.get())) {
            is->set_time(t);
        } else if (auto* asrc = dynamic_cast<ASRCDevice*>(dev.get())) {
            asrc->set_time(t);
        }
    }
}

void fill_optran_integrator_context(Circuit& ckt, double t, double dt,
                                    double prev_dt, double prev_prev_dt,
                                    int order, bool first_step, int method) {

    ckt.integrator_ctx.order = order;
    ckt.integrator_ctx.delta = dt;
    ckt.integrator_ctx.current_time = t;
    ckt.integrator_ctx.delta_old[0] = dt;
    ckt.integrator_ctx.delta_old[1] = prev_dt > 0.0 ? prev_dt : dt;
    ckt.integrator_ctx.delta_old[2] = prev_prev_dt > 0.0 ? prev_prev_dt : ckt.integrator_ctx.delta_old[1];
    ckt.integrator_ctx.integrate_method = method;
    ckt.integrator_ctx.mode = MODETRAN_BIT | (first_step ? MODEINITTRAN_BIT : MODEINITPRED_BIT);

    if (order <= 1) {
        // optran.c initializes CKTag to zero before entering its time loop, but
        // NIcomCof recomputes these backward-Euler coefficients immediately
        // before the first NIiter call.  MODEINITTRAN makes state0 == state1,
        // so the initial companion current is zero while the conductance still
        // regularizes the operating-point matrix.
        ckt.integrator_ctx.ag[0] = 1.0 / dt;
        ckt.integrator_ctx.ag[1] = -1.0 / dt;
        ckt.integrator_ctx.ag[2] = 0.0;
        ckt.integrator_ctx.xmu_ratio = 0.0;
    } else if (method == 0) {
        double xmu = ckt.options.xmu;
        double one_minus_xmu = 1.0 - xmu;
        ckt.integrator_ctx.ag[0] = 1.0 / (dt * one_minus_xmu);
        // NIcomCof stores the previous-current coefficient in ag[1] for
        // second-order trapezoidal integration. NIintegrate then evaluates
        // ag0 * (q0 - q1), preserving ngspice's operation order.
        ckt.integrator_ctx.ag[1] = xmu / one_minus_xmu;
        ckt.integrator_ctx.ag[2] = 0.0;
        ckt.integrator_ctx.xmu_ratio = ckt.integrator_ctx.ag[1];
    } else {
        double h_old = prev_dt > 0.0 ? prev_dt : dt;
        double sum = dt + h_old;
        ckt.integrator_ctx.ag[0] = (2.0 * dt + h_old) / (dt * sum);
        ckt.integrator_ctx.ag[1] = -(dt + h_old) / (dt * h_old);
        ckt.integrator_ctx.ag[2] = dt / (h_old * sum);
        ckt.integrator_ctx.xmu_ratio = 0.0;
    }
}

} // namespace

namespace {

// Shared continuation loop behind gmin_stepping (ngspice cktop.c dynamic_gmin)
// and true_gmin_stepping (new_gmin). They sweep different conductances and
// configure different final solves. ngspice47 also uses distinct lower bounds
// when a successful step needs many iterations: 1.00005 for diagonal gmin,
// 3 for device gmin.
// `apply_step` publishes the current sweep value;
// `setup_final` configures the final solve at the target.
template <typename ApplyStep, typename SetupFinal>
NewtonResult gmin_continuation(Circuit& ckt, ISolver& solver,
                               std::vector<double>& solution,
                               const SimOptions& opts,
                               int firstmode, int continuemode,
                               double gtarget, const char* label, double slow_step_factor_floor,
                               ApplyStep apply_step, SetupFinal setup_final) {
    const double gmin_factor = 10.0;
    double factor = gmin_factor;
    double OldGmin = 1e-2;
    double g = OldGmin / factor;  // starts at 1e-3

    int total_iterations = 0;
    bool done = false;

    SimOptions step_opts = opts;
    // NIiter floors every requested budget at 100 iterations
    // (ngspice maths/ni/niiter.c).  Keep itl2 for the continuation-step
    // adaptation thresholds, but honor that independent Newton floor.
    step_opts.max_iter = std::max(opts.itl2, 100);
    const int dc_trcv_max_iter = opts.itl2;

    // Start from zero initial guess (matching ngspice)
    solution.assign(solution.size(), 0.0);
    clear_state0(ckt);

    std::vector<double> saved_solution = solution;
    StateCheckpoint saved_state = save_state(ckt);

    // Set first mode (e.g. MODETRANOP|MODEINITJCT)
    ckt.integrator_ctx.mode = firstmode;

    while (!done) {
        apply_step(step_opts, g);
        if (opts.verbose)
            std::cerr << "[" << label << "] trying g=" << g
                      << " factor=" << factor << "\n";

        NewtonResult result;
        try {
            result = newton_solve(ckt, solver, solution, step_opts);
        } catch (const std::runtime_error&) {
            result.converged = false;
        }

        int iters = result.iterations;
        total_iterations += iters;
        if (opts.verbose)
            std::cerr << "[" << label << "] result converged=" << result.converged
                      << " iters=" << iters
                      << " residual=" << result.residual << "\n";

        if (result.converged) {
            // Switch to continuation mode (e.g. MODETRANOP|MODEINITFLOAT)
            ckt.integrator_ctx.mode = continuemode;

            if (g <= gtarget) {
                done = true;
            } else {
                // Save accepted solution and state
                saved_solution = solution;
                saved_state = save_state(ckt);

                // Adapt factor based on iteration count (ngspice cktop.c:187-194)
                if (iters <= dc_trcv_max_iter / 4) {
                    factor *= std::sqrt(factor);
                    if (factor > gmin_factor)
                        factor = gmin_factor;
                }
                if (iters > (3 * dc_trcv_max_iter / 4)) {
                    factor = std::max(std::sqrt(factor), slow_step_factor_floor);
                }

                OldGmin = g;

                // Reduce g, clamping to gtarget (ngspice cktop.c:198-203)
                if (g < factor * gtarget) {
                    factor = g / gtarget;
                    g = gtarget;
                } else {
                    g /= factor;
                }
            }
        } else {
            // Convergence failure at this level
            if (factor < 1.00005) {
                // ngspice still performs one final NIiter at the target.  That
                // solve can converge even when the last tiny increment did not.
                done = true;
            } else {
                // Reduce factor aggressiveness and retry (ngspice cktop.c:213-214)
                factor = std::sqrt(std::sqrt(factor));
                g = OldGmin / factor;

                // Restore last accepted solution
                solution = saved_solution;
                restore_state0(ckt, saved_state);
            }
        }
    }

    // ngspice always resets the swept knob and runs one final solve,
    // regardless of how the continuation loop terminated.
    SimOptions final_opts = opts;
    setup_final(final_opts);
    NewtonResult final_result;
    try {
        final_result = newton_solve(ckt, solver, solution, final_opts, false);
    } catch (const std::runtime_error&) {
        final_result.converged = false;
    }
    total_iterations += final_result.iterations;
    final_result.iterations = total_iterations;
    return final_result;
}

} // namespace

NewtonResult gmin_stepping(Circuit& ckt, ISolver& solver,
                           std::vector<double>& solution,
                           const SimOptions& opts,
                           int firstmode, int continuemode) {
    return gmin_continuation(
        ckt, solver, solution, opts, firstmode, continuemode,
        std::max(opts.gmin, opts.gshunt), "gmin", 1.00005,
        [](SimOptions& step_opts, double g) { step_opts.diag_gmin = g; },
        [&opts](SimOptions& final_opts) {
            final_opts.diag_gmin = std::max(opts.gshunt, 0.0);
        });
}

NewtonResult true_gmin_stepping(Circuit& ckt, ISolver& solver,
                                std::vector<double>& solution,
                                const SimOptions& opts,
                                int firstmode, int continuemode) {
    // new_gmin sweeps the device gmin itself, so each step must also be
    // published on ckt.options for devices reading it through
    // tls_integrator_ctx->options->gmin.  Note the target is taken from
    // ckt.options.gmin, not opts.gmin, and is deliberately left in place.
    const double gtarget = std::max(ckt.options.gmin, opts.gshunt);
    return gmin_continuation(
        ckt, solver, solution, opts, firstmode, continuemode, gtarget,
        "true_gmin", 3.0,
        [&ckt](SimOptions& step_opts, double g) {
            ckt.options.gmin = g;
            step_opts.gmin = g;
        },
        [&ckt, gtarget](SimOptions& final_opts) {
            ckt.options.gmin = gtarget;
            final_opts.gmin = gtarget;
        });
}

NewtonResult source_stepping(Circuit& ckt, ISolver& solver,
                             std::vector<double>& solution,
                             const SimOptions& opts) {
    double fraction = 0.0;
    double step = 0.001;
    const double min_step = 1e-7;
    const int dc_trcv_max_iter = opts.itl2;
    int total_iterations = 0;
    double last_residual = 0.0;
    int32_t last_worst_idx = -1;

    int base_mode = ckt.integrator_ctx.mode & ~INITF_MASK;
    ckt.integrator_ctx.mode = base_mode | MODEINITJCT_BIT;

    // gillespie_src clears CKTrhsOld for every CKTnode (both voltage and
    // current-equation nodes) and clears CKTstate0 only.  Deeper transient
    // histories are deliberately left untouched.
    solution.assign(solution.size(), 0.0);
    clear_state0(ckt);

    SimOptions step_opts = opts;
    step_opts.src_fact = 0.0;
    ckt.options.src_fact = 0.0;

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
            ckt.options.src_fact = 1.0;
            // gillespie_src resets a failed zero-source gmin attempt to the
            // ordinary shunt before returning to CKTop.
            ckt.options.diag_gmin = std::max(opts.gshunt, 0.0);
            return {false, total_iterations, result.residual, result.worst_node_idx};
        }
    }
    if (opts.verbose)
        std::cerr << "[source] fraction=0 converged=" << result.converged
                  << " iters=" << result.iterations << "\n";
    total_iterations += result.iterations;
    std::vector<double> accepted_solution = solution;
    StateCheckpoint accepted_state = save_state(ckt);

    ckt.integrator_ctx.mode = base_mode | MODEINITFLOAT_BIT;

    // gillespie_src keeps the scheduled source fraction separate from the
    // increment that it adapts after a successful solve.  In particular, the
    // next fraction is scheduled *before* `raise` is changed, and a failed
    // fraction is followed by one solve at the last accepted fraction.  Both
    // details affect which branch strongly nonlinear macromodels follow.
    double scheduled_fraction = step;
    while (step >= min_step && fraction < 1.0) {
        const double next_frac = std::min(scheduled_fraction, 1.0);

        solution = accepted_solution;
        restore_state0(ckt, accepted_state);

        step_opts.src_fact = next_frac;
        ckt.options.src_fact = next_frac;
        try {
            result = newton_solve(ckt, solver, solution, step_opts);
        } catch (const std::runtime_error&) {
            result.converged = false;
        }

        last_residual = result.residual;
        last_worst_idx = result.worst_node_idx;
        if (opts.verbose)
            std::cerr << "[source] fraction=" << next_frac
                      << " converged=" << result.converged
                      << " iters=" << result.iterations
                      << " step=" << step << "\n";

        if (result.converged) {
            total_iterations += result.iterations;
            fraction = next_frac;
            accepted_solution = solution;
            accepted_state = save_state(ckt);
            scheduled_fraction = fraction + step;
            // gillespie_src uses CKTdcTrcvMaxIter (itl2), not the ordinary
            // DC iteration budget, to adapt the source-fraction increment.
            if (result.iterations <= dc_trcv_max_iter / 4)
                step *= 1.5;
            if (result.iterations > 3 * dc_trcv_max_iter / 4)
                step *= 0.5;
        } else {
            if (next_frac - fraction < 1e-8)
                break;
            solution = accepted_solution;
            restore_state0(ckt, accepted_state);
            step *= 0.1;
            if (step > 0.01) step = 0.01;
            scheduled_fraction = fraction;
        }
    }

    ckt.options.src_fact = 1.0;
    // Once the zero-source point converged, gillespie_src exits its stepping
    // loop with both CKTdiagGmin and CKTgmin restored to gminstart.  OPtran is
    // called immediately afterward and therefore inherits this value.
    ckt.options.diag_gmin = opts.gmin;
    if (fraction != 1.0) {
        // When the final retry is already at the last accepted source
        // fraction, gillespie_src breaks before restoring its checkpoint.
        // CKTrhsOld and state0 from that failed NIiter therefore seed the
        // immediately following OPtran attempt.
        result.converged = false;
    }
    result.iterations = total_iterations;
    result.residual = last_residual;
    result.worst_node_idx = last_worst_idx;
    return result;
}

NewtonResult gain_stepping(Circuit& ckt, ISolver& solver,
                           std::vector<double>& solution,
                           const SimOptions& opts) {
    const double original_dep_src_fact = ckt.options.dep_src_fact;

    double fraction = 0.0;
    double step = 0.001;
    const double min_step = 1e-7;
    int total_iterations = 0;
    double last_residual = 0.0;
    int32_t last_worst_idx = -1;

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

// [3B] Variable-gain homotopy. Mirrors gain_stepping(), but ramps
// ckt.options.device_gain_fact (0->1) to scale *semiconductor device*
// nonlinearity (MOSFET channel/junction, BJT Gummel-Poon) instead of E/G/F/H
// controlled-source gains. At device_gain_fact=0 the semiconductor devices
// contribute ~nothing, so the circuit reduces to its passive network (a unique,
// easy linear solution); lambda then ramps to 1 and the solution deforms
// continuously to the true operating point. Realizes the homotopy
// H(x,lambda) = lambda*F_device(x) + F_passive(x) = 0, exact at lambda=1.
NewtonResult variable_gain_homotopy(Circuit& ckt, ISolver& solver,
                                    std::vector<double>& solution,
                                    const SimOptions& opts) {
    const double original_device_gain_fact = ckt.options.device_gain_fact;

    double fraction = 0.0;
    double step = 0.001;
    const double min_step = 1e-7;
    int total_iterations = 0;
    double last_residual = 0.0;
    int32_t last_worst_idx = -1;

    int base_mode = ckt.integrator_ctx.mode & ~INITF_MASK;
    ckt.integrator_ctx.mode = base_mode | MODEINITJCT_BIT;

    // Start with all semiconductor device nonlinearity at zero (passive net).
    solution.assign(solution.size(), 0.0);
    clear_state(ckt);

    SimOptions step_opts = opts;
    step_opts.device_gain_fact = 0.0;
    ckt.options.device_gain_fact = 0.0;

    NewtonResult result;
    try {
        result = newton_solve(ckt, solver, solution, step_opts);
    } catch (const std::runtime_error&) {
        result.converged = false;
    }
    if (!result.converged) {
        // Try gmin stepping with devices at zero gain (passive-network solve).
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
            ckt.options.device_gain_fact = original_device_gain_fact;
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

        step_opts.device_gain_fact = next_frac;
        ckt.options.device_gain_fact = next_frac;
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
            step_opts.device_gain_fact = fraction;
            ckt.options.device_gain_fact = fraction;
            step *= 0.1;
            if (step > 0.01) step = 0.01;
            if (step < min_step) {
                ckt.options.device_gain_fact = original_device_gain_fact;
                return {false, total_iterations, last_residual, last_worst_idx};
            }
        }
    }

    ckt.options.device_gain_fact = original_device_gain_fact;
    result.iterations = total_iterations;
    return result;
}

NewtonResult transient_operating_point(Circuit& ckt, ISolver& solver,
                                        std::vector<double>& solution,
                                        const SimOptions& opts) {
    const std::vector<double> entry_solution = solution;
    const StateCheckpoint entry_state = save_state(ckt);
    const int saved_mode = ckt.integrator_ctx.mode;
    const double saved_time = ckt.integrator_ctx.current_time;

    // ngspice's default optran setup in frontend/init.c: "1 1 1 100n 10u 0".
    // CKTop calls this after direct Newton, gmin stepping, and source stepping
    // have failed.  Unlike diagonal pseudo-transient continuation, OPtran runs
    // the real transient companion models and returns the final transient state
    // as the operating point; it does not require a final singular DC solve.
    constexpr double op_step = 100e-9;
    constexpr double op_final = 10e-6;
    constexpr double kNewtonFailureDtFactor = 8.0;
    constexpr int kMaxOptranSteps = 200000;

    const int method = (opts.method == "gear") ? 1 : 0;
    const double dt_max = op_step;
    const double dt_min = dt_max * 1e-11;
    double dt = std::min(op_final / 100.0, op_step) / 10.0;
    double save_delta = op_final / 50.0;

    // First optran point starts at the t=0 breakpoint.  Match optran.c's
    // breakpoint logic: limit by 0.1 * saveDelta, then divide once more for
    // the first point.
    dt = std::min(dt, 0.1 * std::min(save_delta, op_final));
    dt /= 10.0;
    dt = std::max(dt, dt_min * 2.0);

    ckt.integrator_ctx.options = &ckt.options;
    ckt.integrator_ctx.integrate_method = method;
    enable_optran_devices(ckt, dt, method);
    init_optran_device_state(ckt, solution);

    const int32_t ns = ckt.num_states();
    if (ns > 0) {
        std::copy_n(ckt.state0(), ns, ckt.state1());
        // ngspice rotates once before its first MODEINITTRAN load, putting
        // this same entry state into state2 for the device predictor.
        std::copy_n(ckt.state0(), ns, ckt.state2());
    }

    NewtonWorkspace workspace(ckt.pattern());
    SimOptions step_opts = opts;
    // OPtran passes CKTtranMaxIter to NIiter; NIiter then applies its global
    // floor of 100 iterations (niiter.c).  Preserve larger explicit budgets.
    step_opts.max_iter = std::max(opts.itl4, 100);
    // OPtran inherits CKTdiagGmin from the immediately preceding CKTop path.
    // In particular, failed Gillespie source stepping leaves it at gminstart.
    step_opts.diag_gmin = std::max(opts.diag_gmin, 0.0);

    double time = 0.0;
    // optran.c seeds CKTdeltaOld with CKTmaxStep, then shifts the
    // attempted interval into slot zero. Startup subdivision must not
    // replace the older intervals used by device truncation.
    double prev_dt = dt_max;
    double prev_prev_dt = dt_max;
    int order = 1;
    int total_iterations = 0;
    bool first_step = true;
    bool tried_delmin = false;
    NewtonResult last_result;

    for (int step = 0; step < kMaxOptranSteps && time < op_final - 1e-18; ) {
        dt = std::min(dt, dt_max);
        dt = std::min(dt, op_final - time);
        if (dt <= 0.0) break;

        const double t = time + dt;
        update_optran_device_timestep(ckt, dt, order);
        update_optran_source_time(ckt, t);
        fill_optran_integrator_context(ckt, t, dt, prev_dt, prev_prev_dt,
                                       order, first_step, method);

        try {
            last_result = newton_solve(ckt, solver, solution, step_opts, workspace);
        } catch (const std::runtime_error&) {
            last_result.converged = false;
        }

        if (opts.verbose) {
            std::cerr << "[optran] step=" << step
                      << " time=" << time
                      << " trial_time=" << t
                      << " dt=" << dt
                      << " order=" << order
                      << " converged=" << last_result.converged
                      << " iters=" << last_result.iterations
                      << " residual=" << last_result.residual << "\n";
        }

        if (!last_result.converged) {
            // optran.c retains CKTrhsOld and state0 from the failed NIiter as
            // the seed for its smaller-delta retry; it backs up time only.
            dt /= kNewtonFailureDtFactor;
            order = 1;
            first_step = (step == 0);
            if (dt < dt_min) {
                if (!tried_delmin) {
                    dt = dt_min;
                    tried_delmin = true;
                } else {
                    cleanup_optran_devices(ckt);
                    solution = entry_solution;
                    restore_state(ckt, entry_state);
                    ckt.integrator_ctx.mode = saved_mode;
                    ckt.integrator_ctx.current_time = saved_time;
                    return {false, total_iterations + last_result.iterations,
                            last_result.residual, last_result.worst_node_idx};
                }
            }
            continue;
        }

        total_iterations += last_result.iterations;

        // optran.c skips LTE testing only for its first successfully solved
        // point.  Later candidates whose CKTtrunc proposal is below 90% of the
        // attempted interval are rejected and repeated from the last accepted
        // state; accepting them first changes the nonlinear trajectory.
        // CKTtrunc permits at most 2x growth, then each device may reduce it.
        // The first successful point jumps directly to optran.c's nextTime
        // label before CKTtrunc runs, so the second point uses the same delta.
        // Growth (up to 2x) starts only after that second successful solve.
        double proposed = first_step ? dt : std::min(2.0 * dt, dt_max);
        if (!first_step) {
            for (const auto& dev : ckt.devices()) {
                proposed = std::min(
                    proposed, dev->compute_trunc(ckt.integrator_ctx, opts));
            }
            if (proposed <= 0.9 * dt) {
                // A truncation rejection likewise keeps the converged trial
                // iterate/state and retries it against the prior history with
                // a smaller delta (optran.c's 650 loop).
                dt = std::max(proposed, dt_min);
                continue;
            }

            if (order == 1) {
                const int saved_order = ckt.integrator_ctx.order;
                ckt.integrator_ctx.order = 2;
                double order2_dt = std::min(2.0 * dt, dt_max);
                for (const auto& dev : ckt.devices()) {
                    order2_dt = std::min(
                        order2_dt,
                        dev->compute_trunc(ckt.integrator_ctx, opts));
                }
                // OPtran uses the second-order truncation proposal for the
                // next interval, even if it subsequently restores order one.
                // Keeping the first-order proposal discards the LTE estimate
                // that was used to choose the next integration order.
                proposed = order2_dt;
                if (order2_dt > 1.05 * dt)
                    order = 2;
                ckt.integrator_ctx.order = saved_order;
            }
        } else if (ns > 0) {
            // ngspice initializes the deeper history only after the first
            // NIiter, when MODEINITTRAN has made state1 equal the solved state.
            std::copy_n(ckt.state1(), ns, ckt.state2());
            std::copy_n(ckt.state1(), ns, ckt.state3());
        }

        time = t;
        step++;
        tried_delmin = false;

        accept_optran_step(ckt, time, solution);
        // Match optran.c: CKTaccept() records the converged point into state0
        // before the history pointers are rotated for the next candidate.
        ckt.rotate_state();

        prev_prev_dt = prev_dt;
        prev_dt = dt;
        first_step = false;
        ckt.integrator_ctx.mode = MODETRAN_BIT | MODEINITPRED_BIT;
        dt = std::max(proposed, dt_min);
    }

    cleanup_optran_devices(ckt);
    ckt.integrator_ctx.mode = saved_mode;
    ckt.integrator_ctx.current_time = saved_time;

    if (time >= op_final - 1e-18) {
        return {true, total_iterations, last_result.residual, last_result.worst_node_idx};
    }

    solution = entry_solution;
    restore_state(ckt, entry_state);
    return {false, total_iterations, last_result.residual, last_result.worst_node_idx};
}

NewtonResult pseudo_transient(Circuit& ckt, ISolver& solver,
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
