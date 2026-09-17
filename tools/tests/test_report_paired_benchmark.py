import copy
import json
from pathlib import Path

import pytest

from report_paired_benchmark import generate, load_experiment, tables
from run_paired_benchmark import file_record
from summarize_paired_benchmark import summarize


def experiment(tmp_path, verify=False, failed=False):
    root = tmp_path/'run'; root.mkdir()
    signal = dict(name='v(out)', points=1, max_absolute_error=0,
                  max_normalized_error=0, coordinate=0, unit='OP',
                  reference=1, actual=1, reference_imag=0, actual_imag=0)
    comparison = dict(passed=True, points=1, worst_error=0, worst_signal='', signals=[signal])
    rows = [dict(type='metadata', schema=1, population=['x'], verify_only=verify,
                 samples=0 if verify else 1, warmup=0,
                 reference_version='ngspice-47', reference_settings='num_threads 1\ninputdir /declared\n',
                 relative_tolerance=1e-3, denominator_floor=1e-9),
            dict(type='workload', id='x', file='x.cir', command='op')]
    for i in range(1 if verify else 2):
        row = dict(type='pair', id='x', iteration=i, neo_first=(i == 0),
                   phase='validation' if i == 0 else 'sample', comparison=copy.deepcopy(comparison),
                   reference_diagnostics='Using SPARSE 1.3 as Direct Linear Solver')
        if not verify:
            row.update(neo=dict(load_us=1, analysis_us=2, cleanup_us=1, total_us=4),
                       reference=dict(load_us=2, analysis_us=4, cleanup_us=2, total_us=8))
        rows.append(row)
    if failed:
        rows[-1] = dict(type='failure', id='x', iteration=0 if verify else 1,
                        error='reference aborted', comparison=dict(passed=False, points=0, worst_error=None))
    rows += [dict(type='case_end', id='x', accepted=not failed), dict(type='end', success=not failed)]
    (root/'pairs.jsonl').write_text(''.join(json.dumps(r)+'\n' for r in rows))
    (root/'summary.json').write_text(json.dumps(summarize(rows)))
    manifest = dict(complete=True, evidence_valid=True, verify_only=verify,
                    inventory_before={'binary': 'test-fixture'}, inventory_after={'binary': 'test-fixture'},
                    description=dict(workloads=[dict(id='x', file='x.cir', command='op')]),
                    command=['bench'], environment={'SPICE_SCRIPTS': '/declared'},
                    all_workloads_qualified=not failed, returncode=1 if failed else 0,
                    artifacts={p.name: file_record(p) for p in root.iterdir()}, limits=['synthetic test data'])
    (root/'manifest.json').write_text(json.dumps(manifest))
    return root


def update_artifact(root, name):
    p = root/'manifest.json'; m = json.loads(p.read_text())
    m['artifacts'][name] = file_record(root/name)
    p.write_text(json.dumps(m))


def test_report_verification_has_no_timing_artifacts(tmp_path):
    run = experiment(tmp_path, verify=True)
    out = tmp_path/'report'
    result = generate(run, out)
    assert result['verify_only']
    assert (out/'qualification.pdf').is_file()
    assert (out/'qualification.svg').is_file()
    assert not (out/'timings.csv').exists()
    assert not (out/'timing-total.pdf').exists()
    assert 'Verification only' in (out/'report.md').read_text()


def test_report_regenerates_measured_medians_and_dispersion(tmp_path):
    run = experiment(tmp_path)
    out = tmp_path/'report'
    generate(run, out)
    assert (out/'timing-total.pdf').is_file()
    assert (out/'timings.csv').read_text().splitlines()[-1] == 'x,total_us,1,4,4,4,8,8,8,2.0'
    with pytest.raises(FileExistsError):
        generate(run, out)


def test_failure_after_valid_pair_cannot_create_timing_rows(tmp_path):
    run = experiment(tmp_path, failed=True)
    _, rows, summary, _ = load_experiment(run)
    qualification, timing = tables(rows, summary)
    assert not timing
    assert len(qualification) == 1
    assert qualification[0]['failure'] == 'reference aborted'
    assert not qualification[0]['accepted']
    assert qualification[0]['max_normalized_error'] is None


def test_rejects_changed_raw_artifact(tmp_path):
    run = experiment(tmp_path)
    with (run/'pairs.jsonl').open('a') as out: out.write('\n')
    with pytest.raises(ValueError, match='hash mismatch'):
        load_experiment(run)


def test_rejects_summary_even_with_updated_hash(tmp_path):
    run = experiment(tmp_path)
    p = run/'summary.json'; r = json.loads(p.read_text())
    r['cases'][0]['phases']['total_us']['neo']['median_us'] = 100
    p.write_text(json.dumps(r)); update_artifact(run, p.name)
    with pytest.raises(ValueError, match='differs from raw'):
        load_experiment(run)


@pytest.mark.parametrize('change', ['missing_signals', 'wrong_points', 'wrong_worst', 'nan_value'])
def test_rejects_incomplete_or_inconsistent_signal_evidence(tmp_path, change):
    run = experiment(tmp_path)
    p = run/'pairs.jsonl'; rows = [json.loads(s) for s in p.read_text().splitlines()]
    c = rows[2]['comparison']
    if change == 'missing_signals': c['signals'] = []
    elif change == 'wrong_points': c['signals'][0]['points'] = 2
    elif change == 'wrong_worst': c['signals'][0]['max_normalized_error'] = 1e-5
    else: c['signals'][0]['actual'] = float('nan')
    p.write_text(''.join(json.dumps(r)+'\n' for r in rows)); update_artifact(run, p.name)
    with pytest.raises(ValueError): load_experiment(run)


def test_tlv_report_labels_allowance_fraction_and_retains_policy(tmp_path):
    # Synthetic evidence checks report semantics; it is not circuit measurement.
    from tools.tests.test_run_paired_benchmark import tlv_records
    records, description = tlv_records()
    root = experiment(tmp_path, verify=True)
    (root/'pairs.jsonl').write_text(''.join(json.dumps(r)+'\n' for r in records))
    (root/'summary.json').write_text(json.dumps(summarize(records)))
    p=root/'manifest.json'; m=json.loads(p.read_text()); m['description']=description
    m['environment']['SPICE_SCRIPTS']='/startup'
    p.write_text(json.dumps(m)); update_artifact(root,'pairs.jsonl'); update_artifact(root,'summary.json')
    out=tmp_path/'report'; result=generate(root,out)
    assert result['validation_policy']['kind']=='tlv3201-v1'
    assert 'Maximum allowance fraction' in (out/'report.md').read_text()
    assert 'not pointwise waveform certification' in (out/'report.md').read_text()
    assert 'Maximum fraction of allowed error' in (out/'qualification.svg').read_text()
    assert not (out/'timings.csv').exists()
