#pragma once
#include "core/matrix.hpp"
#include <memory>
#include <vector>

namespace neospice {

// Abstract linear-solver interface shared by the production Markowitz solver
// (NeoSolver) and the experimental AMD-ordered Gilbert-Peierls LU solver
// (AmdLuSolver). The method set and return conventions match NeoSolver exactly
// so the Newton / convergence / transient code can be solver-agnostic.
//
// Return conventions (mirroring NeoSolver):
//   - numeric()/refactorize()/refactorize_complex() return true on singular.
//   - numeric() must be called after symbolic(); refactorize() after numeric().
class ISolver {
public:
    virtual ~ISolver() = default;

    virtual void symbolic(const SparsityPattern& pattern) = 0;
    virtual bool numeric(const SparsityPattern& pattern, const NumericMatrix& mat,
                         double diag_gmin = 0.0) = 0;
    virtual bool refactorize(const NumericMatrix& mat, double diag_gmin = 0.0) = 0;
    virtual void solve(std::vector<double>& rhs) = 0;
    virtual void numeric_complex(const SparsityPattern& pattern,
                                 const std::vector<double>& ax) = 0;
    virtual bool refactorize_complex(const std::vector<double>& ax) = 0;
    virtual void solve_complex(std::vector<double>& rhs) = 0;

    // Stable identifier of the concrete solver actually doing the work, for
    // diagnostics and selection-policy tests. Wrappers (e.g. the auto-fallback
    // solver) report the engine currently in use.
    //   "markowitz" — NeoSolver
    //   "amdlu"     — AmdLuSolver
    virtual const char* name() const = 0;
};

// Factory for the real (DC/transient) solver path.
//
// Selection policy (Stage 4):
//   NEOSPICE_SOLVER controls the engine:
//     - "auto" (default): pick by problem size — Markowitz (NeoSolver) below the
//       auto-enable threshold, AMD-LU (AmdLuSolver) at/above it. Auto-selected
//       AMD-LU is wrapped so a hard factor failure (structural/numeric
//       singularity at the first factorization) falls back to Markowitz for that
//       solve instead of failing outright.
//     - "amdlu" / "klu": force AMD-LU always (no size gate, no fallback wrapper).
//     - "markowitz" / "sparse": force Markowitz always.
//   NEOSPICE_FORCE_AMDLU=<non-empty,non-"0"> is an alias for forced "amdlu".
//
// `num_vars` is the matrix dimension (Circuit::num_vars() / pattern.size()),
// `is_linear` is Circuit::is_linear() (true iff no nonlinear device). Both are
// used only by the "auto" policy, which engages AMD-LU iff the circuit is BOTH
// large (>= threshold) AND linear. Linear circuits have a unique solution so
// AMD-LU's static pivot ordering is provably result-identical to Markowitz;
// nonlinear circuits are basin/pivot sensitive and stay on Markowitz. The
// zero-arg overload selects with size 0 (always Markowitz under auto) and is
// kept for callers without a size.
//
// AC/noise/tf/pz paths construct NeoSolver directly and are unaffected.
std::unique_ptr<ISolver> make_solver(int num_vars, bool is_linear = true);
std::unique_ptr<ISolver> make_solver();

// AMD-LU auto-enable threshold (unknowns). Circuits with num_vars >= this use
// AMD-LU under the "auto" policy; smaller circuits stay on Markowitz. See
// make_solver.cpp for the derivation from the measured KiCad-suite max.
int amdlu_auto_threshold();

}  // namespace neospice
