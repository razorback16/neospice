#!/usr/bin/env python3
"""Regenerate qualification tables and optional timing figures from a valid run.

Install neospice[benchmarks] (Matplotlib) to render figures. A verification-only
run never produces timing tables/plots. Each output directory must be new.
"""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import re
import sys

from run_paired_benchmark import check_protocol, file_record


def require(condition, text):
    if not condition:
        raise ValueError(text)


def load_experiment(directory):
    directory = Path(directory).resolve(strict=True)
    manifest = json.loads((directory/'manifest.json').read_text())
    require(manifest.get('complete') is True and manifest.get('evidence_valid') is True,
            'experiment is incomplete or invalid')
    require(manifest.get('inventory_before') == manifest.get('inventory_after') and
            bool(manifest.get('inventory_before')), 'inventory changed or missing')
    artifacts = manifest.get('artifacts', {})
    require({'pairs.jsonl', 'summary.json'} <= set(artifacts), 'missing evidence artifacts')
    for name, record in artifacts.items():
        require(Path(name).name == name and name not in ('.', '..'), 'unsafe artifact name')
        path = directory/name
        require(path.is_file() and path.stat().st_size == record['bytes'] and
                file_record(path)['sha256'] == record['sha256'], 'artifact hash mismatch: '+name)
    records = [json.loads(line) for line in (directory/'pairs.jsonl').read_text().splitlines()]
    require(bool(records), 'empty raw records')
    meta = records[0]
    match = re.search(r'\bngspice-(\d+)\b', meta.get('reference_version', ''))
    require(match is not None, 'missing reference version')
    version = int(match[1])
    command = manifest.get('command', [])
    case = command[command.index('--case')+1] if '--case' in command else None
    scripts = manifest.get('environment', {}).get('SPICE_SCRIPTS')
    require(bool(scripts), 'missing recorded startup directory')
    summary = check_protocol(records, manifest['description'], version,
                             meta['verify_only'], meta['samples'], meta['warmup'],
                             case, Path(scripts)/'spinit')
    require(summary == json.loads((directory/'summary.json').read_text()),
            'saved summary differs from raw records')
    require(manifest.get('all_workloads_qualified') is all(c['accepted'] for c in summary['cases']),
            'qualification flag disagrees with raw records')
    require(manifest.get('verify_only') is meta['verify_only'], 'verification flag disagrees')
    require(manifest.get('returncode') == (0 if manifest['all_workloads_qualified'] else 1),
            'process exit status disagrees')
    for row in records:
        if row['type'] != 'pair':
            continue
        c = row['comparison']
        signals = c.get('signals', [])
        require(bool(signals), 'passing pair lacks per-signal diagnostics')
        require(sum(s['points'] for s in signals) == c['points'], 'signal point accounting differs')
        for s in signals:
            require(type(s.get('points')) is int and s['points'] > 0 and s.get('name') and s.get('unit'),
                    'invalid signal identity or count')
            for key in ('max_absolute_error', 'max_normalized_error', 'coordinate',
                        'reference', 'actual', 'reference_imag', 'actual_imag'):
                value = s.get(key)
                require(type(value) in (int, float) and math.isfinite(value), 'nonfinite signal evidence')
            require(s['max_absolute_error'] >= 0 and s['max_normalized_error'] >= 0,
                    'negative signal error')
        require(math.isclose(max(s['max_normalized_error'] for s in signals), c['worst_error'],
                             rel_tol=1e-12, abs_tol=0), 'worst error disagrees with signals')
    return manifest, records, summary, version


