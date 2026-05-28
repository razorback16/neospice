#include "core/transient.hpp"
#include "core/newton.hpp"
#include "core/convergence.hpp"
#include "core/neo_solver.hpp"
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
#include <cstdio>
#include <functional>
#include <iostream>
#include <set>

namespace neospice {

// ===================================================================
// Named constants — replace magic numbers with descriptive names
// ===================================================================

/// Initial dt = min(tstop/100, tstep) / 10  (ngspice dctran.c:134)
constexpr double kInitialStopDivisor = 100.0;
constexpr double kInitialStepDivisor = 10.0;

/// Maximum dt = min(tstep, tstop/50) (ngspice dctran.c:317)

/// Minimum dt = max_step * kMinTimeStepRatio
constexpr double kMinTimeStepRatio = 1e-11;

/// Safety iteration cap for the main transient loop
constexpr int kMaxTransientIterations = 500000;

/// Steps to settle after a source breakpoint before enabling global LTE
constexpr int kBreakpointSettleSteps = 3;

/// Value indicating "no recent breakpoint" (sentinel for steps_after_bp)
constexpr int kNoRecentBreakpoint = 1000;

/// Factor by which dt is reduced on Newton failure (ngspice dctran.c ~802)
constexpr double kNewtonFailureDtFactor = 8.0;

/// Threshold for device LTE rejection: reject if device_dt < dt * kDeviceLteRejectRatio
constexpr double kDeviceLteRejectRatio = 0.9;

/// Maximum dt growth factor per accepted step
constexpr double kMaxDtGrowthFactor = 2.0;

/// Minimum steps before ringing detection is active after a breakpoint
constexpr int kRingingMinStepsAfterBp = 5;

/// Minimum steps before global LTE proposal is used in next-dt computation
constexpr int kGlobalLteMinStepsAfterBp = 4;

/// Minimum number of accepted steps before LTE evaluation starts
constexpr int kLteMinStepCount = 2;

// CKTmode bits (ngspice cktdefs.h)
constexpr int MODETRANOP_BIT      = 0x20;
constexpr int MODEINITJCT_BIT     = 0x200;
constexpr int MODEINITFLOAT_BIT   = 0x100;
constexpr int MODEINITFIX_BIT     = 0x400;
constexpr int MODETRAN_BIT        = 0x1;
constexpr int MODEINITTRAN_BIT    = 0x1000;
constexpr int MODEINITPRED_BIT    = 0x2000;

// ===================================================================
// Small utility functions
// ===================================================================

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

// ===================================================================
// Helper: Compute DC operating point
// ===================================================================
// Tries Newton, then gmin stepping, source stepping, pseudo-transient.
// Returns the converged solution; throws SimulationError on failure.
static void compute_dc_operating_point(Circuit& ckt, NeoSolver& solver,
                                       std::vector<double>& solution,
                                       int& total_newton_iters) {
    // Initial guess: zeros + .nodeset hints; .ic as fallback for unpinned nodes.
    const int32_t n = ckt.num_vars();
    std::vector<char> pinned(n, 0);
    for (auto& [node_id, value] : ckt.nodeset) {
        int32_t node_idx = static_cast<int32_t>(node_id);
        if (node_idx >= 0 && node_idx < n) {
            solution[node_idx] = value;
            pinned[node_idx] = 1;
        }
    }
    for (auto& [node_id, value] : ckt.ic) {
        int32_t node_idx = static_cast<int32_t>(node_id);
        if (node_idx >= 0 && node_idx < n && !pinned[node_idx]) {
            solution[node_idx] = value;
        }
    }

    // DC preamble — the transient initial operating point uses MODETRANOP
    // (0x20), NOT the full MODEDC mask (0x70) or MODEDCOP (0x10).
    ckt.integrator_ctx.mode = MODETRANOP_BIT | MODEINITJCT_BIT;
    auto result = newton_solve(ckt, solver, solution, ckt.options);
    if (result.converged) {
        total_newton_iters += result.iterations;
        ckt.options.diag_gmin = ckt.options.gshunt;
        return;
    }

    result = gmin_stepping(ckt, solver, solution, ckt.options,
                           MODETRANOP_BIT | MODEINITJCT_BIT,
                           MODETRANOP_BIT | MODEINITFLOAT_BIT);
    if (result.converged) {
        total_newton_iters += result.iterations;
        ckt.options.diag_gmin = ckt.options.gshunt;
        return;
    }

    ckt.integrator_ctx.mode = MODETRANOP_BIT | MODEINITJCT_BIT;
    result = source_stepping(ckt, solver, solution, ckt.options);
    if (result.converged) {
        total_newton_iters += result.iterations;
        ckt.options.diag_gmin = ckt.options.gshunt;
        return;
    }

    ckt.integrator_ctx.mode = MODETRANOP_BIT | MODEINITJCT_BIT;
    result = pseudo_transient(ckt, solver, solution, ckt.options);
    if (ckt.options.verbose)
        std::fprintf(stderr, "[dc] Pseudo-transient %s (%d iters)\n",
                     result.converged ? "converged" : "failed", result.iterations);
    if (result.converged) {
        total_newton_iters += result.iterations;
        ckt.options.diag_gmin = ckt.options.gshunt;
        return;
    }

    SimStatus dc_fail_status;
    dc_fail_status.converged = false;
    dc_fail_status.residual = result.residual;
    dc_fail_status.worst_node_idx = result.worst_node_idx;
    throw SimulationError("DC operating point failed to converge", dc_fail_status);
}

// ===================================================================
// Helper: Apply .ic overrides to solution vector
// ===================================================================
static void apply_ic_overrides(Circuit& ckt, std::vector<double>& solution) {
    const int32_t n = ckt.num_vars();
    for (auto& [node_id, value] : ckt.ic) {
        int32_t node_idx = static_cast<int32_t>(node_id);
        if (node_idx >= 0 && node_idx < n)
            solution[node_idx] = value;
    }
}

// ===================================================================
// Helper structs for result extraction slots
// ===================================================================
struct TranSlot { std::string key; int32_t idx; };

struct ExtractionSlots {
    std::vector<TranSlot> v_slots;
    std::vector<TranSlot> c_slots;
};

// ===================================================================
// Helper: Build extraction slots for output storage
// ===================================================================
static ExtractionSlots build_extraction_slots(const Circuit& ckt, int32_t num_nodes,
                                              int32_t n) {
    ExtractionSlots slots;

    for (int32_t i = 0; i < num_nodes; ++i) {
        if (ckt.is_internal_node(i)) continue;
        slots.v_slots.push_back({"v(" + to_lower(ckt.node_name(i)) + ")", i});
    }

    auto add_slot = [&](int32_t br, const std::string& dname) {
        if (br >= 0 && br < n)
            slots.c_slots.push_back({make_branch_key(dname), br});
    };
    for (const auto& dev : ckt.devices()) {
        if (auto* vs = dynamic_cast<const VSource*>(dev.get()))
            add_slot(vs->branch_index(), dev->name());
        else if (auto* ind = dynamic_cast<const Inductor*>(dev.get()))
            add_slot(ind->branch_index(), dev->name());
        else if (auto* e = dynamic_cast<const VCVS*>(dev.get()))
            add_slot(e->branch_index(), dev->name());
        else if (auto* h = dynamic_cast<const CCVS*>(dev.get()))
            add_slot(h->branch_index(), dev->name());
        else if (auto* enl = dynamic_cast<const NonlinearVCVS*>(dev.get()))
            add_slot(enl->branch_index(), dev->name());
        else if (auto* etbl = dynamic_cast<const TableVCVS*>(dev.get()))
            add_slot(etbl->branch_index(), dev->name());
        else if (auto* bs = dynamic_cast<const ASRCDevice*>(dev.get()))
            if (bs->mode() == ASRCDevice::Mode::VOLTAGE)
                add_slot(bs->branch_index(), dev->name());
    }

    return slots;
}

// ===================================================================
// Helper: Enable transient mode and set integration method on devices
// ===================================================================
static void enable_transient_on_devices(Circuit& ckt, double tstep, bool use_gear) {
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
    }
}

