#!/usr/bin/env python3
"""Run frozen primary/rescue fixtures with retained raw data and runtime preflight.

Compatibility evidence only: subprocess timings include startup, executions may
run concurrently, and this is not the paired performance benchmark harness.
"""
import argparse
from collections import Counter
from concurrent.futures import ThreadPoolExecutor, wait, FIRST_COMPLETED
import json
import math
import os
from pathlib import Path
import platform
import re
import subprocess
import time

from kicad_experiment import verify, digest, CORPUS_TOKEN, ASSET_TOKEN
from compare_kicad_models import (
    parse_raw_file, compare_values, is_internal_var, is_trivial_solution,
    RELTOL, VNTOL, ABSTOL,
)


def write_json(path, data):
    temporary = path.with_name(path.name + '.tmp')
    temporary.write_text(json.dumps(data, indent=2, allow_nan=False) + '\n')
    temporary.replace(path)


def file_record(path):
    path = Path(path).resolve()
    return dict(path=str(path), sha256=digest(path.read_bytes()), bytes=path.stat().st_size)


def expected_observables(text):
    """Public nodes/source currents from the limited historical fixture grammar.

    Never parse included vendor devices here: those are not top-level ports.
    Unknown generated element syntax is an input error, not an empty signal set.
    """
    nodes, currents = set(), set()
    widths = {'r': 2, 'c': 2, 'l': 2, 'v': 2, 'i': 2, 'd': 2,
              'q': 3, 'm': 4, 'j': 3, 'z': 3, 'e': 4, 'g': 4}
    for raw in text.splitlines():
        tokens = raw.strip().lower().split()
        if not tokens or tokens[0].startswith(('*', '.')):
            continue
        kind = tokens[0][0]
        if kind == 'x':
            body = []
            for token in tokens[1:]:
                if '=' in token or token == 'params:':
                    break
                body.append(token)
            if len(body) < 2:
                raise ValueError('malformed generated X instance')
            nodes.update(body[:-1])
        elif kind in widths:
            # POLY is covered by a separate explicit preflight expectation.
            if len(tokens) <= widths[kind]:
                raise ValueError('malformed generated element')
            nodes.update(tokens[1:1 + widths[kind]])
            if kind == 'v':
                currents.add('i(' + tokens[0] + ')')
        else:
            raise ValueError(f'unsupported generated fixture grammar: {tokens[0]}')
    nodes -= {'0', 'gnd'}
    result = sorted({'v(' + n + ')' for n in nodes} | currents)
    if not result:
        raise ValueError('fixture has no required public observables')
    return result


def runtime_environment(spinit):
    spinit = Path(spinit).resolve()
    if spinit.name != 'spinit' or not spinit.is_file():
        raise ValueError('--spinit must name an existing spinit file')
    env = dict(os.environ)
    env.update(SPICE_SCRIPTS=str(spinit.parent), OPENBLAS_NUM_THREADS='1',
               OMP_NUM_THREADS='1', LC_ALL='C')
    return env


def linked_libraries(path):
    """Record the resolved ELF dependencies; non-ELF test executables have none."""
    with Path(path).open('rb') as stream:
        if stream.read(4) != b'\x7fELF':
            return []
    result = subprocess.run(['ldd', str(path)], capture_output=True, text=True, timeout=10)
    if result.returncode != 0 or 'not found' in result.stdout:
        raise ValueError(f'unresolved runtime libraries for {path}: {result.stdout} {result.stderr}')
    paths = set(re.findall(r'(?:=>\s+|^\s*)(/\S+)\s+\(', result.stdout, re.MULTILINE))
    return [file_record(p) for p in sorted(paths)]


def runtime_inventory(neo, reference, spinit):
    """Hash explicit startup files and static code-model dependencies.

    Dynamic startup paths/recursive scripts need explicit support before use;
    failing preflight is preferable to silently hashing the wrong installation.
    """
    dependencies = [Path(spinit).resolve()]
    for raw in Path(spinit).read_text().splitlines():
        tokens = raw.strip().split()
        if not tokens or tokens[0].startswith('*'):
            continue
        if tokens[0].lower() in ('source', 'codemodel', 'osdi'):
            # The stock spinit has a disabled OSDI branch. Inventory its commands
            # in the file itself; code-model loading is checked independently.
            if tokens[0].lower() == 'osdi':
                continue
            if len(tokens) != 2 or any(c in tokens[1] for c in '$~'):
                raise ValueError('dynamic runtime dependency requires explicit resolution')
            path = Path(tokens[1].strip('"'))
            if not path.is_absolute() or tokens[0].lower() == 'source':
                raise ValueError('relative or recursive startup dependency needs audit')
            dependencies.append(path)
    return dict(neospice=file_record(neo), reference=file_record(reference),
                startup_and_codemodels=[file_record(p) for p in dependencies],
                linked_libraries={str(p): linked_libraries(p) for p in [neo, reference, *dependencies[1:]]},
                note='Inventory of static codemodel paths, not a trace of loaded modules; OSDI branch is not certified by this preflight')


