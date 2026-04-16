#include "core/transient.hpp"
#include "core/newton.hpp"
#include "core/convergence.hpp"
#include "core/klu_solver.hpp"
#include "devices/vsource.hpp"
#include "devices/isource.hpp"
#include "devices/capacitor.hpp"
#include "devices/inductor.hpp"
#include <algorithm>

namespace neospice {

static std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

TransientResult solve_transient(Circuit& ckt, double tstep, double tstop) {
    const int32_t n = ckt.num_vars();
    const int32_t num_nodes = ckt.num_nodes();

    // ---------------------------------------------------------------
    // 1. DC operating point
    // ---------------------------------------------------------------
    std::vector<double> solution(n, 0.0);

    // Apply nodeset hints
    for (auto& [node_idx, value] : ckt.nodeset) {
        if (node_idx >= 0 && node_idx < n) {
            solution[node_idx] = value;
        }
    }

    KLUSolver solver;
    solver.symbolic(ckt.pattern());

    auto result = newton_solve(ckt, solver, solution, ckt.options);
    if (result.converged) {
        solution = result.solution;
    } else {
        result = gmin_stepping(ckt, solver, solution, ckt.options);
        if (result.converged) {
            solution = result.solution;
        } else {
            result = source_stepping(ckt, solver, solution, ckt.options);
            if (result.converged) {
                solution = result.solution;
            } else {
                throw ConvergenceError("DC operating point failed to converge");
            }
        }
    }

    // ---------------------------------------------------------------
    // 2. Apply .ic overrides on top of DC solution
    // ---------------------------------------------------------------
    for (auto& [node_idx, value] : ckt.ic) {
        if (node_idx >= 0 && node_idx < n) {
            solution[node_idx] = value;
        }
    }

    // ---------------------------------------------------------------
    // Helper: store a time point into the result
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
    // 3. Store t=0 point
    // ---------------------------------------------------------------
    store_point(0.0, solution);

    // ---------------------------------------------------------------
    // 4. Enable transient mode on reactive devices
    // ---------------------------------------------------------------
    for (auto& dev : ckt.devices()) {
        if (auto* cap = dynamic_cast<Capacitor*>(dev.get())) {
            cap->set_transient(tstep);
        } else if (auto* ind = dynamic_cast<Inductor*>(dev.get())) {
            ind->set_transient(tstep);
        }
    }

    // ---------------------------------------------------------------
    // 5. Initialize DC steady-state on reactive devices
    //    (sets v_prev to DC voltage, i_prev to 0 for caps / DC current for inductors)
    // ---------------------------------------------------------------
    for (auto& dev : ckt.devices()) {
        if (auto* cap = dynamic_cast<Capacitor*>(dev.get())) {
            cap->init_dc_state(solution);
        } else if (auto* ind = dynamic_cast<Inductor*>(dev.get())) {
            ind->init_dc_state(solution);
        }
    }

    // ---------------------------------------------------------------
    // 6. Time-stepping loop
    // ---------------------------------------------------------------
    int num_steps = static_cast<int>(std::round(tstop / tstep));
    for (int step = 1; step <= num_steps; ++step) {
        double t = step * tstep;

        // (a) Update time on sources
        for (auto& dev : ckt.devices()) {
            if (auto* vs = dynamic_cast<VSource*>(dev.get())) {
                vs->set_time(t);
            } else if (auto* is = dynamic_cast<ISource*>(dev.get())) {
                is->set_time(t);
            }
        }

        // (b) Newton-Raphson solve
        auto nr = newton_solve(ckt, solver, solution, ckt.options);
        if (!nr.converged) {
            // Clean up transient state before throwing
            for (auto& dev : ckt.devices()) {
                if (auto* cap = dynamic_cast<Capacitor*>(dev.get())) {
                    cap->clear_transient();
                } else if (auto* ind = dynamic_cast<Inductor*>(dev.get())) {
                    ind->clear_transient();
                }
            }
            throw ConvergenceError("Transient analysis failed to converge at t=" + std::to_string(t));
        }
        solution = nr.solution;

        // (c) Accept step on reactive devices
        for (auto& dev : ckt.devices()) {
            if (auto* cap = dynamic_cast<Capacitor*>(dev.get())) {
                cap->accept_step_from_solution(solution);
            } else if (auto* ind = dynamic_cast<Inductor*>(dev.get())) {
                ind->accept_step_from_solution(solution);
            }
        }

        // (d) Store point
        store_point(t, solution);
    }

    // ---------------------------------------------------------------
    // 7. Clean up: disable transient mode
    // ---------------------------------------------------------------
    for (auto& dev : ckt.devices()) {
        if (auto* cap = dynamic_cast<Capacitor*>(dev.get())) {
            cap->clear_transient();
        } else if (auto* ind = dynamic_cast<Inductor*>(dev.get())) {
            ind->clear_transient();
        }
    }

    return tran_result;
}

} // namespace neospice
