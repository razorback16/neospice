/**********
Portions of this file are translated from UC Berkeley SPICE3F5
(niiter.c, the NIiter Newton-Raphson iteration loop) and derive from
the earlier SPICE2G6 (1983) Fortran, tracing to L. Nagel's SPICE,
UC Berkeley ERL Memorandum M382 (1973).

Copyright 1990 Regents of the University of California. All rights reserved.
(Permissive BSD-style "Berkeley Spice3" license.)
See NOTICE and CREDITS.md for full attribution.
**********/

#include "core/newton.hpp"
#include "core/circuit.hpp"
#include "core/solver_iface.hpp"
#include "core/ckt_mode.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <span>
#include <limits>
#include <stdexcept>

namespace neospice {

namespace {

// Above 1/epsilon a double no longer has unit resolution, so the relative
// Newton test can accept a large absolute step.  Reject such a candidate only
// when a voltage constraint is also carrying current: that means the rounded
// voltage participates in the circuit.  An unloaded behavioral source remains
// valid, including ngspice's intentional 1/(gmin*1e-20) result for 1/0.
bool has_loaded_unresolved_voltage_constraint(
        const std::vector<Device*>& load_order,
        const std::vector<double>& solution,
        const SimOptions& opts) {
    const double unresolved = 1.0 / std::numeric_limits<double>::epsilon();
    for (const Device* dev : load_order) {
        const std::string type = dev->device_type();
        if (type != "B" && type != "E" && type != "H" && type != "V")
            continue;

        const int32_t branch = dev->branch_index();
        if (branch < 0 || branch >= static_cast<int32_t>(solution.size()))
            continue;

        const auto nodes = dev->external_nodes();
        if (nodes.size() < 2)
            continue;
        const double vp = nodes[0] >= 0 ? solution[nodes[0]] : 0.0;
        const double vn = nodes[1] >= 0 ? solution[nodes[1]] : 0.0;
        if (std::abs(vp - vn) > unresolved &&
            std::abs(solution[branch]) > opts.abstol)
            return true;
    }
    return false;
}

} // namespace

thread_local OneBasedEvalArrays* tls_one_based_eval_arrays = nullptr;

NewtonWorkspace::NewtonWorkspace(const SparsityPattern& pattern)
    : mat(pattern),
      matrix_size(pattern.size()),
      matrix_nnz(pattern.nnz()) {
    ensure_size(pattern.size());
}

void NewtonWorkspace::ensure_size(int32_t n) {
    if (static_cast<int32_t>(rhs.size()) != n)
        rhs.resize(n);
    if (static_cast<int32_t>(old_solution.size()) != n)
        old_solution.resize(n);
    if (static_cast<int32_t>(proposed.size()) != n)
        proposed.resize(n);
    if (static_cast<int32_t>(one_based_solution.size()) != n + 1)
        one_based_solution.resize(n + 1);
    if (static_cast<int32_t>(one_based_rhs.size()) != n + 1)
        one_based_rhs.resize(n + 1);
}

NewtonResult newton_solve(Circuit& ckt, ISolver& solver,
                          std::vector<double>& solution,
                          const SimOptions& opts,
                          bool preserve_previous_on_convergence) {
    NewtonWorkspace workspace(ckt.pattern());
    return newton_solve(ckt, solver, solution, opts, workspace,
                        preserve_previous_on_convergence);
}

NewtonResult newton_solve(Circuit& ckt, ISolver& solver,
                          std::vector<double>& solution,
                          const SimOptions& opts,
                          NewtonWorkspace& workspace,
                          bool preserve_previous_on_convergence) {
    const int32_t n = ckt.num_vars();
    const int32_t num_nodes = ckt.num_nodes();
    const auto& pattern = ckt.pattern();
    const auto& load_order = ckt.device_load_order();
    if (workspace.matrix_size != pattern.size() ||
        workspace.matrix_nnz != pattern.nnz()) {
        throw std::logic_error("NewtonWorkspace does not match circuit sparsity pattern");
    }
    workspace.ensure_size(n);

    // Dead-node pin: a "dead" node owns only a structural diagonal placeholder
    // (no device stamps organic conductance there — e.g. a node connected only
    // to a zero-valued current source or an open DC capacitor).  Such a node
    // has no DC current path, so its diagonal is 0 and the Jacobian is
    // structurally singular.  ngspice's per-node diagonal lets its direct
    // solver resolve the node to 0 V; we mirror that by stamping a negligible
    // conductance (CKTgmin, 1e-12) to ground on each dead-node diagonal every
    // iteration.  This perturbs nothing else (live nodes are never in this set)
    // and pins the dead node to exactly 0 V, matching ngspice.
    const auto& dead_nodes = ckt.dead_node_indices();
    std::vector<MatrixOffset> dead_diag_offsets;
    dead_diag_offsets.reserve(dead_nodes.size());
    for (int32_t idx : dead_nodes)
        dead_diag_offsets.push_back(pattern.offset(idx, idx));
    const double dead_pin_gmin = (opts.gmin > 0.0) ? opts.gmin : 1e-12;

    NumericMatrix& mat = workspace.mat;
    std::vector<double>& rhs = workspace.rhs;
    std::vector<double>& old_solution = workspace.old_solution;
    std::vector<double>& old_state0 = workspace.old_state0;
    std::vector<double>& proposed = workspace.proposed;
    std::vector<double>& one_based_solution = workspace.one_based_solution;
    std::vector<double>& one_based_rhs = workspace.one_based_rhs;

    if (opts.verbose) {
        std::cerr << "[newton] n=" << n << " nnz=" << pattern.nnz()
                  << " gmin=" << opts.gmin << " start:";
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

    // Retain the entry mode for analysis-type decisions. NIiter mutates
    // CKTmode in place and deliberately leaves the final init phase visible
    // to continuation callers, including after an iteration-limit failure.
    const int saved_mode = ckt.integrator_ctx.mode;

    // Start by reusing the existing pivot order whenever possible.
    // ngspice only forces a full reorder through NISHOULDREORDER
    // (not at every continuation step); MODEINITJCT and singular
    // refactors below set force_numeric when a fresh order is required.
    bool force_numeric = false;

    // Track residual norm and worst node across iterations.
    double max_residual = 0.0;
    int32_t worst_idx = -1;

    // NIiter checks its iteration limit only after loading and solving.  Thus
    // maxIter=N performs N normal iterations plus one final load/solve whose
    // proposal is discarded when the limit is reported.  The device state
    // produced by that final load is deliberately retained.
    for (int iter = 0; iter <= opts.max_iter; ++iter) {
        // Save old solution for convergence check
        std::copy(solution.begin(), solution.end(), old_solution.begin());

        // Clear matrix and RHS
        mat.clear();
        std::fill(rhs.begin(), rhs.end(), 0.0);

        // Evaluate all devices at the current guess.  Publish the
        // Circuit's IntegratorCtx and ngspice-style one-based RHS arrays
        // through thread-locals so UCB-port device loads can stamp into
        // circuit-global storage instead of private per-device copies.
        one_based_solution[0] = 0.0;
        std::copy(solution.begin(), solution.end(), one_based_solution.begin() + 1);
        std::fill(one_based_rhs.begin(), one_based_rhs.end(), 0.0);
        OneBasedEvalArrays eval_arrays{one_based_solution.data(),
                                        one_based_rhs.data(),
                                        static_cast<int32_t>(one_based_solution.size())};
        struct EvalContextGuard {
            EvalContextGuard(const IntegratorCtx& c, OneBasedEvalArrays& arrays) {
                tls_integrator_ctx = &c;
                tls_one_based_eval_arrays = &arrays;
            }
            ~EvalContextGuard() {
                tls_one_based_eval_arrays = nullptr;
                tls_integrator_ctx = nullptr;
            }
        } guard(ckt.integrator_ctx, eval_arrays);
        // Native devices and translated ngspice shims share one buffer: the
        // shims stamp through one_based_rhs[1..n] (published via
        // tls_one_based_eval_arrays), while native devices get the same
        // storage as a zero-based view.  No bridging copies, and no way for a
        // device to be wired to the wrong convention.
        for (Device* dev : load_order)
            dev->evaluate(solution, mat,
                          std::span<double>(one_based_rhs.data() + 1, n));

        // Hand the accumulated loads back to the zero-based vector the solver
        // takes.  One unconditional copy, versus the two-or-more the old
        // per-device convention switch performed on mixed circuits.
        std::copy(one_based_rhs.begin() + 1, one_based_rhs.end(), rhs.begin());

        // Pin dead nodes (see dead_diag_offsets above): stamp a negligible
        // conductance to ground so the diagonal is non-zero and the node
        // settles to 0 V, matching ngspice's always-present per-node diagonal.
        for (MatrixOffset off : dead_diag_offsets)
            mat.add(off, dead_pin_gmin);

        // Pseudo-transient companion current: inject G*V_old for each node
        // to complete the backward Euler companion model (C/dt shunt + source).
        if (opts.ptc_g > 0.0 && opts.ptc_prev != nullptr) {
            for (int32_t i = 0; i < opts.ptc_num_nodes; ++i) {
                rhs[i] += opts.ptc_g * opts.ptc_prev[i];
            }
        }

        // Check if any device changed matrix structure this iteration
        // (e.g. a switch flipped state).  If so, force full numeric
        // factorization for this solve to re-establish valid pivot ordering.
        // Only relevant during DC — transient already handles this via the
        // warm-start path.
        if (!(saved_mode & MODETRAN_BIT)) {
            for (const Device* dev : load_order) {
                if (dev->matrix_structure_changed()) {
                    force_numeric = true;
                    break;
                }
            }
        }

        // Compute residual norm from the RHS before the solve overwrites it.
        max_residual = 0.0;
        worst_idx = -1;
        for (int32_t i = 0; i < num_nodes; ++i) {
            double r = std::abs(rhs[i]);
            if (r > max_residual) {
                max_residual = r;
                worst_idx = i;
            }
        }

        // ngspice niiter.c:107-110: force full reorder when in MODEINITJCT.
        int cur_mode = ckt.integrator_ctx.mode;
        // NISHOULDREORDER is consumed by SMPreorder in this iteration.  Do
        // not latch the request across later MODEINITFLOAT iterations.
        const bool always_reorder =
            (cur_mode & MODEINITJCT_BIT) ||
            ((cur_mode & MODEINITTRAN_BIT) && iter == 0);

        // Factorize: try refactorize first (reuses pivot order).  If
        // refactorization finds a singular pivot, match ngspice NIiter: mark
        // the matrix for a full reorder and retry without solving the failed
        // factorization.  A singular full reorder means this Newton attempt
        // failed at the current continuation point.
        if (force_numeric || always_reorder) {
            bool singular = solver.numeric(pattern, mat, opts.diag_gmin);
            if (singular) {
                return {false, iter + 1, max_residual, worst_idx};
            }
            // SMPreorder clears NISHOULDREORDER.  A mode transition below may
            // request another full reorder for the following iteration.
            force_numeric = false;
        } else {
            try {
                bool singular = solver.refactorize(mat, opts.diag_gmin);
                if (singular) {
                    force_numeric = true;
                    continue;
                }
            } catch (const std::exception&) {
                bool singular = solver.numeric(pattern, mat, opts.diag_gmin);
                if (singular) {
                    return {false, iter + 1, max_residual, worst_idx};
                }
                force_numeric = false;
            }
        }


        const int32_t num_states = ckt.num_states();
        if (static_cast<int32_t>(old_state0.size()) != num_states)
            old_state0.resize(num_states);
        if (num_states > 0)
            std::copy_n(ckt.state0(), num_states, old_state0.begin());

        // Solve: rhs is overwritten with the new solution
        solver.solve(rhs);

        // rhs now contains the proposed new solution from the linear solve
        std::copy(rhs.begin(), rhs.end(), proposed.begin());

        // Apply per-device voltage limiting
        for (Device* dev : load_order) {
            dev->limit_voltages(old_solution, proposed);
        }

        std::copy(proposed.begin(), proposed.end(), solution.begin());

        // ngspice niiter.c tests `iterno > maxIter` immediately after the
        // solve and before convergence testing, damping, or the RHS-buffer
        // swap.  Keep the iterate that was used for this load (CKTrhsOld),
        // while retaining the freshly evaluated device state0.
        if (iter == opts.max_iter) {
            std::copy(old_solution.begin(), old_solution.end(), solution.begin());
            if (opts.verbose)
                std::cerr << "[newton] NOT converged after " << (iter + 1)
                          << " iter (limit " << opts.max_iter << ")\n";
            return {false, iter + 1, max_residual, worst_idx};
        }

        // Check convergence
        bool converged = true;
        for (int32_t i = 0; i < n; ++i) {
            double v_new = solution[i];
            if (std::isnan(v_new) || std::isinf(v_new)) {
                converged = false;
                break;
            }
            double v_old = old_solution[i];
            double diff = std::abs(v_new - v_old);

            if (ckt.is_voltage_variable(i)) {
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
                double other = ckt.is_voltage_variable(i) ? opts.vntol : opts.abstol;
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

        // ngspice NIiter does not accept convergence on the first iteration
        // of a call; it skips NIconvTest and forces CKTnoncon=1 for iterno==1.
        if (iter == 0)
            converged = false;

        // Device-specific convergence check (e.g. BSIM4v7 current-based
        // convergence).  Only evaluated when node/branch convergence passed,
        // so it is purely additive — both must pass.
        if (converged) {
            for (const Device* dev : load_order) {
                if (!dev->device_converged(solution)) {
                    converged = false;
                    if (opts.verbose)
                        std::cerr << "[newton] device '" << dev->name()
                                  << "' reports non-convergence\n";
                    break;
                }
            }
        }

        if (converged && has_loaded_unresolved_voltage_constraint(
                             load_order, solution, opts)) {
            converged = false;
            if (opts.verbose)
                std::cerr << "[newton] loaded voltage constraint exceeds "
                             "double unit resolution\n";
        }

        // Node damping (ngspice niiter.c:295-323) is applied only after the
        // iteration has already been found non-converged.  This keeps the
        // convergence test on the raw Newton proposal and damps only the value
        // carried into the next load.
        if (!converged && opts.node_damping &&
            (saved_mode & (MODEDCOP_BIT | MODETRANOP_BIT)) && iter > 0) {
            double max_diff = 0.0;
            for (int32_t i = 0; i < num_nodes; ++i) {
                if (!ckt.is_voltage_variable(i)) continue;
                double diff = std::abs(solution[i] - old_solution[i]);
                if (diff > max_diff)
                    max_diff = diff;
            }
            if (max_diff > 10.0) {
                double damp_factor = 10.0 / max_diff;
                if (damp_factor < 0.1)
                    damp_factor = 0.1;
                for (int32_t i = 0; i < num_nodes; ++i) {
                    if (!ckt.is_voltage_variable(i)) continue;
                    solution[i] = old_solution[i] + damp_factor * (solution[i] - old_solution[i]);
                }
                for (int32_t i = 0; i < num_states; ++i) {
                    ckt.state0()[i] = old_state0[i] + damp_factor * (ckt.state0()[i] - old_state0[i]);
                }
            }
        }

        // -----------------------------------------------------------------
        // Post-iteration init-phase flip (matches ngspice NIiter.c:234-269).
        //
        // The mode is modified in-place on ckt.integrator_ctx.mode so that
        // the NEXT iteration and continuation calls see the updated phase.
        // -----------------------------------------------------------------
        int m = ckt.integrator_ctx.mode;

        if (m & MODEINITFLOAT_BIT) {
            // Corrector phase: convergence check is meaningful.  Continuation
            // callers retain CKTrhsOld, the iterate against which CKTrhs just
            // converged, so keep the same side of the NIiter buffer pair.
            if (converged) {
                if (preserve_previous_on_convergence)
                    std::copy(old_solution.begin(), old_solution.end(), solution.begin());
                return {true, iter + 1, max_residual, worst_idx};
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
            // niiter.c sets NISHOULDREORDER after the first MODEINITTRAN load,
            // so the first corrector receives its own full pivot search.
            force_numeric = true;
        } else if (m & MODEINITPRED_BIT) {
            // Subsequent transient steps: predictor -> corrector
            ckt.integrator_ctx.mode = (m & ~INITF_MASK) | MODEINITFLOAT_BIT;
        } else {
            // No init flag set (shouldn't happen in well-formed usage).
            // If already converged, return success.
            if (converged) {
                std::copy(old_solution.begin(), old_solution.end(), solution.begin());
                return {true, iter + 1, max_residual, worst_idx};
            }
        }
    }

    throw std::logic_error("unreachable Newton iteration exit");
}

} // namespace neospice
