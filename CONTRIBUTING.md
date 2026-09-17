# Contributing to neospice

## Getting Started

### Prerequisites

- C++20 compiler (GCC 12+ or Clang 15+)
- CMake 3.20+
- OpenBLAS
- SLEEF (vectorized math library)
- libngspice and ngspice CLI (required for the comparison tests)

On Ubuntu/Debian:

```bash
sudo apt install cmake g++ libopenblas-dev libsleef-dev libngspice0-dev ngspice pkg-config
```

### Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### Run Tests

```bash
cd build && ctest -j$(nproc) --output-on-failure
```

#### ngspice Source (for migration tool tests)

Migration roundtrip tests need the pinned ngspice source in addition to the
installed reference library and executable. Follow the acquisition and complete
test commands in [Building neospice](docs/building.md#python-and-tooling-development).
Missing source can skip these tests locally; such a run does not satisfy the
required validation suite. CI requires executed tests without skips.

## Python Development

Use a fresh virtual environment and install the current checkout with both
extras so the full tooling suite has its plotting dependency:

```bash
python3 -m venv .venv-dev
.venv-dev/bin/python -m pip install '.[dev,benchmarks]'
NGSPICE_DIR=/path/to/pinned/ngspice .venv-dev/bin/python -m pytest \
  tests/python python/tests tools/tests --import-mode=importlib -q
```

A regular wheel installation avoids stale editable paths after moving the
checkout. Reinstall after changing C++ bindings or Python sources. C++ tests
remain a separate CMake build. Consult [JOSS progress](docs/joss-progress.md)
for known failing reference checks; a documented failure is still a failure.

## Code Style

- C++20 throughout
- Follow existing patterns in the codebase
- No specific formatter is enforced — match the style of surrounding code
- Prefer clarity over cleverness

## Submitting Changes

1. Fork the repository
2. Create a feature branch from `main`
3. Make your changes
4. Ensure all tests pass (`ctest` and `pytest`)
5. Submit a pull request against `main`

Keep PRs focused — one logical change per PR. Include a clear description of what changed and why.

## Device Models

neospice includes a migration tool (`tools/`) that semi-automatically translates ngspice device models from C to C++. If you're porting a new device model, see the tool's documentation and existing device implementations (e.g., `src/devices/dio/`) as examples.

Ported device code must preserve original copyright headers from the upstream source.

## License

By contributing to neospice, you agree that your contributions will be licensed under the MIT License (see `LICENSE`).

Note that some device model code carries additional third-party copyrights — see `NOTICE` for details.
