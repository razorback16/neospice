#!/usr/bin/env python3
"""Run the paired benchmark with an immutable-input and runtime manifest.

The CMake build must already be complete. Use --verify-only during other work;
--measure requires a quiet period established by the caller. This program
records available system observations, not a certificate of exclusive CPU use.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import pwd
import re
import shlex
import subprocess
import sys

from summarize_paired_benchmark import summarize

SOURCE_ROOTS = {'src', 'include', 'tests', 'tools', 'python'}
SOURCE_TOP = {'CMakeLists.txt', 'pyproject.toml', 'NOTICE', 'LICENSE'}


def file_record(path):
    path = Path(path).resolve(strict=True)
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return {'path': str(path), 'bytes': path.stat().st_size, 'sha256': digest.hexdigest()}


def command(argv, cwd=None):
    result = subprocess.run(argv, cwd=cwd, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, timeout=30)
    if result.returncode:
        raise ValueError(f'{argv[0]} failed ({result.returncode}): {result.stdout}')
    return result.stdout


def logical_lines(path):
    pending = ''
    for line in Path(path).read_text().splitlines():
        line = line.strip()
        if not line or line.startswith('*'):
            continue
        if line.startswith('+'):
            pending += ' ' + line[1:]
        else:
            if pending:
                yield pending
            pending = line
    if pending:
        yield pending


def fixture_closure(paths):
    """Resolve literal includes used by this benchmark; reject dynamic controls.

    This is a bounded inventory reader, not a SPICE parser. .lib section blocks
    are inventoried in their entirety; file+section requests include that file.
    Unsupported include syntax fails instead of silently omitting a dependency.
    """
    found = {}
    queue = list(map(Path, paths))
    while queue:
        path = queue.pop().resolve(strict=True)
        if str(path) in found:
            continue
        found[str(path)] = file_record(path)
        for line in logical_lines(path):
            head = line.split(maxsplit=1)[0].lower()
            if head in ('.control', '.source', '.shell'):
                raise ValueError(f'Uninventoried runtime control in {path}: {head}')
            if head not in ('.include', '.inc', '.lib'):
                continue
            args = shlex.split(line, comments=False)[1:]
            if not args or any(c in args[0] for c in ('$', '{', '}', '\\')):
                raise ValueError(f'Nonliteral include in {path}: {line}')
            if head == '.lib' and len(args) == 1:
                # A bare section name is not a file dependency. A file-looking
                # token must exist; otherwise a missing library could be hidden.
                candidate = path.parent / args[0]
                if not candidate.is_file() and not Path(args[0]).suffix and '/' not in args[0]:
                    continue
            elif len(args) != (2 if head == '.lib' else 1):
                raise ValueError(f'Unsupported include syntax in {path}: {line}')
            if not args or any(c in args[0] for c in ('$', '{', '}', '\\')):
                raise ValueError(f'Nonliteral include in {path}: {line}')
            queue.append(path.parent / args[0])
    return found


def source_inventory(repo):
    names = command(['git', 'ls-files', '--cached', '--others', '--exclude-standard', '-z'], repo)
    paths = set()
    for name in names.split('\0'):
        if name and (Path(name).parts[0] in SOURCE_ROOTS or name in SOURCE_TOP):
            path = repo / name
            if path.is_file():
                paths.add(path.resolve())
    return {str(p): file_record(p) for p in sorted(paths)}


def linked_libraries(binary, require_reference=True):
    output = command(['ldd', str(binary)])
    if 'not found' in output:
        raise ValueError('Unresolved benchmark dependency: ' + output)
    paths = []
    for line in output.splitlines():
        match = re.search(r'(?:=>\s+)?(/[^\s]+)\s+\(', line)
        if match:
            paths.append(Path(match[1]).resolve(strict=True))
    if require_reference and not any('libngspice' in p.name for p in paths):
        raise ValueError('Benchmark does not resolve a libngspice dependency')
    return {'ldd': re.sub(r'\(0x[0-9a-fA-F]+\)', '(address)', output), 'files': {str(p): file_record(p) for p in sorted(set(paths))}}


def runtime_files(spinit):
    files = {str(spinit): file_record(spinit)}
    flags, conditions = {}, []
    for line in logical_lines(spinit):
        args = shlex.split(line, comments=False)
        if not args:
            continue
        op = args[0].lower()
        if op == 'if':
            match = re.fullmatch(r'\$\?(\w+)', args[1]) if len(args) == 2 else None
            if not match:
                raise ValueError('Uninventoried startup condition: ' + line)
            conditions.append(flags.get(match[1]))
            continue
        if op == 'else':
            if not conditions:
                raise ValueError('Unmatched startup else')
            conditions[-1] = None if conditions[-1] is None else not conditions[-1]
            continue
        if op == 'end':
            if not conditions:
                raise ValueError('Unmatched startup end')
            conditions.pop()
            continue
        if op not in ('alias', 'set', 'unset', 'codemodel', 'osdi', 'pre_osdi'):
            raise ValueError('Uninventoried startup command: ' + line)
        inactive = False in conditions
        if op in ('set', 'unset') and not inactive:
            for arg in args[1:]:
                flags[arg.split('=')[0]] = None if None in conditions else op == 'set'
        if op in ('codemodel', 'osdi', 'pre_osdi'):
            if len(args) != 2 or any(c in args[1] for c in ('$', '{', '}')):
                raise ValueError('Nonliteral runtime module path: ' + line)
            path = Path(args[1])
            if not path.is_absolute():
                raise ValueError('Runtime module path must be absolute: ' + line)
            path = path.resolve()
            # Stock spinit explicitly disables OSDI, but lists optional modules
            # in its inactive block. Record their absence instead of claiming
            # they were loaded or treating them as active missing dependencies.
            if inactive and not path.exists():
                files[str(path)] = {'path': str(path), 'exists': False,
                                    'inactive_startup_branch': True}
            else:
                files[str(path)] = file_record(path)
                files.update(linked_libraries(path, require_reference=False)['files'])
    if conditions:
        raise ValueError('Unclosed startup condition')
    return files


def inventory(repo, build, binary, fixtures, spinit):
    build_files = [build / 'CMakeCache.txt',
                   build / f'tests/CMakeFiles/{binary.name}.dir/flags.make',
                   build / f'tests/CMakeFiles/{binary.name}.dir/link.txt',
                   build / 'src/libneospice_lib.a']
    cache = build_files[0].read_text()
    compiler = re.search(r'^CMAKE_CXX_COMPILER:[^=]+=(.*)$', cache, re.M)
    if not compiler:
        raise ValueError('Build cache does not identify the compiler')
    user_dirs = {repo, Path.home(), Path(pwd.getpwuid(os.getuid()).pw_dir)}
    if os.environ.get('SPICE_USERINIT_DIR'):
        user_dirs.add(Path(os.environ['SPICE_USERINIT_DIR']))
    if os.environ.get('USERPROFILE'):
        user_dirs.add(Path(os.environ['USERPROFILE']))
    user_init = {}
    for directory in user_dirs:
        for name in ('.spiceinit', 'spice.rc'):
            path = (directory / name).resolve()
            user_init[str(path)] = runtime_files(path) if path.is_file() else None
    home = re.search(r'^CMAKE_HOME_DIRECTORY:INTERNAL=(.*)$', cache, re.M)
    if not home or Path(home[1]).resolve() != repo:
        raise ValueError('Build directory does not belong to the declared checkout')
    return {'head': command(['git', 'rev-parse', 'HEAD'], repo).strip(),
            'binary': file_record(binary), 'source': source_inventory(repo),
            'fixtures': fixture_closure(fixtures), 'libraries': linked_libraries(binary),
            'startup_and_code_models': runtime_files(spinit), 'user_initialization_candidates': user_init,
            'compiler': dict(file=file_record(compiler[1]), version=command([compiler[1], '--version'])),
            'build': {str(p): dict(file_record(p), **({'text': p.read_text()} if p.suffix in ('.txt', '.make') else {})) for p in build_files}}


def system_observation():
    def read(path):
        try:
            return Path(path).read_text().strip()
        except OSError:
            return None
    cpu = read('/proc/cpuinfo') or ''
    selected_cpu = [line for line in cpu.splitlines()
                    if line.startswith(('model name', 'vendor_id', 'microcode', 'cpu MHz'))]
    return {'utc': datetime.now(timezone.utc).isoformat(), 'platform': platform.platform(),
            'machine': platform.machine(), 'python': sys.version,
            'affinity': sorted(os.sched_getaffinity(0)), 'cpu': selected_cpu,
            'load_average': list(os.getloadavg()), 'memory': read('/proc/meminfo'),
            'clocksource': read('/sys/devices/system/clocksource/clocksource0/current_clocksource'),
            'governor_cpu0': read('/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor'),
            'visible_processes': command(['ps', '-eo', 'pid,comm,pcpu,etime'])}


TLV3201_POLICY = {
    'kind': 'tlv3201-v1', 'crossing_absolute_s': 5e-8, 'crossing_strict': True,
    'port_relative': 0.01, 'port_denominator_floor_v': 0.01,
    'edge_low_v': 0.3, 'edge_high_v': 3.0, 'settle_window_s': 1e-6,
    'required_voltages': ['v(vcc)', 'v(inm)', 'v(inp)', 'v(out)'],
    'dc_ports': ['v(vcc)', 'v(inm)'], 'start_s': 0, 'stop_s': 3e-5,
    'error_scale': 'fraction_of_allowance',
}


def check_tlv_evidence(row):
    c = row['comparison']; signals = c.get('signals', [])
    ports = [s for s in signals if s.get('name') in TLV3201_POLICY['dc_ports']]
    edges = [s for s in signals if s.get('name', '').startswith('v(out):')]
    reference = row.get('reference_edges', []); actual = row.get('actual_edges', [])
    if sorted(s['name'] for s in ports) != sorted(TLV3201_POLICY['dc_ports']) or not edges or \
            len(signals) != len(ports)+len(edges) or len(edges) != len(reference) or len(edges) != len(actual):
        raise ValueError('TLV3201 required port/edge evidence missing')
    for i, (signal, ref, neo) in enumerate(zip(edges, reference, actual)):
        values = [ref.get('cross_time_s'), neo.get('cross_time_s'), ref.get('rise_time_s'), neo.get('rise_time_s')]
        if not all(type(x) in (int, float) and math.isfinite(x) for x in values):
            raise ValueError('TLV3201 incomplete edge evidence')
        r, n, rise_r, rise_n = values
        name = 'v(out):'+('rising:' if rise_r > 0 else 'falling:')+str(i)
        error = abs(r-n); fraction = error/TLV3201_POLICY['crossing_absolute_s']
        if rise_r*rise_n <= 0 or signal['name'] != name or signal['points'] != 1 or \
                signal['unit'] != 's' or not fraction < 1 or \
                not math.isclose(signal['max_absolute_error'], error, rel_tol=1e-12, abs_tol=0) or \
                not math.isclose(signal['max_normalized_error'], fraction, rel_tol=1e-12, abs_tol=0) or \
                signal['reference'] != r or signal['actual'] != n or signal['coordinate'] != r:
            raise ValueError('TLV3201 crossing threshold or direction violated')
    for signal in ports:
        fraction = abs(signal['reference']-signal['actual']) / \
            max(abs(signal['reference']), TLV3201_POLICY['port_denominator_floor_v']) / TLV3201_POLICY['port_relative']
        if not math.isfinite(fraction) or fraction > 1 or not math.isclose(
                signal['max_normalized_error'], fraction, rel_tol=1e-12, abs_tol=0):
            raise ValueError('TLV3201 port allowance evidence invalid')
    if 'informational_waveform_comparison' not in row:
        raise ValueError('TLV3201 informational waveform evidence missing')


def check_protocol(records, description, version, verify, samples, warmup, case=None, spinit=None):
    result = summarize(records)
    meta = result['metadata']
    expected = [w['id'] for w in description['workloads'] if case is None or w['id'] == case]
    policy = description.get('validation_policy')
    if meta.get('validation_policy') != policy:
        raise ValueError('Executed validation policy differs from the binary description')
    if policy is not None:
        if policy != TLV3201_POLICY or description['workloads'] != [
                {'id': 'tran_tlv3201', 'file': 'tlv3201_switching.cir', 'command': 'tran 100n 30u'}]:
            raise ValueError('Unknown or altered specialized validation policy')
        threshold, floor = 1, None
    else:
        threshold, floor = 1e-3, 1e-9
    if meta.get('relative_tolerance') != threshold or meta.get('denominator_floor') != floor:
        raise ValueError('Benchmark qualification threshold changed')
    if not expected or meta['population'] != expected:
        raise ValueError('Executed workload population differs from the binary description')
    if meta['verify_only'] != verify or meta['samples'] != (0 if verify else samples) or meta['warmup'] != (0 if verify else warmup):
        raise ValueError('Executed sample policy differs from the requested policy')
    if not re.search(r'\bngspice-' + re.escape(str(version)) + r'\b', meta.get('reference_version', '')):
        raise ValueError('Reference version differs from the requested baseline')
    if not re.search(r'\bnum_threads\s+1\s*\n', meta.get('reference_settings', '')):
        raise ValueError('Effective reference thread setting is not recorded as one')
    if spinit is not None:
        match = re.search(r'\binputdir\s+([^\n]+)', meta.get('reference_settings', ''))
        if not match or Path(match[1].strip()).resolve() != Path(spinit).parent.resolve():
            raise ValueError('Reference startup directory differs from the declared spinit')
    declared = {w['id']: w for w in description['workloads']}
    for row in records:
        if row['type'] == 'workload':
            if any(row.get(k) != declared[row['id']][k] for k in ('file', 'command')):
                raise ValueError('Executed workload differs from its declaration')
        if row['type'] == 'pair':
            if policy is not None:
                check_tlv_evidence(row)
            if row['comparison']['worst_error'] > meta['relative_tolerance']:
                raise ValueError('Accepted pair exceeds its qualification threshold')
            text = row.get('reference_diagnostics', '')
            if 'Using SPARSE 1.3 as Direct Linear Solver' not in text:
                raise ValueError('Active default Sparse solver is not confirmed')
    return result


def check_immutable(before, after):
    if before != after:
        changed = [key for key in before.keys() | after.keys() if before.get(key) != after.get(key)]
        raise ValueError('Benchmark inputs changed during execution: ' + ', '.join(changed))


def run(args):
    repo = args.repo.resolve(strict=True)
    build = args.build.resolve(strict=True)
    binary = (build / 'tests' / args.benchmark).resolve(strict=True)
    spinit = args.spinit.resolve(strict=True)
    if spinit.name != 'spinit':
        raise ValueError('The declared startup file must be named spinit')
    if os.environ.get('LD_PRELOAD') or os.environ.get('LD_AUDIT'):
        raise ValueError('Loader injection requires a separate dependency audit')
    output = args.output.resolve()
    for name in SOURCE_ROOTS:
        if output.is_relative_to(repo / name):
            raise ValueError('Output must be outside inventoried source directories')
    output.mkdir(parents=True, exist_ok=False)
    manifest_path = output / 'manifest.json'
    state = {'schema': 1, 'phase': 'preparing', 'complete': False,
             'evidence_valid': False, 'verify_only': args.verify_only,
             'limits': ['Pre/post hashes detect persistent changes, not every transient mutation.',
                        'Build/source association requires the recorded build verification; inventory alone does not prove a clean rebuild.',
                        'System observations do not prove absence of competing host jobs.',
                        'Literal include/startup reader supports the declared benchmark inputs, not arbitrary SPICE control scripts.']}
    def save():
        temp = manifest_path.with_suffix('.tmp')
        temp.write_text(json.dumps(state, indent=2, allow_nan=False) + '\n')
        temp.replace(manifest_path)
    save()
    try:
        binary_before_description = file_record(binary)
        description = json.loads(command([str(binary), '--describe']))
        if description.get('schema') != 1 or not description.get('workloads'):
            raise ValueError('Invalid binary workload description')
        fixtures = [Path(description['circuits_root']) / w['file'] for w in description['workloads']]
        before = inventory(repo, build, binary, fixtures, spinit)
        if before['binary'] != binary_before_description:
            raise ValueError('Binary changed while reading its workload declaration')
        state.update(description=description, inventory_before=before,
                     git_status=command(['git', 'status', '--porcelain'], repo),
                     observation_before=system_observation())
        env = os.environ.copy()
        env.update(OMP_NUM_THREADS='1', OPENBLAS_NUM_THREADS='1', SPICE_SCRIPTS=str(spinit.parent))
        argv = [str(binary), '--output', str(output / 'pairs.jsonl'),
                '--samples', str(args.samples), '--warmup', str(args.warmup)]
        if args.verify_only:
            argv.append('--verify-only')
        if args.case:
            argv += ['--case', args.case]
        state.update(phase='running', command=argv,
                     environment={key: env[key] for key in ('OMP_NUM_THREADS', 'OPENBLAS_NUM_THREADS', 'SPICE_SCRIPTS')})
        save()
        with (output / 'stdout.log').open('x') as stdout, (output / 'stderr.log').open('x') as stderr:
            process = subprocess.run(argv, cwd=repo, env=env, stdout=stdout, stderr=stderr,
                                     timeout=args.timeout)
        state.update(returncode=process.returncode, observation_after=system_observation())
        after = inventory(repo, build, binary, fixtures, spinit)
        state['inventory_after'] = after
        check_immutable(before, after)
        records = [json.loads(line) for line in (output / 'pairs.jsonl').read_text().splitlines()]
        result = check_protocol(records, description, args.reference_version, args.verify_only,
                                args.samples, args.warmup, args.case, spinit)
        expected_return = 0 if all(c['accepted'] for c in result['cases']) else 1
        if process.returncode != expected_return:
            raise ValueError('Process exit status contradicts the recorded results')
        (output / 'summary.json').write_text(json.dumps(result, indent=2, allow_nan=False) + '\n')
        state.update(phase='complete', complete=True, evidence_valid=True,
                     all_workloads_qualified=expected_return == 0,
                     artifacts={p.name: file_record(p) for p in output.iterdir()
                                if p.is_file() and p != manifest_path})
        save()
        return expected_return
    except Exception as error:
        state.update(phase='failed', complete=False, evidence_valid=False,
                     error=f'{type(error).__name__}: {error}')
        save()
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--benchmark', choices=('bench_comprehensive', 'bench_ths4131', 'bench_tlv3201'),
                        default='bench_comprehensive', help='Declared paired workload population')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--spinit', type=Path, required=True)
    parser.add_argument('--reference-version', type=int, choices=(47,), default=47,
                        help='Required ngspice compatibility reference (47)')
    modes = parser.add_mutually_exclusive_group(required=True)
    modes.add_argument('--verify-only', action='store_true')
    modes.add_argument('--measure', action='store_true')
    parser.add_argument('--samples', type=int, default=30)
    parser.add_argument('--warmup', type=int, default=3)
    parser.add_argument('--case')
    parser.add_argument('--timeout', type=float, default=3600)
    args = parser.parse_args()
    if args.samples < 1 or args.warmup < 0 or args.timeout <= 0:
        parser.error('invalid sample counts or timeout')
    try:
        raise SystemExit(run(args))
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        parser.exit(2, str(error) + '\n')


if __name__ == '__main__':
    main()
