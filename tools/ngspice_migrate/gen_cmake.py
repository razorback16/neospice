"""CMakeLists.txt generator for migrated ngspice device models.

Emits a minimal ``CMakeLists.txt`` that creates an OBJECT library from the
list of generated C++ source files and sets appropriate include directories.
"""

from __future__ import annotations

from typing import Protocol


# ---------------------------------------------------------------------------
# Minimal protocol for the descriptor fields we consume
# ---------------------------------------------------------------------------

class _Descriptor(Protocol):
    neospice_name: str


# ---------------------------------------------------------------------------
# Public API
# ---------------------------------------------------------------------------

def generate_cmake(desc: _Descriptor, source_files: list[str]) -> str:
    """Return the text of a ``CMakeLists.txt`` for the given model.

    Parameters
    ----------
    desc:
        Model descriptor; only ``neospice_name`` is used (as the library
        prefix, e.g. ``dio`` -> ``dio_obj``).
    source_files:
        List of generated ``.cpp`` filenames to compile.
    """
    ns = desc.neospice_name
    files_block = "\n".join(f"    {f}" for f in source_files)
    return (
        f"add_library({ns}_obj OBJECT\n"
        f"{files_block}\n"
        f")\n"
        f"target_include_directories({ns}_obj PUBLIC ${{CMAKE_SOURCE_DIR}}/src)\n"
    )
