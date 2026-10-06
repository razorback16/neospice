# Building neospice

## Prerequisites

- C++20 compiler (GCC 12+ or Clang 15+)
- CMake 3.20+
- OpenBLAS
- SLEEF (vectorized math library)
- libngspice 47 and ngspice 47 CLI (required JOSS comparison reference)

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
building it. For offline acquisition, supply the archive as a second argument;
the checksum requirement is identical. Use a new prefix for each build. Build
logs are retained in that prefix. Both interfaces preserve release defaults,
including XSPICE and Sparse as the selected solver; tests do not request KLU.
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

> **Note:** `brew install ngspice` also installs the Homebrew `libngspice` bottle as a dependency. That bottle may have version mismatches or missing features compared to CI. Use the build script below to get a known-good libngspice for the comparison tests.

### Building libngspice from source

A macOS convenience script defaults to ngspice 47 as a shared library with XSPICE
and installs it into `third_party/libngspice` (gitignored). CMake automatically
prefers this local copy over the Homebrew bottle. This tag-based convenience
build is separate from the checksum-pinned Linux CI build; its new default has
not yet been exercised on macOS, and the CLI version must also be checked.

```bash
./scripts/build-libngspice-mac.sh     # ngspice 47
```

Then reconfigure and rebuild:

```bash
rm -f build/CMakeCache.txt
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(sysctl -n hw.ncpu)
```

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

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

Acquisition or hash failures stop configuration; they do not skip the regression.
`RFF70N06.ReferenceIsInconclusive` uses the original corpus's three
100 kOhm terminations, external observables and PSpice compatibility mode in a
dedicated test process. The pinned ngspice 47 reference does not produce a
converged operating point, and neospice also fails explicitly. The regression
asserts this reference-inconclusive classification; it does not count the
fixture as a numerical match. See [checkpoint 39](joss-progress.md#milestone-3-triage-checkpoint-39-in-progress).
This is one operating-point fixture, not model certification.
The library is fetched separately; its header refers to its original vendor disk
README for licensing terms. Redistribution review remains open for the paper's
corpus archive.

```bash
cd build && ctest -j$(nproc) --output-on-failure
```

## Python and tooling development

### Continuous integration

The native CI workflow requires the generated support matrix to match the
tests. New reference-calling helpers must be classified in
`tools/support_matrix.py`; the browser gallery bridge is circuit-level
coverage. Test-report validation runs only after the test step has executed,
so an earlier setup failure does not produce misleading missing-report errors.

Changes to native sources, bindings or packaging run Python 3.12 wheel builds
and API tests on Linux x86_64, Linux ARM64 and macOS 14 ARM64 before release.
Release tags and manually requested wheel builds still cover Python 3.10–3.14;
only release tags publish to PyPI. This catches Apple Clang and standard-library
portability issues before tagging. The 0.2.0 fixes provide explicit template
deduction for the sensitivity helper and automatically joined `std::thread`
workers where macOS 14 lacks `std::jthread`.

### Local environment

Use a new virtual environment and install the current checkout. A regular wheel
installation avoids stale editable-install hooks after moving or renaming a
checkout. Reinstall after changing Python or C++ sources.

```bash
python3 -m venv .venv-dev
.venv-dev/bin/python -m pip install '.[dev,benchmarks]'
```

The `dev` extra declares pytest, PyYAML, scikit-build-core, and nanobind; NumPy is
a runtime dependency. The `benchmarks` extra supplies Matplotlib, required by
the report-generation tests included in the full tooling suite. The system
prerequisites above are still required. The
wheel build disables C++ tests; build and run them separately with CMake.

Migration tests use the same checksum-verified ngspice 47 source extracted by
the reference-build script. No separate source checkout is required:

```bash
NGSPICE_DIR="$PWD/third_party/ngspice47-reference/source" .venv-dev/bin/python -m pytest \
  tests/python python/tests tools/tests --import-mode=importlib -q
```

Run these commands from the repository root. If the source is already available,
set `NGSPICE_DIR` to that checkout; record its revision when reporting results.
Without the source the migration roundtrip tests fail, naming this setup step,
rather than disappearing from the run. Set
`NEOSPICE_ALLOW_MISSING_NGSPICE_SOURCE=1` to record a deliberate opt-out and skip
them explicitly; that is the only legitimate skip in these suites. CI supplies
the pinned source and rejects any skipped required test. Wheel-platform tests run both
Python test directories; migration and C++ reference checks run in the Linux CI
job. An unexecuted platform workflow is not evidence that platform passed.

The YAML files for ASRC and LTRA describe manual implementations and explicitly
reject automatic generation. JFET level 1 is a native implementation without a
`jfet.yaml` migration descriptor; `jfet2.yaml` covers the migrated level 2 device.
The descriptor suite checks all 18 automatic migration descriptors and both
manual metadata files.

## Browser / WebAssembly

The [Emscripten build](webassembly.md) uses a separate build directory and does
not require native OpenBLAS, SLEEF, libngspice or OpenMP. Build with Emscripten
6.0.11 using `emcmake cmake -S . -B build-wasm -DCMAKE_BUILD_TYPE=Release`.
The JS/TypeScript API, demo and validation commands are documented there.
