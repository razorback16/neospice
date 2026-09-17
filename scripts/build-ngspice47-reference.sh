#!/usr/bin/env bash
# Pinned Linux reference used by JOSS validation and CI. Build both interfaces.
set -euo pipefail

reference_root="${1:?Usage: bash scripts/build-ngspice47-reference.sh PREFIX [ARCHIVE]}"
archive_input="${2:-}"
jobs="${NEOSPICE_REFERENCE_JOBS:-4}"
mkdir -p "${reference_root}"
reference_root="$(cd "${reference_root}" && pwd)"
for directory in source build-cli build-shared cli shared; do
    if [[ -e "${reference_root}/${directory}" ]]; then
        echo "Refusing to overwrite ${reference_root}/${directory}" >&2
        exit 1
    fi
done
archive="${reference_root}/ngspice-47.tar.gz"
if [[ -n "${archive_input}" ]]; then
    cp "${archive_input}" "${archive}"
else
    curl --fail --location --retry 3 --proto '=https' --proto-redir '=https' \
        'https://sourceforge.net/projects/ngspice/files/ng-spice-rework/47/ngspice-47.tar.gz/download' \
        --output "${archive}"
fi
python3 - "${archive}" <<'PY'
import hashlib, pathlib, sys
expected = '894e649651f1838a14095e5a5439e7d3aa63e87ede14d283173fda4fcdef675f'
actual = hashlib.sha256(pathlib.Path(sys.argv[1]).read_bytes()).hexdigest()
if actual != expected:
    raise SystemExit(f'ngspice 47 archive checksum mismatch: {actual}')
PY
mkdir "${reference_root}/source"
tar -xzf "${archive}" --strip-components=1 -C "${reference_root}/source"
for interface in cli shared; do
    mkdir "${reference_root}/build-${interface}"
    options=()
    if [[ "${interface}" == shared ]]; then options+=(--with-ngshared); fi
    (
        cd "${reference_root}/build-${interface}"
        "${reference_root}/source/configure" \
            --prefix="${reference_root}/${interface}" \
            --without-x --with-readline=no --disable-debug \
            "${options[@]}" CFLAGS='-O2 -g'
        make -j"${jobs}"
        make install
    ) > "${reference_root}/build-${interface}.log" 2>&1
done
PKG_CONFIG_LIBDIR="${reference_root}/shared/lib/pkgconfig" \
    pkg-config --exact-version=47 ngspice
"${reference_root}/cli/bin/ngspice" --version > "${reference_root}/version.txt"
echo "ngspice 47 CLI: ${reference_root}/cli/bin/ngspice"
echo "ngspice 47 pkg-config: ${reference_root}/shared/lib/pkgconfig"
echo "Reference startup: ${reference_root}/shared/share/ngspice/scripts/spinit"
