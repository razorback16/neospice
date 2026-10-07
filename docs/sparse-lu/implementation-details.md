# Sparse solver implementation

## Solver selection

The [solver factory](../../src/core/make_solver.cpp) uses Sparse-derived
`NeoSolver` for nonlinear circuits and linear circuits below 256 unknowns.
Larger linear circuits use `AmdLuSolver`, with a first-factorization fallback
to `NeoSolver`. Complex operations use `NeoSolver`.

Record environment overrides from the [solver interface](../../src/core/solver_iface.hpp)
in any experiment. The legacy `klu` name selects the in-tree implementation,
not the external SuiteSparse KLU library.

## Ordering and symbolic setup

`AmdLuSolver::symbolic` converts the pattern to compressed sparse columns.
`amd_ordering` symmetrizes the graph, defers dense vertices, and selects the
lowest-degree live vertex from a set keyed by degree and vertex index.
It explicitly adds fill edges among neighbors and appends dense vertices.
This is an explicit elimination graph, not SuiteSparse's quotient-graph AMD.

The solver retains the column structure, permutation, workspaces, and factor
storage. A depth-first search visits existing lower-factor columns. Symbolic
setup also initializes the separate solver for complex operations.
See [ordering](../../src/core/amd.cpp) and [LU](../../src/core/amd_lu_solver.cpp).

## Refactorization and fallback

`numeric` performs full factorization. `refactorize` first attempts to reuse
factor structure and pivot order. If that attempt fails, it performs full
factorization and records new reuse data. Diagnostic counters distinguish the
paths.

The automatic wrapper's first-factorization fallback is separate from this
internal retry. It reacts to reported singularity. Different successful
orderings can still produce different floating-point results.

## Other helpers

`btf_decompose` computes strongly connected components. `maximum_transversal`
provides matching. Production solver paths do not call these helpers.

Use [benchmark methods](../benchmark-methods.md) for numerical qualification
and timing. The [performance guide](../performance-analysis.md)
contains measured circuit results. [NOTICE](../../NOTICE) records attribution.
