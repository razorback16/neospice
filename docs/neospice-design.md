# neospice architecture

Updated October 6, 2026. This page describes the implemented engine. Future
simulation domains are tracked in the [roadmap](ROADMAP.md) and their design
proposals.

The long-term goal is a unified simulator for analog, digital, and mixed-signal
circuits. Its API should make circuit construction and parallel studies easy
to use across simulation methods. The existing SPICE engine provides the
foundation. Proposed simulation domains share circuit and result infrastructure
while retaining the numerical methods each domain needs.

## Interfaces and execution

The [public header](../include/neospice/neospice.hpp) exposes `Simulator`,
`Circuit`, analysis options, typed handles, results, and measurement helpers.
The [API implementation](../src/api/neospice.cpp) connects those types to the
parser and analysis drivers. The CLI, nanobind Python module, and WebAssembly
wrapper use this engine.

A circuit can come from a file, inline netlist, or typed builder calls.
The parser resolves parameters, models, includes, and subcircuits into a flat
circuit. `Circuit::include()` and `Circuit::X()` reuse the library/subcircuit
path for programmatic construction. Finalization establishes nodes, internal
variables, state offsets, and the matrix pattern. Topology is immutable after
finalization; supported value updates invalidate the relevant preparation.

Results expose named and handle-based access. Native result storage includes
dense arrays; Python exposes waveform vectors as NumPy arrays. Numerical
failures throw or return failed status under the documented `no_throw` mode.
Unsupported operations and invalid API inputs remain explicit errors.

## Devices and model ownership

The [Device interface](../src/devices/device.hpp) defines setup, matrix loading,
AC hooks, state handling, convergence, and noise contributions. The registry
selects implementations; CMake discovers device subdirectories. The circuit owns
model cards and device instances, including the state used by translated models.

Many compact models adapt Berkeley/ngspice C routines through C++ compatibility
shims. Shared constants, initialization helpers, model-card parsing, and the
migration generator keep that integration consistent. See
[device integration](device-migration-status.md) and [credits](../CREDITS.md).

## Numerical core

Modified nodal analysis produces the circuit equations. DC uses Newton iteration,
device limiting, and convergence checks. Operating-point recovery includes
gmin stepping, source stepping, transient operating point, and additional
continuation attempts. The [ngspice 47 guide](ngspice47-reference.md) describes
verified reference behavior.

Transient analysis uses adaptive steps with trapezoidal, Gear-2, and backward
Euler integration, device truncation, source breakpoints, and state rollback.
The optional global node-voltage LTE proposal is separate from the default
device truncation path. See [transient behavior](capabilities.md#transient-behavior).

AC caches frequency-independent conductance/capacitance stamps and applies
frequency-dependent device corrections at each frequency. Noise factors the
complex admittance matrix once per frequency, solves the forward excitation,
and reuses the factors for a plain-transpose adjoint solve.

The default real solver is Sparse-derived `NeoSolver`. Linear circuits with at
least 256 unknowns select `AmdLuSolver`, with an initial-factorization fallback.
Its ordering uses an explicit elimination graph and an ordered degree set.
Complex operations use `NeoSolver`; BTF helpers are not part of the production
solver pipeline. See [sparse solver details](sparse-lu/implementation-details.md).

## Repeated and parallel studies

[Parallel sweeps](parallel-studies.md) give each job its own parsed circuit,
models, state, and results in a bounded worker pool. Python releases the GIL
while native jobs run. Seeded Monte Carlo produces variations before execution
and preserves sample order.

[Adjoint gradients](adjoint-gradients.md) reuse a linearized system and solve
its transpose per requested output. Parameter derivatives come from supported
analytical stamps. The finite-difference `.sens` analysis remains a separate API.

[Incremental DC/AC](incremental-simulation.md) retain symbolic structure and
workspaces per circuit. Checked value updates invalidate bias and temperature
preparation, and failed solves discard affected cached factors. Ordinary
`run_dc()` and `run_ac()` keep their fresh-solver behavior.

## Browser application

The [WebAssembly target](webassembly.md) builds the engine with Emscripten and
exposes JavaScript/TypeScript APIs. [Circuit Lab](circuit-lab.md) is a React
application with schematic/netlist editing, generated decks, Worker execution,
and interactive plots. Circuit data and computation stay local to the browser.

## Verification and extension

Analytical tests, ngspice 47 comparisons, Python tests, browser checks, and
workflow-specific regressions cover different parts of the stack. The
[support matrix](support-matrix.md) records device/analysis evidence;
[validation methods](validation-methods.md) defines comparison semantics.

Use the [paired benchmark protocol](benchmark-methods.md) for performance
claims. Source-level caching or solver choices alone do not establish a speedup.
PWL, digital/mixed-signal, Verilog-A, GPU, and learned initial guesses remain
[future directions](ROADMAP.md#longer-term-engineering-and-research).
