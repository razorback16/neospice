# JOSS readiness status

**Not ready for submission.** This page tracks open publication work.
The [roadmap](ROADMAP.md) tracks product development. The [JOSS goal](joss-readiness-goal.md)
and [requirements checklist](joss-requirements-checklist.md) define acceptance.

## Current scope

ngspice 47 is the reference for numerical comparisons, corpus evaluation,
benchmarks, and migration source. The generated [support matrix](support-matrix.md)
records per-device analysis coverage. The [capabilities guide](capabilities.md)
distinguishes implemented features from verified combinations.

Parallel studies, adjoint gradients, incremental DC/AC, WebAssembly, and Circuit
Lab are implemented. Their [API guides](README.md#simulation-workflows) define
supported behavior. Final publication evidence must cover the selected release.

## Remaining work

| Area | Required work |
|---|---|
| Numerical coverage | Extend model-option and transient coverage. Resolve or explicitly scope the findings below. |
| Corpus | Repeat the frozen experiment for the final candidate. Audit binding, grouping, and held-out multi-analysis evaluation. |
| Performance | Run the final candidate with the [paired protocol](benchmark-methods.md). Keep raw samples, qualification, and environment records. |
| Attribution | Complete the [source and distribution review](joss-attribution-audit.md), including vendor corpus redistribution. |
| Research use | Document an actual research workflow and its scientific result. Examples alone do not establish adoption. |
| Eligibility | Confirm the actual first-public date and required development history using the [history audit](joss-public-history.md). |
| Release | Obtain author confirmations, an independent installation check, final evidence reconciliation, and an updated paper rendering. |

## Open numerical findings

- **Corpus mismatches:** the [triage](corpus-mismatch-triage.md) groups the
  recorded failures by cause. Re-evaluate them against the final candidate.
- **RFF70N06:** both engines fail the fixture. The [regression contract](rff70n06-investigation.md)
  classifies it as reference-inconclusive. The underlying convergence diagnosis
  remains open.
- **CoolMOS and diode options:** investigate source-step state transfer,
  stationary fallback solutions, exact `M=1`, and registry geometry. See
  [open diode questions](diode-coolmos-investigation.md).
- **TLV3201:** its edge/DC-port contract passes, while a separate strict
  pointwise check retains a discrepancy. See [validation scope](tlv3201-validation.md).
- **LTRA transient currents:** the DC port-current comparison does not establish
  transient port-current agreement. Review result exposure and the existing
  `TransientRC` tolerance without weakening it.
- **VBIC and VDMOS:** complete the option coverage documented in
  [VBIC](vbic-compatibility.md) and [VDMOS](vdmos-compatibility.md).
- **Parameter diagnostics:** investigate the `dig000` path that reports
  `failed to evaluate .param 'dpwr' -- defaulting to 0`.
- **Sanitizers:** the recorded GCC 14.2 `-O2` run reported a null-store warning
  at thread-local guard resets. `-O0` did not reproduce it. Neither a changed
  TLS model nor UBSan without ASan removed it. A compiler artifact is a
  hypothesis, not an established explanation. Reference-library leaks also
  need separate accounting in the final sanitizer run.

## Evidence records

These dated records establish their own checkpoint state, not the state of
all later revisions:

- [Checkpoint 39 triage and sanitizer record](evidence/joss/2026-09-17-checkpoint39-milestone3.json)
- [Checkpoint 38 validation](evidence/joss/2026-09-17-checkpoint38-milestone2.json)
- [Frozen experiment](kicad-experiment.md): 67,359 runs over 34,908 declaration
  cases, including 32,451 separately driven variants.
- [Development performance results](performance-analysis.md)

## Required author information

Subhagato Dutta, Carnegie Mellon University, ORCID 0009-0007-7724-8863, and use
of Codex and Claude Code are author-supplied facts. Research use, contributions,
AI assistance details, funding/conflicts, human review, and independent
installation evidence remain pending. See [author information](joss-author-information.md).
