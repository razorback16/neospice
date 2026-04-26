# neospice

A modern C++20 SPICE circuit simulator. Drop-in replacement for ngspice with a clean embeddable API, 1.5--6x faster in-process performance, and auto-differentiation in behavioral sources.

## Features

- **8 analysis types** -- DC OP, DC sweep, transient (adaptive Trap/Gear-2/BE), AC small-signal, noise (adjoint method), transfer function, sensitivity, pole-zero, Fourier/THD, parameter sweep (`.step`), and `.measure` post-processing
- **28 device models** -- passives, independent/dependent/behavioral sources, switches, transmission lines, diodes, BJTs, JFETs, HFETs, and MOSFETs through BSIM4v7
- **Embeddable C++ API** -- `Simulator`/`Circuit`/`Result` types with typed accessors, fluent `CircuitBuilder`, and circuit introspection
- **High performance** -- 3-tier linear solver (dense / sparse SmallSolver / SuiteSparse KLU), G/C matrix caching for AC, adjoint-method noise
- **ngspice-compatible** -- reads standard SPICE netlists, writes `.raw` files in ngspice format
- **910+ tests** validated against ngspice with tolerances as tight as 1e-6

## Quick Start

### Prerequisites

- C++20 compiler (GCC 12+ or Clang 15+)
- CMake 3.20+
- SuiteSparse (KLU)
- OpenBLAS

On Ubuntu/Debian:

```bash
sudo apt install cmake g++ libsuitesparse-dev libopenblas-dev
```

### Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### Run

```bash
# Simulate a netlist
./build/neospice circuit.cir

# Specify output file
./build/neospice circuit.cir -o result.raw

# Split output by analysis type
./build/neospice circuit.cir --split
```

