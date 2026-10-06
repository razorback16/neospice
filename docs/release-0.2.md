# neospice 0.2.0

This beta release brings Circuit Lab and the expanded C++ and Python APIs
together. Version 1.0 is reserved for further stability work; 0.2.0 does not
promise compatibility for every SPICE model or analysis.

## Use it

- [Open Circuit Lab](https://razorback16.github.io/neospice/) to build and
  simulate circuits directly in your browser, without an installation.
- Install the Python package with `python -m pip install neospice==0.2.0`.
  The release targets Python 3.10–3.14, with wheels for Linux x86_64/aarch64 and
  macOS 14+ Apple Silicon. Other platforms require a supported source build;
  Windows wheels are not provided.

## Highlights

- A visual schematic editor with eight editable circuits, component placement
  and drag-and-drop, wiring, undo/redo, and local project saving.
- DC, AC and transient simulation in a WebAssembly worker, interactive
  waveforms, voltage/current probes, parameter sliders and CSV export.
- Parallel parameter studies, adjoint gradients and incremental solves in the
  native APIs. See [capabilities](capabilities.md) for supported scope.
- Solver and device improvements, including shared model temperature setup and
  factorization reuse for noise analysis.
- An explicit sensitivity-helper deduction guide for Apple Clang compatibility.
- Validation against checksum-pinned ngspice 47, with browser acceptance and
  numerical checks for the gallery and current probes.

## Stability and scope

The [support matrix](support-matrix.md) defines verified device/analysis
combinations. The [progress tracker](joss-progress.md),
[VBIC compatibility notes](vbic-compatibility.md) and
[transient readiness notes](transient-readiness.md) retain known limitations
and unresolved discrepancies. Passing tests do not certify untested models,
parameters or circuit topologies. The RFF70N06 corpus fixture remains
reference-inconclusive: neither engine provides a converged operating point.

The browser uses a single-threaded WASM module and supports inline models;
arbitrary SPICE-to-schematic import and external model includes are unavailable.
Projects live in browser storage; export a project to move it between origins.
See the [Circuit Lab guide](circuit-lab.md) for details.

## Publishing

The `v0.2.0` tag triggers `.github/workflows/wheels.yml`. Each platform builds
and tests its wheels before the publish job uploads the wheels and source
distribution using the existing PyPI trusted publisher. Circuit Lab is
published separately through the WebAssembly workflow with **deploy** enabled.
