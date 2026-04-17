"""Post-migration validation helpers.

Provides lightweight quality checks for migrated setup and load sources,
counting TSTALLOC sites, stamp rewrites, and detecting unrewritten stamp
patterns that still use raw pointer dereferencing.
"""

from __future__ import annotations

import re


# ---------------------------------------------------------------------------
# Public API
# ---------------------------------------------------------------------------

def count_tstalloc_sites(setup_src: str) -> int:
    """Count lines containing ``TSTALLOC(`` that are *not* ``#define TSTALLOC`` lines.

    Parameters
    ----------
    setup_src:
        Full text of the setup source file.
    """
    count = 0
    for line in setup_src.splitlines():
        stripped = line.strip()
        if "TSTALLOC(" in stripped and not stripped.startswith("#define TSTALLOC"):
            count += 1
    return count


def count_stamp_rewrites(load_src: str, prefix: str) -> int:
    """Count occurrences of ``mat.add(here-><PREFIX>*Ptr`` in *load_src*.

    Parameters
    ----------
    load_src:
        Full text of the load source file.
    prefix:
        Device prefix (e.g. ``"DIO"``).
    """
    pattern = re.compile(rf"mat\.add\(here->{re.escape(prefix)}\w*Ptr")
    return len(pattern.findall(load_src))


def validate_migration(
    setup_src: str,
    load_src: str,
    prefix: str,
) -> list[str]:
    """Validate a migration and return a list of issue / info strings.

    Checks performed:

    * **Unrewritten stamp sites** -- ``*(here-><PREFIX>*Ptr)`` patterns that
      still use raw pointer dereferencing instead of ``mat.add``.
    * **Summary counts** -- TSTALLOC and ``mat.add`` tallies (INFO level).

    Parameters
    ----------
    setup_src:
        Full text of the setup source file.
    load_src:
        Full text of the load source file.
    prefix:
        Device prefix (e.g. ``"DIO"``).
    """
    issues: list[str] = []

    # Detect unrewritten stamp sites: *(here->DIO*Ptr)
    unrewritten_pat = re.compile(
        rf"\*\(\s*here->{re.escape(prefix)}\w*Ptr\s*\)"
    )
    unrewritten_matches = unrewritten_pat.findall(load_src)
    if unrewritten_matches:
        issues.append(
            f"ERROR: {len(unrewritten_matches)} unrewritten stamp site(s) found"
        )

    # Informational counts
    tstalloc_count = count_tstalloc_sites(setup_src)
    stamp_count = count_stamp_rewrites(load_src, prefix)
    issues.append(
        f"INFO: TSTALLOC sites={tstalloc_count}, mat.add calls={stamp_count}"
    )

    return issues
