#!/usr/bin/env python3
"""Validate a complete bench_comprehensive JSONL run and summarize accepted pairs.

No speed result is emitted for verification-only, failed, partial, or malformed
experiments. Individual failed cases remain visible alongside accepted cases.
"""
import argparse
import json
import math
from pathlib import Path
import statistics

PHASES = ('load_us', 'analysis_us', 'cleanup_us', 'total_us')


def require(condition, detail):
    if not condition:
        raise ValueError(detail)


def summarize(records):
    require(bool(records) and records[0].get('type') == 'metadata', 'missing metadata')
    meta = records[0]
    require(meta.get('schema') == 1, 'unsupported schema')
    population = meta.get('population', [])
    require(population and len(population) == len(set(population)), 'invalid population')
    require(records[-1].get('type') == 'end', 'incomplete experiment')
    require(sum(r.get('type') == 'end' for r in records) == 1, 'multiple terminal records')
    require(type(meta.get('verify_only')) is bool, 'missing verification mode')
    verify = meta['verify_only']
    count, warmup = meta.get('samples'), meta.get('warmup')
    require(type(count) is int and type(warmup) is int and
            ((count == warmup == 0) if verify else (count > 0 and warmup >= 0)),
            'invalid sample policy')
    cases, active = {}, None
    for row in records[1:-1]:
        kind, name = row.get('type'), row.get('id')
        require(name in population, 'undeclared workload')
        if kind == 'workload':
            require(active is None and name not in cases, 'duplicate or overlapping workload')
            cases[name] = {'workload': row, 'pairs': [], 'failure': None}
            active = name
        else:
            require(active == name, 'record outside its workload')
            case = cases[name]
            if kind in ('pair', 'failure'):
                require(case['failure'] is None, 'records after failure')
                iteration = len(case['pairs'])
                require(row.get('iteration') == iteration, 'missing or duplicate iteration')
                if kind == 'failure':
                    case['failure'] = row
                    continue
                require(row.get('neo_first') is (iteration % 2 == 0), 'incorrect paired order')
                phase = 'validation' if iteration == 0 else 'warmup' if iteration <= warmup else 'sample'
                require(row.get('phase') == phase, 'incorrect pair phase')
                c = row.get('comparison', {})
                require(c.get('passed') is True and type(c.get('points')) is int and c['points'] > 0,
                        'pair did not pass output validation')
                error = c.get('worst_error')
                require(type(error) in (int, float) and math.isfinite(error) and error >= 0,
                        'invalid comparison error')
                if verify:
                    require('neo' not in row and 'reference' not in row, 'verification contains timings')
                else:
                    for engine in ('neo', 'reference'):
                        timing = row.get(engine, {})
                        require(set(timing) == set(PHASES), 'missing measurement phases')
                        require(all(type(v) in (int, float) and math.isfinite(v) and v >= 0
                                    for v in timing.values()), 'invalid timing')
                        require(timing['total_us'] > 0 and
                                math.isclose(timing['total_us'], sum(timing[p] for p in PHASES[:3]),
                                             rel_tol=1e-9, abs_tol=1e-6), 'inconsistent timing boundaries')
                case['pairs'].append(row)
            elif kind == 'case_end':
                require(type(row.get('accepted')) is bool, 'missing case acceptance')
                expected = 1 if verify else 1 + warmup + count
                if row['accepted']:
                    require(case['failure'] is None and len(case['pairs']) == expected,
                            'accepted case has failures or missing samples')
                else:
                    require(case['failure'] is not None, 'rejected case has no failure evidence')
                case['accepted'] = row['accepted']
                active = None
            else:
                raise ValueError('unknown record type')
    require(active is None and set(cases) == set(population), 'missing workload completion')
    require(records[-1].get('success') is all(c['accepted'] for c in cases.values()),
            'terminal status disagrees with case results')
    result = {'metadata': meta, 'cases': []}
    for name in population:
        case = cases[name]
        item = {'id': name, 'accepted': case['accepted'], 'failure': case['failure']}
        if case['accepted'] and not verify:
            pairs = [p for p in case['pairs'] if p['phase'] == 'sample']
            item['phases'] = {}
            for phase in PHASES:
                stats = {}
                for engine in ('neo', 'reference'):
                    values = [p[engine][phase] for p in pairs]
                    stats[engine] = {'n': len(values), 'median_us': statistics.median(values),
                                     'min_us': min(values), 'max_us': max(values)}
                neo = stats['neo']['median_us']
                stats['reference_over_neo'] = stats['reference']['median_us'] / neo if neo else None
                item['phases'][phase] = stats
        result['cases'].append(item)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    records = [json.loads(line) for line in args.input.read_text().splitlines()]
    result = summarize(records)
    with args.output.open('x') as out:
        json.dump(result, out, indent=2, allow_nan=False)
        out.write('\n')


if __name__ == '__main__':
    main()
