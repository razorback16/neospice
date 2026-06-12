#!/usr/bin/env python3
"""
Compare neospice vs ngspice on KiCad SPICE model library.

For each model/subcircuit, generates a minimal .op test circuit, runs both
simulators, parses the binary .raw output, and compares node voltages and
branch currents.

Result categories:
  MATCH      — both converge and all values agree within tolerance
  MISMATCH   — both converge but values differ beyond tolerance
  NEO_ONLY   — neospice converges, ngspice fails
  NG_ONLY    — ngspice converges, neospice fails
  BOTH_FAIL  — neither simulator converges

Usage:
    python3 tools/compare_kicad_models.py [options]
    python3 tools/compare_kicad_models.py --max 100 --verbose
    python3 tools/compare_kicad_models.py --file LinearTech --save results.json
"""

import argparse
import json
import os
import re
import struct
import subprocess
import sys
import tempfile
import time
from collections import defaultdict
from concurrent.futures import ProcessPoolExecutor, as_completed
from pathlib import Path

# Reuse test circuit generation from the existing test script
sys.path.insert(0, str(Path(__file__).parent))
from test_kicad_models import (
    extract_models, extract_subcircuits, make_model_test,
    make_subcircuit_test, KICAD_LIB
)

# Standard SPICE tolerances
RELTOL = 1e-3    # 0.1%
VNTOL  = 1e-6    # 1 uV
ABSTOL = 1e-9    # 1 nA (SPICE ITOL default)


def _parse_raw_plot(header_text, binary):
    """Parse a single plot's header + binary payload.

    Returns (plotname, flags, values_dict) for the FIRST data point of the
    plot, or None if it can't be parsed. `binary` must start at the byte
    immediately following this plot's "Binary:\n" marker.
    """
    plotname = ''
    flags = ''
    num_vars = 0
    variables = []
    in_vars = False
    for line in header_text.splitlines():
        if line.startswith('Plotname:'):
            plotname = line.split(':', 1)[1].strip()
        elif line.startswith('Flags:'):
            flags = line.split(':', 1)[1].strip().lower()
        elif line.startswith('No. Variables:'):
            num_vars = int(line.split(':')[1].strip())
        elif line.startswith('Variables:'):
            in_vars = True
        elif in_vars:
            parts = line.strip().split('\t')
            if len(parts) >= 3:
                variables.append(parts[1].strip().lower())
            if num_vars and len(variables) >= num_vars:
                break

    if not variables:
        return None

    # Real plots store 8 bytes/value; complex store 16 bytes (re, im).
    complex_plot = 'complex' in flags
    stride = 16 if complex_plot else 8
    values = {}
    for i, name in enumerate(variables):
        offset = i * stride
        if offset + 8 > len(binary):
            break
        # For complex data take the real part (first double) — the op-point
        # path never relies on complex plots, this is only a fallback.
        val = struct.unpack('d', binary[offset:offset + 8])[0]
        values[name] = val

    return plotname, flags, values


