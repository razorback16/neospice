# Frozen KiCad experiment inputs

The JOSS compatibility experiment is in preparation. Input freezing, runtime
preflight and execution with separate fixture outcomes are implemented. The full
checksum-pinned ngspice47 runs are complete. This is the sole reference target.
Binding/dependency audits, honest evaluation grouping and
final reporting remain required. The old
operating-point percentages are historical measurements, not validation of the
current simulator or the new experiment.

## Freeze before simulation

Use the KiCad Spice Library checkout at
`a8688952bcaab19f567bc4db237b60bde03ef310`. From the neospice checkout:

```sh
python tools/kicad_experiment.py freeze \
  --corpus /path/to/KiCad-Spice-Library/Models \
  --output /path/to/new-experiment
python tools/kicad_experiment.py verify \
  --corpus /path/to/KiCad-Spice-Library/Models \
  --output /path/to/new-experiment
```

`freeze` requires the pinned, clean Git checkout and a new output directory.
It does not invoke either simulator. It writes `manifest.json` and content-hashed
files under `assets/`, verifies their bytes, and rejects corpus or generator
changes during generation. A failed freeze may leave a partial directory;
use a new directory for a new attempt. A partial directory without a successfully
verified manifest is not an experiment input package.

The manifest contains all source-file hashes under Models, generator hashes,
declaration identities and source locations, generated fixture hashes, planned
rescue availability and declarations without generated fixtures. Paths in generated
netlists use `__KICAD_MODELS_ROOT__` and `__FROZEN_ASSETS_ROOT__` placeholders.
The execution script materializes these explicitly and retains the actual
runtime paths/configuration. Do not pass the templates directly to a simulator.

`verify` checks the exact source-file inventory, case IDs, required primary
variant, fixture IDs and every referenced asset's size/hash. Save the manifest's
own SHA-256 in the execution record: its internal hashes are integrity checks,
not a signature or proof of an independently trustworthy input selection.
The source inventory does not establish closure of `.include` dependencies
outside Models or capture simulator startup files. Those remain preflight work.

## Declaration identity and coverage

A case ID hashes the corpus-relative file, declaration kind, lexical scope,
case-insensitive declared name and occurrence within that namespace. Scope
includes repeated enclosing subcircuits and named `.lib` sections. Source line
numbers are retained as provenance; inserting blank lines does not change the
identity. Paths and simulator results are not used to choose a case ID.

Scope is a lexical annotation from exact `.subckt`/`.ends` and `.lib`/`.endl`
directive tokens. It is not ngspice's resolved namespace; malformed terminators,
concatenated decks and vendor dialects require further interpretation. Some
recorded scopes are deeply nested and must be audited before making binding
or per-definition coverage claims.

The existing generator's population is retained, including repeated declarations
and its parsing/fixture limitations. Adding source provenance does not change
the generator's existing tuple interface or generated circuits. Declarations for
which it produces no fixture are listed separately with a reason. Any later
parser or population correction requires a new freeze and explicit reconciliation
with this population, rather than overwriting the old experiment.

A generated case includes the whole library and instantiates a name. Its origin
at a specific declaration does **not** prove that the simulator resolves that
particular declaration when names repeat, declarations are nested, or library
sections require selection. The manifest records lexical scope and the number
of generated cases sharing a file/kind/name. These cases must remain visible;
do not count them as independently validated model definitions without resolving
and checking those semantics. Distinct case IDs may share identical fixture
bytes, which must also be disclosed in the eventual coverage report.

Legacy `(file, name)` baseline dictionaries cannot recover these distinctions.
The old harness now rejects ambiguous status-transition/isolation baselines
instead of silently overwriting rows. Historical JSON remains unchanged.

## Primary and rescue fixtures

Every generated case has one `primary` fixture. For every subcircuit, the freeze
also attempts the existing isolation/driving transformation before any outcome
is known. Successful generation produces an `isolated_driven` variant; failed
extraction is recorded. Dependency block ordering is deterministic across Python
hash seeds. Selection cannot depend on neospice succeeding.

The isolation helper remains heuristic: its dependency and lexical-scope handling
need further audit. A generated rescue is neither proof of equivalent circuitry
nor proof that a simulation will succeed. The runner retains both outcomes, including failed rescues. It never replaces a
primary failure with a successful rescue or uses an outcome to select a variant.

`compare_kicad_models.py` retains its historical adaptive rescue workflow for
older diagnostic work. It is not the execution path for the paper experiment.
No new paper compatibility percentage or performance claim is supported by
freezing alone. Timings collected during concurrent builds/corpus preparation
are not benchmark evidence.

## Reproduction and redistribution

The local freeze contains source-derived isolated libraries. It is not published
or committed as a redistributable vendor-model archive. Preserve upstream notices
and resolve the actual vendor redistribution terms before preparing an archive.
The compact checkpoint evidence records hashes and generation results; a reviewer
will still need the pinned source checkout or a suitably licensed input archive.
Runtime provenance, current ngspice baseline, full outcome retention, evaluation
splits and clean-checkout reproduction remain part of the active JOSS goal.

