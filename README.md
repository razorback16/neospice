# neospice

A modern C++20 SPICE circuit simulator. Drop-in replacement for ngspice with a clean embeddable API, 1.7--8x faster per-analysis in-process performance, and auto-differentiation in behavioral sources.

## Features

- **8 analysis types** -- DC OP, DC sweep, transient (adaptive Trap/Gear-2/BE), AC small-signal, noise (adjoint method), transfer function, sensitivity, pole-zero, Fourier/THD, parameter sweep (`.step`), and `.measure` post-processing
- **29 device models** -- passives, independent/dependent/behavioral sources, switches, transmission lines, diodes, BJTs, JFETs, MESFETs, HFETs, and MOSFETs through BSIM4v7
- **Embeddable C++ API** -- `Simulator`/`Circuit`/`Result` types with handle-based and string-based accessors, typed device methods, and circuit introspection
- **High performance** -- NeoSolver (dense + sparse column-LU with AMD ordering), G/C matrix caching for AC, adjoint-method noise
- **ngspice-compatible** -- reads standard SPICE netlists, writes `.raw` files in ngspice format
- **997 tests** validated against ngspice with tolerances as tight as 1e-6

## Quick Start (C++)

### Prerequisites

- C++20 compiler (GCC 12+ or Clang 15+)
- CMake 3.20+
- OpenBLAS

On Ubuntu/Debian:

```bash
sudo apt install cmake g++ libopenblas-dev
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
| MESFET | MES (GaAs MESFET -- NMF/PMF) |
| HFET | HFET1 (Curtice Cubic), HFET2 (Chalmers) |
| MOSFET | MOS1, MOS3, MOS9, BSIM3v32, BSIM3, BSIM4v7, BSIMSOI, HiSIM2, HiSIM_HV |

## C++ API

### Running a netlist

```cpp
#include "neospice/neospice.hpp"

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

auto ac = sim.run_ac(ckt, neospice::ACMode::DEC, 10, 1.0, 100e6);
auto gain = ac.magnitude_db("out");     // vector<double>
```

### Building circuits programmatically

```cpp
using namespace neospice;

Circuit ckt;
auto in  = ckt.node("in");
auto out = ckt.node("out");

ckt.V("V1", in, GROUND_INTERNAL, 0.0, 1.0);  // DC=0, AC=1
ckt.R("R1", in, out, 1e3);
ckt.C("C1", out, GROUND_INTERNAL, 100e-12);

Simulator sim;
auto ac = sim.run_ac(ckt, ACMode::DEC, 10, 1.0, 100e6);
auto gain = ac.magnitude_db("out");
```

### Handle-based result access

```cpp
// String-based (works with any circuit)
double vout = dc.voltage("out");

// Handle-based (O(1) dense array access, type-safe)
NodeId out_id = ckt.find_node("out");
double vout_h = dc.voltage(out_id);
auto vout_ac  = ac.magnitude_db(out_id);
```

### Measurement utilities

```cpp
#include "neospice/measure.hpp"

NodeId out_id = ckt.find_node("out");
double bw     = measure::bandwidth_3db(ac, out_id);
double rt     = measure::rise_time(tran, out_id, 0.5, 4.5);
double st     = measure::settling_time(tran, out_id, 5.0, 0.05);
double vrms   = measure::rms(tran, out_id);
```

### Circuit introspection

```cpp
auto nodes   = ckt.node_names();            // {"in", "out", ...}
auto devices = ckt.device_names();          // {"R1", "C1", "V1"}
auto info    = ckt.device_info("R1");       // type, nodes, value
auto conn    = ckt.devices_at_node("out");  // {"R1", "C1"}

// Handle-based introspection
NodeId nid   = ckt.find_node("out");
DevId  did   = ckt.find_device("R1");
auto   name  = ckt.name(nid);              // "out"
auto   dinfo = ckt.device_info(did);       // DeviceInfo struct
```

## Python

```bash
pip install neospice
```

```python
import neospice as ns

# One-liner convenience functions
dc = ns.dc("amplifier.cir")
print(dc.voltage("out"))

ac = ns.ac("filter.cir", mode="dec", npoints=100, fstart=1, fstop=1e9)
plt.semilogx(ac.frequency, ac.magnitude_db("out"))

