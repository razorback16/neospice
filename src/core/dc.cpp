#include "core/dc.hpp"
#include "core/newton.hpp"
#include "core/convergence.hpp"
#include "core/neo_solver.hpp"
#include "core/topology.hpp"
#include "devices/vsource.hpp"
#include "devices/isource.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
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
    direct_opts.max_iter = opts.itl1;
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

    // 2. Create solver and perform symbolic analysis
    auto solver = std::make_unique<NeoSolver>();
    solver->symbolic(ckt.pattern());

    // Publish SimOptions for BSIM4v7Device (and any future state-storing
    // device) via the same integrator_ctx channel used for CKTmode/ag.
    ckt.integrator_ctx.options = &ckt.options;

    // Run topology check and emit diagnostics
    check_topology(ckt);

    // CKTmode bits (ngspice cktdefs.h; mirrored in devices/bsim4v7/bsim4v7_shim.hpp):
    //   MODEDC=0x70 (mask: DCOP|TRANOP|DCTRANCURVE), MODEDCOP=0x10,
    //   MODEINITJCT=0x200, MODEINITFIX=0x400.
    // Plain DC operating point uses MODEDCOP (0x10), NOT the full MODEDC mask.
    // This matters because BSIM4v7's load function checks MODEDCTRANCURVE to
    // decide whether to compute charges; for a plain DC op that bit must be off.
    constexpr int MODEDCOP_BIT       = 0x10;
    constexpr int MODEINITJCT_BIT    = 0x200;
    constexpr int MODEINITFLOAT_BIT  = 0x100;
    constexpr int MODEINITFIX_BIT    = 0x400;

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
        sim_status.iterations = result.iterations;
        sim_status.residual = result.residual;
        sim_status.worst_node_idx = result.worst_node_idx;
    }
    if (!result.converged) {
        if (ckt.options.verbose)
            std::cerr << "[dc] direct Newton failed (iters=" << result.iterations
                      << " residual=" << result.residual << "), trying gmin stepping\n";
        // 4. Try gmin stepping
        try {
            result = gmin_stepping(ckt, *solver, solution, ckt.options,
                                   MODEDCOP_BIT | MODEINITJCT_BIT,
                                   MODEDCOP_BIT | MODEINITFLOAT_BIT);
            if (ckt.options.verbose)
                std::cerr << "[dc] gmin stepping: converged=" << result.converged
                          << " iters=" << result.iterations
                          << " residual=" << result.residual << "\n";
        } catch (const std::runtime_error& e) {
            result.converged = false;
            if (ckt.options.verbose)
                std::cerr << "[dc] gmin stepping threw: " << e.what() << "\n";
        }
        if (result.converged) {
            sim_status.iterations = result.iterations;
            sim_status.convergence_method = ConvergenceMethod::GMIN_STEPPING;
            sim_status.residual = result.residual;
            sim_status.worst_node_idx = result.worst_node_idx;
            sim_status.gmin_steps = 1;
            sim_status.warnings.push_back("gmin stepping used");
        }
        if (!result.converged) {
            if (ckt.options.verbose)
                std::cerr << "[dc] trying true gmin stepping\n";
            // 4b. Try true gmin stepping (modifies device-level gmin)
            try {
                result = true_gmin_stepping(ckt, *solver, solution, ckt.options,
                                            MODEDCOP_BIT | MODEINITJCT_BIT,
                                            MODEDCOP_BIT | MODEINITFLOAT_BIT);
                if (ckt.options.verbose)
                    std::cerr << "[dc] true gmin stepping: converged=" << result.converged
                              << " iters=" << result.iterations
                              << " residual=" << result.residual << "\n";
            } catch (const std::runtime_error& e) {
                result.converged = false;
                if (ckt.options.verbose)
                    std::cerr << "[dc] true gmin stepping threw: " << e.what() << "\n";
            }
            if (result.converged) {
                sim_status.iterations = result.iterations;
                sim_status.convergence_method = ConvergenceMethod::GMIN_STEPPING;
                sim_status.residual = result.residual;
                sim_status.worst_node_idx = result.worst_node_idx;
                sim_status.gmin_steps = 1;
                sim_status.warnings.push_back("true gmin stepping used");
            }
        }
        if (!result.converged) {
            if (ckt.options.verbose)
                std::cerr << "[dc] trying source stepping\n";
            // 5. Try source stepping
            ckt.integrator_ctx.mode = MODEDCOP_BIT | MODEINITJCT_BIT;
            try {
                result = source_stepping(ckt, *solver, solution, ckt.options);
                if (ckt.options.verbose)
                    std::cerr << "[dc] source stepping: converged=" << result.converged
                              << " iters=" << result.iterations
                              << " residual=" << result.residual << "\n";
            } catch (const std::runtime_error& e) {
                result.converged = false;
                if (ckt.options.verbose)
                    std::cerr << "[dc] source stepping threw: " << e.what() << "\n";
            }
            if (result.converged) {
                sim_status.iterations = result.iterations;
                sim_status.convergence_method = ConvergenceMethod::SOURCE_STEPPING;
                sim_status.residual = result.residual;
                sim_status.worst_node_idx = result.worst_node_idx;
                sim_status.source_steps = 1;
                sim_status.warnings.push_back("source stepping used");
            }
        }
        if (!result.converged) {
            if (ckt.options.verbose)
                std::cerr << "[dc] trying pseudo-transient\n";
            // 6. Try pseudo-transient continuation
            ckt.integrator_ctx.mode = MODEDCOP_BIT | MODEINITJCT_BIT;
            try {
                result = pseudo_transient(ckt, *solver, solution, ckt.options);
                if (ckt.options.verbose)
                    std::cerr << "[dc] pseudo-transient: converged=" << result.converged
                              << " iters=" << result.iterations
                              << " residual=" << result.residual << "\n";
            } catch (const std::runtime_error& e) {
                result.converged = false;
                if (ckt.options.verbose)
                    std::cerr << "[dc] pseudo-transient threw: " << e.what() << "\n";
            }
            if (result.converged) {
                sim_status.iterations = result.iterations;
                sim_status.convergence_method = ConvergenceMethod::PSEUDO_TRANSIENT;
                sim_status.residual = result.residual;
                sim_status.worst_node_idx = result.worst_node_idx;
                sim_status.warnings.push_back("pseudo-transient continuation used");
            }
        }
        if (!result.converged) {
            // 7. All failed
            sim_status.converged = false;
            sim_status.iterations = result.iterations;
            sim_status.residual = result.residual;
            sim_status.worst_node_idx = result.worst_node_idx;
            if (!ckt.options.no_throw) {
                throw SimulationError("DC operating point failed to converge", sim_status);
            }
            // Fall through to build partial result with converged=false
        }
    }

    // Persist diag_gmin baseline after DC convergence (ngspice: every path out
    // of CKTop sets CKTdiagGmin = CKTgshunt, never CKTgmin).
    ckt.options.diag_gmin = ckt.options.gshunt;

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

    // Dense node voltage array (ALL nodes, including internal)
    dc_result.node_voltages_dense.assign(num_nodes, 0.0);
    for (int32_t i = 0; i < num_nodes; ++i) {
        dc_result.node_voltages_dense[i] = solution[i];
    }

    // Dense branch current array (indexed by device ordinal)
    dc_result.branch_currents_dense.resize(ckt.devices().size(), 0.0);
    for (std::size_t d = 0; d < ckt.devices().size(); ++d) {
        int32_t br = ckt.devices()[d]->branch_index();
        if (br >= 0 && br < n)
            dc_result.branch_currents_dense[d] = solution[br];
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

    // Find source pointers by name (VSource or ISource)
    // We support up to 2 sweep sources (outer = params[0], inner = params[1])
    auto find_source = [&](const std::string& name) -> Device* {
        std::string lname = to_lower(name);
        for (auto& dev : ckt.devices()) {
            if (auto* vs = dynamic_cast<VSource*>(dev.get())) {
                if (to_lower(vs->name()) == lname) return vs;
            }
            if (auto* is = dynamic_cast<ISource*>(dev.get())) {
                if (to_lower(is->name()) == lname) return is;
            }
        }
        return nullptr;
    };

    auto get_dc_value = [](Device* dev) -> double {
        if (auto* vs = dynamic_cast<VSource*>(dev)) return vs->dc_value();
        if (auto* is = dynamic_cast<ISource*>(dev)) return is->dc_value();
        return 0.0;
    };

    auto set_dc_value = [](Device* dev, double v) {
        if (auto* vs = dynamic_cast<VSource*>(dev)) vs->set_dc_value(v);
        else if (auto* is = dynamic_cast<ISource*>(dev)) is->set_dc_value(v);
    };

    Device* src0 = find_source(params[0].source_name);
    if (!src0) {
        throw std::invalid_argument("DC sweep: source '" +
                                    params[0].source_name + "' not found");
    }

    Device* src1 = nullptr;
    if (params.size() >= 2) {
        src1 = find_source(params[1].source_name);
        if (!src1) {
            throw std::invalid_argument("DC sweep: source '" +
                                        params[1].source_name + "' not found");
        }
    }

    // Save original DC values so we can restore them after sweep
    const double orig_val0 = get_dc_value(src0);
    const double orig_val1 = src1 ? get_dc_value(src1) : 0.0;

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
    constexpr int MODEDCTRANCURVE_BIT  = 0x40;
    constexpr int MODEINITJCT_BIT     = 0x200;
    constexpr int MODEINITFLOAT_BIT   = 0x100;
    constexpr int MODEINITFIX_BIT     = 0x400;

    // Initial guess: zeros
    std::vector<double> solution(n, 0.0);
    for (auto& [node_id, value] : ckt.nodeset) {
        int32_t node_idx = static_cast<int32_t>(node_id);
        if (node_idx >= 0 && node_idx < n) solution[node_idx] = value;
    }
    for (auto& [node_id, value] : ckt.ic) {
        int32_t node_idx = static_cast<int32_t>(node_id);
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

    // Initialize dense arrays for handle-based access
    sweep_result.voltages_dense.resize(num_nodes);
    sweep_result.currents_dense.resize(ckt.devices().size());

    auto collect_point = [&](double sweep_x) {
        sweep_result.sweep_values.push_back(sweep_x);
        for (std::size_t k = 0; k < sv_slots.size(); ++k)
            sv_ptrs[k]->push_back(solution[sv_slots[k].idx]);
        for (std::size_t k = 0; k < sc_slots.size(); ++k)
            sc_ptrs[k]->push_back(solution[sc_slots[k].idx]);

        // Dense node voltage array (ALL nodes, including internal)
        for (int32_t i = 0; i < num_nodes; ++i)
            sweep_result.voltages_dense[i].push_back(solution[i]);
        // Dense branch current array (indexed by device ordinal)
        for (std::size_t d = 0; d < ckt.devices().size(); ++d) {
            int32_t br = ckt.devices()[d]->branch_index();
            if (br >= 0 && br < n)
                sweep_result.currents_dense[d].push_back(solution[br]);
            else
                sweep_result.currents_dense[d].push_back(0.0);
        }
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
            first_point = false;
            return;
        }
        res = gmin_stepping(ckt, *solver, solution, ckt.options,
                            MODEDCTRANCURVE_BIT | MODEINITJCT_BIT,
                            MODEDCTRANCURVE_BIT | MODEINITFLOAT_BIT);
        if (res.converged) {
            first_point = false;
            return;
        }
        ckt.integrator_ctx.mode = MODEDCTRANCURVE_BIT | MODEINITJCT_BIT;
        res = source_stepping(ckt, *solver, solution, ckt.options);
        if (res.converged) {
            first_point = false;
            return;
        }
        ckt.integrator_ctx.mode = MODEDCTRANCURVE_BIT | MODEINITJCT_BIT;
        res = pseudo_transient(ckt, *solver, solution, ckt.options);
        if (res.converged) {
            first_point = false;
            return;
        }
        SimStatus sweep_fail_status;
        sweep_fail_status.converged = false;
        sweep_fail_status.residual = res.residual;
        sweep_fail_status.worst_node_idx = res.worst_node_idx;
        if (!ckt.options.no_throw) {
            throw SimulationError("DC sweep: convergence failed", sweep_fail_status);
        }
        // no_throw: continue with unconverged solution
    };

    if (!src1) {
        // Single-variable sweep
        for (double v : inner_vals) {
            set_dc_value(src0, v);
            run_newton();
            collect_point(v);
        }
    } else {
        // Nested sweep: outer = params[0], inner = params[1]
        for (double vout : outer_vals) {
            set_dc_value(src0, vout);
            for (double vin : inner_vals) {
                set_dc_value(src1, vin);
                run_newton();
                collect_point(vin);
            }
        }
    }

    // Restore original source values
    set_dc_value(src0, orig_val0);
    if (src1) set_dc_value(src1, orig_val1);

    // Persist diag_gmin baseline after DC sweep convergence
    ckt.options.diag_gmin = ckt.options.gshunt;

    auto t_end = std::chrono::steady_clock::now();
    sweep_result.status.converged = true;
    sweep_result.status.elapsed_seconds = std::chrono::duration<double>(t_end - t_start).count();

    return sweep_result;
}

} // namespace neospice