// ===================================================================
// Helper: Initialize DC state on reactive devices and apply UIC overrides
// ===================================================================
static void initialize_device_dc_state(Circuit& ckt, std::vector<double>& solution,
                                       bool uic) {
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

    if (uic) {
        for (auto& dev : ckt.devices()) {
            if (auto* cap = dynamic_cast<Capacitor*>(dev.get())) {
                cap->apply_ic_override();
            } else if (auto* ind = dynamic_cast<Inductor*>(dev.get())) {
                ind->apply_ic_override(solution);
            }
        }
    }
}

// ===================================================================
// Helper: Resolve TL initial conditions into the solution vector
// ===================================================================
// After DC OP (TL is short circuit) and TL IC initialization (history
// filled with IC values), the node voltages don't reflect the TL ICs.
// This solve uses the TL companion model with IC-initialized history
// to make node voltages consistent before storing the t=0 output.
// ngspice achieves this implicitly: its first output point is after
// the first MODEINITTRAN Newton step, so ICs are already resolved.
static void resolve_tl_initial_conditions(Circuit& ckt, NeoSolver& solver,
                                          std::vector<double>& solution,
                                          double tstep) {
    bool tl_has_ic = false;
    for (auto& dev : ckt.devices()) {
        if (auto* tl = dynamic_cast<TransmissionLine*>(dev.get())) {
            if (tl->has_ic()) { tl_has_ic = true; break; }
        }
    }
    if (!tl_has_ic) return;

    ckt.integrator_ctx.current_time = 0.0;
    ckt.integrator_ctx.delta = tstep;
    ckt.integrator_ctx.mode = MODETRANOP_BIT | MODEINITFIX_BIT;
    auto result = newton_solve(ckt, solver, solution, ckt.options);
    if (!result.converged && ckt.options.verbose)
        std::fprintf(stderr, "[tran] TL IC resolve did not converge (%d iters)\n",
                     result.iterations);
}

