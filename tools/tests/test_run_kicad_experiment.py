"""Frozen execution must preserve failures, required signals and both variants."""
import json
import os
from pathlib import Path
import struct
import sys
from types import SimpleNamespace

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import run_kicad_experiment as runner
from kicad_experiment import freeze


def raw_bytes(values):
    header = 'Title: fake\nPlotname: Operating Point\nFlags: real\nNo. Variables: %d\nNo. Points: 1\nVariables:\n' % len(values)
    header += ''.join(f'\t{i}\t{name}\tvoltage\n' for i,name in enumerate(values))
    return header.encode()+b'Binary:\n'+struct.pack('d'*len(values),*values.values())


def fake_runtime(tmp_path, body):
    executable=tmp_path/'fake-simulator'
    executable.write_text('#!'+sys.executable+'\n'+body)
    executable.chmod(0o755)
    return executable


def test_required_observables_from_generated_ports_and_sources():
    assert runner.expected_observables('* deck\n.include "x"\nVCC supply 0 5\nX1 out supply 0 MOD PARAMS: x=1\nR1 out 0 1k\n.op\n.end\n') == ['i(vcc)','v(out)','v(supply)']
    with pytest.raises(ValueError):runner.expected_observables('* empty\n.op\n.end\n')
    with pytest.raises(ValueError):runner.expected_observables('Y1 a b model\n')


@pytest.mark.parametrize('version,accepted', [('47', True), ('42', False), ('48', False), ('470', False), ('47-dev', False), ('unknown', False)])
def test_preflight_requires_ngspice47_even_when_analytical_probes_pass(tmp_path, monkeypatch, version, accepted):
    monkeypatch.setattr(runner, 'runtime_inventory', lambda *args: {})
    def simulate(binary, is_ref, deck, directory, env, timeout):
        values = {'v(in)': 2.0, 'v(out)': 1.0, 'i(v1)': -.001} if directory.name == 'divider' else {'v(in)': 2.0, 'v(out)': 3.0}
        return dict(ok=True, values=values)
    monkeypatch.setattr(runner, 'run_simulator', simulate)
    monkeypatch.setattr(runner.subprocess, 'run', lambda *args, **kwargs:
                        SimpleNamespace(returncode=0, stdout=f'** ngspice-{version} : Circuit simulator\n', stderr=''))
    env = dict(SPICE_SCRIPTS=str(tmp_path), OPENBLAS_NUM_THREADS='1', OMP_NUM_THREADS='1', LC_ALL='C')
    args = (tmp_path, Path('neospice'), Path('ngspice'), tmp_path/'spinit', env, 10)
    if accepted:
        assert runner.preflight(*args)['passed']
    else:
        with pytest.raises(ValueError, match='preflight failed'):
            runner.preflight(*args)
    record = json.loads((tmp_path/'preflight.json').read_text())
    assert record['reference_version_matches'] is accepted
    assert record['passed'] is accepted
    assert not (tmp_path/'cases').exists()


def test_public_signal_with_internal_looking_name_is_still_compared():
    neo=dict(ok=True,values={'v(net_x1.port)':2.0})
    ref=dict(ok=True,values={'v(net_x1.port)':1.0})
    result=runner.compare_outcomes(neo,ref,['v(net_x1.port)'])
    assert result['status']=='MISMATCH'
    assert result['comparisons'][0]['var']=='v(net_x1.port)'
    assert result['excluded_reference_signals']==[]


def test_both_missing_a_required_signal_cannot_match():
    result=runner.compare_outcomes(dict(ok=True,values={'v(a)':1.0}),dict(ok=True,values={'v(a)':1.0}),['v(a)','v(b)'])
    assert result['status']=='MISMATCH'
    assert result['missing_required']=={'reference':['v(b)'],'neospice':['v(b)']}


@pytest.mark.parametrize('internal', ['v(m1#gate)', 'v(m1#body_diode)'])
def test_generated_vdmos_internal_voltage_is_excluded_and_recorded(internal):
    neo = dict(ok=True, values={'v(out)': 1.0})
    ref = dict(ok=True, values={'v(out)': 1.0, internal: 2.0})
    result = runner.compare_outcomes(neo, ref, ['v(out)'])
    assert result['status'] == 'MATCH'
    assert result['excluded_reference_signals'] == [internal]
    assert result['reference_required_signals'] == ['v(out)']


