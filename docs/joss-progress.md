# JOSS readiness progress

**Active; not ready for submission.** The [goal](joss-readiness-goal.md) targets
ngspice47 exclusively for runtime behavior, compatibility evaluation, benchmarks
and migration-tool source inputs. Earlier experiment records are immutable
provenance; they are not additional compatibility targets or required reruns.

Work remains on `main`, based on `39bc809e5a95bcf6b8aa50f8b18bd1e790210a43`,
with the accumulated working-tree changes preserved. No remote publication or
submission is authorized by this goal.

| Milestone | Status and remaining work |
|---|---|
| 1. Scope, eligibility and environment | Pinned ngspice47 CLI/shared build verified locally. CI and migration tests use the same47 source. Full support matrix, actual public timeline and eligibility remain. |
| 2. Trustworthy validation | Acceptance met for checkpoint 38. The one condition carried out of it, the explained `RFF70N06.OriginalCorpusOperatingPoint` failure, is discharged in milestone 3 by classifying the fixture reference-inconclusive rather than by solving it. 1,267 C++ and 405 fresh-wheel Python/tooling cases run with zero skips; four reference-gating negative controls pass. See [checkpoint 38](#milestone-2-checkpoint-38). |
| 3. Numerical correctness | Triaged against the seven goal items in [checkpoint 39](#milestone-3-triage-checkpoint-39-in-progress). Items 5 and 6 are closed: VDMOS self-heating now fails explicitly instead of returning a converged result with the thermal network absent, and the advertised sensitivity scope matches the implementation. Item 2 needs a classification decision, since ngspice 47 aborts RFF70N06 and so provides no reference answer. Items 3, 4 and 7 remain open; item 4 is blocked on declaring the supported matrix. |
| 4. Compatibility experiment | Checkpoint 36 executes and independently audits all 67,359 fixtures, retaining all 1,037 outcome changes and unchanged reference results. Candidate 37 repairs need a full rerun. Generated-internal-observable classification is resolved in checkpoint 38; broader parameter/scope coverage and grouping/held-out multi-analysis evaluation remain. |
| 5. Benchmarks | All34 comprehensive workloads qualify47; THS4131 qualifies5/5. TLV passes its original edge/DC-port contract but retains a strict pointwise discrepancy. Final candidate measurements and interpretation remain. |
| 6. Documentation and attribution | Current documentation uses47 exclusively. Component copyright/license/distribution audit remains incomplete. |
| 7. Manuscript and release | Draft paper exists. Actual research use, author confirmations, final release metadata, independent installation and final evidence reconciliation remain. |

## Milestone 3 triage, checkpoint 39 (in progress)

Milestone 3 has seven items. This is their state established from evidence, not
from the previous tracker text, which did not record most of them.

| Item | State | Evidence |
|---|---|---|
| 1. LTRA current discrepancy | Comparison enforced and passing | `LTRAValidation.TransientRC/RLC/LC` assert `compare_transient`; see the caveat below |
| 2. RFF70N06 | Classified reference-inconclusive, assertion made executable | `RFF70N06.ReferenceIsInconclusive` pins both engines' failure |
| 3. VBIC / diode transient | Not yet triaged | `vbic-compatibility.md`, `transient-readiness.md`, `vbic_delay_*.cir` |
| 4. Corpus mismatch triage | Blocked on the supported matrix | checkpoint 36 is the input population |
| 5. Empty/incomplete analyses | Closed | VDMOS AC/noise reject explicitly; MES noise implemented with nine fixtures; VDMOS self-heating now rejected |
| 6. Advertised analysis scope | Closed, no overclaim found | `sens.cpp:154-163` perturbs exactly resistor resistance and independent-source DC, which is what `capabilities.md` and `README.md` claim |
| 7. Sanitizers | No ASan errors; leaks are all inside libngspice; one unexplained UBSan artifact | see below |

Item 2 is closed as a classification, not as a repair. The goal document's
premise was that ngspice solved `RFF70N06_HA` and neospice failed it. That is no
longer true against the pinned reference: ngspice 47 aborts the fixture with
`doAnalyses: OP: Timestep too small; trouble with x1:dbdmod-instance d.x1.dbody`,
and neospice fails explicitly after 101 iterations at residual 836.875. With no
reference operating point there is nothing to compare against in either
direction, so the fixture is reference-inconclusive: retained in corpus
accounting, never counted as a match, never deleted.

`RFF70N06.OriginalCorpusOperatingPoint` demanded a converged reference and so
stood permanently red, reporting this as an unexplained failure.
`RFF70N06.ReferenceIsInconclusive` replaces it and asserts the observed state
instead: the reference returns no operating point and reports a diagnostic, and
neospice fails explicitly with a finite residual rather than fabricating one.
Either half changing fails the test, which is exactly when the classification
must be revisited.

**The suite is now green, and that must not be read as "no known problems".**
The fixture is still unsolved by both engines; what changed is that the
limitation is now stated rather than displayed as a bare failure. It remains an
open limitation in the support matrix.

Item 1 is passing but deserves a second look rather than a tick: the transient
comparison runs at a 5e-2 relative tolerance with an in-test comment calling it
"very loose". That tolerance predates this work and was not loosened here, but
whether it is wide enough to hide the original current discrepancy has not been
established.

Item 5's remaining gap closed this checkpoint. neospice ignored the VDMOS
`thermal` instance flag, so the self-heating form returned a converged operating
point with the thermal network absent (`v(tj) = v(tc) = 0` against 36.477 and
36.172). It is now rejected at parse time, mirroring ngspice 47's activation
condition exactly so that every isothermal form still runs and still agrees.
See [vdmos-compatibility](vdmos-compatibility.md).

Item 7's baseline runs ASan and UBSan against the pinned 47 install from a fresh
build directory, since the in-repo build directories point at system ngspice 42.
A first run used `-fno-sanitize-recover=undefined`, which aborted each test on
its first report and so measured almost nothing; the numbers below come from the
rerun with recovery enabled, where every test runs to completion.

Of 1,268 cases, 19 fail. Eighteen of those fail only because the process exits
non-zero after LeakSanitizer reports; their assertions all pass. Every leaked
allocation traces to `tmalloc`/`trealloc` in the reference library's own
allocator (`src/misc/alloc.c` in the ngspice 47 source) with no neospice frame
in the stack, so these are leaks inside libngspice, which the in-process
comparison harness links, and not in the code under test. The nineteenth was the
then-current `RFF70N06.OriginalCorpusOperatingPoint`, since reclassified.

There are **no AddressSanitizer memory errors** -- no use-after-free, no
overflow, no invalid access. The only UBSan finding is
`store to null pointer of type 'const struct IntegratorCtx *'`, reported at
three sites (`newton.cpp:196`, `ac.cpp:103`, `noise.cpp:105`) that are the same
statement: a guard destructor assigning `nullptr` to a `thread_local` pointer.
That statement cannot dereference a null pointer; the identical store to a
thread-local defined in the same translation unit does not report, while the
three that do all target one defined in `circuit.cpp`; it does not appear in an
`-O0` build of the same tree; and it does not reproduce in a minimal program
under the same compiler and flags, including with a shared library, a static
archive, a guard destructor and `-O2`. It is therefore most likely a GCC 14
instrumentation artifact rather than a defect, but that is **not proven**, and it
is recorded as unresolved rather than dismissed.

Repeating the run on the tree that includes the self-heating rejection and the
RFF70N06 reclassification gives the same picture with the one real failure gone:
1,268 cases, 19 failures, **zero assertion failures** -- every remaining failure
is a leak-only non-zero exit, all 19 leak stacks allocate through
`tmalloc`/`trealloc` in the reference library, and no leak stack contains a
single neospice frame. This is the milestone 3 baseline; it still has to be
repeated on the frozen candidate for milestone 4.

Nothing in this section changes corpus totals: checkpoint 36 remains the latest
complete corpus experiment.

## Milestone 2 checkpoint 38

Milestone 2 acceptance is met, conditional on one explained failure. The
[record](evidence/joss/2026-09-17-checkpoint38-milestone2.json) and its
[archive](evidence/joss/2026-09-17-checkpoint38-milestone2.tar.gz) retain the
build, both suites, the negative controls, the VDMOS probe and a patch that
reconstructs this tree from checkpoint 37.

The C++ suite runs 1,267 cases against the pinned 47 build with zero skips;
`RFF70N06.OriginalCorpusOperatingPoint` is the only failure. ngspice 47 also
fails that fixture, so it is reference-inconclusive rather than a neospice
regression, and its transparent classification is milestone 3 work. Do not read
this milestone as "all suites pass". A fresh virtual environment installing
`.[dev,benchmarks]` from the working tree runs all 405 Python and tooling cases
with zero skips. The candidate binary is byte-identical to the checkpoint-37
development binary, so these results attribute to the recorded source.

Reference gating is demonstrated rather than assumed. CMake refuses to configure
against the system ngspice 42 (`ngspice = 47` unsatisfied); the corpus preflight
rejects a 42 CLI and accepts the pinned 47; the test binary resolves
`libngspice.so.0` from the pinned 47 install through RPATH with and without
`LD_LIBRARY_PATH`. The Python extension does not link libngspice at all, so
`PKG_CONFIG_PATH` is not load-bearing for the wheel and the record says so.

A negative control found the last silent skip: without the ngspice 47 source the
three BSIM4v7 migration roundtrip tests disappeared rather than failing. They now
fail with the documented setup step unless `NEOSPICE_ALLOW_MISSING_NGSPICE_SOURCE=1`
records a deliberate opt-out, which is the only remaining legitimate skip.

The VDMOS generated-internal-observable classification is complete and
[documented](kicad-experiment.md). Rerunning the checkpoint-36 reduced VDMOS
option probe through the real comparison path moves `rg`, `rb` and the combined
case from MISMATCH to MATCH; each had failed only on a missing `v(m1#gate)` or
`v(m1#body_diode)` while every public signal already agreed. The exclusion is
like-for-like because neospice allocates the same nodes and marks them internal,
and the new `VDMOSValidation.GeneratedInternalNodesAreModeledButPrivate`
regression holds both that fact and the completeness of the pattern against the
reference. Excluded signals are listed per fixture and an explicitly required
port is still compared. The `theta` residual from that probe is also closed by
the checkpoint-36 mobility fix.

Every `compare_*` call site in the C++ tests was audited for discarded or
unasserted results; all nine candidates are definitions or assertions on the
following line, and the LTRA transient comparisons are asserted.

Both suites are reproduced from the pinned 47 install with `PATH`,
`LD_LIBRARY_PATH`, `SPICE_SCRIPTS` and `NGSPICE_DIR` pointed at it and
`OMP_NUM_THREADS=OPENBLAS_NUM_THREADS=1`: `ctest --test-dir <build> -j$(nproc)
--output-on-failure --no-tests=error` and `python -m pytest tests/python
python/tests tools/tests --import-mode=importlib`. The in-repo build directories
still point at the system ngspice 42 and were not used for any recorded result.
The record carries the full environment, both commands and an invalid-data
coverage matrix naming the test that rejects each of nine classes — empty or
unconverged results, NaN/Inf on either side, zero-point or truncated series,
missing required signals, malformed vector lengths, invalid axes, incomplete
grids, derived-error overflow and physically invalid quantities.

Two acceptance questions are closed explicitly. There is no `tools/descriptors/
jfet.yaml` because the migrated device is `jfet2`, whose descriptor exists and
whose suite is driven by an explicit hardcoded list that names it with no skip
marker, so nothing silently disappears. The skip-to-failure change was also
checked against packaging: the wheel job runs only `tests/python` and
`python/tests`, and CI runs `tools/tests` only after building the pinned 47
reference and setting `NGSPICE_DIR`, so no workflow depends on the opt-out.

One new milestone 3 blocker was found and is recorded rather than deferred
silently: neospice accepts the thermal VDMOS form and returns a converged result
with the thermal network ignored, giving `v(tj) = v(tc) = 0` against 36.48/36.17
in ngspice 47. That is an apparently valid partial result, which milestone 3
forbids. No corpus model declares `Rthjc`/`Rthca`, so no frozen fixture changes.

The candidate-37 full-corpus rerun, sanitizers and benchmark accuracy gates
remain milestone 3-5 work. Checkpoint 36 is still the latest complete corpus
experiment and its totals must not be attributed to this checkpoint.

## Current development: candidate 37

The rebuilt Release suite passes 1,265/1,266 C++ tests against ngspice 47,
with only `RFF70N06.OriginalCorpusOperatingPoint` failing. All 53 corpus-runner
and paired-benchmark tooling tests pass, including rejection of reference
versions other than 47. The repository instructions, roadmap, build and CI
policy consistently require 47; archived evidence is preserved.

The new reference regressions cover four repairs: MOS model/node name
collisions during subcircuit expansion, direct/AKO temperature-assignment
precedence, MOS3 `KP`/`VTO` and diode `BV` temperature expressions, and unused
models whose names collide with source keywords, nodes or instance parameters.
The last defect explains the reduced DB3 unknown-parameter failure: a voltage
source's `DC` keyword incorrectly caused an unused global model named `DC` to
be evaluated. That case does not demonstrate a forward/local parameter defect.

This is an in-progress candidate. Checkpoint 38 above adds the fresh-wheel and
CI-equivalent validation and closes the generated VDMOS internal-observable
classification. Sanitizer, full-corpus and benchmark validation have still not
been repeated for these changes. Checkpoint 36 remains the latest complete
experiment; its corpus totals must not be attributed to candidate 37. GAUSS
arity and the other supported-scope blockers remain. The
[development record](evidence/joss/2026-09-11-checkpoint37-development.json)
retains the current source changes and test evidence.

## Latest complete checkpoint: 36

The frozen candidate corrects ordinary global three-terminal VDMOS model
scope, grouped/quoted model expressions, selected runtime temperature updates,
operating-point invalidation, ngspice 47 VDMOS mobility reduction and BJT nominal
junction-potential correction. All six previously failing VT6K1 bias/temperature
probes now match. See [VDMOS](vdmos-compatibility.md) and
[model expressions](model-card-compatibility.md) for the tested boundaries.

Release C++ validation passes 1,261/1,262 cases; the required RFF70N06 failure
remains. All 393 fresh-wheel Python/tooling tests, 58 focused parser/BJT/
temperature sanitizer tests and 11 VDMOS sanitizer tests pass. Leak detection
is enabled. All 34 comprehensive, five THS4131 and one TLV benchmark accuracy
gates pass. No new timing or publication-readiness claim is made.

The entire 67,359-fixture corpus completes and its outcomes are independently
recomputed. Primary has 20,027 MATCH, 1,273 MISMATCH, 224 NG_ONLY, 4,179
NEO_ONLY, 5,101 NEO_TRIVIAL and 4,104 BOTH_FAIL. Driven has 22,447 MATCH,
1,272 MISMATCH, 493 NG_ONLY, 6,902 NEO_ONLY and 1,337 BOTH_FAIL. Keep these
populations separate; they are not a combined matched-model percentage.

All 1,037 changed outcomes become explicit native parse failures: 892 unsupported
runtime parameter cases, 138 unresolved expression-parameter cases, five model/
node scope collisions and two GAUSS-arity cases. This includes 644 former
matches. An explicit error is not an accuracy win. Native values/status change
in 1,071 fixtures, while every reference value/status is unchanged. The
[summary](evidence/joss/2026-09-11-checkpoint36-corpus-run47-summary.json),
[ledger](evidence/joss/2026-09-11-checkpoint36-corpus-run47-records.json.gz),
[transitions](evidence/joss/2026-09-11-checkpoint36-candidate47-transitions.json)
and [triage](evidence/joss/2026-09-11-checkpoint36-transition-triage.json)
retain all fixtures and failures. The
[complete raw archive](evidence/joss/2026-09-11-checkpoint36-full-corpus.tar.gz)
contains 648,308 regular files; its
[verification](evidence/joss/2026-09-11-checkpoint36-full-corpus-archive.json)
checks all 648,276 audited artifacts plus the remaining run metadata.

The stronger diagnostics establish the immediate next work:

- Direct and AKO temperature expressions override later literals in ngspice 47;
  this candidate keeps the literal. Both probes fail at 0/27/60 °C. The parser's
  numeric-override unit assertion describes native behavior and must change
  with the implementation.
- A global model named `S` collides with the formal source terminal `s` of a
  four-terminal MOS card. The reduced fixture passes candidate 35 and fails
  candidate 36 while the reference passes. This is a new scoping regression.
- MOS3 `KP` accounts for 870 newly explicit errors, MOS3 `VTO` for 18 and
  diode `BV` for four. Forward/local parameters and GAUSS compatibility also
  remain; do not hide their failures or classify earlier matches as proof of
  correct expression evaluation.
- The corpus filter does not recognize generated VDMOS `#gate` and
  `#body_diode` nodes as internal. Reduced RG/RB probes fail on those missing
  observables while public currents agree. Correct the classification with
  explicit regression tests and retain every fixture in the accounting.

[Checkpoint 36](evidence/joss/2026-09-11-validation-36.json) records the 914-file
source inventory, candidate hash, reconstructable patch, build/test logs,
positive diagnostics and counterexamples, benchmark accuracy checks and corpus
archives. This is a verified development checkpoint, not a release candidate.

## Previous complete checkpoint: 35

The eight-cell terminal-layout regression verifies actual N/P VDMOS devices,
three/four/five-terminal forms, mixed-case model references, optional instance
multipliers, and reference operating-point agreement. Three-terminal non-VDMOS
cards and six-terminal VDMOS cards fail explicitly. All four affected
SGN20N40L/VT6K1 corpus fixtures now bind their VDMOS devices and match their
minimal reference operating points. See [VDMOS compatibility](vdmos-compatibility.md).

Candidate 35 passes 1,251/1,252 C++ tests, all 393 Python/tooling tests, 51
parser/BJT and eight VDMOS sanitizer cases with leak detection, and all
34 + 5 + 1 benchmark accuracy gates. The required RFF failure remains.
The full 67,359-fixture rerun and independent outcome audit complete with no
outcome regressions or changed reference results. Four NG_ONLY outcomes become
MATCH: both SGN20N40L and VT6K1 variants. Seven BOTH_FAIL outcomes become six
NEO_ONLY and one NEO_TRIVIAL; reference failure is not a compatibility success.
All 11 outcome changes are [triaged](evidence/joss/2026-09-11-checkpoint35-transition-triage.json).

Primary results contain 20,229 MATCH and 1,273 MISMATCH; driven results contain
22,889 MATCH and 1,273 MISMATCH. The [summary](evidence/joss/2026-09-11-checkpoint35-corpus-run47-summary.json),
[ledger](evidence/joss/2026-09-11-checkpoint35-corpus-run47-records.json.gz)
and [transitions](evidence/joss/2026-09-11-checkpoint35-candidate47-transitions.json)
retain every failure category and all fixtures. The
[complete raw archive](evidence/joss/2026-09-11-checkpoint35-full-corpus.tar.gz)
contains all 649,345 regular files; [verification](evidence/joss/2026-09-11-checkpoint35-full-corpus-archive.json)
checks every archived file, including 649,313 audited result artifacts.

Stronger diagnostics preserve the remaining defects: six VT6K1 bias/temperature
cases disagree with ngspice 47; global model-card expressions can be omitted;
and a three-terminal VDMOS inside a subcircuit fails when using a global model.
Literal model-parameter and local-model controls distinguish those failures.
These are open correctness blockers, not exclusions from the supported scope.
The [checkpoint record](evidence/joss/2026-09-11-validation-35.json) preserves
those diagnostics, the regression/build logs, benchmark accuracy checks and
the reconstructable candidate patch. Next work should correct model scope
and expression evaluation, then recheck the stronger VT6K1 probes; the
remaining numerical, evaluation, licensing, manuscript and human eligibility
requirements of the full goal remain in force.

## Previous verified checkpoint: 34

ngspice 47 remains the sole target; there is no older-version build, benchmark
or migration-test obligation. The current configuration and documentation have
been checked again for stale baseline instructions. Rejected-version regression
inputs and immutable experiment records remain evidence only.

Mixed syntax such as `NPN LEVEL=4(IS=1e-14) TD=1u` now preserves the model type
and parameters. The new nine-cell regression checks actual BJT/VBIC device
classes, positive-delay states, nonzero collector currents and ngspice 47
operating-point agreement. All six affected corpus wrappers now instantiate a
VBIC device with 74 states. Missing/wrong Q models and unsupported BJT levels
fail explicitly; classical levels 0/1/2 and VBIC levels 4/9 remain supported.
See [model-card compatibility](model-card-compatibility.md).

The frozen candidate passes 1,248/1,249 C++ tests, all 393 Python/tooling tests,
51 parser/BJT and ten VBIC sanitizer tests with leak detection, and all
34 + 5 + 1 benchmark accuracy gates. RFF remains the sole C++ failure. No
tolerance was relaxed and no new performance timing was collected.

All 67,359 corpus fixtures complete and their outcomes are independently
recomputed. Four mismatches become matches: two driven BFQ790 fixtures and
both TL072-R variants. All 456 newly explicit native failures have an earlier
warning that the same offending card was skipped. Of these, 452 also fail in
the reference. The other four are SGN20N40L and VT6K1, whose three-terminal
VDMOS cards were omitted; three had misleading MATCH outcomes and one was
a MISMATCH. The underlying VDMOS parser defect remains a blocker. No reference
result changes, fixtures are removed, or tolerances are relaxed.

Primary results contain 20,227 MATCH and 1,273 MISMATCH; driven results contain
22,887 MATCH and 1,273 MISMATCH. All failure and trivial-result categories are
retained in the [summary](evidence/joss/2026-09-11-checkpoint34-corpus-run47-summary.json),
[ledger](evidence/joss/2026-09-11-checkpoint34-corpus-run47-records.json.gz),
[transitions](evidence/joss/2026-09-11-checkpoint34-candidate47-transitions.json)
and [case-level triage](evidence/joss/2026-09-11-checkpoint34-transition-triage.json).
The [complete raw archive](evidence/joss/2026-09-11-checkpoint34-full-corpus.tar.gz)
and its [verification](evidence/joss/2026-09-11-checkpoint34-full-corpus-archive.json)
preserve all 649,334 regular files, including successful and unsuccessful
outputs. The [checkpoint record](evidence/joss/2026-09-11-validation-34.json)
includes the reconstructable source patch, tests, benchmark accuracy checks
and artifact hashes.

## Previous verified checkpoint: 33

The remaining active BSIM4 golden-value provenance now uses unmodified ngspice
47 and its explicitly selected BSIM4 4.7.0 kernel. All 13 preprocessing values
match the earlier constants; the drain-current constant now retains the full
printed precision. No tolerance changed. The [capture record](evidence/joss/2026-09-11-ngspice47-goldens.json)
and [reproduction instructions](../tests/goldens/README.md) replace the previous
unrecorded source-patch procedure. Current test comments describe 47 behavior;
other versions remain only as rejected inputs or archived provenance.
The focused verification passes all 10 C++ and 53 Python tests, with no skips.

Positive-TD VBIC comparisons now pass for AC, ordinary transient, Gear, UIC
and noise, with NPN/PNP, zero-delay controls, shared cards and geometry/charge
variation. UIC now performs one initial load without a DC solve and preserves
node initial conditions on capacitors. Noise uses the common operating-point
solver. The PNP noise case reproduces ngspice's retained auxiliary delay
conductances after transient OP recovery. See the [VBIC option audit](vbic-compatibility.md).

The frozen candidate passes 1,244/1,245 C++ tests (RFF remains failing), all
393 Python/tooling tests and all 34 + 5 + 1 benchmark accuracy gates. Ten VBIC
and 52 isolated core sanitizer cases pass leak detection. The broader core
run fails on the existing 120-byte reference TRAsetup leak; that result and
its isolated allocation stack are retained.

[Checkpoint 33](evidence/joss/2026-09-11-validation-33.json) freezes binary
SHA-256 `db85f6cdb74c4883f2e749fb921149fc923233f56f7f7469eebbc47ca398dc6f`.
All 67,359 fixtures complete with no outcome or saved engine-result changes
from checkpoint 32. The audit verifies 649,758 artifact hashes and recomputes
all outcome decisions. Evidence:
[summary](evidence/joss/2026-09-11-checkpoint33-corpus-run47-summary.json),
[ledger](evidence/joss/2026-09-11-checkpoint33-corpus-run47-records.json.gz),
[transitions](evidence/joss/2026-09-11-checkpoint33-candidate47-transitions.json).
The [complete raw-run archive](evidence/joss/2026-09-11-checkpoint33-full-corpus.tar.gz)
retains all 649,790 regular files, including successful and unsuccessful raw
outputs; its [verification record](evidence/joss/2026-09-11-checkpoint33-full-corpus-archive.json)
checks every archived file. These are operating-point results, not performance
measurements or full-option certification.

The [model-binding diagnostic](evidence/joss/2026-09-11-checkpoint33-model-binding-audit.json)
proved a parser defect in candidate 33: `NPN LEVEL=4(...)` became an incorrect model type and
its Q device is skipped. All six corpus fixtures enclosing the three lexical
VBIC declarations report that skip. A two-format minimal control gives the
same reference current in ngspice 47, but neospice omits Q1 and returns zero
supply current for the mixed syntax. A MATCH on a minimally excited wrapper
cannot establish that its transistor was instantiated.

## Previous complete numerical evidence

[Checkpoint 32](evidence/joss/2026-09-11-validation-32.json) corrects ordinary
VBIC intrinsic base–collector initialization to match ngspice 47. A regression
covers both polarities, levels 4/9 and OFF/UIC controls. The existing switching
comparison now passes the standard relative tolerance 1e-3 and denominator
floor 1e-9, replacing 0.27 and 0.05. Its worst normalized error is about
8.65e-8. See the [VBIC scope and option audit](vbic-compatibility.md).

The new full-corpus run has no outcome or saved engine-result changes from
checkpoint 31. This does not establish which VBIC options are exercised by the
generated fixtures; the new initialization regression supplies direct evidence.
Checkpoint 32 full-corpus evidence:
[summary](evidence/joss/2026-09-11-checkpoint32-corpus-run47-summary.json),
[ledger](evidence/joss/2026-09-11-checkpoint32-corpus-run47-records.json.gz),
[transitions](evidence/joss/2026-09-11-checkpoint32-candidate47-transitions.json).

Next: fix mixed model-card syntax and prove transistor binding in the affected
wrapper family. Then address VBIC query, noise scaling/temperature and
semiconductor node-IC audit leads. Continue
PA84 and remaining supported mismatch investigations and complete the support,
option and model-binding audits. The fifth thermal terminal remains outside
the parser interface. These are still publication blockers.

[Checkpoint 31](evidence/joss/2026-09-11-validation-31.json) fixes the ignored
BJT/VBIC `OFF` initialization parameter. In the entire IRF-library population
plus PA84 (790 fixtures), 26 driven mismatches become matches, with no outcome
regressions and unchanged reference results. All six IGBT regressions below
are resolved in this family rerun; PA84 remains. See the
[investigation and family evidence](bjt-off-investigation.md).

The full 67,359-fixture run also completes: 35 driven mismatches become matches,
with no outcome regressions and no changed reference results. Primary has
20,228 MATCH and 1,274 MISMATCH; driven has 22,885 MATCH and 1,277 MISMATCH.
All failure and trivial-result categories remain in the accounting. Native
values/status change in 1,628 fixtures, so unchanged outcome labels must not
be read as proof that every individual error margin improved. Benchmark
accuracy verification passes all 34 comprehensive workloads, five THS4131
workflows and the original TLV contract; no new timing claims are made.

Checkpoint 31 full-corpus evidence:
[summary](evidence/joss/2026-09-11-checkpoint31-corpus-run47-summary.json),
[ledger](evidence/joss/2026-09-11-checkpoint31-corpus-run47-records.json.gz),
[transitions](evidence/joss/2026-09-11-checkpoint31-candidate47-transitions.json).

[Checkpoint29](evidence/joss/2026-09-11-validation-29.json) contains the source
inventories, reconstructable patch, build/test logs, full-corpus ledgers and
family/benchmark audits. Its earlier multi-version records remain intact;
current requirements use only its47 results.

- Stage1 corrects the true-gmin slow-step factor floor to3; dynamic gmin retains
  1.00005. The new boundary regression fails before the correction;15 convergence
  sanitizer tests pass afterward.
- Stage2 preserves diode grading coefficients above0.9 and temperature-adjusted
  values. `TEMP`/`DTEMP` reach both the parser and registry adapter, and thermal
  noise uses the instance temperature. Analytical, AC, transient and noise
  regressions pass47. The complete13-test diode sanitizer run passes with leak
  detection; it does not certify unrelated reference/device paths.
- CoolMOS retains all241 declarations and both variants. Primary has240 MATCH
  and1 NEO_TRIVIAL. Driven results improve from224 to230 MATCH, leaving10
  MISMATCH and1 reference failure. Selection was made from previously observed
  data and is not held out.
- The diode fix repairs ten full-corpus outcome mismatches against47. Seven
  earlier MATCH-to-MISMATCH transitions were introduced in stage1. Checkpoint
  31 resolves the six IGBT cases in a family rerun; PA84 remains. Neither
  aggregate gains nor reference failures erase the remaining blockers.
- Twelve earlier benchmark verification runs preserved their input inventories;
  the47 comparison scalars are unchanged from checkpoint27. No new performance
  timing was collected in checkpoint29.

Earlier full-corpus evidence (checkpoint 29; predates the OFF fix):
[summary](evidence/joss/2026-09-11-checkpoint29-stage2-corpus-run47-summary.json),
[ledger](evidence/joss/2026-09-11-checkpoint29-stage2-corpus-run47-records.json.gz),
[transitions](evidence/joss/2026-09-11-checkpoint29-stage2-candidate47-transitions.json).
See [experiment methods](kicad-experiment.md) and
[diode/CoolMOS investigation](diode-coolmos-investigation.md).

## ngspice47-only cleanup

CMake requires version47 for comparison builds. Corpus preflight checks the
actual version even when analytical probes succeed. The paired benchmark CLI
accepts only47. CI uses the checksum-pinned release for the library, executable
and migration source; no second source checkout or older-version run remains.
The macOS convenience script builds47 without a version selector. Its execution
and remote Actions remain unverified.

Earlier documentation and the previous paper PDF are preserved in the
[pre-cleanup archive](evidence/joss/2026-09-11-pre-ngspice47-only-docs.tar.gz).
The original audit and version-specific diagnostics are retired evidence, not
current setup or compatibility instructions. No numerical assertions or
comparison thresholds were weakened by this cleanup.

Verification of this cleanup:1,234/1,235 C++ tests against47 (only RFF fails),
393 Python/tooling tests with47 source, passing real corpus/benchmark smoke
runs, and rejection of other reference versions. The three-page paper PDF was
rebuilt and all pages inspected. No full-corpus simulation or performance timing
was repeated: numerical behavior, fixture selection and accuracy thresholds did
not change. [Checkpoint30](evidence/joss/2026-09-11-validation-30.json) records
these checks and the cleanup's source/documentation inventories.

## Required human and release information

Subhagato Dutta, Carnegie Mellon University, ORCID0009-0007-7724-8863, and use
of Codex and Claude Code are author-supplied facts. Actual research workflows,
contributions, AI assistance details, funding/conflicts, human review and an
independent installation check remain pending. See
[author information](joss-author-information.md) and
[JOSS requirements](joss-requirements-checklist.md).

Public repository creation and package-upload metadata do not establish the
actual first-public date or the required development history. The
[public-history audit](joss-public-history.md) records the available bounds.
No DOI, release, submission, external correspondence or human approval is implied.
