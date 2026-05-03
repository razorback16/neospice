# Building neospice

## Prerequisites

- C++20 compiler (GCC 12+ or Clang 15+)
- CMake 3.20+
- OpenBLAS
- SLEEF (vectorized math library)
- libngspice (for comparison tests)
- ngspice CLI (for comparison tests)

## Ubuntu/Debian

```bash
sudo apt install cmake g++ libopenblas-dev libsleef-dev libngspice0-dev ngspice pkg-config
```

The Ubuntu `libngspice0-dev` package is built with XSPICE, KLU, and OSDI support. All tests should pass out of the box.

## macOS (Homebrew)

```bash
brew install cmake openblas sleef ngspice
```

> **Note:** `brew install ngspice` also installs the Homebrew `libngspice` bottle as a dependency. That bottle may have version mismatches or missing features compared to CI. Use the build script below to get a known-good libngspice for the comparison tests.

### Building libngspice from source

A one-command script builds ngspice 42 as a shared library with XSPICE and installs it into `third_party/libngspice` (gitignored). CMake automatically prefers this local copy over the Homebrew bottle.

```bash
./scripts/build-libngspice-mac.sh     # default: ngspice 42
./scripts/build-libngspice-mac.sh 44  # or specify a version
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

```bash
cd build && ctest -j$(nproc) --output-on-failure
```
