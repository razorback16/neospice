#!/usr/bin/env python3
"""Derive the device x analysis support matrix from test evidence.

The matrix is *derived*, never hand-maintained: every cell is backed by named
tests in this repository, and the tier of each cell is decided by what those
tests actually do, not by what the prose says the simulator supports.

Tiers
-----
V  reference-verified  a test calls the pinned ngspice 47 reference for this
                       analysis and asserts against its result
~  exercised only      a test runs this analysis and asserts something, but no
                       ngspice reference result is involved (self-consistency)
X  unsupported         the combination fails explicitly, by assertion
-  no coverage         no test exercises it

`-` cells are the residual surface for the milestone 3 item-5 hazard (silent
acceptance of an unsupported combination). Enumerating them is the point.

Usage: tools/support_matrix.py [--check] > docs/support-matrix.md
"""
from __future__ import annotations
import argparse, json, re, subprocess, sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TESTS = ROOT / 'tests'

# Analyses as the engine exposes them, in the order capabilities.md lists them.
ANALYSES = ['dc_op', 'dc_sweep', 'transient', 'ac', 'noise',
            'tf', 'sens', 'pz', 'four']

# run_* call -> analysis column. run_dc covers the operating point.
RUN_TO_ANALYSIS = {
    'run_dc': 'dc_op', 'run_dc_sweep': 'dc_sweep', 'run_transient': 'transient',
    'run_ac': 'ac', 'run_noise': 'noise', 'run_tf': 'tf', 'run_sens': 'sens',
    'run_pz': 'pz', 'run_four': 'four', 'run_fourier': 'four',
}

# Device directories under tests/devices map to a display name. Devices with no
# directory of their own are attributed through the fixtures their tests load.
DEVICE_DIRS = {
    'asrc': 'B (ASRC)', 'bjt': 'BJT (Gummel-Poon)', 'bsim3': 'BSIM3',
    'bsim3v32': 'BSIM3v32', 'bsim4v7': 'BSIM4v7', 'bsimsoi': 'BSIMSOI',
    'dio': 'Diode', 'hfet1': 'HFET1', 'hfet2': 'HFET2', 'hisim2': 'HiSIM2',
    'hisimhv': 'HiSIM_HV', 'jfet2': 'JFET2', 'ltra': 'O (LTRA)', 'mes': 'MES',
    'mos1': 'MOS1', 'mos2': 'MOS2', 'mos3': 'MOS3', 'mos9': 'MOS9',
    'vbic': 'VBIC', 'vdmos': 'VDMOS',
}

# Devices with reference coverage but no directory of their own.
EXTRA_DEVICES = ['JFET', 'K (mutual inductance)']

# Reference-calling test files outside tests/devices/. Every such file must be
# listed here or the tool refuses to run, so a new reference test cannot quietly
# fall outside the matrix. A file maps either to the devices it isolates, or to
# CIRCUIT, meaning it exercises a multi-device circuit or the harness itself and
# so supports no per-device claim.
CIRCUIT = object()
NON_DEVICE_FILES = {
    'tests/unit/test_bjt.cpp': ['BJT (Gummel-Poon)'],
    'tests/unit/test_kicad_bc547a.cpp': ['BJT (Gummel-Poon)'],
    'tests/unit/test_bjt_jfet_noise.cpp': ['BJT (Gummel-Poon)', 'JFET'],
    'tests/unit/test_bsim4v7_ac.cpp': ['BSIM4v7'],
    'tests/unit/test_coupled_inductor.cpp': ['K (mutual inductance)'],
    'tests/unit/test_noise.cpp': CIRCUIT,
    'tests/unit/test_noise_temp.cpp': CIRCUIT,
    'tests/unit/test_noise_flicker.cpp': CIRCUIT,
    'tests/unit/test_dc_sweep.cpp': CIRCUIT,
    'tests/unit/test_ths4131.cpp': CIRCUIT,
    'tests/unit/test_kicad_lm358_ns.cpp': CIRCUIT,
    'tests/unit/test_kicad_opa1632.cpp': CIRCUIT,
    'tests/unit/test_ngspice_compare.cpp': CIRCUIT,
    'tests/unit/test_paired_measurement.cpp': CIRCUIT,
    'tests/unit/test_parallel_sweep.cpp': CIRCUIT,
    'tests/unit/test_gradient.cpp': CIRCUIT,
    'tests/unit/test_rff70n06.cpp': CIRCUIT,
    'tests/framework/ngspice_runner.cpp': CIRCUIT,
    'tests/wasm/gallery_reference.cpp': CIRCUIT,
}

