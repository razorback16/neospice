# RFF70N06: reference-inconclusive operating point

`RFF70N06.ReferenceIsInconclusive` asserts that both neospice and ngspice 47
fail the original fixture. It remains in experiment accounting and does not
count as a numerical match.

## Fixture and test contract

The circuit instantiates `RFF70N06_HA` from `harprmos.lib` with three 100 kOhm
port terminations and `ngbehavior=psa`. [Build instructions](building.md) pin
the library revision and checksum. Acquisition failures must not skip the test.

ngspice 47 reports `OP: Timestep too small` at `d.x1.dbody`. neospice must fail
explicitly with a finite residual. A change in either outcome fails the test
and requires review. If ngspice 47 converges, restore a numerical comparison
using `abs(neo-ng) <= 1e-3*abs(ng)+1e-6 V`.

## Remaining diagnosis

Reduce the direct Newton, continuation-state transfer, and transient
operating-point paths to identify why neither engine finds a solution.
Preserve post-solve device convergence checks, reference options, and original
model parameters. Floating internal networks and large failed iterates are
observations to investigate, not reasons to relax tolerances.

The native residual is a loaded-RHS diagnostic, not an independent KCL check.
See [recorded evidence](evidence/joss/2026-09-17-checkpoint39-milestone3.json)
and [corpus triage](corpus-mismatch-triage.md).
