# Goal: Make neospice ready for a JOSS submission

**Activated September 10, 2026.** See [JOSS progress](joss-progress.md) for current
status, open findings, and evidence.

Prepare a reproducible release candidate and a complete research-software paper
for local review. Demonstrate the research need, supported behavior, and measured
benefits of the C++20 engine and its Python integration. Preserve attribution
for inherited SPICE methods, algorithms, and device models.

## Execution rules

- Follow `AGENTS.md`. Work on `main` and preserve unrelated changes.
- Use the checksum-pinned **ngspice 47** reference for validation and migration.
  Follow the [reference setup](ngspice47-reference.md). Check local source versions
  before use.
- Fix neospice or a demonstrated measurement defect. Do not weaken tolerances,
  change reference options to conceal differences, remove assertions, or skip
  difficult fixtures.
- Preserve raw evidence and provenance. Record new experiments separately.
  Historical percentages are not acceptance targets. Git history retains retired audits.
- Keep evidence linked to the tested revision, commands, results, and remaining
  work. Reuse results only while their dependencies and provenance remain valid.
- Resolve routine engineering choices independently. Request missing author
  information together, then continue independent work.
- Keep this goal within the paper's supported scope. Future GPU, ML, Verilog-A,
  digital, mixed-signal, PWL, and solver proposals do not expand that scope.
- Use focused regression tests during development. Run required suites and
  affected corpus experiments at release checkpoints.
- Prepare artifacts locally. Pushing, submission, publication, DOI creation,
  and correspondence remain separate user actions.

## 1. Establish scope and a reproducible baseline

Check the current checkout against the [readiness status](joss-progress.md).
Retain useful temporary artifacts in reproducible storage before relying on them.

Check official JOSS submission, review, format, authorship, and AI requirements.
Record source links and dates in the [requirements checklist](joss-requirements-checklist.md).
Establish actual public availability. A first commit date does not establish it.

Collect author identities, contributions, funding, conflicts, AI use, human
review, and actual research use. Separate author statements from repository evidence.
Never invent adoption, impact, or review.

Define supported devices, analyses, and options before final evaluation.
Select workloads and accuracy metrics independently of passing results.
Document a clean environment with explicit build, Python, and tooling dependencies.

**Acceptance:** the baseline, support scope, setup, and requirements checklist
are reproducible. Assign owners to external dependencies or record outstanding requests.

## 2. Make validation trustworthy

Audit C++ and Python comparisons and their call sites. Required checks cover:

- Successful status and completion of the requested analysis.
- Finite values, nonempty results, valid axes, and consistent vector lengths.
- Explicit required signals, sufficient coverage, and nonzero comparison counts.
- Reference/actual argument order and enforcement of comparison results.

Document formulas, floors, interpolation, specialized waveform metrics, and
thresholds in [validation methods](validation-methods.md). Demonstrate mathematical
corrections with analytical controls and disclose effects on historical results.
Retain successful and failed comparison records, including errors and diagnostics.

Check migration descriptors and generated code against the actual interface.
Run CTest and both Python test directories in CI. Required reference checks
must fail when the reference environment is missing. Explain legitimate platform skips.

**Acceptance:** regression tests reject invalid data and accept valid controls.
Required C++, Python, and tooling suites pass without unexplained skips or hidden failures.

## 3. Resolve numerical defects

