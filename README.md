# neospice

[![CI](https://github.com/razorback16/neospice/actions/workflows/ci.yml/badge.svg)](https://github.com/razorback16/neospice/actions/workflows/ci.yml)
[![PyPI](https://img.shields.io/pypi/v/neospice.svg)](https://pypi.org/project/neospice/)

**A programmable SPICE engine for C++, Python, and your browser.**

[Open Circuit Lab](https://razorback16.github.io/neospice/) · [Documentation](docs/README.md) · [Roadmap](docs/ROADMAP.md)

neospice is an open-source C++20 circuit simulator based on Berkeley SPICE3 and
ngspice. Build circuits through simple APIs, run parallel studies, and use the
results in your design tools.

[Try Circuit Lab](https://razorback16.github.io/neospice/) to draw and simulate
circuits locally in your browser without installation.

## Project goals

Make circuit simulation easy to embed, automate, and extend.
The long-term goal is **one unified simulator for analog, digital, and
mixed-signal circuits**, with shared APIs and circuit data.

The SPICE engine provides the foundation for additional simulation methods,
device models, and interactive design tools.

## Current support

- **Analyses:** DC operating point and sweeps, transient, AC, noise, transfer
  function, sensitivity, pole-zero, Fourier/THD, and measurements.
- **Devices:** passives, sources, switches, transmission lines, diodes, BJTs,
  JFETs, MES/HFET models, MOS1/2/3/9, BSIM, HiSIM, and VDMOS DC models.
- **Netlists:** parameters, expressions, functions, subcircuits, model libraries,
  waveforms, `.step`, `.measure`, and SPICE raw output.
- **Automation:** parallel sweeps, seeded Monte Carlo, DC adjoint gradients,
  linear AC gradients, and incremental DC/AC simulation.
- **Circuit Lab:** schematic editing, nine example circuits, waveform plots,
  voltage/current probes, parameter controls, saved projects, and CSV export.

See the [capabilities guide](docs/capabilities.md) for model and API details.

## Why choose neospice over ngspice?

neospice combines typed APIs, parallel studies, and browser simulation in one project.

| Area | neospice | ngspice |
|---|---|---|
| C++ and Python | Typed circuit objects, NumPy results, and gradient APIs | [Shared-library API](https://ngspice.sourceforge.io/shared.html) with callbacks and wrappers such as PySpice |
| Parallel studies | Built-in sweeps and seeded Monte Carlo through one API | [Multiple library instances](https://ngspice.sourceforge.io/parallel.html) managed by a host application |
| Interactive design | Circuit Lab runs locally in your browser | [CLI and control scripts](https://ngspice.sourceforge.io/ngspice-control-language-tutorial.html), plus external GUIs such as KiCad |
| Device extensions | Modular C++20 device interface | [Compiled Verilog-A models through OSDI](https://ngspice.sourceforge.io/osdi.html) |
| [Recorded benchmark](docs/performance-analysis.md) | **1.89× geometric-mean speedup** across 34 workloads | ngspice 47 reference timing |

## Performance

The October 3, 2026 development benchmark measured a **1.89× geometric-mean speedup**
over ngspice 47 across 34 workloads. neospice had the lower median time in
**30 of 34 cases**.

See [benchmark results and methods](docs/performance-analysis.md)
for the recorded code, timings, and test conditions.

## Verification

Our verification harness compares neospice with **ngspice 47** at several levels:

- **Devices and circuits:** DC, sweeps, AC, transient waveforms, and noise.
- **Model libraries:** 34,908 generated KiCad cases, with results and diagnostics for each case.
- **Browser:** Circuit Lab examples at their default settings and current probes.
- **Performance:** numerical checks for every measured pair.

Analytical tests provide independent checks. Regression tests preserve reproduced
failures and check fixes. See the [harness guide](docs/validation-methods.md#verification-harness)
and [support matrix](docs/support-matrix.md).

In the [recorded sanitizer run](docs/validation-methods.md#memory-checks),
all reported leaks came from ngspice library paths. The report attributed none to neospice.

## Use from Python

```sh
python -m pip install neospice
```

```python
import neospice as ns

circuit = ns.Circuit()
circuit.V("V1", "in", "0", 10.0)
circuit.R("R1", "in", "out", 1000.0)
circuit.R("R2", "out", "0", 1000.0)

result = ns.Simulator().run_dc(circuit)
print(result.voltage("out"))  # 5.0
```

The APIs also accept SPICE netlists. See the [API guide](docs/programmatic-hierarchy-api.md)
or the [C++ and CLI build guide](docs/building.md).

## Roadmap

Near-term work expands model coverage, improves speed, and develops the APIs
and Circuit Lab. The path toward a unified simulator adds piecewise-linear
simulation, digital events, mixed-signal coordination, and Verilog-A models.
Further plans include streaming results, GPU support, and learned convergence
hints. See the [roadmap](docs/ROADMAP.md).

## Documentation and contributing

Read the [documentation](docs/README.md) and [contribution guide](CONTRIBUTING.md).
Contributions to models, APIs, tests, examples, and documentation are welcome.
Include a minimal netlist and reproduction steps with bug reports.

## License and credits

Original contributions use the [MIT license](LICENSE). Derived code retains
its upstream notices and terms. Credits include Berkeley SPICE3, ngspice,
Sparse 1.3, SuiteSparse, OpenBLAS, and SLEEF.
See [CREDITS.md](CREDITS.md) and [NOTICE](NOTICE).
