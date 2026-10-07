# Documentation

Use neospice through [Circuit Lab](https://razorback16.github.io/neospice/),
Python, C++, the CLI, or JavaScript.

## Start here

| Task | Guide |
|---|---|
| Install, build, and test | [Building neospice](building.md) |
| Draw and simulate in the browser | [Circuit Lab](circuit-lab.md) |
| Build circuits in Python or C++ | [Programmatic circuits](programmatic-hierarchy-api.md) |
| Check analyses and models | [Capabilities](capabilities.md) · [Support matrix](support-matrix.md) |
| Read release changes | [0.2.0 release](release-0.2.md) |

## Simulation workflows

- [Parallel sweeps and Monte Carlo](parallel-studies.md)
- [Adjoint gradients](adjoint-gradients.md)
- [Incremental DC and AC simulation](incremental-simulation.md)
- [WebAssembly and JavaScript/TypeScript](webassembly.md)
- [Optimization example](../examples/optimization/adjoint_divider.py)

## Performance and compatibility

- [Performance compared with ngspice](performance-analysis.md)
- [Benchmark methods](benchmark-methods.md)
- [ngspice 47 setup](ngspice47-reference.md) and [validation methods](validation-methods.md)
- [Model cards](model-card-compatibility.md) and [sources and frequency grids](source-compatibility.md)
- [VBIC compatibility](vbic-compatibility.md) and [VDMOS compatibility](vdmos-compatibility.md)
- [Frozen KiCad experiment](kicad-experiment.md) and [corpus mismatch triage](corpus-mismatch-triage.md)
- Open findings: [diode/CoolMOS](diode-coolmos-investigation.md),
  [RFF70N06](rff70n06-investigation.md), and [TLV3201](tlv3201-validation.md)

## Development and plans

- [Contributing](../CONTRIBUTING.md), [roadmap](ROADMAP.md), and [architecture](neospice-design.md)
- [Device integration](device-migration-status.md) and [sparse solver](sparse-lu/implementation-details.md)
- Design proposals: [PWL](pwl-simulation-design.md), [mixed-signal](mixed-signal-architecture.md),
  [unified simulation](unified-simulation-architecture.md), and [learned initial guesses](ml-initial-guess.md)
- [PSpice language reference](pspice-model-language-specification.md)
- [Credits](../CREDITS.md), [NOTICE](../NOTICE), and [license](../LICENSE)

## Research and publication

The [paper guide](../paper/README.md), [JOSS goal](joss-readiness-goal.md),
[readiness status](joss-progress.md), and [requirements checklist](joss-requirements-checklist.md)
track publication work. Supporting records cover [attribution](joss-attribution-audit.md),
[author information](joss-author-information.md), [public history](joss-public-history.md),
and [simulator positioning](industry-comparison.md).

Machine-readable experiment records remain under `evidence/`. Results apply to
the recorded inputs and revisions. Git history retains completed investigations
and previous documentation.
