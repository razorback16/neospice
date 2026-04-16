#include "core/newton.hpp"
#include "core/circuit.hpp"
#include "core/klu_solver.hpp"
#include <cmath>
#include <stdexcept>

namespace neospice {

NewtonResult newton_solve(Circuit& ckt, KLUSolver& solver,
                          std::vector<double>& solution,
                          const SimOptions& opts) {
    const int32_t n = ckt.num_vars();
    const int32_t num_nodes = ckt.num_nodes();
    const auto& pattern = ckt.pattern();

    NumericMatrix mat(pattern);
    std::vector<double> rhs(n, 0.0);

    for (int iter = 0; iter < opts.max_iter; ++iter) {
        // Save old solution for convergence check
        std::vector<double> old_solution = solution;

        // Clear matrix and RHS
        mat.clear();
        std::fill(rhs.begin(), rhs.end(), 0.0);

        // Evaluate all devices at the current guess
        for (auto& dev : ckt.devices()) {
            dev->evaluate(solution, mat, rhs);
        }

        // Add gmin to diagonal of node equations
        for (int32_t i = 0; i < num_nodes; ++i) {
            try {
                MatrixOffset off = pattern.offset(i, i);
                mat.add(off, opts.gmin);
            } catch (const std::out_of_range&) {
                // Diagonal entry doesn't exist in pattern, skip
            }
        }

        // Factorize
        if (iter == 0) {
            solver.numeric(pattern, mat);
        } else {
            solver.refactorize(mat);
        }

        // Solve: rhs is overwritten with the delta or new solution
        solver.solve(rhs);

        // rhs now contains the new proposed solution
        solution = rhs;

        // Apply per-device voltage limiting between old and proposed solutions
        // to tame large Newton swings at nonlinear junctions.
        for (auto& dev : ckt.devices()) {
            dev->limit_voltages(old_solution, solution);
        }

        // Check convergence
        bool converged = true;
        for (int32_t i = 0; i < n; ++i) {
            double v_new = solution[i];
            double v_old = old_solution[i];
            double diff = std::abs(v_new - v_old);

            if (i < num_nodes) {
                // Node voltage convergence
                double tol = opts.reltol * std::max(std::abs(v_new), std::abs(v_old)) + opts.vntol;
                if (diff > tol) {
                    converged = false;
                    break;
                }
            } else {
                // Branch current convergence
                double tol = opts.reltol * std::max(std::abs(v_new), std::abs(v_old)) + opts.abstol;
                if (diff > tol) {
                    converged = false;
                    break;
                }
            }
        }

        if (converged) {
            return {true, iter + 1, solution};
        }
    }

    return {false, opts.max_iter, solution};
}

} // namespace neospice
