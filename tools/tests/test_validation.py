"""Unit tests for the post-migration validation module."""

from __future__ import annotations

from ngspice_migrate.validation import (
    count_stamp_rewrites,
    count_tstalloc_sites,
    validate_migration,
)


# ---------------------------------------------------------------------------
# count_tstalloc_sites
# ---------------------------------------------------------------------------

def test_count_tstalloc():
    setup_src = """\
#define TSTALLOC(ptr,first,second) \\
do { /* ... */ } while(0)
    TSTALLOC(DIOposPosPtr,DIOposNode,DIOposNode);
    TSTALLOC(DIOnegNegPtr,DIOnegNode,DIOnegNode);
    TSTALLOC(DIOposPrimePosPrimePtr,DIOposPrimeNode,DIOposPrimeNode);
"""
    assert count_tstalloc_sites(setup_src) == 3


def test_count_tstalloc_no_false_positives():
    setup_src = "#define TSTALLOC(ptr,first,second) stuff\n"
    assert count_tstalloc_sites(setup_src) == 0


# ---------------------------------------------------------------------------
# count_stamp_rewrites
# ---------------------------------------------------------------------------

def test_count_stamp_rewrites():
    load_src = """\
    mat.add(here->DIOposPosPtr, gspr);
    mat.add(here->DIOnegNegPtr, gd);
    mat.add(here->DIOposPrimePosPrimePtr, -(gd + gspr));
"""
    assert count_stamp_rewrites(load_src, "DIO") == 3


def test_count_stamp_rewrites_zero():
    load_src = "// nothing here\n"
    assert count_stamp_rewrites(load_src, "DIO") == 0


# ---------------------------------------------------------------------------
# validate_migration
# ---------------------------------------------------------------------------

def test_validate_finds_unrewritten():
    setup = "TSTALLOC(DIOposPosPtr, x, y);"
    load = "*(here->DIOposPosPtr) += gspr;"  # unrewritten!
    issues = validate_migration(setup, load, "DIO")
    assert any("unrewritten" in i.lower() for i in issues)


def test_validate_clean_output():
    setup = "TSTALLOC(DIOposPosPtr, x, y);"
    load = "mat.add(here->DIOposPosPtr, gspr);"
    issues = validate_migration(setup, load, "DIO")
    assert not any("unrewritten" in i.lower() for i in issues)


def test_validate_reports_info_counts():
    setup = "TSTALLOC(DIOposPosPtr, x, y);\nTSTALLOC(DIOnegNegPtr, a, b);"
    load = "mat.add(here->DIOposPosPtr, gspr);\nmat.add(here->DIOnegNegPtr, gd);"
    issues = validate_migration(setup, load, "DIO")
    info_lines = [i for i in issues if i.startswith("INFO:")]
    assert len(info_lines) == 1
    assert "TSTALLOC sites=2" in info_lines[0]
    assert "mat.add calls=2" in info_lines[0]
