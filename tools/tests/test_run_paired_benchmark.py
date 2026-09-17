import copy
import json
from pathlib import Path
import sys
import pytest
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from run_paired_benchmark import (check_immutable, check_protocol, file_record,
                                 fixture_closure, runtime_files)


def test_transitive_includes_with_spaces_and_cycles_are_hashed(tmp_path):
    top, lib, nested = [tmp_path / n for n in ['top.cir', 'model library.lib', 'nested.lib']]
    top.write_text('title\n.include "model library.lib"\n.end\n')
    lib.write_text('.include nested.lib\n')
    nested.write_text('.include "model library.lib"\n')
    records = fixture_closure([top])
    assert set(records) == {str(p) for p in [top, lib, nested]}
    before = copy.deepcopy(records)
    nested.write_text('changed model\n')
    with pytest.raises(ValueError, match='changed'):
        check_immutable({'fixtures': before}, {'fixtures': fixture_closure([top])})


def test_missing_include_and_runtime_controls_are_rejected(tmp_path):
    top = tmp_path / 'top.cir'
    top.write_text('title\n.include missing.lib\n')
    with pytest.raises(FileNotFoundError):
        fixture_closure([top])
    top.write_text('title\n.control\nsource /outside/file\n.endc\n')
    with pytest.raises(ValueError, match='runtime control'):
        fixture_closure([top])


def test_library_file_section_and_continuation(tmp_path):
    top, lib = tmp_path / 'top.cir', tmp_path / 'models.lib'
    top.write_text('title\n.lib models.lib\n+ typical\n')
    lib.write_text('.lib typical\nR1 a b 1k\n.endl typical\n')
    assert len(fixture_closure([top])) == 2


@pytest.mark.parametrize('line', ['.include $variable', '.include {expression}', '.include file extra', '.lib $variable'])
def test_nonliteral_or_ambiguous_dependencies_fail(tmp_path, line):
    top = tmp_path / 'top.cir'
    top.write_text(line + '\n')
    with pytest.raises(ValueError):
        fixture_closure([top])


def test_missing_codemodel_and_external_startup_commands_fail(tmp_path):
    startup = tmp_path / 'spinit'
    startup.write_text('codemodel /no-such-codemodel/file.cm\n')
    with pytest.raises(FileNotFoundError):
        runtime_files(startup)
    for text in ('source other-startup', 'shell arbitrary-command'):
        startup.write_text(text + '\n')
        with pytest.raises(ValueError, match='Uninventoried'):
            runtime_files(startup)


def fixture_records():
    description = dict(schema=1, workloads=[dict(id='x', file='x.cir', command='op')])
    records = [
        dict(type='metadata', schema=1, population=['x'], verify_only=True, samples=0, warmup=0,
             relative_tolerance=1e-3, denominator_floor=1e-9,
             reference_version='stdout ngspice-47\n', reference_settings='stdout num_threads\t1\nstdout inputdir\t/startup\n'),
        dict(type='workload', **description['workloads'][0]),
        dict(type='pair', id='x', iteration=0, phase='validation', neo_first=True,
             comparison=dict(passed=True, points=3, worst_error=0),
             reference_diagnostics='stdout Using SPARSE 1.3 as Direct Linear Solver\n'),
        dict(type='case_end', id='x', accepted=True), dict(type='end', success=True)]
    return records, description


def test_reference_version_threads_solver_and_population_are_verified():
    records, desc = fixture_records()
    assert check_protocol(records, desc, 47, True, 30, 3, spinit=Path('/startup/spinit'))['cases'][0]['accepted']


@pytest.mark.parametrize('change', [
    lambda r, d: r[0].update(relative_tolerance=1),
    lambda r, d: r[2]['comparison'].update(worst_error=0.002),
    lambda r, d: r[0].update(reference_version='ngspice-42'),
    lambda r, d: r[0].update(reference_settings='num_threads\t8\n'),
    lambda r, d: r[2].update(reference_diagnostics='Using KLU'),
    lambda r, d: r[1].update(command='tran 1 10'),
    lambda r, d: d['workloads'].append(dict(id='missing', file='y.cir', command='op')),
    lambda r, d: r[0].update(reference_settings='num_threads\t1\ninputdir\t/wrong\n'),
])
def test_mislabeled_or_changed_protocol_fails(change):
    records, desc = fixture_records()
    change(records, desc)
    with pytest.raises(ValueError):
        check_protocol(records, desc, 47, True, 30, 3, spinit=Path('/startup/spinit'))


def test_ordinary_failed_simulation_is_retained_as_failure_evidence():
    records, desc = fixture_records()
    records[2] = dict(type='failure', id='x', iteration=0, error='reference failed')
    records[3]['accepted'] = False
    records[4]['success'] = False
    result = check_protocol(records, desc, 47, True, 30, 3)
    assert not result['cases'][0]['accepted']
    assert 'phases' not in result['cases'][0]