def classify_failure(returncode, stdout, stderr, raw_values):
    combined = (stdout + '\n' + stderr).lower()
    if any(s in combined for s in ("can't find the initialization file", 'could not load codemodel')):
        return 'runtime_environment_error'
    if any(s in combined for s in ('cannot open file', 'could not find include file', 'no such file or directory')):
        return 'input_file_error'
    if any(s in combined for s in ('parse error:', 'error on line', 'unknown model', 'unknown subckt')):
        return 'parse_error'
    if any(s in combined for s in ('simulation error:', 'doanalyses:', 'simulation(s) aborted', 'timestep too small', 'failed to converge')):
        return 'analysis_error'
    if returncode != 0:
        return 'process_error'
    if re.search(r'(?m)^\s*(?:fatal\s+)?error\b', combined):
        return 'reported_error'
    if raw_values is None:
        return 'invalid_or_missing_operating_point'
    return None


def run_simulator(binary, reference, deck, directory, env, timeout):
    """Keep command, stdout, stderr, raw bytes and finite parsed OP values."""
    name = 'reference' if reference else 'neospice'
    raw = directory / (name + '.raw')
    command = ([str(binary), '-n', '-D', 'ngbehavior=psa', '-b', str(deck), '-r', str(raw)]
               if reference else [str(binary), str(deck), '-D', 'ngbehavior=psa', '-o', str(raw)])
    started = time.perf_counter()
    try:
        process = subprocess.run(command, cwd=directory, env=env, capture_output=True, timeout=timeout)
        stdout, stderr = process.stdout, process.stderr
        code, timed_out, launch_error = process.returncode, False, False
    except subprocess.TimeoutExpired as error:
        stdout, stderr = error.stdout or b'', error.stderr or b''
        code, timed_out, launch_error = None, True, False
    except OSError as error:
        stdout, stderr = b'', str(error).encode()
        code, timed_out, launch_error = None, False, True
    elapsed = time.perf_counter() - started
    (directory / (name + '.stdout')).write_bytes(stdout)
    (directory / (name + '.stderr')).write_bytes(stderr)
    values = parse_raw_file(raw, require_operating_point=True) if raw.exists() else None
    failure = ('launch_error' if launch_error else 'timeout' if timed_out else classify_failure(
        code, stdout.decode(errors='replace'), stderr.decode(errors='replace'), values))
    result = dict(ok=failure is None, failure_class=failure, returncode=code,
                  command=command, cwd=str(directory), elapsed_seconds=elapsed,
                  values=values, observed_signals=sorted(values or {}),
                  stdout=file_record(directory / (name + '.stdout')),
                  stderr=file_record(directory / (name + '.stderr')),
                  raw=file_record(raw) if raw.exists() else None)
    write_json(directory / (name + '.json'), result)
    return result


def compare_outcomes(neo, reference, required):
    if neo['ok'] and reference['ok']:
        missing = {key: sorted(set(required) - set(value['values']))
                   for key, value in [('neospice', neo), ('reference', reference)]}
        reference_required = {name: value for name, value in reference['values'].items()
                              if name in required or not is_internal_var(name)}
        passed, details = compare_values(neo['values'], reference_required, external_only=False)
        return dict(status='MATCH' if passed and not any(missing.values()) else 'MISMATCH',
                    missing_required=missing, comparisons=details,
                    reference_required_signals=sorted(reference_required),
                    excluded_reference_signals=sorted(set(reference['values']) - set(reference_required)))
    status = ('NEO_TRIVIAL' if is_trivial_solution(neo['values']) else 'NEO_ONLY') if neo['ok'] else ('NG_ONLY' if reference['ok'] else 'BOTH_FAIL')
    return dict(status=status, comparisons=[])