@pytest.mark.parametrize('signal', ['v(m1#gate)', 'v(m1#body_diode)'])
@pytest.mark.parametrize('present', [False, True])
def test_explicit_port_with_vdmos_internal_name_remains_required(signal, present):
    neo = dict(ok=True, values={'v(out)': 1.0})
    if present:
        neo['values'][signal] = 3.0
    ref = dict(ok=True, values={'v(out)': 1.0, signal: 2.0})
    result = runner.compare_outcomes(neo, ref, ['v(out)', signal])
    assert result['status'] == 'MISMATCH'
    assert result['excluded_reference_signals'] == []
    assert signal in result['reference_required_signals']
    assert any(row['var'] == signal and not row['ok'] for row in result['comparisons'])


@pytest.mark.parametrize('signal', ['v(user#gate)', 'v(m1#public)', 'i(m1#gate)', 'i(auto_dac3)'])
def test_unrecognized_hash_nodes_and_currents_are_not_internal(signal):
    result = runner.compare_outcomes(dict(ok=True, values={'v(out)': 1.0}),
                                    dict(ok=True, values={'v(out)': 1.0, signal: 2.0}),
                                    ['v(out)'])
    assert result['status'] == 'MISMATCH'
    assert result['excluded_reference_signals'] == []


@pytest.mark.parametrize('code,message,expected',[(0,'Error: failed','reported_error'),(0,'doAnalyses: failed','analysis_error'),(1,'Parse error: bad','parse_error'),(1,'unclassified failure','process_error'),(0,"Note: can't find the initialization file spinit.",'runtime_environment_error')])
def test_reported_errors_override_raw_data(code,message,expected):
    assert runner.classify_failure(code,message,'',{'v(out)':1})==expected


def test_subprocess_retains_partial_raw_and_diagnostics_on_failure(tmp_path):
    data=raw_bytes({'v(a)':1.0})
    binary=fake_runtime(tmp_path,"import sys,pathlib\np=pathlib.Path(sys.argv[sys.argv.index('-o')+1]);p.write_bytes("+repr(data)+")\nprint('retained output')\nprint('Simulation error: nonconverged',file=sys.stderr)\nsys.exit(1)\n")
    directory=tmp_path/'run';directory.mkdir();deck=directory/'fixture.cir';deck.write_text('* test\n')
    result=runner.run_simulator(binary,False,deck,directory,os.environ,2)
    assert not result['ok'] and result['failure_class']=='analysis_error'
    assert result['values']=={'v(a)':1.0}
    assert Path(result['raw']['path']).read_bytes()==data
    assert 'retained output' in Path(result['stdout']['path']).read_text()
    assert result['returncode']==1


def test_timeout_is_retained_without_accepting_partial_values(tmp_path):
    binary=fake_runtime(tmp_path,"import time\nprint('started',flush=True)\ntime.sleep(10)\n")
    directory=tmp_path/'run';directory.mkdir();deck=directory/'fixture.cir';deck.write_text('* test\n')
    result=runner.run_simulator(binary,False,deck,directory,os.environ,0.05)
    assert not result['ok'] and result['failure_class']=='timeout'
    assert result['returncode'] is None
    assert Path(result['stdout']['path']).exists()


def test_preflight_failure_stops_before_any_corpus_execution(tmp_path,monkeypatch):
    root=tmp_path/'Models';root.mkdir();(root/'a.lib').write_text('.model D D (IS=1e-14)\n')
    inputs=tmp_path/'inputs';freeze(root,inputs,check_revision=False)
    binary=fake_runtime(tmp_path,"import sys\nprint('missing runtime',file=sys.stderr)\nsys.exit(1)\n")
    startup=tmp_path/'spinit';startup.write_text('* no modules\n')
    output=tmp_path/'output'
    with pytest.raises(ValueError,match='preflight failed'):
        runner.run_experiment(inputs,root,output,binary,binary,startup)
    assert not (output/'cases').exists()
    assert not json.loads((output/'run.json').read_text())['complete']
    assert not json.loads((output/'preflight.json').read_text())['passed']


