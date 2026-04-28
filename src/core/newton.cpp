#include "core/newton.hpp"
#include "core/circuit.hpp"
#include "core/linear_solver.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace neospice {

NewtonResult newton_solve(Circuit& ckt, LinearSolver& solver,
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

    // CKTmode init-phase bits (mirrors ngspice cktdefs.h).  After each
    // Newton iteration the driver flips the init-phase flag following the
    // same cascade as ngspice's NIiter (niiter.c:234-269):
    //
    //   MODEINITFLOAT  -> check convergence, return if converged
    //   MODEINITJCT    -> flip to MODEINITFIX
    //   MODEINITFIX    -> if converged, flip to MODEINITFLOAT
    //   MODEINITSMSIG  -> flip to MODEINITFLOAT
    //   MODEINITTRAN   -> flip to MODEINITFLOAT  (first transient step)
    //   MODEINITPRED   -> flip to MODEINITFLOAT  (subsequent transient steps)
    //
    // The INITF mask clears all init-flag bits before setting the new one.
    constexpr int INITF_MASK         = 0x3F00;
    constexpr int MODEINITFLOAT_BIT  = 0x100;
    constexpr int MODEINITJCT_BIT    = 0x200;
    constexpr int MODEINITFIX_BIT    = 0x400;
    constexpr int MODEINITSMSIG_BIT  = 0x800;
    constexpr int MODEINITTRAN_BIT   = 0x1000;
    constexpr int MODEINITPRED_BIT   = 0x2000;

    // Save the caller's mode so we can restore it on exit.
    const int saved_mode = ckt.integrator_ctx.mode;

    // Warm-start: during transient analysis (MODEINITTRAN/MODEINITPRED)
    // the matrix from the previous timestep is structurally similar, so
    // iter 0 can try refactorize() instead of full numeric().  For DC
    // and convergence helpers the matrix changes drastically (different
    // gmin, source scaling), so always start with numeric().
    constexpr int MODETRAN_BIT = 0x1;
    bool force_numeric = !(saved_mode & MODETRAN_BIT);

    for (int iter = 0; iter < opts.max_iter; ++iter) {
        // Save old solution for convergence check
        std::vector<double> old_solution = solution;

        // Clear matrix and RHS
        mat.clear();
        std::fill(rhs.begin(), rhs.end(), 0.0);

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

        // Add gmin only to diagonals that devices actually stamp into.
        // Non-organic diagonals (e.g. VCVS-only nodes) stay at zero so
        // the solver picks better off-diagonal pivots and V-source nodes
        // don't get a spurious gmin current artifact.
        for (int32_t i = 0; i < num_nodes; ++i) {
            if (!ckt.has_organic_diagonal(i)) continue;
            MatrixOffset off = pattern.offset(i, i);
            mat.add(off, opts.gmin);
        }

        // Factorize: try refactorize first (reuses pivot order), fall back
        // to full numeric factorization if the pivot order is unstable.
        try {
            if (force_numeric) {
                solver.numeric(pattern, mat);
                force_numeric = false;
            } else {
                try {
                    solver.refactorize(mat);
                } catch (const std::exception&) {
                    solver.numeric(pattern, mat);
                }
            }
        } catch (const std::runtime_error&) {
            return {false, iter, solution};
        }

        // Solve: rhs is overwritten with the delta or new solution
        solver.solve(rhs);

        // rhs now contains the new proposed solution
        solution = rhs;

        // Global node damping: clamp voltage updates that exceed a threshold.
        // Only apply during corrector (MODEINITFLOAT) iterations where the
        // solution is refining around the operating point.  During junction
        // init (INITJCT/INITFIX) large voltage jumps are expected as the
        // solver bootstraps from zero toward supply rails.
        if (ckt.integrator_ctx.mode & MODEINITFLOAT_BIT) {
            constexpr double VDAMP_THRESHOLD = 3.5;  // ~10 * Vt at 300K
            for (int32_t i = 0; i < num_nodes; ++i) {
                double delta = solution[i] - old_solution[i];
                if (std::abs(delta) > VDAMP_THRESHOLD) {
                    solution[i] = old_solution[i] +
                        VDAMP_THRESHOLD * ((delta > 0) ? 1.0 : -1.0);
                }
            }
        }

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

        // Device-specific convergence check (e.g. BSIM4v7 current-based
        // convergence).  Only evaluated when node/branch convergence passed,
        // so it is purely additive — both must pass.
        if (converged) {
            for (const auto& dev : ckt.devices()) {
                if (!dev->device_converged()) {
                    converged = false;
                    if (opts.verbose)
                        std::cerr << "[newton] device '" << dev->name()
                                  << "' reports non-convergence\n";
                    break;
                }
            }
        }

        // -----------------------------------------------------------------
        // Post-iteration init-phase flip (matches ngspice NIiter.c:234-269).
        //
        // The mode is modified in-place on ckt.integrator_ctx.mode so that
        // the NEXT iteration sees the updated phase.  On convergence or
        // max-iter exit we restore saved_mode.
        // -----------------------------------------------------------------
        int m = ckt.integrator_ctx.mode;

        if (m & MODEINITFLOAT_BIT) {
            // Corrector phase — convergence check is meaningful.
            if (converged) {
                ckt.integrator_ctx.mode = saved_mode;
                return {true, iter + 1, solution};
            }
            // else: keep iterating in MODEINITFLOAT
        } else if (m & MODEINITJCT_BIT) {
            // Junction-init -> fix mode (try reading CKTrhsOld next iter)
            ckt.integrator_ctx.mode = (m & ~INITF_MASK) | MODEINITFIX_BIT;
            force_numeric = true;
        } else if (m & MODEINITFIX_BIT) {
            // Fix mode -> float once converged under FIX
            if (converged)
                ckt.integrator_ctx.mode = (m & ~INITF_MASK) | MODEINITFLOAT_BIT;
            // else: stay in MODEINITFIX and keep iterating
        } else if (m & MODEINITSMSIG_BIT) {
            ckt.integrator_ctx.mode = (m & ~INITF_MASK) | MODEINITFLOAT_BIT;
        } else if (m & MODEINITTRAN_BIT) {
            // First transient step: predictor/init -> corrector
            ckt.integrator_ctx.mode = (m & ~INITF_MASK) | MODEINITFLOAT_BIT;
        } else if (m & MODEINITPRED_BIT) {
            // Subsequent transient steps: predictor -> corrector
            ckt.integrator_ctx.mode = (m & ~INITF_MASK) | MODEINITFLOAT_BIT;
        } else {
            // No init flag set (shouldn't happen in well-formed usage).
            // If already converged, return success.
            if (converged) {
                ckt.integrator_ctx.mode = saved_mode;
                return {true, iter + 1, solution};
            }
        }
    }

    if (opts.verbose)
        std::cerr << "[newton] NOT converged after " << opts.max_iter << " iter\n";

    ckt.integrator_ctx.mode = saved_mode;
    return {false, opts.max_iter, solution};
}

} // namespace neospice