tran = ns.transient("osc.cir", tstep=1e-9, tstop=1e-6)
plt.plot(tran.time, tran.voltage("out"))

# Parse inline netlists directly
dc = ns.dc("Divider\nV1 in 0 DC 10\nR1 in mid 1k\nR2 mid 0 1k\n.op\n.end\n")
print(dc.voltage("mid"))  # 5.0

# Or use the full Simulator API
sim = ns.Simulator()
ckt = sim.load("amplifier.cir")          # from file
ckt = sim.parse("...\n.op\n.end\n")      # or from string
result = sim.run_ac(ckt, ns.ACMode.DEC, 100, 1, 1e9)

# Build circuits programmatically with typed methods
ckt = ns.Circuit()
ckt.V("V1", "in", "0", 0.0, 1.0)        # DC=0, AC=1
ckt.R("R1", "in", "out", 1e3)
ckt.C("C1", "out", "0", 100e-12)

# SPICE engineering notation
from neospice import parse_value
r = parse_value("4.7k")                  # 4700.0
```

All result vectors are returned as NumPy arrays. Supports Python 3.10+ on Linux (x86_64, aarch64) and macOS (x86_64, arm64).

## Performance

Benchmarked in-process against ngspice-42 on Intel Core Ultra 9 285K, GCC 14, `-O3`. Both simulators linked as libraries -- no subprocess overhead, no file I/O in timed sections. Median of 10-30 runs.

| Benchmark | ngspice | neospice | Speedup |
|---|---:|---:|---:|
| **Parse** THS4131 (77 nodes) | 431 us | 199 us | 2.2x |
| **Parse** resistor divider | 44 us | 8 us | 5.1x |
| **DC OP** THS4131 (14 BJTs) | 620 us | 363 us | 1.7x |
| **DC OP** resistor divider | 49 us | 6 us | 7.8x |
| **AC** THS4131, 81 points | 964 us | 523 us | 1.8x |
| **AC** THS4131, 8001 points | 21.6 ms | 19.3 ms | 1.1x |
| **AC** RC lowpass, 91 points | 111 us | 14 us | 7.9x |
| **Transient** RC lowpass, 500 us | 1.11 ms | 607 us | 1.8x |
| **Transient** RLC series, 100 us | 1.56 ms | 1.96 ms | 1.3x ngspice |
| **Transient** pulse source, 100 us | 992 us | 334 us | 3.0x |
| **Noise** resistor divider, 91 pts | 89 us | 56 us | 1.6x |
| **DC sweep** V1, 1001 pts | 816 us | 197 us | 4.1x |
| **E2E** THS4131 (.op + .ac) | 726 us | 524 us | 1.4x |
| **E2E** OPA1632 (.op + .ac) | 6.42 ms | 5.90 ms | 1.1x |
| **Total** | **28.3 ms** | **23.6 ms** | **1.20x** |

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
cli/            Command-line interface
include/
  neospice/     Public API headers (types.hpp, neospice.hpp, measure.hpp)
src/
  api/          C++ API (Simulator, typed device methods, measurement utils)
  core/         Analysis engines and linear solvers
  devices/      29 device models, each self-contained with factory registration
  parser/       Netlist parser and expression evaluator
  output/       Raw file writer
python/
  bindings.cpp  nanobind C++ → Python bridge
  neospice/     Python package (convenience API, SPICE notation parser)
tests/
  unit/         Unit tests for all components (990+)
  devices/      Per-device validation against ngspice
  circuits/     Integration test netlists
  python/       Python binding tests
  bench/        Performance benchmarks
docs/           Architecture, performance, and design documentation
tools/          Device migration tooling (ngspice model auto-porter)
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
| — | Handle-based API redesign (NodeId/DevId, typed methods, dense results, measurements) | **Done** |
| 1 | Python bindings (nanobind, PyPI wheels, typed construction) | **Done** |
| 2 | WebAssembly build for browser simulation | Planned |
| 3 | Adjoint sensitivity / gradient computation | Planned |
| 4 | Parallel parameter sweeps / Monte Carlo | Planned |
| 5 | Incremental re-simulation | Planned |
| 6 | GPU-accelerated simulation (CUDA) | Planned |
| 7 | Extended devices (BSIM-CMG, Verilog-A) | Ongoing |

See [docs/ROADMAP.md](docs/ROADMAP.md) for details.