// ===================================================================
// Helper: Resolve source defaults and set up timestep controller
// ===================================================================
static void resolve_source_defaults(Circuit& ckt, double tstep, double tstop) {
    for (auto& dev : ckt.devices()) {
        if (auto* vs = dynamic_cast<VSource*>(dev.get()))
            vs->resolve_defaults(tstep, tstop);
        else if (auto* is = dynamic_cast<ISource*>(dev.get()))
            is->resolve_defaults(tstep, tstop);
    }
}

// ===================================================================
// Helper: Update timestep on reactive devices for current dt
// ===================================================================
static void update_device_timestep(Circuit& ckt, double dt, int order = 1) {
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

// ===================================================================
// Helper: Update time on sources and behavioral elements
// ===================================================================
static void update_source_time(Circuit& ckt, double t) {
    for (auto& dev : ckt.devices()) {
        if (auto* vs = dynamic_cast<VSource*>(dev.get())) {
            vs->set_time(t);
        } else if (auto* is = dynamic_cast<ISource*>(dev.get())) {
            is->set_time(t);
        } else if (auto* bs = dynamic_cast<ASRCDevice*>(dev.get())) {
            bs->set_time(t);
        }
    }
}

// ===================================================================
// Helper: Fill integrator context with coefficients for current step
// ===================================================================
static void fill_integrator_context(Circuit& ckt, double dt, int step_count,
                                    const TimeStepController& ctrl,
                                    double prev_prev_dt) {
    bool first_step = (step_count == 0);
    int cur_order = ctrl.order();
    ckt.integrator_ctx.order = cur_order;
    ckt.integrator_ctx.delta = dt;
    ckt.integrator_ctx.current_time = ctrl.current_time() + dt;
    ckt.integrator_ctx.delta_old[0] = dt;
    ckt.integrator_ctx.delta_old[1] = first_step ? dt : ctrl.prev_dt();
    ckt.integrator_ctx.delta_old[2] = prev_prev_dt;
    ckt.integrator_ctx.mode = MODETRAN_BIT | (first_step ? MODEINITTRAN_BIT : MODEINITPRED_BIT);

    // Determine effective method: trap unless user chose gear or
    // ringing detection temporarily switched to gear.
    bool eff_gear = (ckt.integrator_ctx.integrate_method == 1);

    if (cur_order == 1) {
        // Backward Euler: y'(t_n) ~ (y_n - y_{n-1}) / dt
        ckt.integrator_ctx.ag[0] =  1.0 / dt;
        ckt.integrator_ctx.ag[1] = -1.0 / dt;
        ckt.integrator_ctx.ag[2] =  0.0;
        ckt.integrator_ctx.xmu_ratio = 0.0;  // BE: no previous-derivative term
    } else if (!eff_gear) {
        // Trapezoidal with xmu damping: i_n = ag[0]*(q_n - q_{n-1}) - xmu_ratio*i_{n-1}
        double xmu = ckt.options.xmu;
        double one_minus_xmu = 1.0 - xmu;
        ckt.integrator_ctx.ag[0] =  1.0 / (dt * one_minus_xmu);
        ckt.integrator_ctx.ag[1] = -1.0 / (dt * one_minus_xmu);
        ckt.integrator_ctx.ag[2] =  0.0;
        ckt.integrator_ctx.xmu_ratio = xmu / one_minus_xmu;
    } else {
        // Variable-step Gear-2 (BDF2)
        double h_old = ctrl.prev_dt();
        if (h_old > 0.0) {
            double sum = dt + h_old;
            ckt.integrator_ctx.ag[0] =  (2.0 * dt + h_old) / (dt    * sum  );
            ckt.integrator_ctx.ag[1] = -(dt + h_old)        / (dt    * h_old);
            ckt.integrator_ctx.ag[2] =  dt                  / (h_old * sum  );
        } else {
            // Fallback to BE if prev_dt not yet set
            ckt.integrator_ctx.ag[0] =  1.0 / dt;
            ckt.integrator_ctx.ag[1] = -1.0 / dt;
            ckt.integrator_ctx.ag[2] =  0.0;
        }
        ckt.integrator_ctx.xmu_ratio = 0.0;  // Gear: no previous-derivative term
    }
}

// ===================================================================
// Helper: Clean up transient state on all devices
// ===================================================================
static void cleanup_transient_devices(Circuit& ckt) {
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
}

// ===================================================================
// Helper: Evaluate global and device LTE; return whether step is accepted.
// On rejection, sets proposed_dt_out to the suggested next timestep.
// ===================================================================
static bool evaluate_lte(Circuit& ckt, TimeStepController& ctrl,
                         const std::vector<double>& solution,
                         const std::vector<double>& sol_prev,
                         const std::vector<double>& sol_prev2,
                         int32_t num_nodes, double dt, double dt_min,
                         int step_count, int steps_after_bp,
                         double& device_dt_out) {
    bool accepted = true;

    // Device-specific LTE for step rejection (matches ngspice CKTtrunc).
    device_dt_out = 1e30;
    if (step_count >= kLteMinStepCount) {
        for (const auto& dev : ckt.devices()) {
            device_dt_out = std::min(device_dt_out,
                dev->compute_trunc(ckt.integrator_ctx, ckt.options));
        }
        device_dt_out = std::min(kMaxDtGrowthFactor * dt, device_dt_out);
        if (device_dt_out < dt * kDeviceLteRejectRatio && dt > dt_min * 1.01) {
            accepted = false;
            ctrl.record_rejection();
            ctrl.set_dt(dt);
        }
    }

    // Global node-voltage LTE — gated on .option newtrunc or .option interp.
    // When enabled: proposal-only (never rejects when devices provide LTE).
    // When disabled: no global voltage LTE at all (ngspice default behavior).
    bool has_device_lte = (device_dt_out < 1e29);
    if ((ckt.options.newtrunc || ckt.options.interp) &&
        step_count >= kLteMinStepCount && steps_after_bp >= kBreakpointSettleSteps) {
        ctrl.set_dt(dt);
        bool global_ok = ctrl.evaluate_step(solution, sol_prev, sol_prev2,
                                            num_nodes, ckt.options);
        if (!global_ok && !has_device_lte && accepted) {
            if (dt > dt_min * 1.01) {
                accepted = false;
            }
        }
    }

    return accepted;
}

// ===================================================================
// Helper: Accept step on all reactive / stateful devices
// ===================================================================
static void accept_step_on_devices(Circuit& ckt, double t,
                                   const std::vector<double>& solution) {
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
    }
}