def parse_raw_file(path):
    """Parse a SPICE binary .raw file, returning the operating-point values.

    A single .raw may contain MULTIPLE plots (ngspice honors stray file-scope
    .AC/.TRAN cards in included libraries and emits AC + Operating Point +
    Transient plots in one file). We must select the Operating Point plot, not
    blindly take the first one (which is often a stimulus-free AC plot full of
    zeros/denormals).

    Selection order:
      1. the plot whose Plotname starts with "operating point"
      2. else the first real-valued (non-complex) plot
      3. else the first plot

    Returns dict mapping variable name (lowercase) to the first data point's
    value, or None if the file can't be parsed.
    """
    try:
        with open(path, 'rb') as f:
            data = f.read()
    except Exception:
        return None

    marker = b'Binary:\n'
    plotname_marker = b'Plotname:'

    # Locate every plot block: each starts at a "Plotname:" line and its
    # binary payload starts at the next "Binary:\n".
    plot_starts = [m.start() for m in re.finditer(re.escape(plotname_marker), data)]

    parsed = []  # list of (plotname, flags, values)
    if plot_starts:
        for pi, start in enumerate(plot_starts):
            end = plot_starts[pi + 1] if pi + 1 < len(plot_starts) else len(data)
            block = data[start:end]
            bidx = block.find(marker)
            if bidx < 0:
                continue
            header_text = block[:bidx].decode('utf-8', errors='replace')
            binary = block[bidx + len(marker):]
            res = _parse_raw_plot(header_text, binary)
            if res is not None:
                parsed.append(res)
    else:
        # No Plotname line at all — parse the whole file as one plot.
        idx = data.find(marker)
        if idx < 0:
            return None
        header_text = data[:idx].decode('utf-8', errors='replace')
        binary = data[idx + len(marker):]
        res = _parse_raw_plot(header_text, binary)
        if res is not None:
            parsed.append(res)

    if not parsed:
        return None

    # 1. prefer an Operating Point plot
    for plotname, flags, values in parsed:
        if plotname.lower().startswith('operating point'):
            return values
    # 2. else first real (non-complex) plot
    for plotname, flags, values in parsed:
        if 'complex' not in flags:
            return values
    # 3. else first plot
    return parsed[0][2]


def run_simulator(netlist_text, sim_bin, is_ngspice=False, timeout=10):
    """Run a simulator on a netlist.

    Returns (success, raw_values, stderr_text, elapsed_seconds) where
    elapsed_seconds is the wall-clock time of the simulator subprocess only
    (process startup + simulation), excluding netlist write and raw parsing.
    """
    with tempfile.NamedTemporaryFile(
        mode='w', suffix='.cir', delete=False, dir='/tmp'
    ) as f:
        f.write(netlist_text)
        tmp_cir = f.name

    tmp_raw = tmp_cir.replace('.cir', '.raw')

    try:
        if is_ngspice:
            cmd = [sim_bin, '-D', 'ngbehavior=psa', '-b', tmp_cir, '-r', tmp_raw]
        else:
            cmd = [sim_bin, tmp_cir, '-o', tmp_raw]

        t0 = time.perf_counter()
        result = subprocess.run(
            cmd, capture_output=True, timeout=timeout
        )
        elapsed = time.perf_counter() - t0
        stderr = result.stderr.decode('utf-8', errors='replace')
        stdout = result.stdout.decode('utf-8', errors='replace')
        combined = stdout + stderr

        if result.returncode != 0:
            return False, None, combined.strip(), elapsed

        # For ngspice, also check for "run simulation(s) aborted" or no raw file
        if is_ngspice and ('aborted' in combined.lower()):
            return False, None, combined.strip(), elapsed

        if not os.path.exists(tmp_raw):
            return False, None, 'No raw file produced', elapsed

        values = parse_raw_file(tmp_raw)
        if values is None:
            return False, None, 'Failed to parse raw file', elapsed

        return True, values, combined.strip(), elapsed

    except subprocess.TimeoutExpired:
        return False, None, 'Timeout', float(timeout)
    except Exception as e:
        return False, None, str(e), 0.0
    finally:
        for p in (tmp_cir, tmp_raw):
            try:
                os.unlink(p)
            except OSError:
                pass


def is_internal_var(name):
    """Check if a variable is internal to a subcircuit expansion."""
    # Internal node voltages: v(x1.42), v(x1.foo)
    # Internal branch currents: i(v.x1.vb), i(v.x1.vc)
    return '.x' in name or 'x1.' in name or 'x2.' in name


