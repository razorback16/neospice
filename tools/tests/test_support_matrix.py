"""Pin the support-matrix extractor's semantics.

`--check` catches a stale document, but not a regex change that silently
reclassifies cells: that just prints "regenerate", and whoever regenerates has
no reason to look. These assertions are the known answers the extractor was
validated against when it was written, so a semantic drift fails here instead.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import support_matrix as sm

CELLS = sm.collect()


def tier(device, analysis, test):
    """Which tier `test` lands in for this cell, or None."""
    for t, entries in CELLS.get(device, {}).get(analysis, {}).items():
        if any(test in name for name, _ in entries):
            return t
    return None


def test_self_heating_regression_verifies_dc():
    # It compares an isothermal VDMOS operating point against ngspice 47.
    assert tier('VDMOS', 'dc_op', 'SelfHeatingFailsExplicitlyAndOnlyThatForm') == 'V'


def test_self_heating_throw_is_not_read_as_an_analysis_rejection():
    # The EXPECT_THROW is on load(), not on an analysis, so it must not mark
    # any cell X -- otherwise the same test would both verify and reject.
    for analysis, states in CELLS['VDMOS'].items():
        assert not any('SelfHeating' in n for n, _ in states.get('X', [])), analysis


def test_unsupported_ac_and_noise_are_rejections_not_verifications():
    assert tier('VDMOS', 'ac', 'RejectsUnsupportedAC') == 'X'
    assert tier('VDMOS', 'noise', 'RejectsUnsupportedNoise') == 'X'


def test_ltra_transient_is_reference_verified():
    assert tier('O (LTRA)', 'transient', 'TransientRC') == 'V'


def test_audit_test_that_cannot_fail_is_excluded():
    # Compares at 1e30 and asserts EXPECT_TRUE(true).
    for device, analyses in CELLS.items():
        for analysis, states in analyses.items():
            for entries in states.values():
                assert not any('BSIM4v7_DC_Audit_BiasRegions' in n for n, _ in entries)
    assert any('BSIM4v7_DC_Audit_BiasRegions' in n for n, _ in sm.NON_ASSERTING)


def test_bjt_compare_file_does_not_confer_reference_coverage():
    # tests/devices/bjt/test_bjt_compare.cpp never calls ngspice despite its
    # name; BJT's transient and sweep coverage is self-consistency only.
    assert not CELLS['BJT (Gummel-Poon)']['transient'].get('V')
    assert CELLS['BJT (Gummel-Poon)']['transient'].get('~')


def test_analyses_with_no_reference_path_have_no_verified_cell():
    for analysis in ('tf', 'sens', 'pz', 'four'):
        for device, analyses in CELLS.items():
            assert not analyses.get(analysis, {}).get('V'), (device, analysis)


def test_wide_tolerance_cells_are_recorded():
    # BSIM3 transient compares at 2e-1; it must not look like a 1e-3 claim.
    assert sm.WIDE.get(('BSIM3', 'transient'), 0) > 1e-3
