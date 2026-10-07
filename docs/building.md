# Building neospice

## Prerequisites

- C++20 compiler (GCC 12+ or Clang 15+)
- CMake 3.20+
- OpenBLAS
- SLEEF (vectorized math library)
- libngspice 47 and ngspice 47 CLI for comparison tests and benchmarks

## Library and CLI only

The engine and Python bindings do not need ngspice at runtime. After installing
the compiler, CMake, OpenBLAS, SLEEF, and pkg-config:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DNEOSPICE_BUILD_TESTS=OFF
cmake --build build --parallel
./build/neospice tests/circuits/resistor_divider.cir -o result.raw
```

Tests require the reference setup below.

## Ubuntu/Debian

```bash
sudo apt install cmake g++ libopenblas-dev libsleef-dev pkg-config curl bison flex
bash scripts/build-ngspice47-reference.sh "$PWD/third_party/ngspice47-reference"
export PKG_CONFIG_PATH="$PWD/third_party/ngspice47-reference/shared/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
export LD_LIBRARY_PATH="$PWD/third_party/ngspice47-reference/shared/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export PATH="$PWD/third_party/ngspice47-reference/cli/bin:$PATH"
export SPICE_SCRIPTS="$PWD/third_party/ngspice47-reference/shared/share/ngspice/scripts"
```

The script checks the ngspice 47 release archive SHA-256 before extracting or
building it. For offline setup, supply the archive as the second argument.
The same checksum applies. Use a new prefix for each build. The prefix retains build logs. Both interfaces preserve release defaults,
including XSPICE and Sparse as the selected solver. Tests do not request KLU.
The stock startup file controls code-model loading and its own thread setting.
See [reference runtime details](ngspice47-reference.md).

ngspice 47 is the sole behavioral target and migration-source version. CMake
requires version 47 for reference tests, and the corpus runner rejects other
versions during preflight. Use the pinned build rather than a distribution's
unpinned ngspice package.

## macOS (Homebrew)

```bash
brew install cmake openblas sleef ngspice
```

The Homebrew library may differ from CI. Use the source script below for
comparison tests.

### Building libngspice from source

A macOS convenience script defaults to ngspice 47 as a shared library with XSPICE
and installs it into `third_party/libngspice` (gitignored). CMake automatically
prefers this local copy over the Homebrew bottle. This tag-based convenience
build differs from the checksum-pinned Linux CI build. Its ngspice 47 default
remains untested on macOS. Check the CLI version separately.

```bash
./scripts/build-libngspice-mac.sh     # ngspice 47
```

Then reconfigure and rebuild:

```bash
rm -f build/CMakeCache.txt
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(sysctl -n hw.ncpu)
```

## Build with tests

After configuring the reference paths above:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DNEOSPICE_BUILD_TESTS=ON
cmake --build build --parallel
```

The explicit test option also enables tests in a directory previously configured
for a library-only build.

## Test

Configuring with tests enabled also acquires the RFF70N06 regression's unchanged
`harprmos.lib` from KiCad-Spice-Library revision
`a8688952bcaab19f567bc4db237b60bde03ef310`. Both downloaded and supplied files must
have SHA-256 `61469f6ff311b0c8efb6e62b037293843806ecb1b23c5de71472983628858dae`.
The download uses HTTPS with certificate verification and lives under the build
directory. For an offline build, provide an already acquired copy:

```bash
cmake -B build -DNEOSPICE_RFF70N06_LIBRARY=/absolute/path/to/harprmos.lib
```

Acquisition or checksum failures stop configuration rather than skip the regression.
`RFF70N06.ReferenceIsInconclusive` asserts that both engines fail the original
operating-point fixture. This classification is not a numerical match.
See [open findings](joss-progress.md#open-numerical-findings).

The library header refers to its vendor disk README for licensing terms.
Redistribution review remains open for the paper's corpus archive.

```bash
ctest --test-dir build --parallel --output-on-failure
```

## Python and tooling development

### Continuous integration

Native CI checks that the generated support matrix matches the tests.
Classify new reference helpers in `tools/support_matrix.py`.
The browser gallery bridge supplies circuit-level coverage.

Native, binding and packaging changes trigger Python 3.12 wheel builds and API
tests on Linux x86_64/ARM64 and macOS 14 ARM64.
Release tags and manual wheel builds cover Python 3.10–3.14.
Only release tags publish to PyPI.

### Local environment

Use a new virtual environment and install the current checkout. A regular wheel
installation avoids stale editable-install hooks after moving or renaming a
checkout. Reinstall after changing Python or C++ sources.

```bash
python3 -m venv .venv-dev
.venv-dev/bin/python -m pip install '.[dev,benchmarks]'
```

The `dev` extra declares pytest, PyYAML, scikit-build-core, and nanobind. NumPy is
a runtime dependency. The `benchmarks` extra supplies Matplotlib, required by
the full tooling suite. Install the system prerequisites above.
The wheel build disables C++ tests. Run them separately with CMake.

Migration tests use the same checksum-verified ngspice 47 source extracted by
the reference-build script. No separate source checkout is required:

```bash
NGSPICE_DIR="$PWD/third_party/ngspice47-reference/source" .venv-dev/bin/python -m pytest \
  tests/python python/tests tools/tests --import-mode=importlib -q
```

Run commands from the repository root. `NGSPICE_DIR` must identify ngspice 47
source. Record its revision when reporting results.
Missing source fails migration tests with a setup message.
For a deliberate local opt-out, set `NEOSPICE_ALLOW_MISSING_NGSPICE_SOURCE=1`.
CI supplies pinned source and rejects skipped required tests.

Wheel jobs run both Python test directories. Linux CI also runs migration and
C++ reference checks. The descriptor suite checks 18 automatic migration
files plus manual ASRC/LTRA metadata. JFET level 1 uses a native implementation.

## Browser / WebAssembly

The [Emscripten build](webassembly.md) uses a separate build directory and does
not require native OpenBLAS, SLEEF, libngspice or OpenMP. Build with Emscripten
6.0.11 using `emcmake cmake -S . -B build-wasm -DCMAKE_BUILD_TYPE=Release`.
That guide documents the JS/TypeScript API, demo and validation commands.