Resolve the [open numerical findings](joss-progress.md#open-numerical-findings)
within the declared scope. Reduce failures to regression circuits and check
fixes across affected model families. Separate interpolation artifacts,
reference failures, unsupported operations, and neospice defects.

Incomplete analyses must produce results that pass numerical checks or reject
unsupported use explicitly. Check advertised sensitivity and analysis scope against the code.
Run affected paths under sanitizers and fresh Release builds.

**Acceptance:** supported defects have fixes and regression coverage.
Classify remaining cases explicitly and retain them in corpus totals.
New supported failures block completion. They cannot justify changing the evaluation population.

## 4. Produce a reproducible compatibility experiment

Use the [frozen KiCad experiment](kicad-experiment.md) as the starting record.
Retain corpus revision `a8688952bcaab19f567bc4db237b60bde03ef310` unless a documented
reason requires an additional version.

1. Give fixtures stable identities that distinguish files, declaration kinds,
   scopes, and repeated definitions.
2. Freeze fixtures and hashes before comparison. Keep primary and driven/rescue
   runs separate, including failed rescue attempts.
3. Check reference initialization, runtime options, and code-model availability.
   Separate environment failures, parser failures, nonconvergence, timeouts, and mismatches.
4. Record source/binary hashes, build flags, versions, commands, machine details,
   thread counts, options, fixture hashes, and comparison definitions.
5. Run the full frozen corpus. Report every outcome, family totals, primary/rescue
   counts, excitation, conditional agreement, and uncertainty.
6. Add representative multi-analysis cases for supported claims. Include switching,
   transmission lines, noise, frequency dependence, parameter variation, and temperature.
7. Designate related-model groups and held-out cases before tuning.
   Disclose fixes discovered during evaluation.
8. Provide pinned inputs or a prepared archive, subject to redistribution terms.
   Eliminate reliance on ignored workstation directories.

**Acceptance:** a reviewer can obtain inputs, repeat the experiment, and regenerate
tables. Totals reconcile. Exclusions and configuration differences are visible.
Minimal operating-point fixtures do not establish complete model compatibility.

## 5. Produce fair benchmarks

Follow the [benchmark methods](benchmark-methods.md). Check status and numerical
results before accepting paired samples. Define parse/setup, analysis, cleanup,
and end-to-end timing boundaries consistently for both engines.

Use matching warmups and samples. Alternate or randomize execution order.
Control threads and competing jobs. Save raw samples and report dispersion.
Separate failed or mismatched cases from timings that pass numerical checks.
Retain ngspice wins and performance tails.

Cover the analyses and circuit sizes claimed in the paper. Identify the ngspice
solver configuration. Sparse and KLU can have separate labeled measurements,
each with its own numerical checks. Do not extrapolate broad solver scaling
from banded synthetic matrices.

Use controlled comparisons for causal optimization claims. Generate tables and
figures from saved samples with repository scripts. State each aggregate's population.

**Acceptance:** timings measure comparable work and valid outputs. Claims and
figures regenerate from versioned evidence. Completion does not require a predetermined speedup.

## 6. Align documentation and attribution

Check README, capabilities, roadmap, device coverage, build instructions, and
the paper against the final candidate. Separate implemented features from proposals.
Preserve SPICE3, ngspice, Sparse, SuiteSparse, and model-author attribution.
Review distribution notices against included code and dependencies.

Use current primary sources to explain relevant alternatives and the research
reason for a separate engine. Provide installation, API, extension, testing,
issue-reporting, support, and contribution guidance.

**Acceptance:** material claims have code, measurement, or source evidence.
The paper distinguishes inherited methods from neospice contributions.

## 7. Prepare the paper and release package

Document an actual research workflow. Label demonstrations accurately.
Obtain author facts, disclosure details, and human review statements from the authors.

Prepare the manuscript, bibliography, figures, citation metadata, release notes,
version proposal, and software/data availability statements. Follow current JOSS
requirements and the [paper build guide](../paper/README.md).

Compile and inspect the paper. Trace quantitative claims to final experiment outputs.
Prepare a local archive with checksums and reproduction instructions.
Check clean installation, required tests, and available packaging platforms.
Obtain the required independent installation check and record unavailable platform checks.

**Acceptance:** the reviewed paper and release candidate are ready locally.
Required author information, human checks, and eligibility evidence are present.
Recheck current policy before handoff.

## Completion and handoff

Complete this goal only when all acceptance criteria hold for the final candidate.
Distinguish technical completion from publication eligibility and submission readiness.
Missing research use, public-history evidence, author confirmation, or independent
checks remain explicit blockers. Complete independent work while awaiting them.

The final handoff includes:

- Candidate revision, changed files, test results, and explained skips.
- Compatibility and performance summaries with artifact locations.
- Rendered paper, requirements checklist, and unresolved limitations.
- Exact user actions for approval, submission, remote release, and correspondence.

The [submission guidance](https://joss.readthedocs.io/en/latest/submitting.html)
and [review criteria](https://joss.readthedocs.io/en/latest/review_criteria.html)
are starting references. Check them again when executing publication work.
