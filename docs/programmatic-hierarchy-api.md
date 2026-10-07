# Programmatic circuits and hierarchy

Build a circuit with typed methods or load a SPICE netlist. C++ and Python use
`Circuit` for circuit state and `Simulator` for analyses.

## Python

```python
import neospice as ns

circuit = ns.Circuit()
circuit.V("V1", "in", "0", 10.0)
circuit.R("R1", "in", "out", 1000.0)
circuit.R("R2", "out", "0", 1000.0)

sim = ns.Simulator()
result = sim.run_dc(circuit)
print(result.voltage("out"))  # 5.0
```

Python accepts node names in builder calls. Waveform vectors use NumPy arrays.
Convenience functions such as `ns.dc()`, `ns.ac()`, and `ns.transient()` accept
netlist text or a file path. See [Python sources](../python/neospice/__init__.py)
for function signatures.

## C++

```cpp
#include <neospice/neospice.hpp>

neospice::Circuit circuit;
auto in = circuit.node("in");
auto out = circuit.node("out");
circuit.V("V1", in, neospice::GND, 10.0);
circuit.R("R1", in, out, 1000.0);
circuit.R("R2", out, neospice::GND, 1000.0);

neospice::Simulator sim;
auto result = sim.run_dc(circuit);
double voltage = result.voltage(out);  // 5.0
```

Link the `neospice_lib` target. [Build instructions](building.md) list native
dependencies. `NodeId`, `DevId`, and `ModelId` identify circuit objects.
Results support named access and typed handles. See the
[public header](../include/neospice/neospice.hpp) and
[Circuit declarations](../src/core/circuit.hpp).

## Library files and subcircuits

`Circuit::include(path)` loads definitions from a SPICE file.
`Circuit::X(instance, subcircuit, ports, parameters)` instantiates a definition
with an ordered list of port names and optional string-valued parameters.
Python exposes `include(path)` and `X(instance, subcircuit, ports)`.
The Python `X()` binding does not expose the C++ parameter map.

For a file named `divider.lib`:

```spice
* Divider library
.subckt divider input output ground PARAMS: resistance=1k
R1 input output {resistance}
R2 output ground {resistance}
.ends divider
```

Use it from Python:

```python
circuit = ns.Circuit()
circuit.include("divider.lib")
circuit.V("V1", "in", "0", 10.0)
circuit.X("X1", "divider", ["in", "out", "0"])
result = ns.Simulator().run_dc(circuit)
print(result.voltage("out"))  # 5.0
```

Subcircuits flatten into primitive devices before the solver runs. Library
files can contain nested definitions, models, functions, and parameters. The
netlist parser remains responsible for their supported syntax.

The API owns included definitions per circuit. Simulator-level `include()` /
`subckt()` builders and reusable in-memory subcircuit objects remain proposals.
They are not current method names.

## Inspect and change circuits

Use `node_names()`, `device_names()`, `device_info()`, and `devices_at_node()`
to inspect a circuit. `find_node()` and `find_device()` return typed handles.
A finalized circuit rejects topology changes. Construct another circuit when
nodes or devices change.

Use checked `update_param()` and the [incremental API](incremental-simulation.md)
for repeated DC/AC solves. Use [parallel studies](parallel-studies.md) for
independent parameter jobs and [adjoint gradients](adjoint-gradients.md) for
output derivatives.
