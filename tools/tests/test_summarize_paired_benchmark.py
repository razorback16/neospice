import copy
import sys
from pathlib import Path
import pytest
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from summarize_paired_benchmark import summarize


def valid():
    return [
        dict(type='metadata', schema=1, population=['x'], verify_only=False, samples=1, warmup=0),
        dict(type='workload', id='x'),
        *[dict(type='pair', id='x', iteration=i, phase='validation' if i == 0 else 'sample',
               neo_first=(i == 0), comparison=dict(passed=True, points=3, worst_error=0),
               neo=dict(load_us=1, analysis_us=2, cleanup_us=1, total_us=4),
               reference=dict(load_us=2, analysis_us=4, cleanup_us=2, total_us=8)) for i in range(2)],
        dict(type='case_end', id='x', accepted=True), dict(type='end', success=True)]


def test_raw_samples_define_ratio_and_dispersion():
    result = summarize(valid())['cases'][0]['phases']['total_us']
    assert result['reference_over_neo'] == 2
    assert result['neo'] == dict(n=1, median_us=4, min_us=4, max_us=4)


@pytest.mark.parametrize('mutation', [
    lambda r: r.pop(),
    lambda r: r.pop(2),
    lambda r: r[0]['population'].append('unreported'),
    lambda r: r[3]['comparison'].update(passed=False),
    lambda r: r[3]['comparison'].update(points=0),
    lambda r: r[3]['comparison'].update(worst_error=float('nan')),
    lambda r: r[3]['neo'].update(analysis_us=float('inf')),
    lambda r: r[3]['neo'].update(total_us=100),
    lambda r: r[3].update(neo_first=True),
    lambda r: r[-1].update(success=False),
    lambda r: r[0].update(samples=2),
])
def test_rejects_incomplete_invalid_or_unvalidated_evidence(mutation):
    records = copy.deepcopy(valid())
    mutation(records)
    with pytest.raises(ValueError):
        summarize(records)


def test_failed_case_cannot_yield_speed_result():
    records = valid()
    records[3] = dict(type='failure', id='x', iteration=1, error='reference aborted')
    records[4]['accepted'] = False
    records[5]['success'] = False
    case = summarize(records)['cases'][0]
    assert 'phases' not in case
    assert case['failure']['error'] == 'reference aborted'


def test_verification_has_no_performance_claim():
    records = valid()
    records[0].update(verify_only=True, samples=0)
    records.pop(3)
    records[2].pop('neo')
    records[2].pop('reference')
    assert 'phases' not in summarize(records)['cases'][0]
