# BJT OFF initialization and IGBT operating points

Checkpoint 31 fixes a dropped Q-card `OFF` parameter. The parser previously
ignored the flag, and the BJT/VBIC adapters and registry builders did not pass
it to their underlying device instances. Both translated load implementations
already implement the required behavior: zero junction bias during INITJCT
and, for an OFF device, INITFIX. This is an initialization condition, not a
permanent switch that disables the transistor.

The reference is the checksum-pinned ngspice 47 source: `bjt/bjtload.c` and
`vbic/vbicload.c` in `src/spicelib/devices`, together with their instance
parameter handlers. No reference options or numerical tolerances changed.
`OFF`, mixed-case spelling, and numeric `off=1`/`off=0` now reach the adapters.
The registry's numeric `off` parameter follows the same path.

## Regression evidence

Two new behavioral tests inspect junction voltages after one Newton load:
parser coverage includes NPN/PNP and BJT/VBIC; registry coverage includes both
model families. They fail before the production correction and pass afterward.
A separate authored fixture, `tests/circuits/bjt_off_bias.cir`, checks that
biased transistors marked OFF reach the same final operating point as their
ordinary counterparts. Every public voltage and supply current is compared
with ngspice 47 at relative tolerance 1e-4 and absolute floor 1e-9; reference
implementation-specific internal node names are excluded explicitly.

The Release suite passes 1,237 of 1,238 tests; the existing RFF70N06 failure
remains. All 393 Python/tooling tests pass after rebuilding the wheel. The
three OFF tests and complete BJT/VBIC comparison executables pass 14 sanitizer
tests with AddressSanitizer, UndefinedBehaviorSanitizer and leak detection.
This does not certify unrelated device paths or resolve the reference T-line leak.

The first sanitizer attempt caught a double-finalization error in the new
test helper, corrected before the final run. An initial benchmark verification
attempt rejected a changing source inventory during that test edit. Subsequent
verification runs use stable source: all 34 comprehensive workloads, all five
THS4131 workflows and the original TLV edge/DC-port contract pass. There are no
new performance measurements; the stricter TLV pointwise discrepancy remains.

## Family evaluation

Selection retains every frozen declaration from
`uncategorized/spice_complete/irf.lib`, plus PA84 from `apex.lib`: 411
declarations and all 790 planned fixtures. Selection follows the earlier
regression investigation and is not held out. The reference executable,
startup configuration and materialized fixture hashes match checkpoint 29.
The audit recomputes outcome decisions and verifies raw artifact hashes.

| Variant | MATCH | MISMATCH | Total |
|---|---:|---:|---:|
| Primary | 410 | 1 | 411 |
| Isolated/driven | 372 | 7 | 379 |

The correction changes 26 driven fixtures from MISMATCH to MATCH, including
all six IGBT regressions introduced during the earlier true-gmin correction.
There are no outcome regressions in this population. Native values change in
109 fixtures; reference values and status remain unchanged throughout.
The seven driven mismatches and the PA84 primary mismatch remain blockers.
These minimal operating-point fixtures do not certify transient, AC or noise
behavior, nor prove that all lexical declarations bind independently.

PA84 still finishes through transient operating-point fallback with substantially
different terminal voltages. Both engines report exceptionally large internal
source currents on this fixture. That observation alone neither proves a valid
DC equilibrium nor justifies excluding it. The continuation and stationary-state
checks need further diagnosis.

## Evidence and next work

[Checkpoint 31](evidence/joss/2026-09-11-validation-31.json) records source hashes,
commands, test results, the source patch and archived diagnostics. The family
[summary](evidence/joss/2026-09-11-checkpoint31-irf-family47-summary.json),
[ledger](evidence/joss/2026-09-11-checkpoint31-irf-family47-records.json.gz) and
[transitions](evidence/joss/2026-09-11-checkpoint31-irf-family47-transitions.json)
retain the population and unresolved results.

The full evaluation of all 67,359 fixture runs over 34,908 cases completes
against the frozen corrected binary. Relative to checkpoint 29, 35 driven
fixtures change from MISMATCH to MATCH: the 26 IRF cases, eight triac cases in
`tectriac.lib`, and one IGBT in `m_igbt.lib`. There are no outcome regressions;
1,628 native results change and all reference results remain unchanged. The
audit retains all other outcomes:

| Variant | MATCH | MISMATCH | NG_ONLY | NEO_ONLY | NEO_TRIVIAL | BOTH_FAIL |
|---|---:|---:|---:|---:|---:|---:|
| Primary | 20,228 | 1,274 | 22 | 4,716 | 5,364 | 3,304 |
| Isolated/driven | 22,885 | 1,277 | 50 | 6,939 | 0 | 1,300 |

The full [summary](evidence/joss/2026-09-11-checkpoint31-corpus-run47-summary.json),
[ledger](evidence/joss/2026-09-11-checkpoint31-corpus-run47-records.json.gz) and
[transitions](evidence/joss/2026-09-11-checkpoint31-candidate47-transitions.json)
record these results. Unchanged labels do not imply improved error margins at
every signal. Remaining supported mismatches need case-level diagnosis.
No vendor model source is added to the authored regression fixture; corpus
redistribution review remains open.
