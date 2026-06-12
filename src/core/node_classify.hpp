#pragma once
#include <vector>

namespace neospice {

class Circuit;

/// Compute a physics-informed initial guess for the DC Newton solve.
///
/// Returns a length-`ckt.num_vars()` seed vector.  Node variables that are
/// tied to ground (or to an already-seeded node) through a DC independent
/// voltage source are seeded to the implied rail voltage; everything else is
/// left at 0 (the conservative default).  `.nodeset`/`.ic`-pinned nodes are
/// honoured and never overridden, so the caller may re-apply pins on top of
/// this guess without conflict.
///
/// This is intended ONLY as a fallback seed when the primary all-zero Newton
/// start fails — it must never replace the certified primary initial guess in
/// `solve_dc`, because changing the primary seed would perturb every circuit's
/// Newton trajectory.
std::vector<double> compute_initial_guess(const Circuit& ckt);

} // namespace neospice