## Required reference

Use the checksum-pinned ngspice47 CLI and its stock startup file from the
[reference build](building.md). Preflight rejects other versions before corpus
fixtures execute. The stock startup requests eight simulator threads; preserve
this runtime setting in the manifest. Concurrent corpus timings are not
controlled performance measurements.

## First full freeze, September 10, 2026

The first verified manifest preserves the historical cohort exactly as a multiset
of file/kind/name/info: **34,908 cases**, with no additions or omissions relative
to the audited JSON. It records **34,908 primary** and **32,451 isolated/driven**
fixtures; six subcircuits could not be isolated by the existing helper.

There are **34,764 distinct primary fixture byte sequences**, **169 excess
occurrences under the old file/name key**, and **652 cases with a recorded lexical
scope**. The source inventory covers 2,073 files. Another 61,286 recorded
declarations have no generated fixture: 57,520 are not selected by the historical
extractors and 3,766 are extracted but receive no fixture from the generator.
These are generator/lexical inventory counts, not simulator outcome categories.

The compressed manifest is retained in checkpoint 11 evidence. The local complete
input package is `/tmp/neospice-joss-corpus-freeze-11-final`. All current cases
were already exposed through the historical experiment; freezing them now does
not make them untouched hold-outs. A later evaluation split must disclose that
history and identify any newly designed, previously untested validation inputs.


## Execute with retained outcomes

For the required ngspice47 installation:

```sh
python tools/run_kicad_experiment.py \
  --inputs /path/to/new-experiment \
  --corpus /path/to/KiCad-Spice-Library/Models \
  --output /path/to/new-run \
  --neospice /path/to/neospice \
  --ngspice "$PWD/third_party/ngspice47-reference/cli/bin/ngspice" \
  --spinit "$PWD/third_party/ngspice47-reference/cli/share/ngspice/scripts/spinit" \
  --jobs 8 --timeout 10
```

Use the actual startup file for the selected reference installation. Output must
be new and outside the input/corpus directories. `--case-id case-v1-...` selects
an exact declaration for development checks and always runs all its planned
variants; omit selectors to run the complete frozen cohort. The chosen case and
fixture list is saved before preflight. Unknown/empty selections fail.

Preflight hashes both binaries, resolved ELF libraries, the explicit system
`spinit` and its static code-model paths. It records reference version output,
runner source hashes, OS/Python information and the selected environment values.
The reference uses `SPICE_SCRIPTS` to locate that startup file and `-n` to disable
personal/local startup files; the system startup still loads. Both simulators use
`ngbehavior=psa`, matching the historical corpus mode. The checked workstation has
no local or user `.spiceinit`/`spice.rc` files. These are declared configuration
choices, not options selected to improve numerical agreement.

Analytical divider and POLY probes must succeed in both engines before any corpus
fixture runs. A real negative control with code models omitted passes the divider
but fails the reference POLY check, correctly rejecting the installation. Static
code-model paths are inventoried; this is not a trace proving every module loaded.
Recursive/dynamic startup dependencies are rejected pending explicit support.
The OSDI branch is not certified by these probes. Full build provenance and any
additional runtime dependency audit still belong in the final release evidence.

The runner derives required public nodes and voltage-source currents from the
limited generated-fixture grammar before execution. All 67,359 frozen fixtures
pass this grammar check. Matching requires these signals in both results, plus
all non-internal reference outputs; a public name resembling an internal node
still receives a value comparison. The existing symmetric additive corpus
formula is retained: `abs(neo-ng) <= 1e-3*max(abs(neo),abs(ng)) + floor`, with
floor 1e-6 V for voltages and 1e-9 A for currents. This is distinct from the C++
reference-normalized waveform comparison documented in validation methods.

## Generated internal observables

`is_internal_var` in `tools/compare_kicad_models.py` decides which reference
outputs are generated internals rather than circuit observables. Subcircuit
expansion names (`.x`, `x1.`, `x2.` prefixes) are excluded, and so are the
VDMOS nodes ngspice 47 creates in `vdmosset.c` through
`CKTmkVolt(ckt, VDMOSname, ...)`:

```
v\(m[^()#]*#(?:gate|body_diode)\)      case-insensitive, full match
```

The rule is bounded by what ngspice 47 actually prints, enumerated by running
the reference over the VDMOS option space rather than inferred from the source:

| Model option | Generated raw variable |
|---|---|
| `Rg>0` | `v(<inst>#gate)` |
| `Rb>0` | `v(<inst>#body_diode)` |
| `Rd>0`, `Rs>0` | none printed, although `show` confirms both resistances are applied |
| `Rds`, `Rq` | none |
| `thermal` with `Rthjc` | `v(<inst>#cktTemp)`, `v(<inst>#VdevTemp)` |

