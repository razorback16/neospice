# Goal: Make neospice ready for a JOSS submission

**Activated September 10, 2026. Status and evidence: [JOSS progress](joss-progress.md).**

**Objective**

Bring neospice to a demonstrably JOSS-ready state by resolving the publication blockers in [the September 2026 audit](paper-readiness-audit.md), correcting implementation and measurement defects, producing reproducible validation and performance evidence, and preparing a complete research-software manuscript and release package. Work through the milestones below until their acceptance criteria are met. Actual journal submission, external publication, and remote release actions remain for the user's final review.

Frame the paper around an embeddable C++20 circuit simulator, Python integration, reusable device architecture, and compatibility with ngspice. Establish the research need and benefits over existing approaches with evidence. Do not imply that established SPICE methods, differentiation, minimum-degree ordering, or translated models are new inventions.

**Execution constraints**

- Follow repository `AGENTS.md`; work directly on `main` and preserve unrelated user changes. Re-read relevant instructions and inspect the current revision before implementing anything.
- ngspice is the reference. Never loosen tolerances, adjust reference options to conceal discrepancies, remove assertions, turn failures into skips, or silently drop difficult fixtures to improve results. Fix neospice or the demonstrated measurement defect.
- Reference policy: ngspice 47 exclusively defines default behavior, required JOSS validation and migration-tool source inputs. Do not maintain another compatibility baseline or select a reference per fixture. Prior experiment records remain immutable provenance, not an active validation obligation.
- Preserve the original audit and historical evidence. Record corrected results as new, versioned experiments; historical percentages are not acceptance targets.
- Resolve routine engineering decisions autonomously. Request missing author/research information early in one consolidated request, continue independent work, and record external dependencies explicitly.
- Keep an evidence-linked progress tracker in the repository: milestone, implementation revision, verification command, result, remaining defect or dependency, and next action. Update it as work progresses so execution can resume without reconstructing history.
- Keep the scope to the JOSS paper. Do not add GPU, ML, Verilog-A, full digital/mixed-signal, PWL/POP, or a new solver merely to increase novelty. Correctness work required for the supported scope is included.
- Run meaningful regression tests for numerical changes and measurement defects. Use targeted checks during development and the full required suites at release checkpoints. Run complete corpus experiments after relevant changes; reuse results only when their dependencies and provenance remain valid.
- Prepare all reviewable artifacts locally. Do not push, publish a release or DOI, submit the paper, or contact other people as part of this goal. Identify those final user actions only after their inputs are ready.

**Milestone 1 — Establish scope, eligibility, and a reproducible baseline**

1. Read the audit and confirm its findings against the current checkout; distinguish already-fixed issues from remaining work. Recover useful `/tmp/neospice-paper-*` artifacts if available, but make no workflow depend on their survival.
2. Check current official JOSS submission, review, paper-format, authorship, and AI-disclosure requirements. Maintain a checklist with source links and verification dates.
3. Establish the actual public-development timeline using verifiable evidence. A first commit date alone does not prove public availability. The audited local history begins April 15, 2026; the previously checked policy requires more than six months of public development. Do not declare eligibility until the applicable requirement is satisfied.
4. Identify the human authors, affiliations/ORCIDs where applicable, contributions, funding/conflicts, actual AI assistance, and actual research use. Clearly distinguish author-provided facts from verified repository facts. Never invent adoption, authorship, impact, or human review.
5. Declare the supported device-by-analysis/option scope before final evaluation. Separate supported functionality, explicitly unsupported functionality, and research plans. Define the workload selection and accuracy metrics independently of which cases neospice passes.
6. Provide a documented, clean development environment with explicit Python tooling dependencies, including PyYAML and test/build dependencies. Eliminate reliance on the old `Codes/spice-cpp` editable-install path and undeclared local packages.

**Acceptance:** a current baseline, documented support scope, reproducible setup, and JOSS requirement checklist exist. External dependencies have owners or requested information and do not stop unrelated engineering work.

**Milestone 2 — Make validation trustworthy**

