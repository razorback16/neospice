#!/usr/bin/env python3
"""Freeze and verify KiCad experiment inputs before running any simulator.

This preserves the historical generator's population, including its limitations.
A declaration identity describes where a case originated, not which duplicate
or nested declaration a simulator resolves through a whole-library include.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import subprocess

from test_kicad_models import (
    extract_models, extract_subcircuits, make_model_test, make_subcircuit_test,
)
from compare_kicad_models import make_isolated_driven_netlist

SCHEMA = 'neospice-kicad-inputs-v1'
CORPUS_REVISION = 'a8688952bcaab19f567bc4db237b60bde03ef310'
CORPUS_TOKEN = '__KICAD_MODELS_ROOT__'
ASSET_TOKEN = '__FROZEN_ASSETS_ROOT__'


def digest(data):
    return hashlib.sha256(data).hexdigest()


def canonical(data):
    return json.dumps(data, sort_keys=True, separators=(',', ':'),
                      ensure_ascii=True, allow_nan=False).encode()


def case_id(identity):
    """Paths relative to Models; kind, lexical scope and occurrence are required."""
    required = {'file', 'kind', 'scope', 'name', 'occurrence'}
    if set(identity) != required or identity['kind'] not in ('model', 'subckt'):
        raise ValueError('invalid declaration identity')
    path = Path(identity['file'])
    if path.is_absolute() or '..' in path.parts or not identity['name']:
        raise ValueError('identity needs a relative corpus path and nonempty name')
    if not isinstance(identity['occurrence'], int) or identity['occurrence'] < 1:
        raise ValueError('occurrence must be a positive integer')
    return 'case-v1-' + digest(canonical(identity))


def declarations(text, relative_file):
    """Record lexical subcircuit/library scopes, without claiming SPICE resolution.

    Scope entries carry occurrences, so two same-named enclosing subcircuits
    cannot conflate their internal declarations. Line numbers are provenance;
    blank-line changes do not change identity.
    """
    scope, counts, result = [], Counter(), {}
    for number, raw in enumerate(text.split('\n'), 1):
        tokens = raw.strip().split()
        if not tokens:
            continue
        directive = tokens[0].lower()
        if directive in ('.ends', '.endl'):
            closing = 'subckt' if directive == '.ends' else 'lib'
            for index in range(len(scope) - 1, -1, -1):
                if scope[index]['kind'] == closing:
                    del scope[index:]
                    break
        elif directive in ('.model', '.subckt', '.lib') and len(tokens) > 1:
            # .lib FILE SECTION is an inclusion, not a section declaration.
            if directive == '.lib' and len(tokens) != 2:
                continue
            kind, name = directive[1:], tokens[1]
            key = (canonical(scope), kind, name.lower())
            counts[key] += 1
            identity = dict(file=relative_file, kind=kind, scope=list(scope),
                            name=name.lower(), occurrence=counts[key])
            if kind != 'lib':
                result[number] = dict(identity=identity, case_id=case_id(identity),
                                      source_line=number, declared_name=name)
            if kind in ('subckt', 'lib'):
                scope.append(dict(kind=kind, name=name.lower(), occurrence=counts[key]))
    return result


def source_inventory(root):
    result = {}
    for path in sorted(root.rglob('*')):
        if path.is_symlink():
            raise ValueError(f'corpus symlink requires explicit handling: {path}')
        if path.is_file():
            result[path.relative_to(root).as_posix()] = digest(path.read_bytes())
    if not result:
        raise ValueError('empty corpus')
    return result


def write_asset(output, text):
    raw = text.encode()
    sha = digest(raw)
    path = output / 'assets' / (sha + '.txt')
    if path.exists():
        if path.read_bytes() != raw:
            raise ValueError('asset hash collision')
    else:
        path.write_bytes(raw)
    return dict(path=path.relative_to(output).as_posix(), sha256=sha, bytes=len(raw))


def fixture(output, variant, netlist, library=None):
    assets = {}
    if library is not None:
        assets['library'] = write_asset(output, library)
        netlist = netlist.replace('__ISO_LIB__', ASSET_TOKEN + '/' +
                                  Path(assets['library']['path']).name)
    assets['netlist'] = write_asset(output, netlist)
    return dict(variant=variant, fixture_id='fixture-v1-' + digest(canonical(assets)),
                assets=assets)


def freeze(root, output, *, revision=CORPUS_REVISION, check_revision=True):
    root, output = root.resolve(), output.resolve()
    if output.exists():
        raise ValueError('output already exists; choose a new experiment directory')
    if output == root or root in output.parents:
        raise ValueError('experiment output must be outside the corpus')
    if check_revision:
        actual = subprocess.check_output(
            ['git', '-C', str(root), 'rev-parse', 'HEAD'], text=True).strip()
        if actual != revision:
            raise ValueError(f'expected corpus revision {revision}, got {actual}')
        dirty = subprocess.check_output(
            ['git', '-C', str(root), 'status', '--porcelain'], text=True)
        if dirty.strip():
            raise ValueError('corpus checkout is dirty')
    generators = [Path(__file__), Path(__file__).with_name('test_kicad_models.py'),
                  Path(__file__).with_name('compare_kicad_models.py')]
    generator_hashes = {p.name: digest(p.read_bytes()) for p in generators}
    inventory = source_inventory(root)
    output.mkdir(parents=True)
    (output / 'assets').mkdir()
    cases, not_generated = [], []
    extensions = {'.lib', '.mod', '.sub', '.spice', '.cir'}
    # Exact historical extension selection, including its case sensitivity.
    files = [root / p for p in inventory if Path(p).suffix in extensions]
    for source in files:
        relative = source.relative_to(root).as_posix()
        index = declarations(source.read_text(errors='replace'), relative)
        generated = []
        for name, model_type, path, metadata in extract_models(source, with_source=True):
            generated.append(('model', name, model_type,
                              make_model_test(name, model_type, path), metadata))
        for name, ports, path, roles, params, metadata in extract_subcircuits(source, with_source=True):
            generated.append(('subckt', name, f'{len(ports)}-port',
                              make_subcircuit_test(name, ports, path, roles, params), metadata))
        selected = set()
        for kind, name, info, netlist, metadata in generated:
            entry = index.get(metadata['line'])
            if not entry or entry['identity']['kind'] != kind or entry['declared_name'] != name:
                raise ValueError(f'cannot locate generated declaration: {relative}:{metadata}')
            selected.add(entry['case_id'])
            if not netlist:
                not_generated.append(dict(entry, reason='historical_generator_returned_no_fixture'))
                continue
            primary = netlist.replace(str(root), CORPUS_TOKEN)
            row = dict(entry, info=info, source_sha256=inventory[relative],
                       fixture_resolution='whole_library_name_lookup',
                       fixtures=[fixture(output, 'primary', primary)])
            # Plan rescue for every subcircuit before simulator outcomes exist.
            # This extraction is heuristic and is retained as a separate experiment.
            if kind == 'subckt':
                rescue, library = make_isolated_driven_netlist(netlist)
                if rescue is not None and library is not None:
                    row['fixtures'].append(fixture(output, 'isolated_driven',
                                                    rescue.replace(str(root), CORPUS_TOKEN), library))
                else:
                    row['rescue_unavailable'] = 'historical_isolation_generator_could_not_extract'
            cases.append(row)
        for entry in index.values():
            if entry['case_id'] not in selected:
                not_generated.append(dict(entry, reason='not_selected_by_historical_generator'))
    keys = [c['case_id'] for c in cases]
    if len(keys) != len(set(keys)):
        raise ValueError('duplicate declaration identities in generated cohort')
    aliases = Counter((c['identity']['file'], c['identity']['kind'],
                       c['identity']['name']) for c in cases)
    for row in cases:
        i = row['identity']
        row['same_file_kind_name_cases'] = aliases[(i['file'], i['kind'], i['name'])]
    if inventory != source_inventory(root):
        raise ValueError('corpus changed while freezing inputs')
    if generator_hashes != {p.name: digest(p.read_bytes()) for p in generators}:
        raise ValueError('generator changed while freezing inputs')
    manifest = dict(schema=SCHEMA, corpus_revision=revision,
                    corpus_revision_verified=check_revision,
                    source_files=inventory,
                    generator_sha256=generator_hashes,
                    cases=cases, declarations_without_fixtures=not_generated,
                    fixture_policy='primary and any planned isolated_driven fixture are separate; never replace a primary result',
                    scope_note='Declaration provenance does not prove name resolution. Nested/duplicate and library-section cases retain their generated fixtures and must not be described as independently exercised declarations.',
                    pending=['runtime preflight and manifest', 'external include dependency audit',
                             'isolation dependency/scope correctness', 'held-out grouping',
                             'current reference baseline', 'full execution and outcome reporting'])
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2, allow_nan=False) + '\n')
    verify(root, output)
    return manifest


def verify(root, output):
    root, output = Path(root).resolve(), Path(output).resolve()
    manifest = json.loads((output / 'manifest.json').read_text())
    if manifest['schema'] != SCHEMA:
        raise ValueError('unsupported manifest schema')
    if source_inventory(root) != manifest['source_files']:
        raise ValueError('corpus inventory/hash mismatch')
    seen = set()
    for row in manifest['cases']:
        identity = row['identity']
        if row['case_id'] != case_id(identity) or row['case_id'] in seen:
            raise ValueError('invalid or duplicate case identity')
        seen.add(row['case_id'])
        if row['source_sha256'] != manifest['source_files'][identity['file']]:
            raise ValueError('case source hash mismatch')
        variants = [f['variant'] for f in row['fixtures']]
        if variants.count('primary') != 1 or len(variants) != len(set(variants)):
            raise ValueError('each case needs exactly one primary and distinct variants')
        for item in row['fixtures']:
            if item['fixture_id'] != 'fixture-v1-' + digest(canonical(item['assets'])):
                raise ValueError('fixture identity mismatch')
            for asset in item['assets'].values():
                path = output / asset['path']
                if path.resolve().parent != output / 'assets' or path.is_symlink():
                    raise ValueError('asset must be an ordinary file in assets')
                raw = path.read_bytes()
                if digest(raw) != asset['sha256'] or len(raw) != asset['bytes']:
                    raise ValueError(f'fixture asset mismatch: {path}')
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('freeze', 'verify'))
    parser.add_argument('--corpus', type=Path, required=True, help='pinned KiCad Models directory')
    parser.add_argument('--output', type=Path, required=True, help='new local experiment directory')
    args = parser.parse_args()
    manifest = freeze(args.corpus, args.output) if args.action == 'freeze' else verify(args.corpus, args.output)
    print(json.dumps(dict(cases=len(manifest['cases']),
                          fixtures=sum(len(c['fixtures']) for c in manifest['cases']),
                          declarations_without_fixtures=len(manifest['declarations_without_fixtures']),
                          manifest_sha256=digest((args.output / 'manifest.json').read_bytes())), indent=2))


if __name__ == '__main__':
    main()
