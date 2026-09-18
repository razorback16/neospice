---
title: 'neospice: An embeddable C++20 circuit simulator with Python bindings and differential SPICE validation'
tags:
  - C++
  - Python
  - circuit simulation
  - scientific software
authors:
  - name: Subhagato Dutta
    orcid: 0009-0007-7724-8863
    affiliation: 1
affiliations:
  - name: Carnegie Mellon University
    index: 1
date: 10 September 2026
bibliography: paper.bib
---

<!-- WORKING DRAFT: unresolved engineering and author facts; not submission-ready.
See README.md for the evidence ledger and required replacements before submission.
Do not remove this notice until the readiness checklist is satisfied. -->

# Summary

Electronic circuit simulation predicts how interconnected components respond
to electrical inputs. Researchers use these predictions to examine operating
conditions, frequency response, switching behavior and noise before building
or measuring a circuit. Integrating a simulator into a larger research program
also requires access to circuit construction, simulation settings and results.

neospice is an open-source circuit simulator implemented as a C++20 library,
with a command-line interface and Python bindings. It accepts SPICE netlists
and provides programmatic circuit construction and structured analysis results.
The project combines established circuit-simulation methods and adapted device
models with interfaces intended for embedding and experimentation. Its
contribution is the software implementation, extension architecture and
reproducible comparison infrastructure; it does not claim to introduce the
underlying SPICE algorithms or compact-device models.

# Statement of need

The intended audience is researchers who need to work with the simulation
engine itself while developing automated circuit-characterization programs or
experimental device and analysis implementations. Such work can require
coordinating circuit state, changing components and inspecting numerical results
inside a host application. neospice exposes these operations through C++ objects
and Python bindings, with the engine, parser and device adapters maintained in
the same repository.

This scope carries a substantial correctness obligation. An API that is easy
to embed is useful only when the selected models and analyses behave as
documented. neospice therefore treats agreement with a declared ngspice version
as an empirical requirement. Unsupported analyses must report an error, and
unsuccessful simulations must remain visible to the calling program. A returned
array or successful process exit alone is insufficient evidence that a requested
simulation completed correctly.

**Pending author evidence:** the final paper must identify an actual research
workflow, its scientific question and its use of these interfaces. The current
examples and developer validation experiments are demonstrations; they do not
establish external adoption or prior research use.

# State of the field

ngspice is an established open-source SPICE simulator with extensive device and
analysis support [@ngspice]. It also provides a shared-library interface, so
embedding is not unique to neospice [@ngspice_shared]. PySpice provides Python interfaces to
ngspice and Xyce and integrates simulation with Python data analysis [@pyspice].
Xyce is an independent, modular C++ circuit simulator developed at Sandia
National Laboratories, including large and parallel simulations [@xyce].
Sandia also documents a C interface and Python wrappers [@xyce_interface]. These projects provide existing routes for researchers who
primarily need to run circuits or automate mature simulators.

The proposed justification for a separate implementation is direct access to
a C++ circuit and device architecture whose internal state and extension points
can be changed together with the host research program. That choice exchanges
the maturity of an existing engine for control over its implementation and
interfaces. It does not establish that wrapping or contributing to ngspice is
inadequate for ordinary simulation tasks. The final research workflow must
demonstrate why this trade-off was necessary in the author's application.

# Software design

The parser and programmatic interface construct circuits used by shared analysis
drivers. Device adapters expose operations for setup, equation evaluation,
small-signal stamping and supported noise contributions. Common state and
integration interfaces allow the drivers to coordinate device history during
transient simulation. Descriptor-based migration tools assist with adapting
ngspice-derived model code; translation and code generation do not independently
validate a model's behavior.

The sparse solver layer separates equation construction from numerical solution.
The current automatic real-solver policy uses a separate minimum-degree-ordered
LU implementation for sufficiently large linear circuits and retains the
Sparse-derived path for other cases. Complex operations use the latter path.
Different elimination orders can produce different floating-point results, even
when a mathematical solution is unique. Solver selection is consequently a
compatibility decision as well as a performance decision. No new ordering
algorithm or general speed advantage is claimed here.

AC analysis caches frequency-independent conductance and capacitance stamps,
then assembles the frequency-dependent system and applies device-specific
corrections at each frequency. Noise analysis uses separate gain and adjoint
systems. These are implementation choices with costs as well as benefits:
ngspice can reuse existing factors for a transpose solve, whereas neospice's
current noise path factors separate systems. Paired, validated measurements are
needed before attributing a performance benefit to either arrangement.

The scope of the correctness claim is declared per device and analysis rather
than per device. A generated support matrix records, for every pair, whether a
test compares it against ngspice 47 and asserts the result, merely runs it,
asserts that it fails, or never exercises it; the claim covers only the first
category, at each cell's stated tolerance. Four analysis types -- transfer
function, sensitivity, pole-zero and Fourier -- fall outside it entirely,
because the differential harness cannot obtain a reference result for them, and
they are checked against analytic expectations alone.

Validation combines analytic checks, API and tooling tests, and differential
comparisons against the checksum-pinned ngspice 47 release. Comparisons reject empty,
nonfinite, unsuccessful and structurally invalid results, require the intended
signals and retain error diagnostics. Adaptive transient outputs are compared
on both time grids with a documented interpolation rule. Primary vendor-model
fixtures and separately generated isolated/driven variants retain independent
outcomes, including failures. Fixture construction is frozen before execution,
and the experiment records source, binary, dependency and runtime hashes.

# Research impact statement

The present evidence demonstrates a substantial implementation and a reusable
validation process, but does not yet establish a submission-ready research
result. The frozen operating-point experiment contains 34,908 primary cases
and 32,451 separately planned variants. These are generated cases rather than
independently certified model definitions: duplicate names, lexical scope and
isolation dependencies still require interpretation. Minimal operating-point
fixtures also cannot establish transient, AC or noise correctness.

The working candidate targets ngspice 47 exclusively, including its MES
multiplier, AM-source and frequency-grid semantics. An operating-point
regression failure and corpus mismatches remain. VDMOS AC and noise are
explicitly unsupported. These boundaries must be resolved or
justified within a declared support scope before final claims are made.

**Pending final evidence:** replace this draft status account with the actual
research workflow, accuracy-qualified benchmark results, final compatibility
accounting and independently checked installation instructions. Preserve any
remaining limitations. No publication, outside user, performance improvement
or completed human review is asserted by this draft.

# AI usage disclosure

The author reports using Codex and Claude Code during development. During the
documented JOSS preparation, Codex inspected implementation and reference source,
edited numerical and validation code, added tests, ran comparisons and prepared
documentation and this manuscript draft. Automated verification includes
regression tests, reference comparisons and sanitizer probes, with failures
retained in the repository evidence. These checks do not substitute for human
review or establish correctness of all generated content.

**Pending author confirmation:** record the tools, models and versions used
throughout development, the historical scope of assistance, the human author's
core design decisions and review responsibilities, and approval of the final
software and manuscript. Claude Code's specific contributions have not yet
been supplied.

# Acknowledgements

The implementation builds on work by the SPICE3, ngspice, Sparse and SuiteSparse
communities and the original device-model authors. Distribution notices and
model-specific attribution are maintained in the repository.

**Pending author confirmation:** funding, sponsor involvement, conflicts of
interest, additional authors and individual contributions. An absence of these
has not been assumed.

# References
