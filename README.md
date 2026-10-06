# neospice

[![CI](https://github.com/razorback16/neospice/actions/workflows/ci.yml/badge.svg)](https://github.com/razorback16/neospice/actions/workflows/ci.yml)
[![PyPI](https://img.shields.io/pypi/v/neospice.svg)](https://pypi.org/project/neospice/)
[![Python versions](https://img.shields.io/pypi/pyversions/neospice.svg)](https://pypi.org/project/neospice/)

SPICE circuit simulation in your browser, Python, and C++.

**[Open Circuit Lab →](https://razorback16.github.io/neospice/)** · [Install from PyPI](https://pypi.org/project/neospice/) · [0.2.0 release notes](docs/release-0.2.md)

neospice is an independent C++20 reimplementation of SPICE, derived from
UC Berkeley SPICE3 and ngspice. It reads SPICE netlists and provides an
embeddable simulation engine with Python bindings and a WebAssembly build.
**0.2.0 is a beta release**; 1.0 is reserved for further stability work.

## Circuit Lab

Build and simulate circuits directly in your browser. No installation or
account is required; simulation runs locally using WebAssembly.

- Nine editable examples spanning filters, amplifiers, rectifiers and oscillators.
- A schematic editor with component placement, wiring, voltage/current probes,
  parameter controls, and undo/redo.
- DC, AC and transient results, interactive waveforms, light/dark themes,
  saved projects and CSV export.

See the [Circuit Lab guide](docs/circuit-lab.md) for editing, local hosting,
project files and GitHub Pages deployment. The [WebAssembly guide](docs/webassembly.md)
covers the JavaScript API and build instructions.

## Python

```sh
python -m pip install neospice
```

Wheels are available for **CPython 3.10–3.14** on Linux x86_64/ARM64 and
macOS 14+ Apple Silicon. Source builds need a C++20 compiler, OpenBLAS and
SLEEF; see the [build guide](docs/building.md).

Run an inline netlist:

```python
import neospice as ns

result = ns.dc("""Resistor divider
V1 in 0 DC 10
R1 in out 1k
R2 out 0 1k
.op
.end
""")

print(result.voltage("out"))  # 5.0
```

You can also build circuits with `ns.Circuit()` and run individual analyses
through `ns.Simulator()`. Waveform results are NumPy arrays. See the
[Python examples](docs/ROADMAP.md#phase-1-python-bindings--done) and
[example notebooks](examples/).

## C++ and command line

Install the prerequisites in the [build guide](docs/building.md), then build
the library and CLI without the reference-test dependencies:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DNEOSPICE_BUILD_TESTS=OFF
cmake --build build --parallel
./build/neospice tests/circuits/resistor_divider.cir -o result.raw
```

The C++ API exposes `Simulator`, `Circuit`, typed results, component builders,
node/device handles and measurement utilities. Start with the
[public API header](include/neospice/neospice.hpp) and
[programmatic circuit examples](docs/programmatic-hierarchy-api.md).

## Capabilities

- **Analyses:** DC operating point and sweeps, transient, AC and noise, plus
  transfer function, sensitivity, pole-zero, Fourier and measurement utilities.
- **Devices:** passives, independent/dependent/behavioral sources, switches,
  transmission lines, diodes, BJTs and several FET model families.
- **Parameter studies:** parallel sweeps, seeded Monte Carlo and per-job results.
- **Optimization workflows:** adjoint gradients and incremental re-simulation.
- **Netlists:** parameter expressions, subcircuits, source waveforms and
  ngspice-format raw output.

The [capabilities guide](docs/capabilities.md) describes implemented features;
the [support matrix](docs/support-matrix.md) identifies verified device/analysis
combinations. Browser support is a subset of the native engine.

## Validation and known limits

**ngspice 47 is the sole reference implementation.** Validation combines
analytical tests, numerical comparisons, Python API tests and browser tests.
Known model limitations and corpus mismatches remain; passing tests do not
certify every model, parameter or circuit topology. See the
[validation methods](docs/validation-methods.md) and
[current findings](docs/joss-progress.md).

Benchmarks require accuracy-qualified results from both engines. The
[benchmark methods](docs/benchmark-methods.md) document the protocol; historical
timings are not current performance claims. The [paper draft](paper/README.md)
and research validation remain work in progress, not submission-ready evidence.

## Documentation and contributing

| Topic | Guide |
|---|---|
| Build, test and contribute | [Development setup](docs/building.md) |
| Sweeps and Monte Carlo | [Parallel studies](docs/parallel-studies.md) |
| Gradients and repeated solves | [Adjoint gradients](docs/adjoint-gradients.md) · [Incremental simulation](docs/incremental-simulation.md) |
| Netlist compatibility | [ngspice 47 reference](docs/ngspice47-reference.md) · [Source compatibility](docs/source-compatibility.md) |
| Internals and future work | [Architecture](docs/neospice-design.md) · [Roadmap](docs/ROADMAP.md) |

Bug reports should include a minimal netlist, analysis settings, neospice version
and expected behavior. Contributions should preserve the ngspice 47 reference
checks and document changes to supported behavior.

## License and credits

Original contributions use the [MIT license](LICENSE). Derived SPICE code and
device models retain their upstream notices and terms. The simulator builds on
Berkeley SPICE3, ngspice and Sparse 1.3, with ordering references from SuiteSparse.
See [NOTICE](NOTICE), [credits and lineage](CREDITS.md), and the
[attribution audit](docs/joss-attribution-audit.md).