1. Audit C++ and Python comparison paths for DC, sweeps, transient, AC, noise, and specialized waveform metrics. Reject NaN/Inf, empty results, zero compared points, missing required signals, malformed vector lengths, invalid axes, incomplete time/frequency coverage, and unsuccessful simulation status. Respect legitimately different adaptive time grids while verifying the requested analysis completed.
2. Fix `compare_values()` accepting disjoint signal sets and infinite values. Define the required signal set explicitly; comparing only an arbitrary intersection must never yield an unexplained MATCH.
3. Fix C++ comparisons accepting NaN or empty results. Add regression tests demonstrating that the audit's false-pass probes now fail and valid comparisons still pass. Test the reporting/status paths as well as the arithmetic.
4. Audit all comparison call sites for ignored results and reversed reference/actual arguments. Assert the intended LTRA transient comparison and repair other assertion gaps. Do not substitute weaker checks for a failing comparison.
5. Document the actual error formulas, absolute floors, interpolation, edge handling, and thresholds. Do not silently replace the existing acceptance formula with a more permissive one. Demonstrate any correction to comparison mathematics on analytic/synthetic cases and disclose its effect on historical results.
6. Retain sufficient successful and unsuccessful comparison evidence: status, required/observed signals, points compared, maximum absolute and normalized errors, relevant edge metrics, completion status, and diagnostic data needed to reproduce a decision.
7. Fix the audited 15 migration-tool test failures. Update stale expectations and descriptor fixtures to the real interface while retaining meaningful behavior checks; verify that failures do not reveal broken generated code. Investigate and justify the absent `jfet.yaml` skip.
8. Add tooling tests and both Python test directories to regular CI, together with CTest. Required reference checks must not silently disappear when ngspice or its runtime models are missing. Document legitimate platform-specific skips.

**Acceptance:** meaningful tests cover invalid-data rejection; all intended comparison results are enforced; required C++, Python, and tooling suites pass from the documented environment with no unexplained skips or hidden failures.

**Milestone 3 — Resolve numerical defects and silent unsupported behavior**

1. Fix the LTRA current discrepancy exposed by enforcing the existing comparison. Compare the implementation with the local ngspice source, reduce the failing circuit where useful, and add a durable regression.
2. Reproduce and fix `uncategorized/spice_complete/harprmos.lib::RFF70N06_HA`, which ngspice solved and neospice failed in the audit. Ensure the fixture is exercised by an automated regression, not merely stored as an unused netlist.
3. Investigate the large VBIC-switching, diode-rectifier, and other transient discrepancies identified by diagnostics. Separate demonstrated interpolation/metric artifacts from simulator errors; fix implementation defects and report residual limitations accurately.
4. Triage all newly exposed failures and the full-corpus mismatches by root cause and model family. Fix reproduced defects within the declared supported scope. Reduce repeated failures to representative regression circuits and check fixes across the affected family.
5. Address empty/incomplete analysis implementations, including VDMOS AC/noise and MES noise. Either implement and validate the capability or make unsupported use fail explicitly and describe the boundary in the support matrix. Do not return apparently valid partial results.
6. Verify the actual sensitivity scope and other advertised analyses. Correct overclaims such as sensitivity to all parameters; do not introduce broad new analyses merely to preserve marketing text.
7. Exercise numerically changed paths under appropriate sanitizers and fresh Release builds. Resolve material memory/undefined-behavior findings affecting the supported paper scope.

**Acceptance:** known failures in the declared supported scope are resolved and covered by regressions. Every remaining out-of-scope case has an explicit, defensible classification and is retained in full-corpus accounting. Newly discovered supported failures are blockers, not reasons to change the evaluation population. There is no requirement to implement all ngspice functionality or achieve an artificial 100% pass rate over unsupported models.

**Milestone 4 — Produce an auditable compatibility experiment**