def preflight(output, neo, reference, spinit, env, timeout):
    inventory = runtime_inventory(neo, reference, spinit)
    probes = [
        ('divider', '* Runtime divider\nV1 in 0 2\nR1 in out 1k\nR2 out 0 1k\n.op\n.end\n',
         {'v(in)': 2.0, 'v(out)': 1.0, 'i(v1)': -0.001}),
        ('poly', '* Required POLY runtime\nV1 in 0 2\nE1 out 0 POLY(1) in 0 1 1\nR1 out 0 1k\n.op\n.end\n',
         {'v(in)': 2.0, 'v(out)': 3.0}),
    ]
    results = []
    for name, text, expected in probes:
        directory = output / 'preflight' / name
        directory.mkdir(parents=True)
        deck = directory / 'fixture.cir'; deck.write_text(text)
        pair = [run_simulator(binary, is_ref, deck, directory, env, timeout)
                for binary, is_ref in [(neo, False), (reference, True)]]
        for result in pair:
            result['analytic_passed'] = result['ok'] and all(
                name in result['values'] and math.isclose(result['values'][name], value, rel_tol=1e-10, abs_tol=1e-12)
                for name, value in expected.items())
        results.append(dict(name=name, expected=expected, neospice=pair[0], reference=pair[1]))
    version = subprocess.run([str(reference), '--version'], env=env, cwd=output,
                             capture_output=True, text=True, timeout=timeout)
    version_matches = bool(re.search(r'\bngspice-47(?=\s|:|$)', version.stdout))
    record = dict(inventory=inventory, probes=results,
                  reference_version=dict(command=[str(reference), '--version'], returncode=version.returncode,
                                         stdout=version.stdout, stderr=version.stderr),
                  required_reference_version=47, reference_version_matches=version_matches,
                  passed=version.returncode == 0 and version_matches and all(p[s]['analytic_passed'] for p in results for s in ('neospice', 'reference')),
                  reference_options=['-n', '-D', 'ngbehavior=psa'],
                  startup_policy='Explicit system spinit via SPICE_SCRIPTS; personal/local spiceinit disabled with -n; system startup still loaded',
                  environment={k:env[k] for k in ('SPICE_SCRIPTS','OPENBLAS_NUM_THREADS','OMP_NUM_THREADS','LC_ALL')},
                  platform=platform.platform(), python=platform.python_version(), cpu_count=os.cpu_count(),
                  runner_sources={p.name: file_record(p) for p in [Path(__file__), Path(__file__).with_name('compare_kicad_models.py'), Path(__file__).with_name('kicad_experiment.py')]})
    write_json(output / 'preflight.json', record)
    if not record['passed']:
        raise ValueError('runtime preflight failed; no corpus fixtures executed')
    return record


def run_fixture(row, item, inputs, corpus, output, neo, reference, env, timeout):
    directory = output / 'cases' / row['case_id'] / item['variant']
    directory.mkdir(parents=True)
    template = (inputs / item['assets']['netlist']['path']).read_text()
    text = template.replace(CORPUS_TOKEN, str(corpus)).replace(ASSET_TOKEN, str(inputs / 'assets'))
    required = expected_observables(text)
    deck = directory / 'fixture.cir'; deck.write_text(text)
    neo_result = run_simulator(neo, False, deck, directory, env, timeout)
    ref_result = run_simulator(reference, True, deck, directory, env, timeout)
    result = dict(case_id=row['case_id'], identity=row['identity'], variant=item['variant'],
                  fixture_id=item['fixture_id'], materialized_fixture=file_record(deck),
                  required_signals=required, neospice=neo_result, reference=ref_result,
                  **compare_outcomes(neo_result, ref_result, required))
    write_json(directory / 'comparison.json', result)
    return result


def bounded_results(pool, execute, work, limit):
    """Keep a bounded batch of jobs/results resident while streaming to disk."""
    iterator = iter(work)
    pending = set()
    for _ in range(limit):
        try:
            pending.add(pool.submit(execute, next(iterator)))
        except StopIteration:
            break
    while pending:
        done, pending = wait(pending, return_when=FIRST_COMPLETED)
        for future in done:
            yield future.result()
            try:
                pending.add(pool.submit(execute, next(iterator)))
            except StopIteration:
                pass


