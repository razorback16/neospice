#!/usr/bin/env bash
set -euo pipefail

NGSPICE_VERSION="${1:-42}"
NGSPICE_TAG="ngspice-${NGSPICE_VERSION}"
PREFIX="${PWD}/third_party/libngspice"
JOBS="$(sysctl -n hw.ncpu)"

echo "==> Building libngspice ${NGSPICE_VERSION} into ${PREFIX}"

# Ensure build dependencies are available
brew install bison autoconf automake libtool 2>/dev/null || true

# Unlink Homebrew's libngspice if present — we build our own
if brew ls --versions libngspice &>/dev/null; then
    echo "==> Unlinking Homebrew libngspice to avoid conflicts"
    brew unlink libngspice 2>/dev/null || true
fi

# Clone source
WORKDIR="$(mktemp -d)"
trap 'rm -rf "${WORKDIR}"' EXIT

echo "==> Cloning ${NGSPICE_TAG}..."
git clone --depth 1 --branch "${NGSPICE_TAG}" \
    https://git.code.sf.net/p/ngspice/ngspice "${WORKDIR}/ngspice"

cd "${WORKDIR}/ngspice"

echo "==> Running autoreconf..."
autoreconf -fi

# Homebrew bison is keg-only; put it on PATH for configure AND make
export PATH="$(brew --prefix)/opt/bison/bin:$PATH"

echo "==> Configuring (--with-ngshared --enable-xspice --enable-cider)..."
# Force C11 — ngspice ≤42 has 'typedef int bool' which conflicts with C23's
# built-in bool keyword (Clang 17+ defaults to -std=gnu23).
CFLAGS="-std=gnu11 ${CFLAGS:-}" \
./configure \
    --with-ngshared \
    --enable-cider \
    --enable-xspice \
    --disable-openmp \
    --prefix="${PREFIX}"

echo "==> Building (${JOBS} jobs)..."
make -j"${JOBS}"

echo "==> Installing to ${PREFIX}..."
make install

echo ""
echo "==> Done. libngspice ${NGSPICE_VERSION} installed to ${PREFIX}"
echo ""
echo "To use it, reconfigure with:"
echo "  rm -f build/CMakeCache.txt"
echo "  cmake -B build -DCMAKE_BUILD_TYPE=Release"
