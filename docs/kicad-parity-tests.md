# KiCad SPICE Library Parity Tests

This page describes the historical diagnostic harness. Its adaptive rescue
selection and older measurements are not a publication-ready experiment.
For the JOSS work, see [frozen experiment inputs](kicad-experiment.md) and
[the current progress tracker](joss-progress.md). ngspice remains the reference.

## Prerequisites

- **ngspice** on `PATH`. The harness shells out to `ngspice -D ngbehavior=psa -b`
  (PSpice-compatibility mode), which is needed to parse the vendor PSpice-syntax
  models. Override the binary with `--ngspice PATH`.
- **The KiCad SPICE Library** at `third_party/KiCad-Spice-Library/Models` — the
  harness reads models from there via `KICAD_LIB` in `tools/test_kicad_models.py`.
- **A built neospice** at `build/neospice` (override with `--neospice PATH`):

  ```bash
  cmake -B build -DCMAKE_BUILD_TYPE=Release
  cmake --build build -j$(nproc)
  ```

## Running

The harness is `tools/compare_kicad_models.py`. It generates minimal operating-point
fixtures for a subset of model/subcircuit declarations, runs both simulators,
and classifies the results. Nested declarations, unsupported types and generator
limitations affect coverage. Repeated declarations can produce duplicate fixtures.

```bash
# Historical generated cohort (34,908 fixtures, not independent models), saving results to JSON
python3 tools/compare_kicad_models.py --save results/compare_full.json --jobs 8

# Quick subset (first N models)
python3 tools/compare_kicad_models.py --max 5000 --save results/compare_5k.json --jobs 8

# Only models from files matching a substring (e.g. one vendor)
python3 tools/compare_kicad_models.py --file LinearTech --save results/lt.json --jobs 8

# Verbose, mismatches only
python3 tools/compare_kicad_models.py --max 200 --verbose --mismatches-only

# Run a single model deck directly in neospice
./build/neospice /path/to/test.cir
```

Common flags: `--max N` (0 = all), `--jobs N` (parallel workers), `--file SUB`,
`--category NAME`, `--save PATH`, `--verbose`, `--mismatches-only`,
`--neospice PATH` / `--ngspice PATH` (binary overrides), and
`--baseline OLD.json` (select and rerun both simulators on previously passing
neospice-only rows; this is a filtered population, not a full comparison).
Use `--transition-baseline RESULTS.json` for old/new status transitions, or
`--select-baseline RESULTS.json --select-status STATUS` for a selected cohort.
Ambiguous legacy file/name identities are rejected for transitions and saved
isolation choices; declaration IDs are required to distinguish repeated names.

### Seeing error margins

To print the actual error vs tolerance for every compared signal, reconfigure
with debug-compare and rebuild:

```bash
cmake -B build -DNEOSPICE_DEBUG_COMPARE=ON
cmake --build build -j$(nproc)
```

This affects C++ reference tests that call the comparison helpers; it does not
enable extra diagnostics in the Python corpus harness. The C++ helpers emit
analysis-specific margin/detail records; see [validation methods](validation-methods.md).
The Python harness saves its per-signal `comparisons` records with `--save`.

## Reading the results

Each model lands in one of six buckets:

| Status | Meaning |
|---|---|
| MATCH | both simulators converge and the values agree |
| MISMATCH | both converge but the values differ |
| NG_ONLY | ngspice converges, neospice fails |
| NEO_ONLY | neospice produces a non-trivial solve; ngspice-psa cannot parse the deck even isolated |
| NEO_TRIVIAL | neospice solves an unexcited fixture to ~0 V while ngspice can't parse it — not a win |
| BOTH_FAIL | neither converges |

Only **MATCH + MISMATCH** are real two-simulator comparisons, so the headline
value-agreement rate is `MATCH / (MATCH + MISMATCH)`. When ngspice fails to parse
a whole library, the harness retries with the target subckt extracted into a
clean, dependency-closed library and a 5 V / 1 kΩ stimulus injected (the
**isolated+driven fallback**), adopting the result only if both simulators then
succeed — this is why NEO_ONLY/NEO_TRIVIAL exist as distinct buckets.

## Current baseline

**Certified 2026-06-12** (`results/compare_full_3bcd_v2.json`, full suite of
34,908 models, `ngspice -D ngbehavior=psa`):

| Status | Count | % |
|---|---:|---:|
| MATCH | 24,201 | 69.3% |
| MISMATCH | 1,642 | 4.7% |
| NG_ONLY | 17 | 0.0% |
| NEO_ONLY | 2,118 | 6.1% |
| NEO_TRIVIAL | 3,373 | 9.7% |
| BOTH_FAIL | 3,557 | 10.2% |

**93.6% value-agreement with ngspice** — 24,201 MATCH out of 25,843 real
two-simulator comparisons.
