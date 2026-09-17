# Diode grading, temperature and CoolMOS investigation

Checkpoint 29 has two separately identified implementations. Stage 1 changes
true-gmin continuation; stage 2 also corrects diode grading and instance
temperatures. Both use ngspice47 as the sole behavioral reference. No
reference options or comparison thresholds were changed.

## Reproduced defects and corrections

ngspice 47 `cktop.c` keeps a minimum reduction factor of 3 in slow successful
true-gmin steps. Its dynamic-gmin path retains 1.00005. neospice previously used
1.00005 for both. A controlled nonlinear-device regression exercises the
iteration boundary and fails before the correction. All 15 convergence tests
then pass, including under AddressSanitizer with leak detection. This correction
alone changes none of the 482 CoolMOS fixture native scalar/status results.

The CoolMOS `DGD` model declares `M=1.2`. neospice's `DIOtemp` capped both the
shared model coefficient and its temperature-adjusted value at 0.9. ngspice 47
`diotemp.c` preserves both. Removing these caps repairs the reproduced
capacitance and depletion-charge difference. The original model parameters remain unchanged.

The new temperature checks also exposed ignored diode instance `TEMP` and
`DTEMP`. The parser and registry builder now retain them, and the adapter
converts explicit Celsius temperatures to kelvin with the corresponding given
flags. Explicit `TEMP` overrides `DTEMP`, including at zero Celsius. The thermal
noise source uses the same temperature convention as ngspice 47 `dionoise.c`;


## Regression evidence

`diode_grading_ac.cir` contains ten reverse-biased branches covering `M=0.5`,
`0.9`, `0.91`, `1.2`, first/second-order temperature coefficients, shared models
at different temperatures, `DTEMP`, and `TEMP=0` overriding an offset.
`TLEVC=1`, `CTA=0`, and `TPB=0` hold junction capacitance/potential fixed for
an independent analytical check:

`C = CJO * (1 - V/VJ)^(-M(T))`, measured as `-Im(I(Vsource))/(2*pi*f)`.

The native analytical assertion uses relative error 1e-10. The reference AC
check uses the existing diode AC policy, relative 1e-4 and floor 1e-9. A separate
reverse-biased pulse/RC fixture checks all output voltages and source currents
with relative 1e-3 and floor 1e-9. Three noise fixtures use the existing diode
noise policy, relative 3e-4 and floor 1e-15.

All three initial grading tests fail on stage 1. Removing the grading caps
alone fixes the transient fixture; temperature branches still fail until their
instance parameters reach the adapter. All four final regressions pass against
47. The AC and transient fixtures have zero recorded reference/native error in
the checked runs. The full 13-test diode executable passes with ASan/UBSan and
leak detection against 47. 

The complete Release suites contain 1,235 tests: 1,234 pass against 47, with
RFF70N06 still failing. There are no skips. The
rebuilt Python wheel passes all 387 Python/tooling tests.

## CoolMOS population and limits

The population is all 241 declarations from the frozen corpus file
`uncategorized/Bordodynovs Electronics Lib/sub/CoolMOSsimp.lib`, each with both
primary and isolated/driven fixtures. Selection does not depend on success.
This previously observed population is not a held-out evaluation.

| Implementation / reference | Primary | Isolated/driven |
|---|---|---|
| Before / 47 | 240 MATCH, 1 NEO_TRIVIAL | 224 MATCH, 16 MISMATCH, 1 NEO_ONLY |
| Stage 1 / 47 | 240 MATCH, 1 NEO_TRIVIAL | 224 MATCH, 16 MISMATCH, 1 NEO_ONLY |
| Stage 2 / 47 | 240 MATCH, 1 NEO_TRIVIAL | 230 MATCH, 10 MISMATCH, 1 NEO_ONLY |

The unchanged `spna2n80c2_l0` driven fixture fails in the reference; NEO_ONLY
is not an accuracy match. Primary trivial results do not prove useful
excitation. Full-corpus stage 2 runs and artifact audits are tracked separately
in [JOSS progress](joss-progress.md); these family results do not replace them.

The first representative `SPA11N60CFD_L0` source-step trace diverges at source
fraction approximately 0.741497, after 41 identical nonzero trial decisions.
Failed-source iterates differ before transient operating-point fallback. An
accepted fallback result does not independently establish a stationary DC
solution or KCL residual. The grading correction improves some fixtures but
does not prove the remaining source-step divergence has been resolved.

Exact `M=1` remains an unverified reference edge case: both implementations'
charge formulas contain a division by `1-M`. This work does not claim all
grading values are certified. Diode geometry through the separate registry
builder and the full supported option matrix also need review. Original
fixtures, failures and tolerances remain in accounting.