def compare_values(neo_vals, ng_vals, external_only=False):
    """Compare two sets of operating point values.

    Returns (match, details) where match is True if all common variables
    agree within tolerance, and details is a list of per-variable diffs.
    """
    details = []
    all_match = True

    # Find common variables (skip time/frequency sweep vars)
    common = set(neo_vals.keys()) & set(ng_vals.keys())
    skip = {'time', 'frequency', 'v-sweep'}
    common -= skip

    if external_only:
        common = {v for v in common if not is_internal_var(v)}

    if not common:
        return True, [{'var': '(no common vars)', 'status': 'skip'}]

    for var in sorted(common):
        nv = neo_vals[var]
        gv = ng_vals[var]
        diff = abs(nv - gv)

        # Choose tolerance based on variable type
        if var.startswith('v('):
            tol = RELTOL * max(abs(nv), abs(gv)) + VNTOL
        else:
            tol = RELTOL * max(abs(nv), abs(gv)) + ABSTOL

        ok = diff <= tol
        if not ok:
            all_match = False

        details.append({
            'var': var,
            'neo': nv,
            'ng': gv,
            'diff': diff,
            'tol': tol,
            'ok': ok,
        })

    return all_match, details


# Below this threshold a circuit's entire DC solution is numerical noise: every
# node sits at ~0 V because nothing drives it (an unexcited fixture). ngspice
# returns the same ~0 here when it can parse the deck — see undriven-circuit
# cross-check — so "neospice solved it, ngspice didn't" is NOT a correctness win;
# ngspice merely failed earlier (at parse). Any deck with a real source has that
# source's node >= its value, so this reliably flags only dead fixtures.
TRIVIAL_SOLUTION_V = 1e-6


def is_trivial_solution(vals):
    """True if every node voltage is below TRIVIAL_SOLUTION_V (unexcited deck)."""
    if not vals:
        return False
    vmags = [abs(v) for k, v in vals.items() if k.startswith('v(')]
    if not vmags:
        return False
    return max(vmags) < TRIVIAL_SOLUTION_V


# ---------------------------------------------------------------------------
# Isolated + driven fallback
#
# Many subckt tests come back NEO_ONLY/NEO_TRIVIAL purely because ngspice-psa
# aborts the *entire* .include'd library on one unrelated bad construct
# elsewhere in the file (an unknown device in another subckt, a stray UTF-8
# byte, an undefined PSpice param). The requested part is fine. When that
# happens we retry with (a) the target subckt extracted into a clean,
# dependency-closed library so ngspice can parse it, and (b) a driving source
# injected so an otherwise-unexcited fixture actually exercises the device.
# This rescues the fraction ngspice is technically able to evaluate into real
# MATCH/MISMATCH comparisons; the rest stay NEO_ONLY/NEO_TRIVIAL (ngspice-psa
# genuinely can't handle the PSpice syntax — that is the robustness gap itself).
# ---------------------------------------------------------------------------

def _subckt_blocks(text):
    """Return ({name_lower: [lines]}, {model_lower: line}) for every .subckt
    (including nested) and .model in a library file."""
    blocks, models = {}, {}
    namestack, bufstack = [], []
    for ln in text.splitlines():
        s = ln.strip()
        low = s.lower()
        if low.startswith('.subckt'):
            parts = s.split()
            if len(parts) >= 2:
                namestack.append(parts[1])
                bufstack.append([ln])
        elif low.startswith('.ends'):
            if namestack:
                bufstack[-1].append(ln)
                nm = namestack.pop()
                blk = bufstack.pop()
                blocks[nm.lower()] = blk
                if bufstack:
                    bufstack[-1].extend(blk)
        else:
            if bufstack:
                bufstack[-1].append(ln)
            m = re.match(r'\.model\s+(\S+)', low)
            if m:
                models[m.group(1).lower()] = ln
    return blocks, models


def _x_subckt_name(tokens):
    """The subckt name an X-instance references: the last token before any
    PARAMS:/key=val tail (NOT tokens[-1], which is a param when PARAMS: is used)."""
    body = tokens[1:]
    cut = len(body)
    for i, t in enumerate(body):
        if t.upper() == 'PARAMS:' or '=' in t:
            cut = i
            break
    return body[cut - 1].lower() if cut > 0 else None