TEST_RE = re.compile(r'^\s*TEST(_F|_P)?\s*\(\s*([A-Za-z0-9_]+)\s*,\s*([A-Za-z0-9_]+)\s*\)',
                     re.M)


def split_tests(text: str):
    """Yield (suite, name, body) for each gtest block, plus the file preamble.

    The preamble (everything before the first TEST) carries fixture SetUp() and
    file-local helpers, which is where reference calls often hide.
    """
    marks = [(m.start(), m.group(2), m.group(3)) for m in TEST_RE.finditer(text)]
    if not marks:
        return text, []
    preamble = text[:marks[0][0]]
    out = []
    for i, (start, suite, name) in enumerate(marks):
        end = marks[i + 1][0] if i + 1 < len(marks) else len(text)
        out.append((suite, name, text[start:end]))
    return preamble, out


# A reference call is qualified by an NgspiceRunner-typed receiver. Anything
# else calling run_* is the engine under test; punctuation is not the
# discriminator, because neospice is also called as `sim.run_dc(...)`.
REFERENCE_RECEIVER = r'(?:ngspice_?|reference_?runner|reference|ng)\s*(?:->|\.)\s*'


def analyses_in(body: str, reference: bool):
    """Analyses reached in `body`, restricted to reference calls when asked."""
    found = set()
    for call, analysis in RUN_TO_ANALYSIS.items():
        pat = (REFERENCE_RECEIVER + call + r'\b') if reference else (call + r'\b')
        if re.search(pat, body):
            found.add(analysis)
    return found


TRIVIAL_RE = re.compile(r'(?:EXPECT|ASSERT)_TRUE\s*\(\s*(?:true|1)\s*\)|\bSUCCEED\s*\(\s*\)')


def real_assertions(body: str) -> int:
    """Assertions that can actually fail.

    A test whose only assertion is EXPECT_TRUE(true) proves nothing, however
    much work its body does. One such test exists (a BSIM4v7 audit that runs a
    reference comparison at a 1e30 tolerance and prints the margins to stderr).
    It must not be counted as evidence of agreement.
    """
    return len(re.findall(r'\b(?:EXPECT|ASSERT)_[A-Z_]+', TRIVIAL_RE.sub('', body)))


def asserts(body: str) -> bool:
    return real_assertions(body) > 0


ASSERT_CALL_RE = re.compile(r'\b(?:EXPECT|ASSERT)_[A-Z_]+\s*\(')


def assertion_args(body: str):
    return [_balanced(body, m.end(), '(', ')') for m in ASSERT_CALL_RE.finditer(body)]


def reference_ids(body: str):
    """Identifiers bound to the result of a reference call.

    Matches both `auto x = ng.run_dc(...)` and a bare reassignment to a
    previously declared variable, which the DC sweep tests use. One level of
    transitivity is followed, because those tests bind the reference's current
    vector to a second name before comparing.
    """
    ids = set(re.findall(r'([A-Za-z_]\w*)\s*=\s*' + REFERENCE_RECEIVER + r'run_[a-z_]+',
                         body))
    for _ in range(2):
        for m in re.finditer(r'([A-Za-z_]\w*)\s*=\s*([^;]+);', body):
            name, expr = m.group(1), m.group(2)
            if name in ids:
                continue
            if any(re.search(r'\b' + re.escape(i) + r'\b', expr) for i in ids):
                ids.add(name)
    return ids


