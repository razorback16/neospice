# KiCad SPICE Library Parity Tests

How to run neospice's parity test against the KiCad SPICE Library and read the
results. ngspice is the reference simulator: every model is simulated in both
tools and the node values are compared.

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

The harness is `tools/compare_kicad_models.py`. It extracts every `.model` and
`.subckt` from the library, runs each in both simulators, and classifies the
result.

```bash
# Full suite (all 34,908 models), saving results to JSON
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
`--baseline OLD.json` (diff this run against a previous saved run).

### Seeing error margins

To print the actual error vs tolerance for every compared signal, reconfigure
with debug-compare and rebuild:

```bash
cmake -B build -DNEOSPICE_DEBUG_COMPARE=ON
cmake --build build -j$(nproc)
```

This emits `MARGIN_TRAN|signal|actual_err|tolerance|headroom` to stderr for each
`compare_transient` / `compare_dc` / `compare_ac` call.

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