def test_binary_mutation_invalidates_evidence(tmp_path):
    binary = tmp_path / 'binary'
    binary.write_bytes(b'old executable')
    before = {'binary': file_record(binary)}
    binary.write_bytes(b'new executable')
    with pytest.raises(ValueError, match='binary'):
        check_immutable(before, {'binary': file_record(binary)})


def test_inactive_optional_module_absence_is_recorded(tmp_path):
    startup = tmp_path / 'spinit'
    missing = tmp_path / 'missing.osdi'
    startup.write_text(f'unset osdi_enabled\nif $?osdi_enabled\nosdi {missing}\nend\n')
    record = runtime_files(startup)[str(missing)]
    assert record['exists'] is False and record['inactive_startup_branch'] is True
    startup.write_text(f'set osdi_enabled\nif $?osdi_enabled\nosdi {missing}\nend\n')
    with pytest.raises(FileNotFoundError):
        runtime_files(startup)


def test_unknown_conditional_assignment_cannot_prove_module_inactive(tmp_path):
    startup = tmp_path / 'spinit'
    missing = tmp_path / 'required.osdi'
    startup.write_text(f'if $?unknown\nunset osdi_enabled\nend\n'
                       f'if $?osdi_enabled\nosdi {missing}\nend\n')
    with pytest.raises(FileNotFoundError):
        runtime_files(startup)


def tlv_records():
    from run_paired_benchmark import TLV3201_POLICY
    records, desc = fixture_records()
    w = dict(id='tran_tlv3201', file='tlv3201_switching.cir', command='tran 100n 30u')
    desc['workloads'] = [w]; desc['validation_policy'] = copy.deepcopy(TLV3201_POLICY)
    records[0].update(population=[w['id']], validation_policy=copy.deepcopy(TLV3201_POLICY),
                      relative_tolerance=1, denominator_floor=None)
    records[1] = dict(type='workload', **w)
    records[2]['id'] = records[3]['id'] = w['id']
    def signal(name, value):
        return dict(name=name, points=1, max_absolute_error=0, max_normalized_error=0,
                    coordinate=0, unit='s', reference=value, actual=value,
                    reference_imag=0, actual_imag=0)
    ports = [signal('v(vcc)', 5), signal('v(inm)', 2.5)]
    edge = signal('v(out):rising:0', 1e-6); edge['coordinate'] = 1e-6
    records[2]['comparison']['signals'] = ports+[edge]
    records[2]['comparison']['worst_signal'] = ''
    metrics = dict(cross_time_s=1e-6, rise_time_s=1e-8, settled_value_v=5, overshoot_v=0)
    records[2].update(reference_edges=[copy.deepcopy(metrics)], actual_edges=[copy.deepcopy(metrics)],
                      informational_waveform_comparison=copy.deepcopy(records[2]['comparison']))
    return records, desc


def test_tlv_policy_is_distinct_and_preserves_original_limits():
    records, desc = tlv_records()
    assert check_protocol(records, desc, 47, True, 30, 3)['cases'][0]['accepted']
    records[0]['validation_policy']['crossing_absolute_s'] = 1e-6
    desc['validation_policy']['crossing_absolute_s'] = 1e-6
    with pytest.raises(ValueError, match='altered specialized'):
        check_protocol(records, desc, 47, True, 30, 3)


@pytest.mark.parametrize('change', [
    lambda r: r[2].update(reference_edges=[]),
    lambda r: r[2]['actual_edges'][0].update(rise_time_s=-1e-8),
    lambda r: r[2]['comparison']['signals'].pop(0),
    lambda r: r[2]['comparison']['signals'][0].update(actual=6),
    lambda r: r[2].pop('informational_waveform_comparison'),
])
def test_tlv_passing_claim_requires_matching_port_edge_and_diagnostic_evidence(change):
    records, desc = tlv_records(); change(records)
    with pytest.raises(ValueError, match='TLV3201'):
        check_protocol(records, desc, 47, True, 30, 3)


def test_tlv_crossing_at_exact_limit_cannot_be_accepted():
    records, desc = tlv_records(); row=records[2]
    row['reference_edges'][0]['cross_time_s']=0
    row['actual_edges'][0]['cross_time_s']=5e-8
    row['comparison']['signals'][-1].update(reference=0,actual=5e-8,coordinate=0,
                                           max_absolute_error=5e-8,max_normalized_error=1)
    row['comparison']['worst_error']=1
    with pytest.raises(ValueError, match='crossing threshold'):
        check_protocol(records, desc, 47, True, 30, 3)
