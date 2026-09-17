"""Evidence-critical identity, immutable input and cohort regressions."""
import json
from pathlib import Path
import subprocess
import sys

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from kicad_experiment import declarations, freeze, verify, case_id
from test_kicad_models import extract_models, extract_subcircuits, make_model_test, make_subcircuit_test


def make_corpus(tmp_path):
    root = tmp_path / 'Models'
    root.mkdir()
    (root / 'duplicate.lib').write_text('''* repeated definitions and kinds
.model DUP D (IS=1e-14)
.model dup D (IS=2e-14)
.subckt DUP a b
R1 a b 1k
.model INNER D (IS=1e-14)
.subckt CHILD c d
R2 c d 2k
.ends CHILD
.ends DUP
.subckt DUP a b
R3 a b 3k
.ends DUP
''')
    return root


def test_identity_distinguishes_scope_kind_and_occurrence():
    text = '''.model X D
.model x D
.subckt X a b
.model X D
.ends
.subckt X a b
.model X D
.ends
.lib fast
.model X D
.endl
.lib slow
.model X D
.endl
'''
    rows = list(declarations(text, 'vendor/a.lib').values())
    assert len(rows) == len({r['case_id'] for r in rows}) == 8
    assert rows[1]['identity']['occurrence'] == 2
    assert rows[3]['identity']['scope'][0]['occurrence'] == 1
    assert rows[5]['identity']['scope'][0]['occurrence'] == 2
    assert rows[6]['identity']['scope'][0]['name'] == 'fast'
    assert rows[7]['identity']['scope'][0]['name'] == 'slow'
    assert [r['case_id'] for r in declarations('\n' + text, 'vendor/a.lib').values()] == [r['case_id'] for r in rows]
    changed = dict(rows[0]['identity'], file='other/a.lib')
    assert case_id(changed) != rows[0]['case_id']


def test_provenance_keeps_legacy_generator_population(tmp_path):
    root = make_corpus(tmp_path)
    path = root / 'duplicate.lib'
    assert [r[:-1] for r in extract_models(path, with_source=True)] == extract_models(path)
    assert [r[:-1] for r in extract_subcircuits(path, with_source=True)] == extract_subcircuits(path)
    # The provenance points at the actual declaration even after blank lines.
    path.write_text('\n\n.model X D (IS=1e-14)\n\n.subckt S a b\nR1 a b 1k\n.ends\n')
    assert extract_models(path, with_source=True)[0][-1]['line'] == 3
    assert extract_subcircuits(path, with_source=True)[0][-1]['line'] == 5


def test_freeze_preserves_duplicates_and_plans_rescue_without_simulators(tmp_path):
    root = make_corpus(tmp_path)
    manifest = freeze(root, tmp_path / 'experiment', check_revision=False)
    expected = 0
    for path in root.glob('*.lib'):
        expected += sum(bool(make_model_test(*r)) for r in extract_models(path))
        expected += sum(bool(make_subcircuit_test(*r)) for r in extract_subcircuits(path))
    assert len(manifest['cases']) == expected == 4
    assert len({c['case_id'] for c in manifest['cases']}) == expected
    for row in manifest['cases']:
        assert row['same_file_kind_name_cases'] == 2
        variants = [f['variant'] for f in row['fixtures']]
        assert variants == (['primary', 'isolated_driven'] if row['identity']['kind'] == 'subckt' else ['primary'])
    assert len(manifest['declarations_without_fixtures']) == 2
    assert verify(root, tmp_path / 'experiment') == manifest
    with pytest.raises(ValueError, match='already exists'):
        freeze(root, tmp_path / 'experiment', check_revision=False)


def test_freeze_is_independent_of_checkout_and_python_hash_seed(tmp_path):
    root = make_corpus(tmp_path)
    other = tmp_path / 'relocated'
    other.mkdir()
    other_root = make_corpus(other)
    # The block closure used to iterate an unordered set.
    for directory in [root, other_root]:
        (directory / 'closure.lib').write_text('.subckt OUT a b\nX1 a b ONE\nX2 a b TWO\n.ends\n.subckt ONE a b\nR1 a b 1k\n.ends\n.subckt TWO a b\nR2 a b 2k\n.ends\n')
    import os
    manifests = []
    for seed, directory in [('1', root), ('17', other_root)]:
        output = tmp_path / ('freeze-' + seed)
        code = 'from pathlib import Path; from kicad_experiment import freeze; import sys; freeze(Path(sys.argv[1]),Path(sys.argv[2]),check_revision=False)'
        subprocess.run([sys.executable, '-c', code, str(directory), str(output)], check=True,
                       env=dict(os.environ, PYTHONHASHSEED=seed, PYTHONPATH=str(Path(__file__).resolve().parents[1])))
        manifests.append((output / 'manifest.json').read_bytes())
    assert manifests[0] == manifests[1]


@pytest.mark.parametrize('change', ['source', 'added_source', 'asset', 'duplicate_id', 'source_hash', 'missing_primary'])
def test_verify_rejects_changed_or_inconsistent_inputs(tmp_path, change):
    root = make_corpus(tmp_path)
    output = tmp_path / 'experiment'
    manifest = freeze(root, output, check_revision=False)
    if change == 'source':
        (root / 'duplicate.lib').write_text('* changed\n')
    elif change == 'added_source':
        (root / 'extra.lib').write_text('.model X D\n')
    elif change == 'asset':
        path = manifest['cases'][0]['fixtures'][0]['assets']['netlist']['path']
        (output / path).write_text('* changed fixture\n')
    else:
        if change == 'duplicate_id':
            manifest['cases'].append(manifest['cases'][0])
        elif change == 'source_hash':
            manifest['cases'][0]['source_sha256'] = '0' * 64
        elif change == 'missing_primary':
            manifest['cases'][0]['fixtures'] = []
        (output / 'manifest.json').write_text(json.dumps(manifest))
    with pytest.raises(ValueError):
        verify(root, output)


def test_source_lines_count_newlines_not_vendor_form_feeds(tmp_path):
    root = tmp_path / 'Models'
    root.mkdir()
    path = root / 'formfeed.lib'
    path.write_text('* page 1\n\f\n* page 2\n.subckt S a b\nR1 a b 1k\n.ends\n')
    manifest = freeze(root, tmp_path / 'experiment', check_revision=False)
    assert len(manifest['cases']) == 1
    assert manifest['cases'][0]['source_line'] == 4
    assert manifest['cases'][0]['identity']['name'] == 's'