def reference_result_is_asserted(body: str) -> bool:
    """Whether the reference result actually enters a comparison.

    Merely calling the reference and asserting something is not agreement: a
    test may assert `reference.status.converged` as a precondition and then
    check only its own output. A cell is reference-verified when the body runs
    a comparator, or when an assertion names the value bound to the reference.
    """
    if re.search(r'\bcompare_[a-z_]+\s*\(', body):
        return True
    ids = reference_ids(body)
    if not ids:
        return False
    return any(re.search(r'\b' + re.escape(i) + r'\b', arg)
               for arg in assertion_args(body) for i in ids)


COMPARE_TOL_RE = re.compile(
    r'compare_(dc|transient|ac|noise)\s*\([^;]*?\{\s*([0-9.eE+-]+)\s*,\s*([0-9.eE+-]+)\s*\}', re.S)


def body_tolerances(body: str):
    """Widest relative tolerance used by each comparator kind in this test."""
    out = {}
    for m in COMPARE_TOL_RE.finditer(body):
        kind, rel = m.group(1), _num(m.group(2))
        out[kind] = max(out.get(kind, 0.0), rel)
    return out


def _balanced(body: str, start: int, open_ch: str, close_ch: str) -> str:
    i, depth = start, 1
    while i < len(body) and depth:
        if body[i] == open_ch: depth += 1
        elif body[i] == close_ch: depth -= 1
        i += 1
    return body[start:i]


def rejection_spans(body: str):
    """Text of each span asserting that the engine under test must fail.

    Two idioms are in use and both must be caught: EXPECT_THROW/ASSERT_THROW,
    and a `try { ...; FAIL(); } catch (...)` block. Only the second is used for
    the VDMOS AC and noise rejections.

    An analysis is *rejected* only when the call that must fail is the analysis
    itself. A test that throws on load() and separately compares a supported
    form -- the VDMOS self-heating regression does exactly this -- must not
    have its comparison misread as a rejection.
    """
    out = []
    for m in re.finditer(r'(?:EXPECT|ASSERT)_THROW\s*\(', body):
        out.append(_balanced(body, m.end(), '(', ')'))
    for m in re.finditer(r'\btry\s*\{', body):
        span = _balanced(body, m.end(), '{', '}')
        if re.search(r'\bFAIL\s*\(', span):
            out.append(span)
    return out


# Reference comparisons that are weaker than the tier alone conveys, each read
# by hand. A `V` here is real, but the note says what it does and does not
# cover. The tool fails on an entry that no longer matches any test, so these
# cannot rot, and fails on an unconfirmed comparison that is not listed.
WEAK_COMPARISONS = {
    'LTRAValidation.TransientRLC':
        'does not call compare_transient. It compares v(out) alone, by '
        'hand-rolled interpolation onto the reference time base, against a '
        '0.15 V absolute bound with no relative bound. No other signal and no '
        'branch current is checked.',
    'LTRAValidation.TransientLC':
        'same hand-rolled, v(out)-only, absolute-bound form as TransientRLC.',
}

NON_ASSERTING = []
UNCOMPARED = []
WIDE = {}      # (device, analysis) -> widest relative tolerance seen


