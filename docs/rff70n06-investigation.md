# RFF70N06 operating-point blocker

`RFF70N06.OriginalCorpusOperatingPoint` remains a required failing regression.
Both neospice and the ngspice47 reference fail the original frozen fixture.
The reference failure prevents an accuracy comparison; it does not certify
neospice's result or justify removing the fixture from accounting.

The circuit instantiates `RFF70N06_HA` from the unchanged `harprmos.lib` at
KiCad-Spice-Library revision `a8688952bcaab19f567bc4db237b60bde03ef310`, with
three100 kOhm port terminations and `ngbehavior=psa`. Its model-library SHA-256 is
`61469f6ff311b0c8efb6e62b037293843806ecb1b23c5de71472983628858dae`.
[Build configuration](building.md) acquires and verifies this input; acquisition
failure must not skip the test.

The test requires successful status and all three finite external voltages.
Its reference-magnitude allowance is `abs(neo-ng) <= 1e-3*abs(ng)+1e-6 V`,
which is stricter than the corpus's symmetric-magnitude allowance. The assertion,
fixture and thresholds remain unchanged. Failure currently occurs before value
comparison. The reported native residual derives from loaded RHS magnitude;
it is not an independently measured KCL residual.

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