def tables(records, summary):
    qualification, timing = [], []
    for case in summary['cases']:
        rows = [r for r in records if r.get('id') == case['id']]
        workload = next(r for r in rows if r['type'] == 'workload')
        comparisons = [r['comparison'] for r in rows if r['type'] in ('pair', 'failure') and 'comparison' in r]
        errors = [c['worst_error'] for c in comparisons if c.get('worst_error') is not None]
        undefined_failure = case['failure'] is not None and case['failure'].get('comparison', {}).get('worst_error') is None
        # A simulator/extraction failure is retained even when no comparison exists.
        qualification.append(dict(id=case['id'], accepted=case['accepted'],
                                  file=workload['file'], command=workload['command'],
                                  compared_pairs=sum(r['type'] == 'pair' for r in rows),
                                  max_normalized_error=max(errors) if errors and not undefined_failure else None,
                                  failure=case['failure'].get('error', '') if case['failure'] else ''))
        for phase, stats in case.get('phases', {}).items():
            timing.append(dict(id=case['id'], phase=phase,
                               n=stats['neo']['n'], neo_median_us=stats['neo']['median_us'],
                               neo_min_us=stats['neo']['min_us'], neo_max_us=stats['neo']['max_us'],
                               reference_median_us=stats['reference']['median_us'],
                               reference_min_us=stats['reference']['min_us'],
                               reference_max_us=stats['reference']['max_us'],
                               reference_over_neo=stats['reference_over_neo']))
    return qualification, timing