def _block_deps(block):
    """Subckt/model names referenced by element lines inside a .subckt block."""
    refs = set()
    for ln in block:
        s = ln.strip()
        if not s or s.startswith('*') or s.startswith('.'):
            continue
        toks = s.split()
        c = s[0].lower()
        if c == 'x':
            nm = _x_subckt_name(toks)
            if nm:
                refs.add(nm)
        elif c in 'qmdjzw' and len(toks) >= 3:
            for t in toks[1:]:
                if re.match(r'^[a-zA-Z]', t) and '=' not in t:
                    refs.add(t.lower())
    return refs


def build_isolated_lib(target, lib_path):
    """Extract `target` subckt + its full dependency closure (nested subckts and
    referenced models) from `lib_path` into a standalone library string.
    Returns (lib_text, found)."""
    try:
        text = Path(lib_path).read_text(errors='replace')
    except Exception:
        return None, False
    blocks, models = _subckt_blocks(text)
    tgt = target.lower()
    if tgt not in blocks:
        return None, False
    want, stack = set(), [tgt]
    while stack:
        n = stack.pop()
        if n in want or n not in blocks:
            continue
        want.add(n)
        for dep in _block_deps(blocks[n]):
            if dep in blocks and dep not in want:
                stack.append(dep)
    needed_models = set()
    for n in want:
        for dep in _block_deps(blocks[n]):
            if dep in models:
                needed_models.add(dep)
    out = [models[m] for m in sorted(needed_models)]
    for n in want:
        out += blocks[n] + ['']
    return "\n".join(out), True


def make_isolated_driven_netlist(netlist):
    """Transform a generated subckt test into an isolated + driven version:
      * replace the whole-library .include with an extracted, dependency-closed lib
      * if the fixture is undriven, inject a 5 V source (through 1k) on the first
        generic net so the device is actually exercised
    Returns (new_netlist, iso_lib_text) or (None, None) if it can't be built."""
    inc = re.search(r'\.include\s+"([^"]+)"', netlist)
    xline = re.search(r'(?m)^[Xx]\S+\s+(.+)$', netlist)
    if not inc or not xline:
        return None, None
    subname = _x_subckt_name(('X ' + xline.group(1)).split())
    if not subname:
        return None, None
    iso_lib, found = build_isolated_lib(subname, inc.group(1))
    if not found:
        return None, None

    lines = netlist.splitlines()
    out = []
    already_driven = any(
        re.match(r'^(VIN|VCC|VEE|VSTIM|ISTIM)\b', l, re.IGNORECASE) for l in lines)
    injected = False
    for l in lines:
        if l.strip().lower().startswith('.include'):
            out.append('.include "__ISO_LIB__"')
            continue
        m = re.match(r'^R_\S+\s+(net_\S+)\s+0\s+100k\s*$', l)
        if m and not already_driven and not injected:
            out.append('VSTIM stim 0 DC 5')
            out.append(f'RSTIM stim {m.group(1)} 1k')
            injected = True
            continue
        out.append(l)
    return "\n".join(out) + "\n", iso_lib


