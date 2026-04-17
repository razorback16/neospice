#include "core/newton.hpp"
#include "core/circuit.hpp"
#include "core/klu_solver.hpp"
#include <cmath>
#include <iostream>
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

    if (opts.verbose) {
        std::cerr << "[newton] gmin=" << opts.gmin << " start:";
        for (int32_t i = 0; i < num_nodes; ++i)
            std::cerr << " " << ckt.node_name(i) << "=" << solution[i];
        std::cerr << "\n";
    }

    // CKTmode init-phase bits (mirrors ngspice cktdefs.h). After iter 0 the
    // Newton driver must flip MODEINITJCT -> MODEINITFIX so state-storing
    // devices (BSIM4v7) start reading CKTrhsOld instead of their hard-coded
    // junction-initialisation branch.  This matches ngspice's NIiter.
    constexpr int MODEINITJCT_BIT = 0x200;
    constexpr int MODEINITFIX_BIT = 0x400;
    const int saved_mode = ckt.integrator_ctx.mode;

    for (int iter = 0; iter < opts.max_iter; ++iter) {
        // Save old solution for convergence check
        std::vector<double> old_solution = solution;

        // Clear matrix and RHS
        mat.clear();
        std::fill(rhs.begin(), rhs.end(), 0.0);

        // Post-iter-0 init-phase flip: JCT -> FIX.  Leaves other mode bits
        // (MODEDC, MODETRAN, ...) untouched.
        if (iter > 0 && (saved_mode & MODEINITJCT_BIT)) {
            ckt.integrator_ctx.mode =
                (saved_mode & ~MODEINITJCT_BIT) | MODEINITFIX_BIT;
        }

        // Evaluate all devices at the current guess.  Publish the
        // Circuit's IntegratorCtx through a thread-local so state-storing
        // devices (BSIM4v7) can read CKTmode/ag/delta/order without an
        // extra parameter on the Device interface.  RAII guard clears the
        // pointer even if a device throws.
        struct IntegratorCtxGuard {
            IntegratorCtxGuard(const IntegratorCtx& c) { tls_integrator_ctx = &c; }
            ~IntegratorCtxGuard()                      { tls_integrator_ctx = nullptr; }
        } guard(ckt.integrator_ctx);
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

        if (opts.verbose) {
            double worst_ratio = 0.0;
            double worst_diff  = 0.0;
            int32_t worst      = -1;
            for (int32_t i = 0; i < n; ++i) {
                double v_new = solution[i];
                double v_old = old_solution[i];
                double diff  = std::abs(v_new - v_old);
                double other = (i < num_nodes) ? opts.vntol : opts.abstol;
                double tol   = opts.reltol * std::max(std::abs(v_new), std::abs(v_old)) + other;
                double ratio = (tol > 0.0) ? diff / tol : 0.0;
                if (ratio > worst_ratio) { worst_ratio = ratio; worst_diff = diff; worst = i; }
            }
            std::cerr << "[newton] iter=" << iter
                      << " max_diff=" << worst_diff
                      << " worst_idx=" << worst;
            if (worst >= 0 && worst < num_nodes)
                std::cerr << " (" << ckt.node_name(worst) << ")";
            std::cerr << (converged ? " (converged)" : "")
                      << "\n";
        }

        if (converged) {
            ckt.integrator_ctx.mode = saved_mode;
            return {true, iter + 1, solution};
        }
    }

    if (opts.verbose)
        std::cerr << "[newton] NOT converged after " << opts.max_iter << " iter\n";

    ckt.integrator_ctx.mode = saved_mode;
    return {false, opts.max_iter, solution};
}

} // namespace neospice