def collect():
    """Walk every C++ test file and attribute analyses to devices."""
    cells = defaultdict(lambda: defaultdict(lambda: {'V': [], '~': [], 'X': []}))
    for path in sorted(TESTS.rglob('*.cpp')):
        rel = path.relative_to(ROOT).as_posix()
        parts = path.relative_to(TESTS).parts
        text = path.read_text(errors='replace')
        if parts[0] == 'devices' and len(parts) > 2:
            devices = [DEVICE_DIRS[parts[1]]] if parts[1] in DEVICE_DIRS else []
        elif re.search(REFERENCE_RECEIVER + r'run_[a-z_]+', text):
            mapped = NON_DEVICE_FILES.get(rel)
            if mapped is None:
                raise SystemExit(
                    f'{rel} calls the reference but is not classified in '
                    'NON_DEVICE_FILES; add it so the matrix cannot silently '
                    'omit reference coverage.')
            devices = [] if mapped is CIRCUIT else mapped
        else:
            continue
        if not devices:
            continue
        preamble, tests = split_tests(text)
        # Fixture SetUp and file helpers apply to every test in the file.
        shared_ref = analyses_in(preamble, reference=True)
        for suite, name, body in tests:
            full = f'{suite}.{name}'
            if not asserts(body):
                if analyses_in(body, reference=True):
                    NON_ASSERTING.append((f'{suite}.{name}', rel))
                continue
            all_calls = analyses_in(body, reference=True) | analyses_in(body, reference=False)
            ref = analyses_in(body, reference=True)
            if all_calls and shared_ref:
                ref |= shared_ref          # fixture SetUp() ran the reference
            if full in WEAK_COMPARISONS and ref:
                UNCOMPARED.append((full, rel, sorted(ref)))
            if ref and not reference_result_is_asserted(body):
                if full in WEAK_COMPARISONS:
                    pass
                else:
                    raise SystemExit(
                        f'{full} ({rel}) obtains an ngspice 47 result but no '
                        'assertion depends on it. Either it is not a reference '
                        'comparison, or it compares in a form this tool cannot '
                        'see. Read it and add it to MANUAL_V with a note, or fix '
                        'the test.')
            rejected = set()
            for span in rejection_spans(body):
                if re.search(REFERENCE_RECEIVER, span):
                    continue          # the reference is expected to fail, not us
                named = analyses_in(span, reference=False)
                if named:
                    rejected |= named
                elif re.search(r'\brun\s*\(', span):
                    # A generic run() dispatches on the netlist. Attribute the
                    # rejection to whatever analysis the reference ran here.
                    rejected |= ref
            tols = body_tolerances(body)
            for device in devices:
                for a in ref - rejected:
                    kind = {'dc_op': 'dc', 'dc_sweep': 'dc'}.get(a, a)
                    if kind in tols:
                        k = (device, a)
                        WIDE[k] = max(WIDE.get(k, 0.0), tols[kind])
                for a in rejected:
                    cells[device][a]['X'].append((full, rel))
                for a in ref - rejected:
                    cells[device][a]['V'].append((full, rel))
                for a in all_calls - ref - rejected:
                    cells[device][a]['~'].append((full, rel))
    return cells


def tolerances():
    """Literal tolerance arguments to compare_* calls, per file."""
    out = defaultdict(set)
    pat = re.compile(r'compare_(dc|transient|ac|noise)\s*\([^;]*?\{\s*([0-9.eE+-]+)\s*,\s*([0-9.eE+-]+)\s*\}',
                     re.S)
    for path in sorted(TESTS.rglob('*.cpp')):
        text = path.read_text(errors='replace')
        for m in pat.finditer(text):
            out[(path.relative_to(ROOT).as_posix(), m.group(1))].add(
                (m.group(2), m.group(3)))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--check', action='store_true',
                    help='exit non-zero if the committed matrix is out of date')
    ap.add_argument('--json', action='store_true')
    args = ap.parse_args()
    cells = collect()
    seen = {n for n, _, _ in UNCOMPARED}
    stale = sorted(set(WEAK_COMPARISONS) - seen)
    if stale:
        raise SystemExit(f'WEAK_COMPARISONS lists tests that no longer back any '
                         f'cell: {stale}. Remove them or fix the note.')
    tol = tolerances()
    doc = render(cells, tol)
    target = ROOT / 'docs/support-matrix.md'
    if args.check:
        current = target.read_text() if target.exists() else ''
        if current.rstrip() != doc.rstrip():
            print('docs/support-matrix.md is stale; regenerate with '
                  'tools/support_matrix.py > docs/support-matrix.md', file=sys.stderr)
            return 1
        return 0
    if args.json:
        json.dump({d: {a: {t: v for t, v in s.items() if v}
                       for a, s in ax.items()} for d, ax in cells.items()},
                  sys.stdout, indent=1)
        return 0
    print(doc)
    return 0


