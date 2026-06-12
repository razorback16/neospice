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


def run_one_test(args_tuple):
    """Worker function for parallel execution. Returns a result dict."""
    kind, name, info, netlist, rel_path, neo_bin, ng_bin, ext_only = args_tuple

    neo_ok, neo_vals, neo_err, neo_time = run_simulator(netlist, neo_bin, is_ngspice=False)
    ng_ok, ng_vals, ng_err, ng_time = run_simulator(netlist, ng_bin, is_ngspice=True)

    if neo_ok and ng_ok:
        match, details = compare_values(neo_vals, ng_vals, external_only=ext_only)
        status = 'MATCH' if match else 'MISMATCH'
        mismatches = [d for d in details if not d.get('ok', True)]
    elif neo_ok and not ng_ok:
        status = 'NEO_ONLY'
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
            elif result['status'] not in ('MATCH', 'BOTH_FAIL'):
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
    for status in ('MATCH', 'MISMATCH', 'NEO_ONLY', 'NG_ONLY', 'BOTH_FAIL'):
        if stats[status]:
            pct = 100.0 * stats[status] / total
            bar = '#' * int(pct / 2)
            print(f"  {status:11s}: {stats[status]:5d} / {total}  ({pct:5.1f}%)  {bar}")

    match_rate = 100.0 * stats['MATCH'] / total if total else 0
    agree_rate = 100.0 * (stats['MATCH'] + stats['NEO_ONLY'] + stats['BOTH_FAIL']) / total if total else 0

    print()
    print(f"Exact match rate:  {match_rate:.1f}% ({stats['MATCH']}/{total})")
    print(f"  (both converge and values agree within reltol={RELTOL}, vntol={VNTOL}, abstol={ABSTOL})")
    print()
    print(f"NG_ONLY (ngspice passes, neospice fails): {stats['NG_ONLY']}")
    print(f"  These represent genuine neospice gaps.")
    print(f"NEO_ONLY (neospice passes, ngspice fails): {stats['NEO_ONLY']}")
    print(f"  These represent neospice being more robust than ngspice.")

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