1. Pin the KiCad corpus and acquisition procedure. The audit used revision `a8688952bcaab19f567bc4db237b60bde03ef310`; preserve this reference unless there is a documented reason to establish an additional corpus version.
2. Use stable identities that distinguish file, model/subcircuit kind, scope, and repeated definitions where needed. Eliminate ambiguous `(file, name)` keys and test selection/transition behavior on duplicates.
3. Freeze generated fixtures and hashes before comparing implementations. Define primary and isolated/driven fixtures separately. A fixture's inclusion must not depend on neospice succeeding; preserve failed rescue attempts and avoid replacing the primary experiment with a successful rescue.
4. Verify reference installation and effective runtime configuration, including initialization and code-model availability, before large runs. Distinguish environment failures from parser limitations, numerical nonconvergence, timeouts, and value mismatches.
5. Save a manifest containing source and binary hashes, compiler/build flags, dependency and simulator versions, corpus revision, effective options, initialization/model configuration, commands, machine/OS details, thread counts, fixture hashes, and comparison definitions. Keep enough raw data to audit successful comparisons as well as failures.
6. Use only the checksum-pinned ngspice 47 release for compatibility evaluation and reference builds. Preserve earlier records as archived provenance; do not rerun or maintain older compatibility baselines.
7. Run the entire frozen corpus with repaired validation. Report every outcome category, total coverage, conditional agreement, nontrivial excitation, primary/rescue counts, family/category breakdowns, and uncertainty or limitations. Do not describe minimal operating-point fixtures as full transient/AC/noise model certification.
8. Add a representative multi-analysis validation set for supported claims, including nonlinear switching, transmission lines, noise, frequency-dependent behavior, and relevant parameter/temperature variation. Identify analytical checks and reference comparisons separately.
9. Group related models and designate held-out fixtures/vendors before tuning. Preserve and disclose their role. Correctness fixes discovered during evaluation must not be disguised as untouched hold-out results.
10. Make the experiment reproducible from a clean checkout using pinned downloads or a prepared archive, not ignored workstation directories. Respect upstream redistribution terms when preparing the corpus artifact. Correct misleading harness flag and diagnostic documentation.

**Acceptance:** a reviewer can obtain the declared inputs, run the experiment, and regenerate the tables with traceable outcome decisions. Corpus totals reconcile; all exclusions and configuration differences are visible; the claimed supported scope has no unresolved known correctness defects.

**Milestone 5 — Produce fair, correctness-validated benchmarks**

1. Repair the in-process harness to check reference command status, retain relevant errors, and validate results before accepting performance samples.
2. Define separate parse/setup, solve/analysis, and end-to-end measurements. Place file reads, initialization, output materialization, and destruction/cleanup consistently in or outside the paired timers; document unavoidable interface differences.
3. Use matching warmup/sample policies, alternate or randomize simulator execution order, control thread counts, and avoid measuring during competing corpus/build jobs. Record raw samples and report dispersion or confidence intervals alongside central estimates.
4. Benchmark identical, correct workloads. Separate failures and mismatches from accuracy-qualified speed results. Retain ngspice wins and performance tails.
5. Cover small circuits, realistic nonlinear macromodels, and larger linear/nonlinear circuits across the analyses claimed in the paper. Add paired reference measurements to the relevant larger-circuit tests; do not extrapolate general solver scaling from diagonally dominant banded matrices alone.
6. Identify ngspice version and solver configuration. Default Sparse and KLU may be separate, labeled performance baselines with their own correctness checks; neither may replace a failing reference run to conceal an accuracy issue.
7. Use ablations only for causal claims retained in the paper, such as caching or solver selection. If a mechanism cannot be isolated and measured credibly, remove the causal claim rather than imply proof from aggregate runtime.
8. Generate all benchmark tables and figures from the saved samples with checked-in scripts. State the applicable population and limitations for any aggregate or maximum speedup.

**Acceptance:** paired timings measure defined, comparable work; all accepted samples correspond to valid outputs; published claims and figures regenerate from versioned evidence. No predetermined speedup is required.

**Milestone 6 — Align documentation and scholarly positioning with the code**

