# Attribution audit in progress

Source inspection on September 11, 2026. This records concrete discrepancies
for milestone 6; it is not a completed distribution-license review.

## Ordering algorithm

`src/core/amd.cpp` constructs the symmetrized adjacency graph, chooses a
minimum-degree live vertex by scanning vertices, explicitly inserts the fill
clique among its live neighbors, and recomputes their degrees. Dense vertices
are removed before this loop and appended in increasing original-degree order.
The implementation is minimum-degree elimination on an explicit graph with a
dense-vertex heuristic. Its function name `amd_ordering` does not establish
algorithmic equivalence to SuiteSparse AMD.

The local SuiteSparse reference `AMD/Source/amd_2.c` describes approximate degree
updates, quotient-graph elements, supervariables, aggressive absorption and a
postordering step. Those structures and steps are absent from neospice's
implementation. The default dense threshold, clamped between 16 and n with
coefficient 10 times sqrt(n), does follow the reference's default formula.
Sharing that threshold does not imply an identical ordering or performance.

## Corrections applied in checkpoint26

Source/header comments, NOTICE, CREDITS and README now describe the explicit
minimum-degree algorithm, preserve SuiteSparse author credit, and remove claims
of algorithm/permutation equivalence. Comments no longer call explicit clique
fill element absorption or assert the same dense-vertex ordering as SuiteSparse.

The solver factory/interface comments now state the actual size-and-linearity
gate, identify the `klu` environment value as an alias for the in-tree solver,
and remove the claim that uniqueness proves identical floating-point results.
No solver choice, threshold or numerical operation changed. Non-comment token
comparison and byte-identical engine archives establish that limited scope.

The sparse-LU documentation now describes production paths and unused BTF
helpers from their call sites. The old speculative documents and unsupported
quantitative projections are retained in historical evidence. A source comment
is not an ablation or proof of general performance.

## Distribution review still required

Read the complete upstream component notices and licenses and compare them
with the files actually included in source archives and installed wheels.
`NOTICE` includes component-specific terms; its condensed Berkeley description
and the broad relicensing wording in `CREDITS.md` do not by themselves establish
that all required text is distributed. Preserve applicable full notices and
identify unresolved component provenance rather than declaring all bundled
code covered solely by the project's MIT license.

Local reference inputs: `~/Codes/ngspice/COPYING` and the copyright/license
headers and license files of `~/Codes/SuiteSparse`. The audit must also cover
model-specific terms and packaged native dependencies. No license conclusion
or completed packaging verification is claimed here.

The installed checkpoint-24 wheel was inspected with `importlib.metadata`.
It includes `LICENSE` and `NOTICE` under
`neospice-0.1.0.dist-info/licenses/`; its `License` metadata is `MIT`.
This confirms those two files are shipped in that local wheel. It does not
verify completeness of their component terms, repaired platform wheels, or
source-distribution contents. The exact inventory is retained with checkpoint25.
