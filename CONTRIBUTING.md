# Contributing to neospice

## Getting Started

### Prerequisites

- C++20 compiler (GCC 12+ or Clang 15+)
- CMake 3.20+
- OpenBLAS
- SLEEF (vectorized math library)
- libngspice (shared library, required for test suite)

On Ubuntu/Debian:

```bash
sudo apt install cmake g++ libopenblas-dev libsleef-dev libngspice0-dev
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

The migration tool roundtrip tests require an ngspice source checkout. Clone the ngspice repository and set the `NGSPICE_DIR` environment variable:

```bash
git clone https://github.com/imr/ngspice.git /path/to/ngspice
export NGSPICE_DIR=/path/to/ngspice
```

Add the export to your `~/.bashrc` or `~/.zshrc` to persist it across sessions. These tests are automatically skipped when `NGSPICE_DIR` is not set or the directory does not exist.

## Python Development

Install in editable mode with test dependencies:

```bash
pip install -e ".[dev]" -C cmake.args="-DNEOSPICE_BUILD_PYTHON=ON;-DNEOSPICE_BUILD_TESTS=OFF"
```

Run Python tests:

```bash
pytest tests/python -v
```

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