@pytest.mark.parametrize('rescue_ok',[True,False])
def test_primary_failure_and_any_rescue_outcome_are_retained(tmp_path,monkeypatch,rescue_ok):
    root=tmp_path/'Models';root.mkdir();(root/'a.lib').write_text('.subckt A a b\nR1 a b 1k\n.ends\n')
    inputs=tmp_path/'inputs';m=freeze(root,inputs,check_revision=False)
    binary=tmp_path/'binary';binary.write_text('not executed')
    startup=tmp_path/'spinit';startup.write_text('* explicit startup\n')
    inventory=runner.runtime_inventory(binary,binary,startup)
    monkeypatch.setattr(runner,'preflight',lambda *a,**kw:dict(inventory=inventory))
    calls=[]
    def simulated(binary,is_ref,deck,directory,env,timeout):
        calls.append((directory.name,is_ref))
        ok=directory.name=='isolated_driven' and rescue_ok
        required=runner.expected_observables(deck.read_text())
        return dict(ok=ok,values={s:1.0 for s in required} if ok else None,
                    failure_class=None if ok else 'analysis_error')
    monkeypatch.setattr(runner,'run_simulator',simulated)
    output=tmp_path/'output'
    result=runner.run_experiment(inputs,root,output,binary,binary,startup)
    assert calls==[('primary',False),('primary',True),('isolated_driven',False),('isolated_driven',True)]
    assert result['counts_by_variant']=={'primary':{'BOTH_FAIL':1},'isolated_driven':{'MATCH' if rescue_ok else 'BOTH_FAIL':1}}
    assert result['completed_fixtures']==2
    for variant in ('primary','isolated_driven'):
        assert (output/'cases'/m['cases'][0]['case_id']/variant/'comparison.json').exists()
    with pytest.raises(ValueError,match='output exists'):
        runner.run_experiment(inputs,root,output,binary,binary,startup)


def test_bounded_execution_does_not_eagerly_consume_the_whole_plan():
    from concurrent.futures import ThreadPoolExecutor
    consumed=[]
    def work():
        for i in range(100):
            consumed.append(i)
            yield i
    with ThreadPoolExecutor(max_workers=2) as pool:
        results=runner.bounded_results(pool,lambda x:x,work(),2)
        first=next(results)
        assert len(consumed)==2
        assert sorted([first,*results])==list(range(100))


def test_nonexecutable_runtime_is_a_retained_launch_failure(tmp_path):
    binary=tmp_path/'binary';binary.write_text('not executable')
    directory=tmp_path/'run';directory.mkdir();deck=directory/'fixture.cir';deck.write_text('* test\n')
    result=runner.run_simulator(binary,False,deck,directory,os.environ,1)
    assert not result['ok'] and result['failure_class']=='launch_error'
    assert Path(result['stderr']['path']).read_text()


def test_missing_runtime_module_is_rejected_before_simulation(tmp_path):
    binary=tmp_path/'binary';binary.write_text('not executed')
    startup=tmp_path/'spinit';startup.write_text('codemodel '+str(tmp_path/'missing.cm')+'\n')
    with pytest.raises(FileNotFoundError):
        runner.runtime_inventory(binary,binary,startup)


@pytest.mark.parametrize('mutation',['manifest','runtime','runner'])
def test_changed_provenance_cannot_be_marked_complete(tmp_path,monkeypatch,mutation):
    root=tmp_path/'Models';root.mkdir();(root/'a.lib').write_text('.model D D (IS=1e-14)\n')
    inputs=tmp_path/'inputs';freeze(root,inputs,check_revision=False)
    binary=tmp_path/'binary';binary.write_text('not executed')
    startup=tmp_path/'spinit';startup.write_text('* explicit startup\n')
    driver=tmp_path/'driver.py';driver.write_text('# original\n')
    inventory=runner.runtime_inventory(binary,binary,startup)
    source=runner.file_record(driver)
    monkeypatch.setattr(runner,'preflight',lambda *a,**kw:dict(inventory=inventory,runner_sources={'driver':source}))
    def simulated(binary,is_ref,deck,directory,env,timeout):
        if mutation=='runtime':
            binary.write_text('changed runtime')
        elif mutation=='runner':
            driver.write_text('# changed source\n')
        else:
            path=inputs/'manifest.json';data=json.loads(path.read_text());data['changed_metadata']=True;path.write_text(json.dumps(data))
        return dict(ok=True,values={s:1.0 for s in runner.expected_observables(deck.read_text())})
    monkeypatch.setattr(runner,'run_simulator',simulated)
    output=tmp_path/'output'
    with pytest.raises(ValueError,match='changed during execution'):
        runner.run_experiment(inputs,root,output,binary,binary,startup)
    assert not json.loads((output/'run.json').read_text())['complete']
    assert list((output/'cases').rglob('comparison.json'))