def render(cells, tol):
    names = sorted(set(DEVICE_DIRS.values()) | set(EXTRA_DEVICES))
    w = max(len(d) for d in names)
    cw = {a: max(len(a), 3) for a in ANALYSES}
    L = []
    L.append('## Device x analysis support matrix')
    L.append('')
    L.append('**Generated by `tools/support_matrix.py`. Do not edit by hand.**')
    L.append('Regenerate with `tools/support_matrix.py > docs/support-matrix.md`;')
    L.append('`--check` fails if this file is stale.')
    L.append('')
    L.append('Every cell is decided by what the tests in this repository actually')
    L.append('do, not by what the prose claims. The reference is ngspice 47 only.')
    L.append('')
    L.append('| Tier | Meaning |')
    L.append('| --- | --- |')
    L.append('| `V` | **Reference-verified.** A test runs this analysis and asserts '
             'against a result obtained from ngspice 47. |')
    L.append('| `~` | **Exercised only.** A test runs this analysis and asserts '
             'something, but no ngspice result is involved. This is '
             'self-consistency, not agreement. |')
    L.append('| `X` | **Unsupported, and fails explicitly.** A test asserts that '
             'the combination raises rather than returning a result. |')
    L.append('| `-` | **No coverage.** No test exercises it. |')
    L.append('| `V*` | Reference-verified, but the widest backing tolerance is '
             'looser than the 1e-3 relative the corpus comparison uses. The '
             'value is given below. |')
    L.append('')
    L.append('| ' + 'Device'.ljust(w) + ' | ' +
             ' | '.join(a.ljust(cw[a]) for a in ANALYSES) + ' |')
    L.append('| ' + '-' * w + ' | ' +
             ' | '.join('-' * cw[a] for a in ANALYSES) + ' |')
    for device in names:
        row = []
        for a in ANALYSES:
            st = cells.get(device, {}).get(a, {})
            mark = ('V' if st.get('V') else 'X' if st.get('X')
                    else '~' if st.get('~') else '-')
            if mark == 'V' and WIDE.get((device, a), 0.0) > 1e-3:
                mark = 'V*'
            row.append(mark.ljust(cw[a]))
        L.append('| ' + device.ljust(w) + ' | ' + ' | '.join(row) + ' |')
    L.append('')
    L.append('### What this table does not cover')
    L.append('')
    L.append('Rows are device *models* that have isolated tests, or tests')
    L.append('attributable to them by file. Twelve device types listed in')
    L.append('`docs/capabilities.md` have no row at all: R, C, L, V, I, E, G, F,')
    L.append('H, S, W and the lossless T line. They are exercised, but only')
    L.append('inside multi-device circuit tests, which support no per-device')
    L.append('claim and so contribute to no cell:')
    L.append('')
    for f in sorted(k for k, v in NON_DEVICE_FILES.items() if v is CIRCUIT):
        L.append(f'- `{f}`')
    L.append('')
    L.append('So "of N cells" below counts only the rows present. It is not a')
    L.append('coverage figure for the simulator as a whole.')
    L.append('')
    L.append('### What the empty columns mean')
    L.append('')
    L.append('`tf`, `sens`, `pz` and `four` are `-` for every device, and that is')
    L.append('not an oversight in the test suite alone: `NgspiceRunner`')
    L.append('(`tests/framework/ngspice_runner.hpp`) exposes only `run_dc`,')
    L.append('`run_dc_sweep`, `run_transient`, `run_ac` and `run_noise`. These')
    L.append('columns therefore have no per-device reference coverage.')
    L.append('Separately, `Gradient.MatchesNgspice47Sensitivity` compares the')
    L.append('adjoint API with ngspice 47 `.sens` through `NgspiceLib`. That')
    L.append('circuit-level test does not verify `run_sens()` or fill these cells.')
    L.append('`tests/unit/test_pz.cpp` and')
    L.append('`tests/unit/test_fourier.cpp` exist and pass, but assert against')
    L.append('analytically derived values, not the reference. `docs/capabilities.md`')
    L.append('lists all four as analysis entry points; that is a statement about')
    L.append('implementation, and this table is the statement about verification.')
    L.append('')
    L.append('### The `-` cells are the residual silent-acceptance surface')
    L.append('')
    L.append('Milestone 3 item 5 forbids returning an apparently valid result for')
    L.append('something that is not supported. A `-` cell is precisely a')
    L.append('combination that no test has ever run against the reference, so')
    L.append('nothing establishes whether it agrees, fails loudly, or returns a')
    L.append('plausible wrong answer. Enumerating them is the point of this table.')
    L.append('')
    counts = {t: 0 for t in 'V~X-'}
    for device in names:
        for a in ANALYSES:
            st = cells.get(device, {}).get(a, {})
            counts['V' if st.get('V') else 'X' if st.get('X')
                   else '~' if st.get('~') else '-'] += 1
    total = len(names) * len(ANALYSES)
    L.append(f'Of {total} cells: **{counts["V"]} reference-verified**, '
             f'{counts["~"]} exercised only, {counts["X"]} explicitly rejected, '
             f'**{counts["-"]} with no coverage**.')
    L.append('')
    L.append('### Tolerances wider than the corpus comparison')
    L.append('')
    L.append('A `V` is not a uniform claim. Every cell below is reference-verified,')
    L.append('but at a tolerance looser than the 1e-3 relative bound the corpus')
    L.append('comparison uses, so it must not be read as the same strength of')
    L.append('agreement. The value is the widest relative tolerance any backing')
    L.append('test passes to a comparator.')
    L.append('')
    L.append('| Device | Analysis | Widest relative tolerance |')
    L.append('| --- | --- | --- |')
    for (device, a), v in sorted(WIDE.items()):
        if v > 1e-3:
            L.append(f'| {device} | {a} | **{v:g}** |')
    L.append('')
    if UNCOMPARED:
        L.append('### Reference comparisons weaker than the tier conveys')
        L.append('')
        L.append('These are genuine `V` cells, but a `V` should not hide what the')
        L.append('comparison actually covers. Each was read rather than inferred.')
        L.append('')
        for name, f, ax in sorted(set((n, f, tuple(a)) for n, f, a in UNCOMPARED)):
            L.append(f'- `{name}` (`{f}`, {", ".join(ax)}): {WEAK_COMPARISONS[name]}')
        L.append('')
    L.append('### Model-form restrictions not visible as a cell')
    L.append('')
    L.append('A cell is a device/analysis pair, so a restriction on a *form* of a')
    L.append('device does not appear as one. Known restrictions:')
    L.append('')
    L.append('- **VDMOS self-heating** is rejected at parse time, so it never')
    L.append('  reaches an analysis. See `docs/vdmos-compatibility.md`. Every')
    L.append('  isothermal form still runs.')
    L.append('')
    L.append('### Backing tests')
    L.append('')
    for device in names:
        rows = []
        for a in ANALYSES:
            st = cells.get(device, {}).get(a, {})
            for tier in ('V', 'X', '~'):
                for name, f in sorted(set(st.get(tier, []))):
                    rows.append(f'| {a} | `{tier}` | `{name}` | `{f}` |')
        if not rows:
            continue
        L.append(f'#### {device}')
        L.append('')
        L.append('| Analysis | Tier | Test | File |')
        L.append('| --- | --- | --- | --- |')
        L.extend(rows)
        L.append('')
    return '\n'.join(L)


def _num(x):
    try: return float(x)
    except ValueError: return 0.0


if __name__ == '__main__':
    sys.exit(main() or 0)
