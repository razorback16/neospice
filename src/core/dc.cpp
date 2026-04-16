#include "core/dc.hpp"
#include "core/newton.hpp"
#include "core/convergence.hpp"
#include "core/klu_solver.hpp"
#include "devices/vsource.hpp"
#include "devices/inductor.hpp"
#include <algorithm>

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

    // 3. Try newton_solve first
    auto result = newton_solve(ckt, solver, solution, ckt.options);
    if (result.converged) {
        solution = result.solution;
    } else {
        // 4. Try gmin stepping
        result = gmin_stepping(ckt, solver, solution, ckt.options);
        if (result.converged) {
            solution = result.solution;
        } else {
            // 5. Try source stepping
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

    // Node voltages
    for (int32_t i = 0; i < num_nodes; ++i) {
        std::string key = "v(" + to_lower(ckt.node_name(i)) + ")";
        dc_result.node_voltages[key] = solution[i];
    }

    // Branch currents for VSource and Inductor devices
    for (const auto& dev : ckt.devices()) {
        if (auto* vs = dynamic_cast<const VSource*>(dev.get())) {
            int32_t br = vs->branch_index();
            if (br >= 0 && br < n) {
                std::string key = "i(" + to_lower(dev->name()) + ")";
                dc_result.branch_currents[key] = solution[br];
            }
        } else if (auto* ind = dynamic_cast<const Inductor*>(dev.get())) {
            int32_t br = ind->branch_index();
            if (br >= 0 && br < n) {
                std::string key = "i(" + to_lower(dev->name()) + ")";
                dc_result.branch_currents[key] = solution[br];
            }
        }
    }

    return dc_result;
}

} // namespace neospice
