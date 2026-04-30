#include "core/dc.hpp"
#include "core/newton.hpp"
#include "core/convergence.hpp"
#include "core/neo_solver.hpp"
#include "devices/vsource.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>

namespace neospice {

static std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

// ngspice-compatible branch current key: for hierarchical device names
// (e.g. "x1.ehf"), prefix the element type letter → "i(e.x1.ehf)".
static std::string make_branch_key(const std::string& dname) {
    std::string lower = to_lower(dname);
    auto dot = lower.rfind('.');
    if (dot != std::string::npos && dot + 1 < lower.size()) {
        char type_letter = lower[dot + 1];
        return "i(" + std::string(1, type_letter) + "." + lower + ")";
    }
    return "i(" + lower + ")";
}

static SimOptions direct_attempt_options(const SimOptions& opts) {
    SimOptions direct_opts = opts;
    direct_opts.max_iter = std::min(opts.max_iter, 25);
    return direct_opts;
}

DCResult solve_dc(Circuit& ckt) {
    auto t_start = std::chrono::steady_clock::now();
    ckt.clear_operating_point();
    const int32_t n = ckt.num_vars();
    const int32_t num_nodes = ckt.num_nodes();

    // 1. Initial guess: zeros + .nodeset hints; .ic as fallback for unpinned nodes.
    // .nodeset wins when both are set.  .ic lets users rescue hard DC starts
    // (ring oscillators, bistable latches, precharged nodes) where the all-zero
    // seed lands in a region of vanishing MOSFET subthreshold conductance.
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

    // 2. Create solver and perform symbolic analysis
    auto solver = std::make_unique<NeoSolver>();
    solver->symbolic(ckt.pattern());

    // Publish SimOptions for BSIM4v7Device (and any future state-storing
    // device) via the same integrator_ctx channel used for CKTmode/ag.
    ckt.integrator_ctx.options = &ckt.options;

    // CKTmode bits (ngspice cktdefs.h; mirrored in devices/bsim4v7/bsim4v7_shim.hpp):
    //   MODEDC=0x70 (mask: DCOP|TRANOP|DCTRANCURVE), MODEDCOP=0x10,
    //   MODEINITJCT=0x200, MODEINITFIX=0x400.
    // Plain DC operating point uses MODEDCOP (0x10), NOT the full MODEDC mask.
    // This matters because BSIM4v7's load function checks MODEDCTRANCURVE to
    // decide whether to compute charges; for a plain DC op that bit must be off.
    constexpr int MODEDCOP_BIT    = 0x10;
    constexpr int MODEINITJCT_BIT = 0x200;
    constexpr int MODEINITFIX_BIT = 0x400;

    SimStatus sim_status;

    // 3. Try newton_solve first (initial junction guess mode)
    ckt.integrator_ctx.mode = MODEDCOP_BIT | MODEINITJCT_BIT;
    NewtonResult result;
    try {
        result = newton_solve(ckt, *solver, solution, direct_attempt_options(ckt.options));
    } catch (const std::runtime_error&) {
        result.converged = false;
    }
    if (result.converged) {
        solution = result.solution;
        sim_status.iterations = result.iterations;
        sim_status.residual = result.residual;
        sim_status.worst_node_idx = result.worst_node_idx;
    } else {
        // 4. Try gmin stepping (fix/iterate mode)
        ckt.integrator_ctx.mode = MODEDCOP_BIT | MODEINITFIX_BIT;
        result = gmin_stepping(ckt, *solver, solution, ckt.options);
        if (result.converged) {
            solution = result.solution;
            sim_status.iterations = result.iterations;
            sim_status.convergence_method = ConvergenceMethod::GMIN_STEPPING;
            sim_status.residual = result.residual;
            sim_status.worst_node_idx = result.worst_node_idx;
            sim_status.gmin_steps = 1;
            sim_status.warnings.push_back("gmin stepping used");
        } else {
            // 5. Try source stepping
            ckt.integrator_ctx.mode = MODEDCOP_BIT | MODEINITFIX_BIT;
            result = source_stepping(ckt, *solver, solution, ckt.options);
            if (result.converged) {
                solution = result.solution;
                sim_status.iterations = result.iterations;
                sim_status.convergence_method = ConvergenceMethod::SOURCE_STEPPING;
                sim_status.residual = result.residual;
                sim_status.worst_node_idx = result.worst_node_idx;
                sim_status.source_steps = 1;
                sim_status.warnings.push_back("source stepping used");
            } else {
                // 6. Try pseudo-transient continuation
                ckt.integrator_ctx.mode = MODEDCOP_BIT | MODEINITFIX_BIT;
                result = pseudo_transient(ckt, *solver, solution, ckt.options);
                if (result.converged) {
                    solution = result.solution;
                    sim_status.iterations = result.iterations;
                    sim_status.convergence_method = ConvergenceMethod::PSEUDO_TRANSIENT;
                    sim_status.residual = result.residual;
                    sim_status.worst_node_idx = result.worst_node_idx;
                    sim_status.warnings.push_back("pseudo-transient continuation used");
                } else {
                    // 7. All failed
                    throw ConvergenceError("DC operating point failed to converge");
                }
            }
        }
    }

    ckt.set_operating_point(solution);

    // 7. Build DCResult
    DCResult dc_result;

    // Node voltages (skip device-internal nodes)
    for (int32_t i = 0; i < num_nodes; ++i) {
        if (ckt.is_internal_node(i)) continue;
        std::string key = "v(" + to_lower(ckt.node_name(i)) + ")";
        dc_result.node_voltages[key] = solution[i];
    }

    // Branch currents for all devices with MNA branch variables
    auto add_dc_current = [&](int32_t br, const std::string& dname) {
        if (br >= 0 && br < n)
            dc_result.branch_currents[make_branch_key(dname)] = solution[br];
    };
    for (const auto& dev : ckt.devices()) {
        add_dc_current(dev->branch_index(), dev->name());
    }

    auto t_end = std::chrono::steady_clock::now();
    sim_status.elapsed_seconds = std::chrono::duration<double>(t_end - t_start).count();
    dc_result.status = sim_status;

    return dc_result;
}

// ---------------------------------------------------------------------------
// DC Sweep
// ---------------------------------------------------------------------------

static std::vector<double> make_sweep_values(double start, double stop, double step) {
    if (step == 0.0) {
        throw std::invalid_argument("DC sweep step cannot be zero");
    }
    std::vector<double> vals;
    // Determine direction
    double tol = std::abs(step) * 1e-9;
    if (step > 0.0) {
        for (double v = start; v <= stop + tol; v += step) {
            vals.push_back(v);
        }
    } else {
        for (double v = start; v >= stop - tol; v += step) {
            vals.push_back(v);
        }
    }
    return vals;
}

DCSweepResult solve_dc_sweep(Circuit& ckt, const std::vector<DCSweepParam>& params) {
    auto t_start = std::chrono::steady_clock::now();
    if (params.empty()) {
        throw std::invalid_argument("DC sweep requires at least one sweep parameter");
    }

    const int32_t n         = ckt.num_vars();
    const int32_t num_nodes = ckt.num_nodes();

    // Find source pointers by name
    // We support up to 2 sweep sources (outer = params[0], inner = params[1])
    auto find_vsource = [&](const std::string& name) -> VSource* {
        std::string lname = to_lower(name);
        for (auto& dev : ckt.devices()) {
            if (auto* vs = dynamic_cast<VSource*>(dev.get())) {
                if (to_lower(vs->name()) == lname) return vs;
            }
        }
        return nullptr;
    };

    VSource* src0 = find_vsource(params[0].source_name);
    if (!src0) {
        throw std::invalid_argument("DC sweep: voltage source '" +
                                    params[0].source_name + "' not found");
    }

    VSource* src1 = nullptr;
    if (params.size() >= 2) {
        src1 = find_vsource(params[1].source_name);
        if (!src1) {
            throw std::invalid_argument("DC sweep: voltage source '" +
                                        params[1].source_name + "' not found");
        }
    }

    // Save original DC values so we can restore them after sweep
    const double orig_val0 = src0->dc_value();
    const double orig_val1 = src1 ? src1->dc_value() : 0.0;

    // Build sweep point lists
    std::vector<double> outer_vals = make_sweep_values(params[0].start,
                                                       params[0].stop,
                                                       params[0].step);
    std::vector<double> inner_vals;
    if (src1) {
        inner_vals = make_sweep_values(params[1].start,
                                      params[1].stop,
                                      params[1].step);
    } else {
        inner_vals = outer_vals;  // single sweep — treat outer as inner
    }

    // Result: inner sweep variable is the x-axis (ngspice convention)
    DCSweepResult sweep_result;
    sweep_result.sweep_var = to_lower(params.back().source_name);

    // Pre-initialise voltage/current vectors
    // Collect node and branch-current names (from a trial DC)
    // We'll build them on the fly as we iterate

    // Solver and initial solution vector
    auto solver = std::make_unique<NeoSolver>();
    solver->symbolic(ckt.pattern());

    ckt.integrator_ctx.options = &ckt.options;

    // DC sweep uses MODEDCTRANCURVE (0x40), NOT the full MODEDC mask.
    // This is the ngspice convention: BSIM4v7's load function checks
    // MODEDCTRANCURVE to decide whether to compute charges during a DC sweep.
    constexpr int MODEDCTRANCURVE_BIT = 0x40;
    constexpr int MODEINITJCT_BIT     = 0x200;
    constexpr int MODEINITFIX_BIT     = 0x400;

    // Initial guess: zeros
    std::vector<double> solution(n, 0.0);
    for (auto& [node_idx, value] : ckt.nodeset) {
        if (node_idx >= 0 && node_idx < n) solution[node_idx] = value;
    }
    for (auto& [node_idx, value] : ckt.ic) {
        if (node_idx >= 0 && node_idx < n) solution[node_idx] = value;
    }

    // Pre-compute extraction slots once (outside sweep loop)
    struct SweepSlot { std::string key; int32_t idx; };
    std::vector<SweepSlot> sv_slots, sc_slots;

    for (int32_t i = 0; i < num_nodes; ++i) {
        if (ckt.is_internal_node(i)) continue;
        sv_slots.push_back({"v(" + to_lower(ckt.node_name(i)) + ")", i});
    }
    auto add_sweep_slot = [&](int32_t br, const std::string& dname) {
        if (br >= 0 && br < n)
            sc_slots.push_back({make_branch_key(dname), br});
    };
    for (const auto& dev : ckt.devices()) {
        add_sweep_slot(dev->branch_index(), dev->name());
    }

    std::vector<std::vector<double>*> sv_ptrs, sc_ptrs;
    for (auto& s : sv_slots)
        sv_ptrs.push_back(&sweep_result.voltages[s.key]);
    for (auto& s : sc_slots)
        sc_ptrs.push_back(&sweep_result.currents[s.key]);

    auto collect_point = [&](double sweep_x) {
        sweep_result.sweep_values.push_back(sweep_x);
        for (std::size_t k = 0; k < sv_slots.size(); ++k)
            sv_ptrs[k]->push_back(solution[sv_slots[k].idx]);
        for (std::size_t k = 0; k < sc_slots.size(); ++k)
            sc_ptrs[k]->push_back(solution[sc_slots[k].idx]);
    };

    // Helper: run Newton at current source settings, using previous solution
    // as warm start. First point uses INITJCT (junction guess); subsequent
    // points use INITFIX so BSIM4 reads from the previous converged state.
    bool first_point = true;
    auto run_newton = [&]() {
        int init_bit = first_point ? MODEINITJCT_BIT : MODEINITFIX_BIT;
        ckt.integrator_ctx.mode = MODEDCTRANCURVE_BIT | init_bit;
        auto res = newton_solve(ckt, *solver, solution, ckt.options);
        if (res.converged) {
            solution = res.solution;
            first_point = false;
            return;
        }
        ckt.integrator_ctx.mode = MODEDCTRANCURVE_BIT | MODEINITFIX_BIT;
        res = gmin_stepping(ckt, *solver, solution, ckt.options);
        if (res.converged) {
            solution = res.solution;
            first_point = false;
            return;
        }
        res = source_stepping(ckt, *solver, solution, ckt.options);
        if (res.converged) {
            solution = res.solution;
            first_point = false;
            return;
        }
        res = pseudo_transient(ckt, *solver, solution, ckt.options);
        if (res.converged) {
            solution = res.solution;
            first_point = false;
            return;
        }
        throw ConvergenceError("DC sweep: convergence failed");
    };

    if (!src1) {
        // Single-variable sweep
        for (double v : inner_vals) {
            src0->set_dc_value(v);
            run_newton();
            collect_point(v);
        }
    } else {
        // Nested sweep: outer = params[0], inner = params[1]
        for (double vout : outer_vals) {
            src0->set_dc_value(vout);
            for (double vin : inner_vals) {
                src1->set_dc_value(vin);
                run_newton();
                collect_point(vin);
            }
        }
    }

    // Restore original source values
    src0->set_dc_value(orig_val0);
    if (src1) src1->set_dc_value(orig_val1);

    auto t_end = std::chrono::steady_clock::now();
    sweep_result.status.converged = true;
    sweep_result.status.elapsed_seconds = std::chrono::duration<double>(t_end - t_start).count();

    return sweep_result;
}

} // namespace neospice
