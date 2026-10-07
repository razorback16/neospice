# Roadmap

Updated October 6, 2026. This roadmap separates delivered APIs from planned
extensions and research proposals. The [capabilities guide](capabilities.md)
describes supported behavior; the [support matrix](support-matrix.md) records
reference coverage. Release details are in [0.2.0 notes](release-0.2.md).

## Direction: one unified simulator

The long-term goal is one extensible engine for analog, digital, and mixed-signal
simulation. Easy C++ and Python APIs and parallel studies are central to that
goal. New simulation methods should share circuit data, device interfaces, and
results where their semantics permit. The current SPICE engine is the foundation.
PWL, digital events, and mixed-signal coordination are the next simulation domains
in the design proposals below.

## Delivered

| Area | Current implementation | Guide |
|---|---|---|
| Native simulation | C++20 library and CLI; DC, transient, AC, noise, TF, sensitivity, pole-zero, Fourier, and measurements | [Capabilities](capabilities.md) |
| Python | nanobind bindings, NumPy results, convenience functions, typed circuit construction; release wheel targets for CPython 3.10–3.14 on Linux x86_64/ARM64 and macOS 14+ ARM64 | [Build and install](building.md) |
| Circuit API | Node/device handles, typed builders, introspection, file-based library inclusion and subcircuit instantiation | [Programmatic circuits](programmatic-hierarchy-api.md) |
| Parallel studies | Bounded worker pool, parameter/device/temperature overrides, seeded Gaussian/uniform Monte Carlo, correlation, and scalar statistics | [Parallel studies](parallel-studies.md) |
| Gradients | DC adjoint Jacobians for R/C/L and independent-source values; linear AC complex derivatives | [Adjoint gradients](adjoint-gradients.md) |
| Incremental simulation | Checked component-value updates, per-circuit symbolic caches, DC/AC re-solves, and reuse counters | [Incremental simulation](incremental-simulation.md) |
| Browser engine | Emscripten build, JavaScript API, TypeScript declarations, and Worker execution | [WebAssembly](webassembly.md) |
| Circuit Lab | Schematic and SPICE editors, nine examples, DC/AC/transient plots, probes, saved projects, and CSV export | [Circuit Lab](circuit-lab.md) |

## Near-term priorities

### Numerical reliability and model coverage

- Extend the device/analysis/parameter coverage against ngspice 47, retaining
  original failing fixtures and comparison tolerances.
- Resolve the remaining model and corpus findings, then repeat the frozen
  corpus with the final candidate and report every outcome.
- Extend regression coverage for temperature, geometry, model options, noise,
  and transient behavior. Keep unsupported requests explicit.
- Prepare stable 1.0 APIs and reproducible release validation.

The [compatibility triage](corpus-mismatch-triage.md) and
[JOSS progress](joss-progress.md) retain the detailed findings. Publication
readiness additionally requires research-use evidence, attribution review,
final measurements, and author review; it is separate from shipping 0.2.0.

### Measured performance

- Measure the final candidate across the complete 34-workload population on
  an idle machine, keeping raw samples and numerical checks.
- Profile remaining expensive workloads and use controlled ablations to
  attribute improvements to individual changes.
- Measure parallel-study throughput and incremental-solve latency on declared
  circuit populations before publishing speedup claims.

See [performance comparison](performance-analysis.md) and
[benchmark methods](benchmark-methods.md).

### Application and automation workflows

- Grow Circuit Lab's example library, schematic editing, and model-library
  workflows; expand browser and platform acceptance testing.
- Add streaming transient results, callbacks, and explicit stop/resume semantics.
- Extend circuit hierarchy construction with reusable in-memory subcircuit
  definitions and typed ports.
- Extend derivatives to nonlinear AC bias dependence, coupled inductors, and
  semiconductor model parameters with independent verification.
- Investigate partial restamping and transient state reuse for incremental
  simulation, with explicit invalidation and recovery contracts.

## Longer-term engineering and research

| Direction | Intended outcome | Status / design |
|---|---|---|
| More devices and analyses | Additional compact models such as BSIM-CMG and distortion analysis | Planned; [device integration](device-migration-status.md) |
| Piecewise-linear simulation | Switching-power transient engine, periodic operating point, and time-domain AC | Proposed; [PWL design](pwl-simulation-design.md) |
| Digital event simulation | Logic primitives, delayed events, and digital waveforms | Proposed; [mixed-signal design](mixed-signal-architecture.md) |
| Mixed-signal coordination | Analog/digital boundary devices and synchronized simulation domains | Proposed; [unified architecture](unified-simulation-architecture.md) |
| Verilog-A | User-defined device models integrated with the device evaluation API | Proposed in the unified architecture |
| GPU acceleration | Device evaluation and sparse/batched solves for suitable large workloads | Research; crossover points must be measured |
| Learned convergence hints | Optional operating-point predictions with normal convergence checks and fallbacks | Research; [initial-guess proposal](ml-initial-guess.md) |

These proposals are design inputs, not shipped features or release commitments.
Priorities follow reproducible use cases and measured results. The existing
node-classification heuristic is implemented; the learned predictor is not.

## Contributing

See [CONTRIBUTING.md](../CONTRIBUTING.md). Changes should include a concrete
circuit or workflow, the applicable regression checks, and documentation of
public behavior. Keep the support matrix generated from tests and performance
claims linked to their experiment records.
