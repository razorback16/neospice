#!/usr/bin/env python3
"""Re-test only previously failing models from a saved results JSON."""

import json
import sys
import os
import argparse
from pathlib import Path
from collections import defaultdict

# Import from test_kicad_models
sys.path.insert(0, str(Path(__file__).parent))
from test_kicad_models import run_neospice

def main():
    parser = argparse.ArgumentParser(description='Re-test previously failing KiCad models')
    parser.add_argument('baseline', help='Path to baseline results JSON')
    parser.add_argument('--neospice', default=str(Path(__file__).parent.parent / 'build' / 'neospice'))
    parser.add_argument('--save', default=None, help='Save results to JSON')
    args = parser.parse_args()

    with open(args.baseline) as f:
        baseline = json.load(f)

    failed = [r for r in baseline['results'] if r['status'] not in ('OK', 'WARNING')]
    print(f"Re-testing {len(failed)} previously failing models...")

    stats = defaultdict(int)
    newly_passing = []
    still_failing = []
    changed = []

    for i, r in enumerate(failed):
        netlist = r['netlist']
        status, output = run_neospice(netlist, args.neospice)
        stats[status] += 1

        if status in ('OK', 'WARNING'):
            newly_passing.append((r['name'], r['file'], r['status'], status))
        elif status != r['status']:
            changed.append((r['name'], r['file'], r['status'], status))
        else:
            still_failing.append((r['name'], r['file'], status))

        if (i + 1) % 50 == 0:
            print(f"  ... {i+1}/{len(failed)} tested", file=sys.stderr)

    print()
    print("=" * 70)
    print(f"NEWLY PASSING: {len(newly_passing)}")
    for name, fpath, old, new in newly_passing[:30]:
        print(f"  {name:30s} {old:12s} -> {new:8s}  ({fpath})")
    if len(newly_passing) > 30:
        print(f"  ... and {len(newly_passing) - 30} more")

    if changed:
        print(f"\nCHANGED STATUS: {len(changed)}")
        for name, fpath, old, new in changed[:20]:
            print(f"  {name:30s} {old:12s} -> {new:12s}  ({fpath})")

    print(f"\nSTILL FAILING: {len(still_failing)}")
    fail_cats = defaultdict(int)
    for name, fpath, status in still_failing:
        fail_cats[status] += 1
    for k, v in sorted(fail_cats.items(), key=lambda x: -x[1]):
        print(f"  {k:12s}: {v}")

    print(f"\nSummary: {len(newly_passing)} fixed, {len(still_failing)} remaining, {len(changed)} changed")

    if args.save:
        with open(args.save, 'w') as f:
            json.dump({
                'newly_passing': len(newly_passing),
                'still_failing': len(still_failing),
                'changed': len(changed),
                'details': {
                    'newly_passing': [{'name': n, 'file': fp, 'old': o, 'new': nw} for n, fp, o, nw in newly_passing],
                    'still_failing': [{'name': n, 'file': fp, 'status': s} for n, fp, s in still_failing],
                    'changed': [{'name': n, 'file': fp, 'old': o, 'new': nw} for n, fp, o, nw in changed],
                }
            }, f, indent=2)
        print(f"Results saved to {args.save}")


if __name__ == '__main__':
    main()
