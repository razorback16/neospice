# Circuit-simulator positioning and evidence

Primary-source review: September 11, 2026. This comparison supports the JOSS
working draft. Numerical and performance claims must come from the repository's
versioned experiments, not a general ranking of simulator brands.

## Relevant existing tools

| Project | Documented role and interfaces | Implication for neospice |
|---|---|---|
| ngspice | SPICE engine with a shared-library API, control functions and callbacks. A host can supply netlists, receive values during a run, stop, change parameters and resume. [Official shared-library description](https://ngspice.sourceforge.io/shared.html). | Embedding and host-driven simulation are existing capabilities. A Python or C++ interface alone does not justify a new engine. |
| PySpice | Python interfaces to ngspice and Xyce; circuit construction, simulation control and analysis with Python packages. Its version-1.5 overview describes ngspice shared/server interfaces and Xyce subprocess execution. [Official overview](https://pyspice.fabrice-salvaire.fr/releases/v1.5/overview.html). | Python automation, NumPy output and programmatic circuits are existing approaches. Versioned backend details must not be generalized to every release. |
| Xyce | Independent C++ simulator with modular design, a DAE formulation and MPI parallel execution. [Sandia overview](https://xyce.sandia.gov/about-xyce/). Sandia's 7.1 application note documents `XyceCInterface` and Python wrappers. [Application note, sections 2–3](https://xyce.sandia.gov/files/xyce/AppNote-MixedSignal.pdf). | A modular C++ engine and language interfaces are not unique to neospice. No comparative parallel-scaling claim is established here. |

Sandia's FAQ still contains a statement denying a Python library interface.
The specific interface documentation above establishes that such an interface
has been supplied; the broader FAQ statement should not support a novelty claim.
The application note is explicitly versioned, not a guarantee that its examples
work unchanged with every newer Xyce release.

## neospice's implementation trade-off

neospice maintains its C++20 circuit objects, analysis drivers, device adapters
and Python bindings together. This offers a local implementation surface for
researchers modifying engine behavior or device integration. The contribution
is that software and its measured usability/correctness in an actual workflow;
C++ object orientation, device abstraction, SPICE methods and Python access are
established techniques. The author still needs to document the research question
and why changing this engine served it better than using or contributing to an
existing project. [Working manuscript](../paper/paper.md).

A separate implementation carries maintenance and compatibility costs. API
availability does not establish reentrancy, thread safety, model coverage or
numerical agreement. [Capabilities](capabilities.md) and [current validation
status](joss-progress.md) define the available evidence and unresolved scope.

## Algorithm claims grounded in source

- ngspice has device convergence checks; these are not a neospice invention.
  Its post-solve convergence checks materially affect the
  reference interpretation. [Source investigation](rff70n06-investigation.md).
- Both engines reuse complex factors for the gain and plain-transpose adjoint
  noise solves. Both engines use a minimum squared gain for input noise referral.
  [Noise implementation](../src/core/noise.cpp) and
  [reference evidence](ngspice47-reference.md).
- neospice assembles AC from frequency-independent stamps plus per-frequency
  device contributions. This describes its code path, not all commercial tools'
  internals or a demonstrated causal optimization benefit. [AC source](../src/core/ac.cpp).
- Adaptive output, timestep error control and source breakpoints are separate
  behaviors. Requested integration methods and supported source semantics are
  documented with reference regressions. [Transient evidence](capabilities.md#transient-behavior)
  and [source compatibility](source-compatibility.md).

## Performance evidence

The canonical paired harness covers 34 declared workloads. Every accepted timing
requires matching analyses, valid outputs and the documented error thresholds.
Its phases include public loading, analysis/result materialization, circuit
cleanup and total elapsed time. These are not isolated sparse-solver timings.
[Method and reproduction](benchmark-methods.md).

No measured comparison with Xyce, PySpice, LTspice, HSPICE, Spectre or PSpice has
been performed in this preparation. The former unsupported numerical speed
rankings and commercial-tool implementation/default tables have been removed;
the historical checkpoint patches preserve them for audit. They must not be
used in the paper. Current evidence does not establish a general speedup,
convergence superiority or performance parity with a commercial simulator.