def run_one_test(args_tuple):
    """Worker function for parallel execution. Returns a result dict."""
    kind, name, info, netlist, rel_path, neo_bin, ng_bin, ext_only = args_tuple

    neo_ok, neo_vals, neo_err, neo_time = run_simulator(netlist, neo_bin, is_ngspice=False)
    ng_ok, ng_vals, ng_err, ng_time = run_simulator(netlist, ng_bin, is_ngspice=True)

    # Fallback: if ngspice failed (almost always a whole-library parse abort) on
    # a subckt test, retry with the target isolated into a clean lib and driven.
    # Only adopt the isolated run if BOTH simulators then succeed — that turns a
    # NEO_ONLY/NEO_TRIVIAL non-comparison into a real MATCH/MISMATCH. Otherwise we
    # keep the original result (ngspice genuinely can't evaluate the PSpice deck).
    isolated = False
    if neo_ok and not ng_ok and kind == 'subckt':
        iso_netlist, iso_lib = make_isolated_driven_netlist(netlist)
        if iso_netlist and iso_lib:
            libf = None
            try:
                with tempfile.NamedTemporaryFile(
                        mode='w', suffix='.lib', delete=False, dir='/tmp') as lf:
                    lf.write(iso_lib)
                    libf = lf.name
                iso_netlist = iso_netlist.replace('__ISO_LIB__', libf)
                i_neo_ok, i_neo_vals, i_neo_err, i_neo_t = run_simulator(
                    iso_netlist, neo_bin, is_ngspice=False)
                i_ng_ok, i_ng_vals, i_ng_err, i_ng_t = run_simulator(
                    iso_netlist, ng_bin, is_ngspice=True)
                if i_neo_ok and i_ng_ok:
                    isolated = True
                    netlist = iso_netlist
                    neo_ok, neo_vals, neo_err, neo_time = i_neo_ok, i_neo_vals, i_neo_err, i_neo_t
                    ng_ok, ng_vals, ng_err, ng_time = i_ng_ok, i_ng_vals, i_ng_err, i_ng_t
            finally:
                if libf:
                    try:
                        os.unlink(libf)
                    except OSError:
                        pass

    if neo_ok and ng_ok:
        match, details = compare_values(neo_vals, ng_vals, external_only=ext_only)
        status = 'MATCH' if match else 'MISMATCH'
        mismatches = [d for d in details if not d.get('ok', True)]
    elif neo_ok and not ng_ok:
        # neospice ran where ngspice failed. Only a real win if neospice
        # actually simulated something — an all-~0 (unexcited) solution is a
        # meaningless "win" (ngspice would also return ~0 if it could parse).
        status = 'NEO_TRIVIAL' if is_trivial_solution(neo_vals) else 'NEO_ONLY'
        details = []
        mismatches = []
    elif not neo_ok and ng_ok:
        status = 'NG_ONLY'
        details = []
        mismatches = []
    else:
        status = 'BOTH_FAIL'
        details = []
        mismatches = []

    return {
        'name': name,
        'kind': kind,
        'info': info,
        'file': rel_path,
        'status': status,
        'isolated': isolated,
        'neo_ok': neo_ok,
        'ng_ok': ng_ok,
        'neo_err': neo_err if not neo_ok else '',
        'ng_err': ng_err if not ng_ok else '',
        'neo_time': neo_time,
        'ng_time': ng_time,
        'mismatches': mismatches,
        'netlist': netlist,
    }