The thermal names are deliberately **not** excluded. Exclusion is only valid
where neospice models the same node and merely keeps it private, which
`ucb_declare_internal_nodes` does for the gate and body-diode nodes
(`__<inst>_gate`, `__<inst>_body diode`, both marked internal and therefore not
exported). neospice has no thermal network: the self-heating form returns
`v(tj) = v(tc) = 0` against 36.48/36.17 in ngspice 47, so there is nothing to
compare like-for-like and the case must keep failing on its public terminals.
No corpus model declares `Rthjc`/`Rthca`, so this affects no frozen fixture.

Two properties keep the exclusion honest on the paper execution path,
`run_kicad_experiment.compare_outcomes`. An explicitly required fixture port is
always compared even when its name matches the pattern, so the filter can never
remove a signal the fixture declares. And every excluded reference signal is
listed per fixture in `excluded_reference_signals`, so the comparison set is
auditable rather than implicit. Only names the reference emits are affected;
neospice never produces these names.

`compare_kicad_models.compare_values(external_only=True)` shares the same
`is_internal_var` classifier but has no required-port override: it drops every
matching name unconditionally. That is the historical adaptive rescue workflow
described above, not the paper experiment path, and no reported corpus outcome
comes from it.

At an operating point the excluded values carry no information: the gate node
sits exactly at the gate terminal voltage because no DC gate current flows, and
the body-diode node sits within 1e-13 V of ground. `Rg`/`Rb` are unobservable
at DC in either engine. VDMOS AC and noise, where these nodes would matter,
fail explicitly as unsupported.

`VDMOSValidation.GeneratedInternalNodesAreModeledButPrivate` holds both halves
of this argument: that neospice allocates exactly the two internal nodes and
keeps them private, and that ngspice 47 prints no generated name for that deck
outside the documented pattern. A new generated observable fails that test
rather than becoming an unexplained corpus mismatch.

Each `cases/<case-id>/<variant>/` directory retains the materialized deck,
commands, process statuses, stdout/stderr, raw output when produced, parsed
operating-point values and comparisons. Process/analysis failures, timeouts and
invalid results remain failures even if partial raw data exists. A fixture result
is never replaced by its rescue. Failure labels are diagnostic classifications;
raw evidence remains available to refine them during triage.

Work submission and result retention are bounded in memory. `progress.json`
reports live counts every 100 fixtures; `run.json` is the authoritative plan and
completion record. It starts with `complete: false` and becomes complete only
after all planned fixtures finish and input/runtime/source integrity checks pass.
Partial runs keep their evidence and must not be summarized as full experiments.
There is no resume command yet: inspect the live process before considering a
new run, and never treat a temporary observation timeout as process termination.

`OPENBLAS_NUM_THREADS` and `OMP_NUM_THREADS` are set to 1 and recorded. Subprocess
wall times include startup and may overlap other jobs. They are diagnostic
throughput measurements, not paired benchmark samples or paper speedup evidence.
The final compatibility report must retain primary/rescue denominators, failed
rescues, nontrivial excitation, duplicate/scoped binding limitations and all
outcome categories. A MATCH is not independent physical-model validation.


All current compatibility experiments use the ngspice 47 source archive, with published
SHA-256 `894e649651f1838a14095e5a5439e7d3aa63e87ede14d283173fda4fcdef675f`
verified against the [official release download](https://sourceforge.net/projects/ngspice/files/ng-spice-rework/47/ngspice-47.tar.gz).
The local archive is `/tmp/neospice-joss-ngspice-47.tar.gz`, with extracted source
at `/tmp/neospice-joss-ng47-source/ngspice-47`. Acquisition/source hashes are in
checkpoint 12 evidence. Checkpoint 13 adds successful CLI/shared-library builds,
runtime preflight and a complete C++ suite with ten retained failures; see
[the reference checkpoint](ngspice47-reference.md).


## Current completed ngspice47 experiment

Checkpoint29 stage2 preserves all67,359 frozen fixtures. Results are separate
for the original34908 primary cases and32451 isolated/driven variants:

| Outcome | Primary | Isolated/driven |
|---|---:|---:|
| MATCH | 20228 | 22850 |
| MISMATCH | 1274 | 1312 |
| NG_ONLY | 22 | 50 |
| NEO_ONLY | 4716 | 6939 |
| NEO_TRIVIAL | 5364 | 0 |
| BOTH_FAIL | 3304 | 1300 |

The [summary](evidence/joss/2026-09-11-checkpoint29-stage2-corpus-run47-summary.json),
[artifact ledger](evidence/joss/2026-09-11-checkpoint29-stage2-corpus-run47-records.json.gz)
and [transitions](evidence/joss/2026-09-11-checkpoint29-stage2-candidate47-transitions.json)
retain failures and exact provenance. Primary and isolated/driven results must
not be pooled into a single matched-model percentage. Minimal OP fixtures do
not certify full device models or transient/AC/noise behavior.

The stage2 diode correction repairs ten outcome mismatches. Seven earlier
MATCH-to-MISMATCH changes introduced during stage1 remain for investigation.
Model binding, grouping, supported option coverage and final research
interpretation remain open. New runner version checks do not change these
recorded simulation results; their original runner hashes remain in the ledger.
