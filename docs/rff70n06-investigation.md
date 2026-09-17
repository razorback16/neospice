# RFF70N06 operating-point blocker

`RFF70N06.ReferenceIsInconclusive` pins this fixture's classification. Both
neospice and the ngspice 47 reference fail the original frozen fixture, so the
reference failure prevents an accuracy comparison; it does not certify
neospice's result or justify removing the fixture from accounting.

The regression was previously written as a comparison that demanded a converged
reference, so it stood permanently red. That reported the situation as an
unexplained failure rather than as the explicit classification milestone 3 asks
for. The test now asserts the observed state directly: ngspice 47 returns no
operating point and reports a diagnostic, and neospice fails explicitly with a
finite residual rather than fabricating a result. Either half changing fails the
test, which is exactly when the classification would need revisiting -- if
ngspice ever solves the fixture a real comparison must replace this test, and if
neospice converges alone the outcome must be adjudicated rather than assumed
good.

Observed 2026-09-17 against the pinned reference:

| Engine | Outcome |
|---|---|
| ngspice 47 | aborts: `doAnalyses: OP: Timestep too small; trouble with x1:dbdmod-instance d.x1.dbody` |
| neospice | explicit non-convergence after 101 iterations, residual 836.875 |

The circuit instantiates `RFF70N06_HA` from the unchanged `harprmos.lib` at
KiCad-Spice-Library revision `a8688952bcaab19f567bc4db237b60bde03ef310`, with
three100 kOhm port terminations and `ngbehavior=psa`. Its model-library SHA-256 is
`61469f6ff311b0c8efb6e62b037293843806ecb1b23c5de71472983628858dae`.
[Build configuration](building.md) acquires and verifies this input; acquisition
failure must not skip the test.

The fixture and its acquisition are unchanged. The former value comparison used
a reference-magnitude allowance of `abs(neo-ng) <= 1e-3*abs(ng)+1e-6 V`, stricter
than the corpus's symmetric-magnitude allowance; it is retained here because it
is the comparison to restore if ngspice 47 ever solves the fixture. No tolerance
was loosened to reach the current state: the comparison was never reached, since
the reference produces no operating point to compare against. The reported native
residual derives from loaded RHS magnitude; it is not an independently measured
KCL residual.

ngspice47 preserves post-solve device-convergence failure flags. neospice must
preserve these checks too. Direct Newton, continuation state transfer and the
transient operating-point fallback still need a reduced numerical diagnosis.
Internal floating networks and very large failed iterates are observations to
investigate, not reasons to loosen tolerances or alter the reference options.

Current evidence is in [checkpoint29](evidence/joss/2026-09-11-validation-29.json)
and [progress](joss-progress.md). Earlier debugger traces and their interpretation
remain in immutable checkpoint artifacts and the
[documentation archive](evidence/joss/2026-09-11-pre-ngspice47-only-docs.tar.gz).
They are provenance, not an obligation to support another reference version.