def main():
    parser = argparse.ArgumentParser(
        description='Compare neospice vs ngspice on KiCad SPICE models'
    )
    parser.add_argument(
        '--neospice',
        default=str(Path(__file__).parent.parent / 'build' / 'neospice'),
        help='Path to neospice binary',
    )
    parser.add_argument(
        '--ngspice',
        default='ngspice',
        help='Path to ngspice binary',
    )
    parser.add_argument('--category', default=None,
                        help='Only test a specific category (Diode, Transistor, etc.)')
    parser.add_argument('--file', default=None,
                        help='Only test models from files matching this substring')
    parser.add_argument('--max', type=int, default=0,
                        help='Max models to test (0=all)')
    parser.add_argument('--verbose', '-v', action='store_true',
                        help='Show details for each test')
    parser.add_argument('--mismatches-only', action='store_true',
                        help='Only show MISMATCH and NG_ONLY results')
    parser.add_argument('--save', default=None,
                        help='Save results to JSON file')
    parser.add_argument('--jobs', '-j', type=int, default=8,
                        help='Number of parallel workers (default: 8)')
    parser.add_argument('--dump-netlist', action='store_true',
                        help='Print netlist for mismatched/failing tests')
    parser.add_argument('--external-only', action='store_true', default=True,
                        help='Only compare external nodes (default; skip subcircuit-internal variables)')
    parser.add_argument('--include-internal', action='store_true',
                        help='Include subcircuit-internal nodes in comparison')
    parser.add_argument('--baseline', default=None,
                        help='Load previous neospice results JSON; skip neospice run, '
                             'only run ngspice on passing tests')
    args = parser.parse_args()

    # Verify binaries exist
    neo_bin = args.neospice
    ng_bin = args.ngspice
    if not os.path.exists(neo_bin):
        print(f"Error: neospice binary not found at {neo_bin}", file=sys.stderr)
        sys.exit(1)

    ng_path = subprocess.run(['which', ng_bin], capture_output=True).stdout.decode().strip()
    if not ng_path:
        print(f"Error: ngspice not found ({ng_bin})", file=sys.stderr)
        sys.exit(1)
    ng_bin = ng_path

    print(f"neospice: {neo_bin}")
    print(f"ngspice:  {ng_bin}")
    print()

    # Collect test circuits
    if args.baseline:
        # Load from baseline: only re-test passing neospice results
        with open(args.baseline) as f:
            baseline = json.load(f)
        all_tests = []
        for r in baseline['results']:
            if r['status'] in ('OK', 'WARNING'):
                all_tests.append((
                    r['kind'], r['name'], r['info'], r['netlist'], r['file']
                ))
        print(f"Loaded {len(all_tests)} passing tests from baseline {args.baseline}")
    else:
        # Generate fresh test circuits
        model_files = []
        for ext in ('*.lib', '*.mod', '*.sub', '*.spice', '*.cir'):
            if args.category:
                pattern = f"**/{args.category}/**/{ext}"
            else:
                pattern = f"**/{ext}"
            model_files.extend(KICAD_LIB.glob(pattern))

        model_files = sorted(set(model_files))
        print(f"Found {len(model_files)} model files")

        all_tests = []
        for mf in model_files:
            for name, mtype, fpath in extract_models(mf):
                netlist = make_model_test(name, mtype, fpath)
                if netlist:
                    rel_path = str(mf.relative_to(KICAD_LIB))
                    all_tests.append(('model', name, mtype, netlist, rel_path))

            for name, ports, fpath, roles, params in extract_subcircuits(mf):
                netlist = make_subcircuit_test(name, ports, fpath, roles, params)
                if netlist:
                    rel_path = str(mf.relative_to(KICAD_LIB))
                    all_tests.append((
                        'subckt', name, f'{len(ports)}-port', netlist, rel_path
                    ))

    if args.file:
        all_tests = [t for t in all_tests if args.file.lower() in t[4].lower()]

    if args.max > 0:
        all_tests = all_tests[:args.max]

    total = len(all_tests)
    print(f"Running {total} test circuits through both simulators...")
    print()

    # Prepare worker args
    ext_only = not args.include_internal
    work_items = [
        (*t, neo_bin, ng_bin, ext_only) for t in all_tests
    ]

    # Run in parallel
    stats = defaultdict(int)
    all_results = []
    mismatch_details = []
    completed = 0

    with ProcessPoolExecutor(max_workers=args.jobs) as executor:
        futures = {
            executor.submit(run_one_test, item): i
            for i, item in enumerate(work_items)
        }

        for future in as_completed(futures):
            result = future.result()
            all_results.append(result)
            stats[result['status']] += 1
            completed += 1

            if result['status'] == 'MISMATCH':
                mismatch_details.append(result)

            # Display logic
            show = False
            if args.verbose:
                show = True
            elif args.mismatches_only:
                show = result['status'] in ('MISMATCH', 'NG_ONLY')
            elif result['status'] not in ('MATCH', 'BOTH_FAIL', 'NEO_TRIVIAL'):
                show = True

            if show:
                tag = result['status']
                print(f"[{tag:11s}] {result['kind']:7s} {result['name']:30s} ({result['file']})")
                if result['status'] == 'MISMATCH' and result['mismatches']:
                    for m in result['mismatches'][:3]:
                        print(f"             {m['var']}: neo={m['neo']:.6g} ng={m['ng']:.6g} "
                              f"diff={m['diff']:.3g} tol={m['tol']:.3g}")
                if result['status'] == 'NG_ONLY' and result['neo_err']:
                    err_line = result['neo_err'].split('\n')[0][:120]
                    print(f"             neo: {err_line}")
                if args.dump_netlist and result['status'] in ('MISMATCH', 'NG_ONLY'):
                    print("--- NETLIST ---")
                    print(result['netlist'])
                    print("--- END ---")

            # Progress
            if completed % 200 == 0:
                print(f"  ... {completed}/{total} compared", file=sys.stderr)

    # Sort results by name for deterministic output
    all_results.sort(key=lambda r: (r['file'], r['name']))

    # Summary
    print()
    print("=" * 75)
    print("COMPARISON SUMMARY")
    print("=" * 75)
    for status in ('MATCH', 'MISMATCH', 'NEO_ONLY', 'NEO_TRIVIAL',
                   'NG_ONLY', 'BOTH_FAIL'):
        if stats[status]:
            pct = 100.0 * stats[status] / total
            bar = '#' * int(pct / 2)
            print(f"  {status:11s}: {stats[status]:5d} / {total}  ({pct:5.1f}%)  {bar}")

    match_rate = 100.0 * stats['MATCH'] / total if total else 0
    # NEO_TRIVIAL is excluded from "agreement": it is neither a value match nor a
    # demonstrated win — neospice merely solved an unexcited deck to ~0 V while
    # ngspice failed to parse it. Counting it as agreement would inflate the rate.
    agree_rate = 100.0 * (stats['MATCH'] + stats['NEO_ONLY'] + stats['BOTH_FAIL']) / total if total else 0

    print()
    print(f"Exact match rate:  {match_rate:.1f}% ({stats['MATCH']}/{total})")
    print(f"  (both converge and values agree within reltol={RELTOL}, vntol={VNTOL}, abstol={ABSTOL})")
    print()
    print(f"NG_ONLY (ngspice passes, neospice fails): {stats['NG_ONLY']}")
    print(f"  These represent genuine neospice gaps.")
    print(f"NEO_ONLY (neospice passes, ngspice fails): {stats['NEO_ONLY']}")
    print(f"  Non-trivial solves where neospice is more robust than ngspice.")
    print(f"NEO_TRIVIAL (neospice solves ~0, ngspice fails): {stats['NEO_TRIVIAL']}")
    print(f"  Unexcited fixtures (all nodes < {TRIVIAL_SOLUTION_V} V) — not a win:")
    print(f"  ngspice returns the same ~0 when it can parse; it merely failed at parse.")

    # Mismatch breakdown by file
    if mismatch_details:
        print()
        print("MISMATCHES BY FILE:")
        print("-" * 75)
        file_mm = defaultdict(list)
        for r in mismatch_details:
            file_mm[r['file']].append(r['name'])
        for path, names in sorted(file_mm.items(), key=lambda x: -len(x[1]))[:20]:
            print(f"  [{len(names):3d}x] {path}")
            if args.verbose:
                for n in names[:5]:
                    print(f"         - {n}")

    # NG_ONLY breakdown
    ng_only = [r for r in all_results if r['status'] == 'NG_ONLY']
    if ng_only:
        print()
        print("NG_ONLY BY FILE (neospice fails, ngspice passes):")
        print("-" * 75)
        file_ng = defaultdict(list)
        for r in ng_only:
            file_ng[r['file']].append(r['name'])
        for path, names in sorted(file_ng.items(), key=lambda x: -len(x[1]))[:20]:
            print(f"  [{len(names):3d}x] {path}")

    # Worst mismatches
    if mismatch_details:
        print()
        print("WORST MISMATCHES (by relative error):")
        print("-" * 75)
        worst = []
        for r in mismatch_details:
            for m in r.get('mismatches', []):
                rel_err = m['diff'] / max(abs(m['ng']), 1e-15)
                worst.append((rel_err, r['name'], m['var'], m['neo'], m['ng'], r['file']))
        worst.sort(key=lambda x: -x[0])
        for rel_err, name, var, neo, ng, fpath in worst[:20]:
            print(f"  {name:30s} {var:20s} neo={neo:12.6g} ng={ng:12.6g} "
                  f"rel_err={rel_err:.2e}  ({fpath})")

    # Timing / speed summary
    # Fair comparison: only circuits where BOTH simulators succeeded, so we
    # compare identical work (both actually ran an OP solve to completion).
    both_ok = [r for r in all_results if r['neo_ok'] and r['ng_ok']]
    if both_ok:
        neo_times = sorted(r['neo_time'] for r in both_ok)
        ng_times = sorted(r['ng_time'] for r in both_ok)
        n = len(both_ok)

        def _median(xs):
            m = len(xs) // 2
            return xs[m] if len(xs) % 2 else 0.5 * (xs[m - 1] + xs[m])

        neo_total, ng_total = sum(neo_times), sum(ng_times)
        neo_med, ng_med = _median(neo_times), _median(ng_times)
        # Per-circuit speedup = ng_time / neo_time (>1 means neospice faster)
        ratios = sorted(r['ng_time'] / r['neo_time']
                        for r in both_ok if r['neo_time'] > 0)

        print()
        print("=" * 75)
        print("SPEED SUMMARY")
        print("=" * 75)
        print(f"  (subprocess wall time incl. process startup; {n} circuits where both succeeded)")
        print(f"  {'':14s}{'neospice':>14s}{'ngspice':>14s}{'ratio (ng/neo)':>18s}")
        print(f"  {'total (s)':14s}{neo_total:14.3f}{ng_total:14.3f}{ng_total / neo_total:17.2f}x")
        print(f"  {'mean (ms)':14s}{1e3 * neo_total / n:14.3f}{1e3 * ng_total / n:14.3f}"
              f"{ng_total / neo_total:17.2f}x")
        print(f"  {'median (ms)':14s}{1e3 * neo_med:14.3f}{1e3 * ng_med:14.3f}"
              f"{(ng_med / neo_med if neo_med else 0):17.2f}x")
        if ratios:
            print(f"  median per-circuit speedup (ng_time/neo_time): {_median(ratios):.2f}x")
            faster = sum(1 for x in ratios if x > 1.0)
            print(f"  neospice faster on {faster}/{len(ratios)} circuits "
                  f"({100.0 * faster / len(ratios):.1f}%)")

    # Save results
    if args.save:
        save_data = {
            'total': total,
            'stats': dict(stats),
            'reltol': RELTOL,
            'vntol': VNTOL,
            'abstol': ABSTOL,
            'results': [{
                'name': r['name'],
                'kind': r['kind'],
                'info': r['info'],
                'file': r['file'],
                'status': r['status'],
                'isolated': r.get('isolated', False),
                'neo_ok': r['neo_ok'],
                'ng_ok': r['ng_ok'],
                'neo_err': r['neo_err'],
                'ng_err': r['ng_err'],
                'neo_time': r['neo_time'],
                'ng_time': r['ng_time'],
                'mismatches': r['mismatches'],
            } for r in all_results],
        }
        with open(args.save, 'w') as f:
            json.dump(save_data, f, indent=2)
        print(f"\nResults saved to {args.save}")


if __name__ == '__main__':
    main()
