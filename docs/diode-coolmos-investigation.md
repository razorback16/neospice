# Open diode and CoolMOS questions

The [capabilities guide](capabilities.md) describes implemented diode grading
and temperature behavior. The following questions remain unresolved against
ngspice 47.

## CoolMOS operating-point selection

The frozen family contains 241 declarations from
`uncategorized/Bordodynovs Electronics Lib/sub/CoolMOSsimp.lib`, with primary
and isolated/driven fixtures. Ten driven fixtures retained mismatches in the
recorded family evaluation. `spna2n80c2_l0` failed in the reference and cannot
count as an accuracy match. Use the [corpus triage](corpus-mismatch-triage.md)
and [frozen experiment](kicad-experiment.md) for the evaluation population.

In `SPA11N60CFD_L0`, source stepping diverges near source fraction 0.741497,
after 41 matching nonzero trial decisions. Failed-source iterates differ before
transient operating-point fallback. Diagnose the state transfer and resulting
stationary solution. A successful fallback alone does not prove a valid DC
solution or an independently checked KCL residual.

## Parameter coverage

- Exact `M=1` needs a reference check because the charge formulas divide by
  `1-M`.
- Diode geometry through the registry builder needs comparison coverage.
- The full supported option and temperature matrix needs review.

Keep original models, fixtures, and comparison tolerances during diagnosis.
[Recorded evidence](evidence/joss/2026-09-11-validation-29.json) identifies the
family run and regression inputs.