def write_csv(path, rows):
    with path.open('x', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def figures(directory, qualification, timing, version, threshold, fraction=False):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    plt.rcParams.update({'font.size': 9, 'pdf.fonttype': 42, 'svg.fonttype': 'none',
                         'svg.hashsalt': 'neospice-paired-report-v1'})
    def save(fig, name):
        fig.tight_layout()
        fig.savefig(directory/(name+'.pdf'), metadata={'CreationDate': None, 'ModDate': None})
        fig.savefig(directory/(name+'.svg'), metadata={'Date': None})
        plt.close(fig)
    fig, ax = plt.subplots(figsize=(9, max(4, 0.25*len(qualification)+1.5)))
    positive = [r['max_normalized_error'] for r in qualification
                if r['max_normalized_error'] is not None and r['max_normalized_error'] > 0]
    lower = min(positive+[threshold])/10
    for i, row in enumerate(qualification):
        value = row['max_normalized_error']
        if value is None:
            ax.text(lower, i, 'error undefined; see failure', va='center', color='#aa2222')
        elif value == 0:
            color = '#226699' if row['accepted'] else '#aa2222'
            ax.plot(lower, i, marker='<' if row['accepted'] else 'x', color=color)
            ax.annotate('exact zero' if row['accepted'] else 'unqualified; zero recorded error',
                        (lower, i), xytext=(6, 0), textcoords='offset points', va='center', color=color)
        else:
            ax.plot(value, i, 'o' if row['accepted'] else 'x',
                    color='#226699' if row['accepted'] else '#aa2222')
    ax.set_xscale('log'); ax.axvline(threshold, color='#aa2222', ls='--', label='qualification threshold')
    ax.set_yticks(range(len(qualification)), [r['id'] for r in qualification]); ax.invert_yaxis()
    ax.set_xlabel('Maximum fraction of allowed error across attempted comparisons' if fraction else
                  'Maximum normalized error across attempted comparisons; exact zeros labeled')
    ax.set_title(f'Paired qualification against ngspice {version} (default Sparse, one thread)')
    ax.grid(axis='x', alpha=0.2); ax.legend(loc='best')
    save(fig, 'qualification')
    totals = [r for r in timing if r['phase'] == 'total_us']
    if totals:
        fig, ax = plt.subplots(figsize=(9, max(4, 0.3*len(totals)+1.5)))
        for engine, color, offset in [('neo', '#226699', -0.12), ('reference', '#cc6611', 0.12)]:
            for i, row in enumerate(totals):
                median, lo, hi = [row[engine+'_'+stat+'_us'] for stat in ('median', 'min', 'max')]
                ax.errorbar(median, i+offset, xerr=[[median-lo], [hi-median]], fmt='o',
                            color=color, markersize=3, capsize=2,
                            label=('neospice' if engine == 'neo' else f'ngspice {version}') if i == 0 else None)
        ax.set_xscale('symlog', linthresh=0.001)
        ax.set_yticks(range(len(totals)), [r['id'] for r in totals]); ax.invert_yaxis()
        ax.set_xlabel('Total time (us): median and observed min–max; linear scale below 0.001 us')
        ax.set_title('Accuracy-qualified paired timings; failed workloads remain in qualification.csv')
        ax.grid(axis='x', alpha=0.2); ax.legend(loc='best')
        save(fig, 'timing-total')
    return matplotlib.__version__


def generate(experiment, output):
    manifest, records, summary, version = load_experiment(experiment)
    qualification, timing = tables(records, summary)
    output = Path(output)
    output.mkdir(parents=True, exist_ok=False)
    write_csv(output/'qualification.csv', qualification)
    if timing:
        write_csv(output/'timings.csv', timing)
    policy = summary['metadata'].get('validation_policy')
    fraction = policy is not None and policy.get('error_scale') == 'fraction_of_allowance'
    plot_version = figures(output, qualification, timing, version, summary['metadata']['relative_tolerance'], fraction)
    lines = [f'# Paired benchmark report: ngspice {version}', '',
             f"Population: {len(qualification)} declared workloads; {sum(r['accepted'] for r in qualification)} qualified.", '',
             'Reference: default Sparse, one simulator thread. Separate populations and reference versions must not be pooled.', '',
             'Qualification uses all attempted comparisons, including validation/warmup pairs. Invalid runs are rejected.', '',
             'Verification only; no timing samples or performance claim.' if summary['metadata']['verify_only'] else
             'Timing table: equal paired samples and warmups, alternating engine order. Load/analysis/cleanup/total boundaries follow docs/benchmark-methods.md. Median and min–max are descriptive statistics, not confidence intervals. Ratios are reference median divided by neospice median; no aggregate speedup is computed.', '',
             'Only accuracy-qualified cases receive timing rows. Min–max bars describe observed spread; they do not establish absence of contention or general scaling.', '',
             '| Workload | Qualified | Maximum normalized error | Failure |', '|---|---|---|---|']
    if fraction:
        lines.insert(4, 'TLV3201 specialized qualification: output crossing error <50 ns with matching nonempty edge directions; v(vcc)/v(inm) error <=0.01*max(abs(reference),0.01 V). Reported normalized errors are fractions of these separate allowances; waveform/internal-node comparisons are informational and retained in raw records. This is not pointwise waveform certification.\n')
        lines = [line.replace('Maximum normalized error', 'Maximum allowance fraction') for line in lines]
    def cell(value): return str(value).replace('|', '\\|').replace('\n', ' ')
    for row in qualification:
        error = '' if row['max_normalized_error'] is None else f"{row['max_normalized_error']:.6g}"
        lines.append(f"| {cell(row['id'])} | {row['accepted']} | {error} | {cell(row['failure'])} |")
    (output/'report.md').write_text('\n'.join(lines)+'\n')
    inventory = {p.name: file_record(p) for p in sorted(output.iterdir()) if p.is_file()}
    record = {'schema': 1, 'source_manifest': file_record(Path(experiment)/'manifest.json'),
              'source_pairs': file_record(Path(experiment)/'pairs.jsonl'),
              'generator': file_record(__file__), 'python': sys.version, 'matplotlib': plot_version,
              'reference_version': version, 'verify_only': summary['metadata']['verify_only'],
              'population': summary['metadata']['population'], 'validation_policy': policy, 'artifacts': inventory,
              'limits': manifest['limits']}
    (output/'report-manifest.json').write_text(json.dumps(record, indent=2)+'\n')
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('experiment', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    try:
        generate(args.experiment, args.output)
    except (ValueError, OSError) as error:
        parser.exit(2, str(error)+'\n')


if __name__ == '__main__':
    main()
