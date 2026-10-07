# Contributing to neospice

Contributions to circuit models, numerical methods, APIs, browser tools,
examples, and documentation are welcome. Start with the
[documentation index](docs/README.md), [architecture](docs/neospice-design.md),
and [roadmap](docs/ROADMAP.md).

## Development setup

Install a C++20 compiler, CMake 3.20+, OpenBLAS, SLEEF, and pkg-config using the
[platform build instructions](docs/building.md). Comparison tests additionally
require the checksum-pinned **ngspice 47** CLI, shared library, and source.
Follow the [reference setup](docs/ngspice47-reference.md) and export the paths
in the build guide before configuring tests. Distribution packages may select
a different ngspice release.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --parallel --output-on-failure
```

The build guide also documents offline fixture acquisition. To build only the
library and CLI, configure with `-DNEOSPICE_BUILD_TESTS=OFF`.

## Python and tooling

From the repository root:

```sh
python3 -m venv .venv-dev
.venv-dev/bin/python -m pip install '.[dev,benchmarks]'
NGSPICE_DIR="$PWD/third_party/ngspice47-reference/source" .venv-dev/bin/python -m pytest \
  tests/python python/tests tools/tests --import-mode=importlib -q
```

Reinstall after changing bindings or Python sources. Native tests run separately
through CMake. Migration tests require the pinned source. Missing source fails
by default. The explicit local opt-out and CI's no-skip requirement are
explained in [Python and tooling development](docs/building.md#python-and-tooling-development).

Browser work has its own [Circuit Lab](docs/circuit-lab.md) and
[WebAssembly](docs/webassembly.md) build and verification commands.

## Numerical changes

ngspice 47 is the reference implementation. When results differ, investigate
neospice first. Preserve the original circuit, reference settings, and
comparison tolerances. Fix the implementation instead of hiding a discrepancy.
Keep reference failures distinct from neospice failures and numerical matches.

For error-margin diagnostics:

```sh
cmake -S . -B build -DNEOSPICE_DEBUG_COMPARE=ON
cmake --build build --parallel
ctest --test-dir build --verbose
```

See [validation methods](docs/validation-methods.md) for comparison rules and
[benchmark methods](docs/benchmark-methods.md) for accuracy-qualified timing.
A new optimization needs numerical verification as well as timing evidence.

## Device models and code style

Use C++20 and follow the surrounding code's conventions. Keep public APIs,
failure behavior, and ownership clear. The [device integration guide](docs/device-migration-status.md)
covers the migration tool, descriptors, native adapters, and comparison tests.
Preserve upstream copyright headers in translated or derived code.

## Submitting changes

Work directly on `main` in the shared project workspace unless a feature branch
is explicitly requested. External contributors can submit a pull request from
their fork against `main`.

Keep each change focused and describe its behavior and validation. Run checks
appropriate to the change. CI runs the required suites. Update relevant guides
before pushing, including capabilities and roadmap status when behavior changes.
After adding or changing test coverage, regenerate the support matrix:

```sh
python3 tools/support_matrix.py > docs/support-matrix.md
python3 tools/support_matrix.py --check
```

Bug reports should include a minimal netlist, neospice version, analysis options,
platform, reproduction command, and expected/actual behavior. Attach the
ngspice 47 result when reporting a compatibility discrepancy.

## License and attribution

Original contributions use the [MIT license](LICENSE). Derived contributions
must preserve the applicable upstream notices and terms. See [NOTICE](NOTICE)
and [CREDITS.md](CREDITS.md). The MIT license does not replace component-specific
licenses.
