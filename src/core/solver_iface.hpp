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
};

// Factory for the real (DC/transient) solver path. Returns NeoSolver by default;
// when the environment requests it (NEOSPICE_SOLVER=amdlu or
// NEOSPICE_FORCE_AMDLU=1) returns AmdLuSolver instead. The selection is read
// once per call. AC/noise/tf/pz paths construct NeoSolver directly and are
// unaffected by this factory.
std::unique_ptr<ISolver> make_solver();

}  // namespace neospice
