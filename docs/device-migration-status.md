# Device integration and migration

Updated October 6, 2026. The [capabilities guide](capabilities.md) lists
implemented devices. The generated [support matrix](support-matrix.md) records
which device/analysis combinations have reference tests.

## Current implementations

The native engine includes passives, independent and controlled sources,
behavioral sources, switches, and transmission lines. Semiconductor adapters
include diode, BJT, VBIC, JFET/JFET2, MES, HFET1/2, MOS1/2/3/9, BSIM3v32,
BSIM3, BSIM4v7, BSIMSOI, HiSIM2, HiSIM_HV, and VDMOS.

A source directory does not certify every option or analysis. In particular,
[VDMOS AC/noise and self-heating](vdmos-compatibility.md) fail explicitly.
Use [VBIC notes](vbic-compatibility.md) and [model-card compatibility](model-card-compatibility.md)
for detailed adapter behavior.

## Integration structure

- `src/devices/<device>/` contains translated routines, the model-card adapter,
  compatibility shim, and its CMake object target.
- `src/devices/device.hpp` defines the engine-facing device interface.
- The device registry handles parser and builder integration.
- `tests/devices/<device>/` contains model and reference tests.
- `tools/descriptors/` contains migration configuration.
- `tools/ngspice_migrate/` implements translation and support-file generation.

Shared helpers include `ucb_compat.hpp`, `ucb_utils.hpp`, `ucb_device_init.hpp`,
and `model_card_utils.hpp`. Preserve model ownership, state offsets, matrix
pointers, and temperature preparation when changing an adapter.

## Migration workflow

1. Obtain and verify the [pinned ngspice 47 source](ngspice47-reference.md).
2. Inspect the upstream model, license headers, and matching descriptor.
3. Generate into a separate output directory for review.
4. Check the shim and adapter against the actual upstream routines.
5. Add analytical and ngspice 47 comparisons for supported analyses and options.
6. Run migration roundtrip tests and regenerate the support matrix.

From the repository root, inspect the command interface with:

```sh
PYTHONPATH=tools python3 -m ngspice_migrate --help
```

The command takes a descriptor, the upstream **device source directory**, and
an output directory. `--dry-run` reports generated files without writing them.
Generation is a starting point. It does not establish correctness or complete
an adapter's manual integration.

The tooling suite checks 18 automatic descriptors and the ASRC/LTRA manual
metadata. Those two manual implementations reject automatic generation.
JFET level 1 is native and has no migration descriptor. `jfet2.yaml` describes
the separate level 2 model. See [tooling setup](building.md#python-and-tooling-development).

## Review requirements

Retain upstream copyright notices and component-specific license terms.
Check shared state before exposing a model to parallel jobs. Preserve explicit
errors for unsupported behavior and original tolerances for reference tests.
Update capabilities and relevant compatibility notes when support changes.
The [roadmap](ROADMAP.md) tracks additional models and Verilog-A work.
