# Sparse solver techniques and current scope

This page describes the current implementation and its measurement limits.
The earlier literature-style notes are [preserved historically](../evidence/joss/2026-09-11-historical-sparse-lu-techniques-overview.md).
Their projected speed factors, pivot reductions and complexity claims are not
validated results for neospice and must not be used in the paper.

## Implemented paths

The [solver factory](../../src/core/make_solver.cpp) selects a Sparse-derived
`NeoSolver` by default for nonlinear circuits and for linear circuits below
256 unknowns. At or above that threshold, linear circuits use `AmdLuSolver`
through a wrapper that can fall back to `NeoSolver` on the first factorization.
Explicit environment overrides are described in the
[solver interface](../../src/core/solver_iface.hpp). They must be recorded as
part of any experiment; the legacy `klu` override name selects the in-tree
implementation, not the external SuiteSparse KLU library.

The [ordering routine](../../src/core/amd.cpp) uses an explicit elimination
graph and minimum live degree, with dense vertices deferred using the default
SuiteSparse AMD threshold. It is not the quotient-graph AMD algorithm.
The [LU implementation](../../src/core/amd_lu_solver.cpp) supports a numeric
refactorization path that attempts to reuse structure and pivots, falling back
to full factorization when that replay fails. Complex operations delegate to
`NeoSolver`. BTF and matching helpers exist, but production solver paths do not
call them; their presence does not establish an integrated BTF-AMD solver.

## Evidence and future work

[Paired benchmark methods](../benchmark-methods.md) defines the completed
34-case experiment, reference configuration and four timing phases. Its
[full figure](../evidence/joss/2026-09-11-paired-measure47-report/timing-total.pdf)
retains wins for both engines and substantial neospice costs on diode ladders
and noise grids. These are public-interface measurements, not isolated solver
factorization timings or evidence for a particular optimization mechanism.

Supernodal, multifrontal, GPU, ISLU or block-factorization proposals must remain
future research unless an implemented path and validated experiment establish
otherwise. No general scaling guarantee, fixed speedup, or pivot-reduction
percentage follows from choosing an ordering. Different elimination orders
can produce different floating-point results even for a unique linear-system
solution. Numerical comparison against the declared reference remains required.

See [implementation details](implementation-details.md), [the harness audit](../benchmark-harness-audit.md)
and [attribution review](../joss-attribution-audit.md) for concrete next work.