// ===================================================================
// Helper: Interpolate solution and store output points
// ===================================================================
static void interpolate_and_store_outputs(
    const TimeStepController& ctrl, double dt, double prev_prev_dt,
    double tstep, double tstop, int step_count, int32_t n,
    const std::vector<double>& solution,
    const std::vector<double>& sol_prev,
    const std::vector<double>& sol_prev2,
    double& next_output_time,
    const std::function<void(double, const std::vector<double>&)>& store_point)
{
    while (next_output_time <= ctrl.current_time() + 1e-18 && next_output_time <= tstop + 1e-18) {
        if (std::abs(ctrl.current_time() - next_output_time) < 1e-18) {
            // Landed exactly on output point
            store_point(next_output_time, solution);
        } else if (step_count >= kLteMinStepCount && prev_prev_dt > 1e-20) {
            // Quadratic (Lagrange) interpolation using 3 history points
            double t2 = ctrl.current_time();
            double t1 = t2 - dt;
            double t0 = t1 - prev_prev_dt;
            double t_out = next_output_time;
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
}

// ===================================================================
// Helper: Ringing detection and integration method switching
// ===================================================================
static void detect_and_handle_ringing(
    Circuit& ckt, TimeStepController& ctrl,
    const std::vector<double>& solution,
    const std::vector<double>& sol_prev,
    const std::vector<double>& sol_prev2,
    const std::vector<double>& sol_prev3,
    int32_t num_nodes, int step_count, bool use_gear, int steps_after_bp)
{
    if (step_count < 3 || use_gear || steps_after_bp < kRingingMinStepsAfterBp)
        return;

    ctrl.check_ringing(solution, sol_prev, sol_prev2, sol_prev3,
                       num_nodes, ckt.options);
    ctrl.tick_cooldown();

    // Switch integration method based on ringing state
    int new_method = (ctrl.ringing_detected() || ctrl.ringing_cooldown() > 0) ? 1 : 0;
    if (new_method != ckt.integrator_ctx.integrate_method) {
        ckt.integrator_ctx.integrate_method = new_method;
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

// ===================================================================
// Cached typed device pointers — built once, eliminates per-step dynamic_cast
// ===================================================================
struct CachedDevicePtrs {
    std::vector<Capacitor*> caps;
    std::vector<Inductor*> inds;
    std::vector<CoupledInductor*> coupled;
    std::vector<VSource*> vsrc;
    std::vector<ISource*> isrc;
    std::vector<ASRCDevice*> asrc;
    std::vector<TransmissionLine*> tline;
    std::vector<LossyTransmissionLine*> ltline;

    void build(Circuit& ckt) {
        for (auto& dev : ckt.devices()) {
            if (auto* p = dynamic_cast<Capacitor*>(dev.get())) caps.push_back(p);
            else if (auto* p = dynamic_cast<Inductor*>(dev.get())) inds.push_back(p);
            else if (auto* p = dynamic_cast<CoupledInductor*>(dev.get())) coupled.push_back(p);
            else if (auto* p = dynamic_cast<VSource*>(dev.get())) vsrc.push_back(p);
            else if (auto* p = dynamic_cast<ISource*>(dev.get())) isrc.push_back(p);
            else if (auto* p = dynamic_cast<ASRCDevice*>(dev.get())) asrc.push_back(p);
            else if (auto* p = dynamic_cast<TransmissionLine*>(dev.get())) tline.push_back(p);
            else if (auto* p = dynamic_cast<LossyTransmissionLine*>(dev.get())) ltline.push_back(p);
        }
    }
};

// ===================================================================
// Cached-pointer overloads of per-step helpers (no dynamic_cast)
// ===================================================================

static void update_device_timestep(const CachedDevicePtrs& c, double dt, int order = 1) {
    for (auto* cap : c.caps) { cap->set_transient(dt); cap->set_integrator_order(order); }
    for (auto* ind : c.inds) { ind->set_transient(dt); ind->set_integrator_order(order); }
    for (auto* ki : c.coupled) { ki->set_transient(dt); ki->set_integrator_order(order); }
}

static void update_source_time(const CachedDevicePtrs& c, double t) {
    for (auto* vs : c.vsrc) vs->set_time(t);
    for (auto* is : c.isrc) is->set_time(t);
    for (auto* bs : c.asrc) bs->set_time(t);
}

static void accept_step_on_devices(const CachedDevicePtrs& c, double t,
                                   const std::vector<double>& solution) {
    for (auto* cap : c.caps) cap->accept_step_from_solution(solution);
    for (auto* ind : c.inds) ind->accept_step_from_solution(solution);
    for (auto* ki : c.coupled) ki->accept_step_from_solution(solution);
    for (auto* tl : c.tline) tl->accept_step(t, solution);
    for (auto* ltl : c.ltline) ltl->accept_step(t, solution);
    for (auto* asrc : c.asrc) { asrc->expression().accept_ddt(); asrc->expression().accept_idt(); }
}

static void detect_and_handle_ringing(
    Circuit& ckt, const CachedDevicePtrs& c, TimeStepController& ctrl,
    const std::vector<double>& solution,
    const std::vector<double>& sol_prev,
    const std::vector<double>& sol_prev2,
    const std::vector<double>& sol_prev3,
    int32_t num_nodes, int step_count, bool use_gear, int steps_after_bp)
{
    if (step_count < 3 || use_gear || steps_after_bp < kRingingMinStepsAfterBp)
        return;

    ctrl.check_ringing(solution, sol_prev, sol_prev2, sol_prev3,
                       num_nodes, ckt.options);
    ctrl.tick_cooldown();

    // Switch integration method based on ringing state
    int new_method = (ctrl.ringing_detected() || ctrl.ringing_cooldown() > 0) ? 1 : 0;
    if (new_method != ckt.integrator_ctx.integrate_method) {
        ckt.integrator_ctx.integrate_method = new_method;
        for (auto* cap : c.caps) cap->set_integration_method(new_method);
        for (auto* ind : c.inds) ind->set_integration_method(new_method);
        for (auto* ki : c.coupled) ki->set_integration_method(new_method);
    }
}

// ===================================================================
// Main entry point: solve_transient
// ===================================================================
TransientResult solve_transient(Circuit& ckt, double tstep, double tstop,
                                bool uic, double tstart) {
    auto t_start = std::chrono::steady_clock::now();
    int total_newton_iters = 0;
    const int32_t n = ckt.num_vars();
    const int32_t num_nodes = ckt.num_nodes();

    // Publish SimOptions so BSIM4v7Device can read user-configured values.
    ckt.integrator_ctx.options = &ckt.options;
    const bool use_gear = (ckt.options.method == "gear");

    // ---------------------------------------------------------------
    // 1. DC operating point
    // ---------------------------------------------------------------
    // ngspice always computes a DC OP, even with UIC — the MODEUIC flag
    // modifies device loading but doesn't skip the solve.  We do the same:
    // the DC OP establishes baseline node voltages, then apply_ic_overrides
    // and initialize_device_dc_state override with user-specified IC values.
    std::vector<double> solution(n, 0.0);
    auto dc_solver = std::make_unique<NeoSolver>();
    dc_solver->symbolic(ckt.pattern());
    try {
        compute_dc_operating_point(ckt, *dc_solver, solution, total_newton_iters);
    } catch (const SimulationError& e) {
        if (!ckt.options.no_throw) throw;
        TransientResult fail_result;
        fail_result.status = e.status();
        auto t_end = std::chrono::steady_clock::now();
        fail_result.status.elapsed_seconds = std::chrono::duration<double>(t_end - t_start).count();
        return fail_result;
    }

    auto solver = std::make_unique<NeoSolver>();
    solver->symbolic(ckt.pattern());
    NewtonWorkspace newton_workspace(ckt.pattern());

    // Cache typed device pointers (one-time cost, eliminates per-step dynamic_cast)
    CachedDevicePtrs cached;
    cached.build(ckt);

    if (ckt.options.verbose) {
        std::cerr << "[tran] DC OP done" << (uic ? " (UIC)" : "") << ". Checking for large values:\n";
        for (int32_t i = 0; i < num_nodes; ++i) {
            if (std::abs(solution[i]) > 100.0)
                std::cerr << "  " << ckt.node_name(i) << " = " << solution[i] << "\n";
        }
    }

    // Apply .ic overrides
    apply_ic_overrides(ckt, solution);

    // ---------------------------------------------------------------
    // 2. Prepare result storage
    // ---------------------------------------------------------------
    TransientResult tran_result;
    auto slots = build_extraction_slots(ckt, num_nodes, n);

    std::vector<std::vector<double>*> v_ptrs, c_ptrs;
    for (auto& s : slots.v_slots)
        v_ptrs.push_back(&tran_result.voltages[s.key]);
    for (auto& s : slots.c_slots)
        c_ptrs.push_back(&tran_result.currents[s.key]);

    // Initialize dense arrays for handle-based access
    tran_result.voltages_dense.resize(num_nodes);
    tran_result.currents_dense.resize(ckt.devices().size());

    auto store_point = [&](double t, const std::vector<double>& sol) {
        if (t < tstart - 1e-18) return;  // skip points before tstart
        tran_result.time.push_back(t);
        for (std::size_t k = 0; k < slots.v_slots.size(); ++k)
            v_ptrs[k]->push_back(sol[slots.v_slots[k].idx]);
        for (std::size_t k = 0; k < slots.c_slots.size(); ++k)
            c_ptrs[k]->push_back(sol[slots.c_slots[k].idx]);

        // Dense node voltage array (ALL nodes, including internal)
        for (int32_t i = 0; i < num_nodes; ++i)
            tran_result.voltages_dense[i].push_back(sol[i]);
        // Dense branch current array (indexed by device ordinal)
        for (std::size_t d = 0; d < ckt.devices().size(); ++d) {
            int32_t br = ckt.devices()[d]->branch_index();
            if (br >= 0 && br < n)
                tran_result.currents_dense[d].push_back(sol[br]);
            else
                tran_result.currents_dense[d].push_back(0.0);
        }
    };

    // ---------------------------------------------------------------
    // 3. Initialize transient state
    // ---------------------------------------------------------------
    enable_transient_on_devices(ckt, tstep, use_gear);
    initialize_device_dc_state(ckt, solution, uic);
    resolve_tl_initial_conditions(ckt, *solver, solution, tstep);

    // Seed state history from DC operating point.
    // ngspice bcopy's state0→state1 (dctran.c:343), then at the start of
    // each step it rotates: state0→state1→state2→…  After the first
    // rotation, state1=DC and state2=DC, so the MODEINITTRAN predictor
    // computes  v = (1+xfact)*state1 − xfact*state2 = DC  (perfect).
    // We don't rotate before solving, so seed state2 too.
    {
        const int32_t ns = ckt.num_states();
        if (ns > 0) {
            std::copy_n(ckt.state0(), ns, ckt.state1());
            std::copy_n(ckt.state0(), ns, ckt.state2());
            std::copy_n(ckt.state0(), ns, ckt.state3());
        }
    }

    // Store t=0 output point
    store_point(0.0, solution);

    // Resolve PULSE/SIN default parameters before breakpoint collection
    resolve_source_defaults(ckt, tstep, tstop);

    // ---------------------------------------------------------------
    // 4. Set up timestep controller
    // ---------------------------------------------------------------
    // ngspice dctran.c:317: maxStep = min(tstep, (tstop-tstart)/50)
    const double max_step = std::min(tstep, tstop / 50.0);
    const double dt_min = max_step * kMinTimeStepRatio;
    const double dt_max = max_step;

    TimeStepController ctrl;
    ctrl.init(tstep, tstop, max_step);
    collect_breakpoints(ckt, ctrl, tstop);

    const bool interp = ckt.options.interp;
    if (interp) {
        int num_output = static_cast<int>(std::round(tstop / tstep));
        for (int i = 1; i <= num_output; ++i) {
            ctrl.add_breakpoint(i * tstep);
        }
    }

    // History for LTE — ring buffer of 3 vectors (pointer rotation instead of 3 copies)
    std::vector<double> hist_buf0 = solution;
    std::vector<double> hist_buf1 = solution;
    std::vector<double> hist_buf2 = solution;
    std::vector<double>* sol_prev  = &hist_buf0;
    std::vector<double>* sol_prev2 = &hist_buf1;
    std::vector<double>* sol_prev3 = &hist_buf2;
    double next_output_time = tstep;  // only used in interp mode
    int step_count = 0;

    // ---------------------------------------------------------------
    // 5. Adaptive time-stepping loop
    // ---------------------------------------------------------------
    // ngspice dctran.c:134: delta = MIN(finalTime/100, step) / 10
    double dt = std::min(tstop / kInitialStopDivisor, tstep) / kInitialStepDivisor;
    ckt.integrator_ctx.integrate_method = use_gear ? 1 : 0;

    double saved_delta = dt;
    int steps_after_bp = kNoRecentBreakpoint;

    // ngspice dctran.c:548-577: at the first breakpoint (t=0 for PULSE with TD=0),
    // clamp dt by breakpoint gap and reduce further for firsttime.
    // Use ngspice's CKTsaveDelta initial value (finalTime/50, dctran.c:317)
    // instead of our already-reduced dt; this prevents the formula from
    // double-reducing dt when dt < bp_gap (as in the CMOS inverter circuit).
    {
        double bp_gap = ctrl.next_breakpoint_gap();
        if (bp_gap < tstop) {
            double init_save_delta = tstop / 50.0;  // ngspice dctran.c:317
            dt = std::min(dt, 0.1 * std::min(init_save_delta, bp_gap));
            dt /= 10;  // firsttime extra reduction (dctran.c:569)
            dt = std::max(dt, dt_min * 2.0);
        }
    }
    saved_delta = dt;
    double prev_prev_dt = dt;

    int total_iterations = 0;
    bool loop_converged = true;  // set false if we break due to no_throw failure
    bool tried_delmin = false;   // ngspice: one last try at dt_min before aborting
    std::set<std::string> soa_warned;  // throttle: one SOA warning per device per sim
    int order_promote_cooldown = 0;

    while (ctrl.current_time() < tstop - 1e-18) {
        if (++total_iterations > kMaxTransientIterations) {
            SimStatus iter_fail_status;
            iter_fail_status.converged = false;
            iter_fail_status.iterations = total_newton_iters;
            iter_fail_status.elapsed_seconds = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - t_start).count();
            if (!ckt.options.no_throw) {
                throw SimulationError(
                    "Transient exceeded " + std::to_string(kMaxTransientIterations) +
                    " iterations at t=" + std::to_string(ctrl.current_time()) +
                    " step_count=" + std::to_string(step_count) +
                    " dt=" + std::to_string(dt) +
                    " order=" + std::to_string(ctrl.order()) +
                    " steps_after_bp=" + std::to_string(steps_after_bp),
                    iter_fail_status);
            }
            // no_throw: break out of loop with partial results
            loop_converged = false;
            break;
        }

        // Clamp dt to bounds and breakpoints
        dt = std::min(dt, dt_max);
        dt = std::max(dt, dt_min);
        double unclamped_dt = dt;
        dt = ctrl.clamp_to_breakpoint(dt);
        if (dt < unclamped_dt - 1e-18)
            saved_delta = unclamped_dt;
        dt = ctrl.clamp_to_end(dt);
        if (dt < 1e-20) break;

        double t = ctrl.current_time() + dt;

        // Prepare devices and integrator for this timestep
        update_device_timestep(cached, dt, ctrl.order());
        update_source_time(cached, t);
        fill_integrator_context(ckt, dt, step_count, ctrl, prev_prev_dt);

        // Newton-Raphson solve
        SimOptions tran_opts = ckt.options;
        tran_opts.max_iter = ckt.options.itl4;
        auto nr = newton_solve(ckt, *solver, solution, tran_opts, newton_workspace);
        if (!nr.converged) {
            if (ckt.options.verbose)
                std::cerr << "[tran] NEWTON FAIL sc=" << step_count << " dt=" << dt << " t=" << t << "\n";
            dt /= kNewtonFailureDtFactor;
            ctrl.set_order(1);  // ngspice: drop to backward Euler on Newton failure
            order_promote_cooldown = 5;
            if (dt < dt_min) {
                if (!tried_delmin) {
                    dt = dt_min;
                    tried_delmin = true;
                } else {
                    cleanup_transient_devices(ckt);
                    SimStatus nr_fail_status;
                    nr_fail_status.converged = false;
                    nr_fail_status.iterations = total_newton_iters;
                    nr_fail_status.min_timestep = dt;
                    nr_fail_status.elapsed_seconds = std::chrono::duration<double>(
                        std::chrono::steady_clock::now() - t_start).count();
                    if (!ckt.options.no_throw) {
                        throw SimulationError("Transient failed to converge at t=" + std::to_string(t),
                                              nr_fail_status);
                    }
                    tran_result.rejected_steps = ctrl.rejected_count();
                    tran_result.status = nr_fail_status;
                    return tran_result;
                }
            }
            solution = *sol_prev;   // restore last accepted solution
            continue;
        }
        // newton_solve modifies solution in-place; no copy needed
        total_newton_iters += nr.iterations;

        // Evaluate LTE — reject step if error too large
        double device_dt = 1e30;
        bool accepted = evaluate_lte(ckt, ctrl, solution, *sol_prev, *sol_prev2,
                                     num_nodes, dt, dt_min, step_count, steps_after_bp,
                                     device_dt);
        if (!accepted) {
            if (ckt.options.verbose)
                std::cerr << "[tran] LTE REJECT sc=" << step_count << " dt=" << dt << " proposed=" << ctrl.proposed_dt() << "\n";
            solution = *sol_prev;
            double proposed = ctrl.proposed_dt();
            if (device_dt < proposed)
                proposed = device_dt;
            dt = std::max(proposed, dt_min);
            continue;
        }

        // Accept step — advance controller
        double accepted_dt = dt;   // save before breakpoint handler modifies dt
        prev_prev_dt = ctrl.prev_dt();
        ctrl.set_prev_dt(dt);
        ctrl.advance(dt);
        ctrl.set_dt(dt);
        step_count++;
        tried_delmin = false;
        if (steps_after_bp < kNoRecentBreakpoint) ++steps_after_bp;

        // After crossing a source breakpoint: reduce dt and drop order
        if (ctrl.crossed_source_breakpoint() && step_count > 2) {
            steps_after_bp = 0;
            ctrl.set_order(1);  // ngspice: CKTorder = 1 at breakpoints (dctran.c:548)
            double bp_gap = ctrl.next_breakpoint_gap();
            double scale = ckt.options.restart_step_scale;
            if (ctrl.last_bp_type() == TimeStepController::BreakpointType::SOFT) {
                scale = std::sqrt(scale);
            }
            dt = std::min(dt, scale * std::min(saved_delta, bp_gap));
            dt = std::max(dt, dt_min * 2.0);
        }

        // LTE-conditioned order promotion (ngspice dctran.c:862-873)
        if (order_promote_cooldown > 0) --order_promote_cooldown;
        if (ctrl.order() == 1 && step_count >= 2 && order_promote_cooldown == 0) {
            ckt.integrator_ctx.order = 2;
            double device_dt_order2 = 1e30;
            for (const auto& dev : ckt.devices()) {
                device_dt_order2 = std::min(device_dt_order2,
                    dev->compute_trunc(ckt.integrator_ctx, ckt.options));
            }
            if (device_dt_order2 > 1.05 * dt) {
                ctrl.set_order(2);
            }
            ckt.integrator_ctx.order = ctrl.order();
        }

        // Rotate state history ring and accept on devices
        ckt.rotate_state();
        accept_step_on_devices(cached, ctrl.current_time(), solution);

        // SOA (Safe Operating Area) checking — informational, once per device
        for (auto& dev : ckt.devices()) {
            if (soa_warned.count(dev->name())) continue;
            auto soa = dev->check_soa(solution);
            if (!soa.ok) {
                std::fprintf(stderr, "Warning: SOA violation in %s: %s = %g (limit %g)\n",
                             dev->name().c_str(), soa.param_name.c_str(),
                             soa.value, soa.limit);
                soa_warned.insert(dev->name());
            }
        }

        if (interp) {
            // Interpolated uniform grid output (.option interp)
            interpolate_and_store_outputs(ctrl, accepted_dt, prev_prev_dt, tstep, tstop,
                                          step_count, n, solution, *sol_prev,
                                          *sol_prev2, next_output_time, store_point);
        } else {
            // Raw adaptive timestep output (default)
            store_point(ctrl.current_time(), solution);
        }

        // Rotate ring buffer: oldest buffer gets current solution (1 copy instead of 3)
        {
            std::vector<double>* tmp = sol_prev3;
            *tmp = solution;
            sol_prev3 = sol_prev2;
            sol_prev2 = sol_prev;
            sol_prev = tmp;
        }

        // Ringing detection and integration method switching
        detect_and_handle_ringing(ckt, cached, ctrl, solution, *sol_prev, *sol_prev2,
                                  *sol_prev3, num_nodes, step_count, use_gear,
                                  steps_after_bp);

        // Propose next dt from device and global LTE
        if (step_count >= kLteMinStepCount) {
            double proposed = device_dt;
            if ((ckt.options.newtrunc || ckt.options.interp) && steps_after_bp >= kGlobalLteMinStepsAfterBp) {
                proposed = std::min(proposed, ctrl.proposed_dt());
            }
            proposed = std::min(proposed, kMaxDtGrowthFactor * dt);
            dt = std::max(proposed, dt_min);
        }
    }

    // ---------------------------------------------------------------
    // 6. Clean up
    // ---------------------------------------------------------------
    cleanup_transient_devices(ckt);

    tran_result.rejected_steps = ctrl.rejected_count();

    auto t_end = std::chrono::steady_clock::now();
    tran_result.status.converged = loop_converged;
    tran_result.status.iterations = total_newton_iters;
    tran_result.status.elapsed_seconds = std::chrono::duration<double>(t_end - t_start).count();

    return tran_result;
}

TransientResult solve_transient(Circuit& ckt, double tstep, double tstop,
                                const TransientOptions& opts) {
    if (opts.ic_from) {
        for (const auto& [key, val] : opts.ic_from->node_voltages) {
            if (key.size() > 3 && key.front() == 'v' && key[1] == '(') {
                std::string node_name = key.substr(2, key.size() - 3);
                int32_t idx = ckt.node_index(node_name);
                if (idx >= 0) {
                    ckt.ic[NodeId{idx}] = val;
                }
            }
        }
    }
    return solve_transient(ckt, tstep, tstop, opts.uic, opts.tstart);
}

} // namespace neospice