def run_experiment(inputs, corpus, output, neo, reference, spinit, *, case_ids=None, jobs=1, timeout=10):
    inputs, corpus, output = [Path(p).resolve() for p in (inputs, corpus, output)]
    neo, reference = [Path(p).resolve() for p in (neo, reference)]
    if output.exists():
        raise ValueError('output exists; choose a new run directory')
    if any(root == output or root in output.parents for root in (inputs, corpus)):
        raise ValueError('run output must be outside frozen inputs and corpus')
    if jobs < 1 or not math.isfinite(timeout) or timeout <= 0:
        raise ValueError('positive jobs and timeout required')
    manifest = verify(corpus, inputs)
    wanted = set(case_ids) if case_ids is not None else {c['case_id'] for c in manifest['cases']}
    selected = [c for c in manifest['cases'] if c['case_id'] in wanted]
    if not wanted or {c['case_id'] for c in selected} != wanted:
        raise ValueError('selection is empty or contains unknown case IDs')
    work = [(row, item) for row in selected for item in row['fixtures']]
    # Validate generated public observables for the entire selected plan first.
    for row, item in work:
        expected_observables((inputs / item['assets']['netlist']['path']).read_text())
    output.mkdir(parents=True)
    plan = dict(input_manifest=file_record(inputs / 'manifest.json'),
                selected_cases=[c['case_id'] for c in selected],
                fixtures=[dict(case_id=r['case_id'], variant=f['variant'], fixture_id=f['fixture_id']) for r,f in work],
                jobs=jobs, timeout_seconds=timeout, complete=False,
                comparison=dict(reltol=RELTOL,vntol=VNTOL,abstol=ABSTOL,
                                formula='abs(neo-ng) <= reltol*max(abs(neo),abs(ng))+absolute_tolerance',
                                signals='required generated public nodes/source currents plus all non-internal reference outputs'),
                timing_scope='subprocess wall time, startup included; concurrent throughput run, not performance evidence')
    write_json(output / 'run.json', plan)
    write_json(output / 'progress.json', dict(phase='preflight', completed_fixtures=0, total_fixtures=len(work)))
    env = runtime_environment(spinit)
    runtime = preflight(output, neo, reference, spinit, env, timeout)
    write_json(output / 'progress.json', dict(phase='running', completed_fixtures=0, total_fixtures=len(work)))
    def execute(args):
        row, item = args
        return run_fixture(row, item, inputs, corpus, output, neo, reference, env, timeout)
    counts, completed = {}, 0
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        for result in bounded_results(pool, execute, work, jobs):
            counts.setdefault(result['variant'], Counter())[result['status']] += 1
            completed += 1
            if completed % 100 == 0:
                write_json(output / 'progress.json', dict(phase='running', completed_fixtures=completed, total_fixtures=len(work), counts_by_variant=counts))
    # Refuse to label a run complete if inputs/runtime changed during execution.
    verify(corpus, inputs)
    if file_record(inputs / 'manifest.json') != plan['input_manifest']:
        raise ValueError('input manifest changed during execution; run remains incomplete')
    if runtime_inventory(neo, reference, spinit) != runtime['inventory']:
        raise ValueError('runtime changed during execution; run remains incomplete')
    if any(file_record(v['path']) != v for v in runtime.get('runner_sources', {}).values()):
        raise ValueError('runner source changed during execution; run remains incomplete')
    if completed != len(work):
        raise ValueError('incomplete fixture accounting')
    summary = {variant: dict(sorted(counts[variant].items())) for variant in sorted(counts)}
    plan.update(complete=True, completed_fixtures=completed, counts_by_variant=summary)
    write_json(output / 'run.json', plan)
    write_json(output / 'progress.json', dict(phase='complete', completed_fixtures=completed, total_fixtures=len(work), counts_by_variant=summary))
    return plan


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('inputs','corpus','output','neospice','ngspice','spinit'):
        parser.add_argument('--'+name, type=Path, required=True)
    parser.add_argument('--case-id', action='append', help='Exact frozen declaration ID; default all cases and every planned variant')
    parser.add_argument('--jobs', type=int, default=1)
    parser.add_argument('--timeout', type=float, default=10)
    args=parser.parse_args()
    plan=run_experiment(args.inputs,args.corpus,args.output,args.neospice,args.ngspice,args.spinit,
                        case_ids=args.case_id,jobs=args.jobs,timeout=args.timeout)
    print(json.dumps({k:plan[k] for k in ('complete','completed_fixtures','counts_by_variant')},indent=2))


if __name__=='__main__':
    main()
