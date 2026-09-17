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
    'tests/unit/test_rff70n06.cpp': CIRCUIT,
    'tests/framework/ngspice_runner.cpp': CIRCUIT,
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


NON_ASSERTING = []


def collect():
    """Walk every C++ test file and attribute analyses to devices."""
    cells = defaultdict(lambda: defaultdict(lambda: {'V': [], '~': [], 'X': []}))
    for path in sorted(TESTS.rglob('*.cpp')):
        rel = path.relative_to(ROOT).as_posix()
        parts = path.relative_to(TESTS).parts
        text = path.read_text(errors='replace')
        if parts[0] == 'devices' and len(parts) > 2:
            devices = [DEVICE_DIRS[parts[1]]] if parts[1] in DEVICE_DIRS else []
        elif 'NgspiceRunner' in text:
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
            for device in devices:
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
            row.append(mark.ljust(cw[a]))
        L.append('| ' + device.ljust(w) + ' | ' + ' | '.join(row) + ' |')
    L.append('')
    L.append('### What the empty columns mean')
    L.append('')
    L.append('`tf`, `sens`, `pz` and `four` are `-` for every device, and that is')
    L.append('not an oversight in the test suite alone: `NgspiceRunner`')
    L.append('(`tests/framework/ngspice_runner.hpp`) exposes only `run_dc`,')
    L.append('`run_dc_sweep`, `run_transient`, `run_ac` and `run_noise`. There is')
    L.append('**no path by which any `.tf`, `.sens`, `.pz` or `.four` result has')
    L.append('ever been compared against ngspice 47.** `tests/unit/test_pz.cpp` and')
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
    L.append('### Tolerances')
    L.append('')
    L.append('A `V` is not a uniform claim. These are the literal tolerances passed')
    L.append('to `compare_*` per file; anything wider than the corpus formula')
    L.append('(1e-3 relative) is a weaker claim and is listed here so it cannot be')
    L.append('read as equivalent.')
    L.append('')
    L.append('| File | Comparator | (relative, absolute) |')
    L.append('| --- | --- | --- |')
    for (f, kind), vals in sorted(tol.items()):
        for rel, absolute in sorted(vals):
            flag = ' **wider than 1e-3**' if _num(rel) > 1e-3 else ''
            L.append(f'| `{f}` | `compare_{kind}` | ({rel}, {absolute}){flag} |')
    L.append('')
    if NON_ASSERTING:
        L.append('### Reference comparisons that cannot fail')
        L.append('')
        L.append('These tests obtain an ngspice 47 result and compare against it,')
        L.append('but assert nothing that can fail, so they are excluded from the')
        L.append('matrix. They still count toward the suite total, which is why')
        L.append('they are named here rather than silently dropped.')
        L.append('')
        for name, f in sorted(set(NON_ASSERTING)):
            L.append(f'- `{name}` (`{f}`) -- compares at a 1e30 tolerance and')
            L.append('  asserts `EXPECT_TRUE(true)`; it prints margins to stderr.')
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
