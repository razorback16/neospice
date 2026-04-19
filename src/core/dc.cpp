#include "core/dc.hpp"
#include "core/newton.hpp"
#include "core/convergence.hpp"
#include "core/klu_solver.hpp"
#include "devices/vsource.hpp"
#include "devices/inductor.hpp"
#include "devices/vcvs.hpp"
#include "devices/ccvs.hpp"
#include "devices/vcvs_nonlinear.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace neospice {

static std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

DCResult solve_dc(Circuit& ckt) {
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

    // 2. Create KLU solver and perform symbolic analysis
    KLUSolver solver;
    solver.symbolic(ckt.pattern());

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

    // 3. Try newton_solve first (initial junction guess mode)
    ckt.integrator_ctx.mode = MODEDCOP_BIT | MODEINITJCT_BIT;
    auto result = newton_solve(ckt, solver, solution, ckt.options);
    if (result.converged) {
        solution = result.solution;
    } else {
        // 4. Try gmin stepping (fix/iterate mode)
        ckt.integrator_ctx.mode = MODEDCOP_BIT | MODEINITFIX_BIT;
        result = gmin_stepping(ckt, solver, solution, ckt.options);
        if (result.converged) {
            solution = result.solution;
        } else {
            // 5. Try source stepping
            ckt.integrator_ctx.mode = MODEDCOP_BIT | MODEINITFIX_BIT;
            result = source_stepping(ckt, solver, solution, ckt.options);
            if (result.converged) {
                solution = result.solution;
            } else {
                // 6. All failed
                throw ConvergenceError("DC operating point failed to converge");
            }
        }
    }

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
            dc_result.branch_currents["i(" + to_lower(dname) + ")"] = solution[br];
    };
    for (const auto& dev : ckt.devices()) {
        if (auto* vs = dynamic_cast<const VSource*>(dev.get()))
            add_dc_current(vs->branch_index(), dev->name());
        else if (auto* ind = dynamic_cast<const Inductor*>(dev.get()))
            add_dc_current(ind->branch_index(), dev->name());
        else if (auto* e = dynamic_cast<const VCVS*>(dev.get()))
            add_dc_current(e->branch_index(), dev->name());
        else if (auto* h = dynamic_cast<const CCVS*>(dev.get()))
            add_dc_current(h->branch_index(), dev->name());
        else if (auto* enl = dynamic_cast<const NonlinearVCVS*>(dev.get()))
            add_dc_current(enl->branch_index(), dev->name());
        else if (auto* etbl = dynamic_cast<const TableVCVS*>(dev.get()))
            add_dc_current(etbl->branch_index(), dev->name());
    }

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

    // KLU solver and initial solution vector
    KLUSolver solver;
    solver.symbolic(ckt.pattern());

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

    // Helper lambda: collect results into sweep_result at the current solution
    auto collect_point = [&](double sweep_x) {
        sweep_result.sweep_values.push_back(sweep_x);

        for (int32_t i = 0; i < num_nodes; ++i) {
            if (ckt.is_internal_node(i)) continue;
            std::string key = "v(" + to_lower(ckt.node_name(i)) + ")";
            sweep_result.voltages[key].push_back(solution[i]);
        }
        auto add_sweep_current = [&](int32_t br, const std::string& dname) {
            if (br >= 0 && br < n)
                sweep_result.currents["i(" + to_lower(dname) + ")"].push_back(solution[br]);
        };
        for (const auto& dev : ckt.devices()) {
            if (auto* vs = dynamic_cast<const VSource*>(dev.get()))
                add_sweep_current(vs->branch_index(), dev->name());
            else if (auto* ind = dynamic_cast<const Inductor*>(dev.get()))
                add_sweep_current(ind->branch_index(), dev->name());
            else if (auto* e = dynamic_cast<const VCVS*>(dev.get()))
                add_sweep_current(e->branch_index(), dev->name());
            else if (auto* h = dynamic_cast<const CCVS*>(dev.get()))
                add_sweep_current(h->branch_index(), dev->name());
            else if (auto* enl = dynamic_cast<const NonlinearVCVS*>(dev.get()))
                add_sweep_current(enl->branch_index(), dev->name());
            else if (auto* etbl = dynamic_cast<const TableVCVS*>(dev.get()))
                add_sweep_current(etbl->branch_index(), dev->name());
        }
    };

    // Helper: run Newton at current source settings, using previous solution
    // as warm start. First point uses INITJCT (junction guess); subsequent
    // points use INITFIX so BSIM4 reads from the previous converged state.
    bool first_point = true;
    auto run_newton = [&]() {
        int init_bit = first_point ? MODEINITJCT_BIT : MODEINITFIX_BIT;
        ckt.integrator_ctx.mode = MODEDCTRANCURVE_BIT | init_bit;
        auto res = newton_solve(ckt, solver, solution, ckt.options);
        if (res.converged) {
            solution = res.solution;
            first_point = false;
            return;
        }
        ckt.integrator_ctx.mode = MODEDCTRANCURVE_BIT | MODEINITFIX_BIT;
        res = gmin_stepping(ckt, solver, solution, ckt.options);
        if (res.converged) {
            solution = res.solution;
            first_point = false;
            return;
        }
        res = source_stepping(ckt, solver, solution, ckt.options);
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

    return sweep_result;
}

} // namespace neospice
