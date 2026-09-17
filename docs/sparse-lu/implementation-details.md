# In-tree sparse solver implementation details

Checked against source September 11, 2026. The previous speculative design and
performance discussion is [archived](../evidence/joss/2026-09-11-historical-sparse-lu-implementation-details.md).
It does not certify an implemented ISLU, BTF-AMD, GPU or right-looking block path.

## Ordering and symbolic setup

`AmdLuSolver::symbolic` converts the matrix pattern to compressed sparse columns
and passes it to `amd_ordering`, which symmetrizes the graph internally. The
ordering removes dense vertices, repeatedly selects the lowest-degree live
vertex, explicitly adds fill edges among its neighbors, and appends the dense
vertices. It uses the historic AMD name but not SuiteSparse AMD's quotient-graph
representation, supervariable handling or postordering.

The numeric solver retains the original matrix column structure, the column
permutation, working vectors and factor storage. Its depth-first search visits
the graph of previously constructed lower-factor columns. Symbolic setup also
initializes the separate `NeoSolver` used for complex operations.
Source: [amd.cpp](../../src/core/amd.cpp) and
[amd_lu_solver.cpp](../../src/core/amd_lu_solver.cpp).

## Refactorization and fallback

`numeric` calls the full factorization path. `refactorize` first attempts to
replay the previous factor structure and pivot order when replay data exists.
A successful replay updates its diagnostic counter; a failed replay falls back
to full factorization and records new replay data. This is an implementation
mechanism, not proof of a performance advantage for a circuit population.

The automatic solver wrapper has a separate first-factorization fallback to
`NeoSolver`. It reacts to reported singularity; it does not detect or correct
all rounding differences between successful solutions. Later internal replay
fallback and this initial solver switch are different operations.
Source: [make_solver.cpp](../../src/core/make_solver.cpp).

## Unused helpers and experimental programs

`btf_decompose` computes strongly connected components and `maximum_transversal`
provides a matching helper. They are not called by the production solver paths.
Do not infer an integrated BTF pipeline from those source files or their tests.

The matrix microbenchmarks and the manually reconstructed Newton profiler are
separate from production circuit runs. Before retaining a causal claim, verify
matrix residuals and failure status, isolate the intended mechanism, and use
paired samples with equal boundaries and recorded dispersion. No threshold
or reference setting may be adjusted to conceal a discrepancy. See the
[benchmark audit](../benchmark-harness-audit.md) and
[current experiment](../benchmark-methods.md).
