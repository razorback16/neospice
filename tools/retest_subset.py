#!/usr/bin/env python3
"""
Re-test a subset of KiCad SPICE models: all baseline failures + random
passing tests as regression canaries.

Usage:
    python3 tools/retest_subset.py [--neospice PATH] [--baseline results/baseline_full.json]
"""

import argparse
import json
import os
import random
import subprocess
import sys
import tempfile
from pathlib import Path


def run_neospice(netlist_text, neospice_bin, timeout=10):
    with tempfile.NamedTemporaryFile(mode='w', suffix='.spice', delete=False) as f:
        f.write(netlist_text)
        f.flush()
        tmp_path = f.name
    try:
        result = subprocess.run(
            [neospice_bin, tmp_path],
            capture_output=True, text=True, timeout=timeout
        )
        combined = result.stdout + result.stderr
        if result.returncode != 0:
            if 'Parse error' in combined or 'ParseError' in combined:
                return 'PARSE_ERROR', combined.strip()
            elif 'convergence' in combined.lower() or 'failed' in combined.lower():
                return 'SIM_ERROR', combined.strip()
            else:
                return 'ERROR', combined.strip()
        elif 'Warning' in combined or 'warning' in combined:
            return 'WARNING', combined.strip()
        else:
            return 'OK', combined.strip()
    except subprocess.TimeoutExpired:
        return 'TIMEOUT', 'Timed out after 10s'
    except Exception as e:
        return 'ERROR', str(e)
    finally:
        os.unlink(tmp_path)


def main():
    parser = argparse.ArgumentParser(description='Re-test KiCad SPICE model subset')
    parser.add_argument('--neospice', default=str(Path(__file__).parent.parent / 'build' / 'neospice'))
    parser.add_argument('--baseline', default=str(Path(__file__).parent.parent / 'results' / 'baseline_full.json'))
    parser.add_argument('--canaries', type=int, default=1000,
                        help='Number of random passing tests to include as regression canaries')
    parser.add_argument('--seed', type=int, default=42, help='Random seed for canary selection')
    parser.add_argument('--save', default=None, help='Save delta results to JSON')
    parser.add_argument('--verbose', '-v', action='store_true')
    args = parser.parse_args()

    if not os.path.exists(args.neospice):
        print(f"Error: neospice binary not found at {args.neospice}")
        sys.exit(1)

    with open(args.baseline) as f:
        baseline = json.load(f)

    all_baseline = baseline['results']
    failures = [r for r in all_baseline if r['status'] not in ('OK', 'WARNING')]
    passing = [r for r in all_baseline if r['status'] in ('OK', 'WARNING')]

    print(f"Loaded {len(failures)} baseline failures, {len(passing)} passing tests")

    rng = random.Random(args.seed)
    canaries = rng.sample(passing, min(args.canaries, len(passing)))
    print(f"Selected {len(canaries)} regression canaries")

    all_tests = [(r, 'failure') for r in failures] + [(r, 'canary') for r in canaries]
    total = len(all_tests)
    print(f"Re-testing {total} models ({len(failures)} failures + {len(canaries)} canaries)")
    print()

    newly_passing = []
    regressions = []
    still_failing = []
    canary_ok = 0
    canary_regressed = []

    for i, (test, role) in enumerate(all_tests):
        netlist = test['netlist']
        status, output = run_neospice(netlist, args.neospice)

        if role == 'failure':
            old_status = test['status']
            if status in ('OK', 'WARNING'):
                newly_passing.append({
                    'name': test['name'], 'file': test['file'],
                    'old_status': old_status, 'new_status': status
                })
                if args.verbose:
                    print(f"  [FIXED]      {test['name']:30s} ({test['file']})")
            else:
                still_failing.append({
                    'name': test['name'], 'file': test['file'],
                    'old_status': old_status, 'new_status': status
                })
        else:
            if status in ('OK', 'WARNING'):
                canary_ok += 1
            else:
                canary_regressed.append({
                    'name': test['name'], 'file': test['file'],
                    'old_status': test['status'], 'new_status': status
                })
                print(f"  [REGRESSION] {test['name']:30s} ({test['file']})")

        if (i + 1) % 100 == 0:
            print(f"  ... {i+1}/{total} tested", file=sys.stderr)

    print()
    print("=" * 70)
    print("DELTA REPORT")
    print("=" * 70)
    print(f"  Newly passing:    {len(newly_passing):4d} / {len(failures)} failures")
    print(f"  Still failing:    {len(still_failing):4d} / {len(failures)} failures")
    print(f"  Canary OK:        {canary_ok:4d} / {len(canaries)} canaries")
    print(f"  Canary regressed: {len(canary_regressed):4d} / {len(canaries)} canaries")
    print()

    if newly_passing:
        print("NEWLY PASSING:")
        for r in newly_passing[:20]:
            print(f"  {r['name']:30s}  {r['old_status']:12s} -> {r['new_status']}")
        if len(newly_passing) > 20:
            print(f"  ... and {len(newly_passing) - 20} more")

    if canary_regressed:
        print()
        print("REGRESSIONS (canaries that broke):")
        for r in canary_regressed:
            print(f"  {r['name']:30s}  {r['old_status']:12s} -> {r['new_status']}")

    if args.save:
        delta = {
            'newly_passing': newly_passing,
            'still_failing': still_failing,
            'regressions': canary_regressed,
            'canary_ok': canary_ok,
            'summary': {
                'failures_tested': len(failures),
                'newly_passing': len(newly_passing),
                'still_failing': len(still_failing),
                'canaries_tested': len(canaries),
                'canary_ok': canary_ok,
                'canary_regressed': len(canary_regressed),
            }
        }
        with open(args.save, 'w') as f:
            json.dump(delta, f, indent=2)
        print(f"\nDelta saved to {args.save}")


if __name__ == '__main__':
    main()