Output is written in ngspice-compatible `.raw` format, viewable in any waveform viewer (e.g. GTKWave, KST, ngspice's built-in plot).

### Test

```bash
cd build && ctest -j$(nproc)
```

## Analyses

| Analysis | Netlist Command | Description |
|---|---|---|
| DC operating point | `.op` | Newton-Raphson with GMIN/source stepping fallback |
| DC sweep | `.dc V1 0 5 0.1` | 1D and nested 2D parameter sweeps |
| Transient | `.tran 1n 100n` | Adaptive timestepping with LTE control |
| AC small-signal | `.ac dec 10 1 100meg` | DEC/OCT/LIN frequency sweeps |
| Noise | `.noise v(out) V1 dec 10 1 100meg` | Adjoint method, per-device breakdown |
| Transfer function | `.tf v(out) V1` | Gain + input/output impedance |
| Sensitivity | `.sens v(out)` | DC sensitivity to all parameters |
| Pole-zero | `.pz` | Transfer function poles and zeros |
| Fourier | `.four 1meg v(out)` | Harmonic decomposition + THD |
| Parameter sweep | `.step param R1 1k 10k 1k` | Sweep any parameter across analyses |

## Device Models

| Category | Models |
|---|---|
| Passives | R (with TC, AC resistance, flicker/thermal noise), C, L, K (mutual inductance) |
| Sources | V, I -- DC, PULSE, SIN, PWL, EXP, SFFM, AM waveforms |
| Dependent sources | E (VCVS), G (VCCS), F (CCCS), H (CCVS) -- linear, POLY, TABLE |
| Behavioral | B-source -- expression-based with auto-diff Jacobians, DDT, IDT, PWL, TABLE |
| Switches | S (voltage-controlled), W (current-controlled) -- hysteresis |
| Transmission lines | T (lossless Branin), O (LTRA lossy -- RC/RG/LC/RLC) |
| Diode | Standard diode (level 1) |
| BJT | Gummel-Poon, VBIC (levels 4/9/12/13) |
| JFET | JFET (Shichman-Hodges), JFET2 (Parker-Skellern) |
| HFET | HFET1 (Curtice Cubic), HFET2 (Chalmers) |
| MOSFET | MOS1, MOS3, MOS9, BSIM3v32, BSIM3, BSIM4v7, BSIMSOI, HiSIM2, HiSIM_HV |

## C++ API

### Running a netlist

```cpp
#include "api/neospice.hpp"

neospice::Simulator sim;
auto ckt = sim.load("amplifier.cir");
auto result = sim.run(ckt);

// Typed access to results
auto& ac = std::get<neospice::ACResult>(result.analysis);
auto gain_db = ac.magnitude_db("out");
auto phase = ac.phase_deg("out");
```

### Individual analyses

```cpp
auto dc = sim.run_dc(ckt);
double vout = dc.voltage("out");

auto tran = sim.run_transient(ckt, 1e-9, 1e-6);
auto vout_wave = tran.voltage("out");   // vector<double>

auto ac = sim.run_ac(ckt, neospice::DEC, 10, 1.0, 100e6);
auto gain = ac.magnitude_db("out");     // vector<double>
```

### Building circuits programmatically

```cpp
auto ckt = neospice::CircuitBuilder()
    .title("RC filter")
    .resistor("R1", "in", "out", 1e3)
    .capacitor("C1", "out", "0", 100e-12)
    .vsource("V1", "in", "0", {.dc = 1.0, .ac = 1.0})
    .build();
```

### Circuit introspection

```cpp
auto nodes = ckt.node_names();            // {"in", "out", ...}
auto devices = ckt.device_names();        // {"R1", "C1", "V1"}
auto info = ckt.device_info("R1");        // type, nodes, value
auto conn = ckt.devices_at_node("out");   // {"R1", "C1"}
```

## Performance

Benchmarked in-process against ngspice-42 on Intel Core Ultra 9 285K, GCC 14, `-O3`. Both simulators linked as libraries -- no subprocess overhead, no file I/O in timed sections. Median of 10-30 runs.

| Benchmark | ngspice | neospice | Speedup |
|---|---:|---:|---:|
| **Parse** THS4131 (77 nodes) | 436 us | 196 us | 2.2x |
| **Parse** resistor divider | 44 us | 8 us | 5.3x |
| **DC OP** THS4131 (14 BJTs) | 624 us | 337 us | 1.8x |
| **DC OP** resistor divider | 50 us | 6 us | 7.8x |
| **AC** THS4131, 81 points | 979 us | 544 us | 1.8x |
| **AC** RC lowpass, 91 points | 114 us | 15 us | 7.7x |
| **Noise** resistor divider, 91 pts | 88 us | 55 us | 1.6x |
| **DC sweep** V1, 1001 pts | 819 us | 209 us | 3.9x |
| **Transient** pulse source | 995 us | 342 us | 2.9x |
| **Total** (all benchmarks) | **28.6 ms** | **26.0 ms** | **1.10x** |

See [docs/performance-comparison-with-ngspice.md](docs/performance-comparison-with-ngspice.md) for the full methodology and results.

## Netlist Compatibility

neospice reads standard SPICE netlists:

- `.param` expressions with arithmetic, functions (`sqrt`, `log`, `exp`, `sin`, `if`, ...)
- `.subckt` / `.ends` with parameter defaults (recursion limit 100)
- `.include` / `.lib` with section selection and circular-inclusion detection
- `.global`, `.ic`, `.nodeset`, `.options`, `.func`, `.save`
- SPICE suffixes: `T`, `G`, `MEG`, `k`, `m`, `u`, `n`, `p`, `f`

## Project Structure

```
cli/          Command-line interface
src/
  api/        C++ API (Simulator, CircuitBuilder, Result types)
  core/       Analysis engines and linear solvers
  devices/    28 device model implementations
  parser/     Netlist parser and expression evaluator
  output/     Raw file writer
tests/
  unit/       Unit tests for all components
  devices/    Per-device validation against ngspice
  circuits/   Integration test netlists
  bench/      Performance benchmarks
docs/         Architecture, performance, and design documentation
tools/        Device migration tooling (ngspice model auto-porter)
```

## Documentation

- [Performance comparison with ngspice](docs/performance-comparison-with-ngspice.md)
- [Algorithmic differences from ngspice](docs/neospice-vs-ngspice.md)
- [Device migration status](docs/device-migration-status.md)
- [Capabilities overview](docs/capabilities.md)
- [Architecture and design](docs/neospice-design.md)
- [Roadmap](docs/ROADMAP.md)

## Roadmap

| Phase | Feature | Status |
|---|---|---|
| 1 | Python bindings (pybind11, PyPI wheels) | Planned |
| 2 | WebAssembly build for browser simulation | Planned |
| 3 | Adjoint sensitivity / gradient computation | Planned |
| 4 | Parallel parameter sweeps / Monte Carlo | Planned |
| 5 | Incremental re-simulation | Planned |
| 6 | GPU-accelerated simulation (CUDA) | Planned |
| 7 | Extended devices (BSIM-CMG, Verilog-A) | Ongoing |

See [docs/ROADMAP.md](docs/ROADMAP.md) for details.