1. Update README, capabilities, roadmap, device migration, discrepancy, hierarchy, architecture, and performance documentation to the final implementation and experiments. Reconcile test/device counts and actual `Circuit::include()`/`X()` support.
2. Correct descriptions of ngspice's device convergence, transpose solve for noise, and expression derivatives using primary sources. Accurately describe the in-tree ordering algorithm, active solver-selection policy, and whether BTF is actually used. Do not claim bit-identical floating-point results merely because a linear system has a unique solution.
3. Separate implemented features, measured improvements, inherited algorithms/models, and future proposals. Preserve SPICE3/ngspice/Sparse/SuiteSparse and model-author attribution. Review distribution notices against the actual included code and dependencies.
4. Position neospice against ngspice, PySpice, Xyce, and other directly relevant alternatives using current primary sources. Explain the concrete research need, architectural trade-offs, and why a separate engine is justified over contributing to or wrapping an existing tool.
5. Provide installation, core API, extension, validation, issue-reporting, support, and contribution guidance sufficient for a new researcher to use and test the package.

**Acceptance:** material claims in the manuscript and public documentation are supported by code, measurements, or cited primary sources; unsupported novelty and capability claims have been removed.

**Milestone 7 — Prepare the JOSS manuscript and release package**

1. Establish and document at least one actual research workflow using neospice. A reproducible demonstration can support the paper, but must not be represented as external adoption or prior research use without evidence. Obtain missing research-use and author facts from the user.
2. Create `paper/paper.md`, its bibliography, figures, and supporting scripts, following the then-current JOSS template and length/section requirements. Cover summary, statement of need, state of the field/build-versus-contribute rationale, software design, research impact, limitations, acknowledgments, and required disclosures as applicable.
3. Include accurate author/affiliation/contribution information and an AI usage disclosure based on actual use. Obtain the human authors' confirmation of review and core design decisions; do not generate that confirmation on their behalf.
4. Prepare citation metadata, release notes, an appropriate version proposal, software/data availability statements, and an archival-release manifest. Prepare the archive locally with checksums and reproduction instructions. Do not invent a DOI or publish externally. Follow JOSS's actual review/release sequence when specifying later archival steps.
5. Compile the paper through the documented JOSS-compatible toolchain, resolve citation/format/render problems, and inspect the resulting PDF. Check that every quantitative statement traces to final experiment outputs.
6. Verify clean installation and required tests against the candidate release, exercise the supported packaging platforms available in the environment, and accurately state any platform checks that require external runners. Have a colleague/new user try installation and the workflow where required; request this human check without claiming it has occurred.
7. Prepare a final submission checklist identifying user-owned final actions: author approval, submission, any needed remote release/archival publication, and human correspondence with editors/reviewers. Recheck eligibility and current policy at handoff.

**Acceptance:** a compiled, reviewed submission package and reproducible release candidate are ready locally; required human facts and confirmations are present; the software meets the applicable JOSS eligibility requirements. External submission/publication itself is not part of this goal.

**Definition of done and external blockers**

Mark this goal complete only when all milestone acceptance criteria are satisfied, the final evidence matches the candidate implementation, and the repository can be handed to the user as ready for a JOSS submission subject to their final approval to submit.

If technical work and the draft package are complete but public-history eligibility, demonstrated research use, human author confirmation, required independent installation checks, or necessary external verification remain unavailable, report those as explicit blockers and handle the goal status under the active execution rules. Do not label the project JOSS-ready, fabricate evidence, or substitute a partial artifact for goal completion. Complete all independent authorized work before stopping on such a dependency.

The final handoff must contain the candidate revision, changed files, complete test results and skips, compatibility and benchmark summaries with artifact locations, compiled paper, checklist of JOSS requirements, unresolved limitations, and the exact remaining user actions. The progress tracker must distinguish technical completion from eligibility and submission readiness.

**Starting references**

- [Repository publication-readiness audit](paper-readiness-audit.md).
- [JOSS submission requirements](https://joss.readthedocs.io/en/latest/submitting.html).
- [JOSS review criteria](https://joss.readthedocs.io/en/latest/review_criteria.html).
- Local reference implementations: `~/Codes/ngspice` and `~/Codes/SuiteSparse`.
- These links and the audit are starting evidence; verify current requirements and source state during execution.
