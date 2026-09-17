# Paired benchmark method

`tests/bench/bench_comprehensive.cpp` now validates paired outputs before accepting
performance samples. The former harness's timings are historical, unvalidated
measurements and must not support the JOSS paper. THS4131 and TLV3201 now use
the shared protocol with separately declared populations and validation policies.
Other standalone programs remain subject to the [harness audit](benchmark-harness-audit.md).

## Workloads and boundaries

The original twelve explicit workloads retain the former harness's DC, AC, transient,
noise, sweep and combined OP/AC cases. Load and total time are measured for each
case, replacing separate parse-only rows. Commands are recorded in JSONL. Both
simulators receive the same analysis parameters. In particular, resistor noise
now runs from 1 Hz to 1 GHz in both engines; the previous reference `run` command
used the netlist's 1 MHz stop while neospice used 1 GHz. Combined OP/AC cases
explicitly execute and validate both results; the public `Simulator::run()`
returns only its last analysis result.

The expanded population adds 22 fixed workloads, for **34 total**. Selection was
recorded before evaluating their outputs; failures remain in the population:

- Eight DC cases preserve every size and electrical topology from
  `bench_solver_throughput.cpp`: square resistor meshes with sides 10, 32, 71
  and 141, and diode ladders with 100, 1,000, 5,000 and 20,000 stages. These are
  paired through the comprehensive harness; the old standalone timings remain
  unvalidated. Meshes use a point drive and opposite grounded corner, rather
  than a uniformly shunted banded test matrix.
- Twelve AC/transient cases use point-driven two-dimensional RC and diode-RC
  grids with sides 8, 24 and 48. Every node has a 1 nF capacitor; nearest-neighbor,
  feed and far-corner return resistors are 1 kOhm. Nonlinear grids add one diode
  per node, with an explicit common model. A fixed 0.2–0.8 V pulse drives five
  periods, with 10 us rise/fall times. AC spans 1 Hz–1 MHz with 10 points/decade;
  transient requests a 1 us step and 500 us stop.
- Two noise cases use the 24-by-24 RC and diode-RC grids over the same AC band,
  observing the center node (`out`) and referring input noise to `v1`.

These synthetic cases provide controlled size/topology/analysis coverage;
they are not evidence of research adoption or general solver scaling. They
complement the existing nonlinear amplifier macromodels. The fixed generator
and checked-in fixture hashes allow reproduction without random seeds:

```sh
python tools/generate_paired_circuits.py --check
```

Regenerate with the same command without `--check`. The paired protocol loads
the generated files identically in both engines; generation is outside all
timers. Acceptance still uses every required reference signal and the original
threshold. Initial screening found two large input-referred noise discrepancies
against47; their failed records are retained. Neospice omitted
ngspice's minimum squared noise-transfer gain (`N_MINGAIN = 1e-20`). The corrected
calculation applies this analysis convention, including at zero gain, without
changing benchmark tolerances. Analytical and reference tests cover zero gain
and values below, at and above that boundary. Final qualification results are
recorded in [JOSS progress](joss-progress.md); these are not timing samples.

Each fresh circuit yields four durations, in microseconds, using `steady_clock`:

- `load_us`: public file loading, including file reads and include resolution.
- `analysis_us`: requested analyses, any deferred setup, and owned result
  materialization. This is not a solve-only measurement.
- `cleanup_us`: circuit and reference-plot cleanup.
- `total_us`: the contiguous interval containing all three phases.

Both engines' owned output objects survive cleanup for comparison outside all
these timers. Their eventual destruction is excluded for both. One-time library
initialization and application object construction are outside the timers.
Each public interface does different internal work: neospice also retains source
text and dense result storage; ngspice's wrapper checks commands and collects
frontend diagnostics. AC may reuse an existing operating point in neospice while
ngspice initializes its requested AC analysis. These interface differences must
accompany any phase comparison. Loading is not labeled equivalent completed
setup, and these measurements cannot isolate numerical solver cost.

## Validation and sampling

The screening threshold is fixed at relative `1e-3` with denominator floor
`1e-9`, using the existing comparison functions and their documented formulas in
[validation methods](validation-methods.md). It was declared before observing
these repaired benchmark outputs. This is a new benchmark qualification rule,
not a replacement for any existing regression's tolerance. All reference voltage
and branch-current signals must be present; waveform, finite-value, status and
axis checks remain enabled. Noise compares both referred amplitude densities.
DC sweeps compare every requested coordinate and all reference signals. Requested
analysis endpoints and frequency counts are checked in addition to paired axes.
Frequency request endpoints allow a grid-length-dependent floating-point
accumulation bound, independently of waveform tolerances.

One validation pair precedes the warmups and recorded samples. Both engines use
the same warmup and sample counts, with alternating execution order. Every pair
is validated, including warmups. A failure stops that case, retains its diagnostic
record and disqualifies the entire case, including any earlier samples. The
other declared cases are still evaluated. JSONL records retain order, raw phase
durations, passing/failing comparison evidence, and reference diagnostics. The
summary reader rejects incomplete runs, missing declared cases, missing samples,
invalid durations, zero-point comparisons and inconsistent acceptance records.
It emits no speed result for a failed case. Median, minimum and maximum are
computed from raw samples; no aggregate speedup is defined.

DC records now retain every compared voltage/current, including exact matches,
with the reference/actual values and absolute/normalized error. Their coordinate
unit is `OP` (coordinate zero is a placeholder, not a time). Sweep records
accumulate each signal's point count and maximum absolute error, while retaining
the source coordinate and values at its maximum normalized error. Voltage/current
sweep coordinates use `V`/`A`. The accepted formula and point population are
unchanged; the two maxima need not occur at the same coordinate.

`--verify-only` runs one correctness pair per workload and writes no timings.
Use it during development or while corpus/build jobs are active:

```sh
OMP_NUM_THREADS=1 OPENBLAS_NUM_THREADS=1 \
  build/tests/bench_comprehensive --verify-only --output verify.jsonl
python tools/summarize_paired_benchmark.py verify.jsonl --output verify-summary.json
```

Timing requires `OMP_NUM_THREADS=1` and `OPENBLAS_NUM_THREADS=1`; the harness also
sets ngspice `num_threads=1` and records its version and settings. This is a
separately labeled benchmark configuration, not the stock corpus configuration.
Reference diagnostics identify the active linear solver. The existing fixtures
use default Sparse, with no KLU option. Do not switch reference solvers or
numerical options to turn a failed comparison into a timing result.

Run performance sampling only after competing corpus/build jobs finish:

```sh
OMP_NUM_THREADS=1 OPENBLAS_NUM_THREADS=1 \
  build/tests/bench_comprehensive --warmup 3 --samples 30 --output paired.jsonl
python tools/summarize_paired_benchmark.py paired.jsonl --output paired-summary.json
```

Output paths must be new; both commands reject existing evidence files.
`--case ID` supports targeted diagnosis and records the smaller population.
A targeted run must not be reported as the complete benchmark population.

## Reproducible tables and figures

Install the plotting dependency with `pip install '.[benchmarks]'`, then run:

```sh
python tools/report_paired_benchmark.py /path/to/paired-run \
  --output /path/to/new-report
```

The report checks the experiment's completion/inventory flags, artifact hashes,
runtime protocol, saved summary against raw records, and passing per-signal
point/error accounting. Older runs lacking DC/sweep diagnostics cannot produce
this report. Archived runs may be relocated as a complete directory: local
artifact bytes must match their manifest hashes; the original source/build
paths remain historical provenance, not a requirement to rerun those binaries.

`qualification.csv`, `report.md` and qualification PDF/SVG figures retain the
entire declared population and all failures. Exact zero errors are labeled
explicitly on the logarithmic error figure. Undefined comparisons are shown as
failures rather than assigned a numeric error. Verification-only runs produce
no timing table or timing figure. Synthetic unit-test measurements are only
reporting tests, never performance evidence for neospice.

A completed measurement run also produces `timings.csv` for all four phases
and a total-time PDF/SVG comparing both engines. Points are medians; bars span
the observed minimum and maximum. These are descriptive ranges, not confidence
intervals. The timing axis is logarithmic above 0.001 us and linear below it,
so a recorded zero is representable. Ratios divide the reference median by the
neospice median; zero denominators remain undefined. No aggregate speedup is
computed. Failed workloads have no timing rows even if earlier pairs passed.

`report-manifest.json` records input and output hashes, generator hash, Python
and Matplotlib versions. Figures are vector PDF/SVG for inspection and export.
Reports do not certify a quiet machine: the sampling run's environment evidence
and competing-work checks must be reviewed separately.

## Provenance runner

Use `tools/run_paired_benchmark.py` for an evidence-producing run after building
the selected benchmark target (by default, `bench_comprehensive`). Select the actual reference version and its stock
`spinit` file; the runner checks the version and startup directory reported by
the loaded library. For example, with the ngspice47 build described in
[reference setup](ngspice47-reference.md):

```sh
python tools/run_paired_benchmark.py \
  --build /tmp/neospice-joss-reference47-build \
  --reference-version 47 \
  --spinit /tmp/neospice-joss-ng47-shared/install/share/ngspice/scripts/spinit \
  --verify-only --output /tmp/paired-verification
```

For an uncontended timing run, replace `--verify-only` with `--measure` and set
`--samples 30 --warmup 3`. The caller must first establish that competing work
has finished. The runner records load and visible process observations, but
cannot prove exclusive CPU access or see every job in other process namespaces.
These environment-specific example paths should be replaced for another build.

Each new output directory contains `manifest.json`, `pairs.jsonl`, stdout/stderr
logs and, for a valid completed experiment, `summary.json`. A manifest with
`complete=true` and `evidence_valid=true` means the protocol and input-integrity
checks succeeded. **It does not mean every workload passed.** Check
`all_workloads_qualified` and the individual case results; failed workloads
produce valid failure evidence and an overall exit status1.
Wrong-version or changed-input evidence is invalidated, exits2, and retains its
raw files without a qualified summary. Successful qualification exits0.

The binary's `--describe` mode declares the complete population, commands and
compiled fixture root without initializing either simulator. The runner checks
those declarations against execution and records:

- Source and binary hashes, checkout revision/status, static engine archive,
  CMake cache, benchmark compile/link flags, compiler executable/version.
- Recursive literal fixture includes, including included library sections.
- Resolved shared libraries, startup file, literal runtime modules and their
  linked dependencies; absent user-init candidates and optional modules in
  explicitly inactive startup branches. ASLR addresses are removed from `ldd`
  text before the inventory comparison.
- Reference version/settings, active Sparse solver diagnostics, fixed numerical
  screening threshold, sample/order policy, and explicit thread/startup settings.
- Available CPU, affinity, OS, memory, governor, clock and load observations.

Pre/post inventories must match. Tests cover changed binaries and transitive
includes, missing dependencies, ambiguous/dynamic include paths, unknown
startup controls, optional-module conditions, and mislabeled versions, solver,
thread settings, thresholds, sample populations and commands. An actual
ngspice47 run deliberately labeled with a different version is rejected by the end-to-end workflow.

The inventory reader is intentionally bounded to literal benchmark inputs and
simple startup conditionals. It rejects commands whose dependencies it cannot
establish. It does not prove that a previously built executable corresponds to
all current sources; retain the build verification and reference acquisition
records as well. It also does not certify a clean checkout installation, detect
every transient file mutation between snapshots, or replace the broader release
provenance work.

## Current evidence and remaining work

Checkpoint25 completes measurements for all 34 declared workloads against
ngspice47/default Sparse, with one simulator thread, three warmups and 30
measured pairs per workload. Every workload qualifies; the source, executable,
dependency and runtime inventories match before and after execution. The
[report](evidence/joss/2026-09-11-paired-measure47-report/report.md),
[timing CSV](evidence/joss/2026-09-11-paired-measure47-report/timings.csv),
[qualification figure](evidence/joss/2026-09-11-paired-measure47-report/qualification.pdf)
and [total-time figure](evidence/joss/2026-09-11-paired-measure47-report/timing-total.pdf)
retain the complete population. The generator also emits SVG figures.

Sampling ran September11 from 07:45:29 to 08:07:00 UTC, fixed to CPU0 of an
Intel Core Ultra9 285K. The observed governor was `powersave`, the clocksource
was `tsc`, and CPU frequency varied. No agent build, test, corpus, rendering or
archive job ran during sampling. Lightweight documentation/metadata work
continued on another core. Before/after load averages and the pre-run CPU
observation are retained; they do not certify exclusive machine use or absence
of unobserved contention.

The CSV contains 136 phase rows, each with 30 samples per engine. neospice has
a lower median total time in22 workloads and ngspice in12. These counts describe
this fixed population, not a general ranking. Large diode ladders and grid-noise
cases retain substantial neospice costs. Ranges are observed minima/maxima,
not confidence intervals; no aggregate speedup or causal attribution is made.

Checkpoint25 diagnostics preserve the exact acceptance, compared-point count,
worst normalized error and worst signal from checkpoint24 against47.
The full checkpoint archive retains raw samples, reports, checksums and build
association. See [progress](joss-progress.md) for the record and reconstruction.

Remaining benchmark work includes reference acquisition/build association for
the release, final-candidate evidence, manuscript interpretation and the
[other harness repairs](benchmark-harness-audit.md). The standalone TLV3201
workload and additional THS4131 sweep densities are not covered by this
34-workload experiment. No numerical tolerance or reference option was changed
to obtain a pass; historical failure evidence is preserved.

## Shared driver and THS4131 population

Checkpoint26 extracts the paired implementation to
`tests/bench/paired_benchmark.hpp`. `bench_comprehensive` retains its original
34-workload declaration. `bench_ths4131` uses the same driver with a separate
five-workflow declaration: OP, AC DEC10, and OP+AC DEC10/100/1000, all using the
original THS4131 circuit and 1 Hz–100 MHz sweep range. Returned AC grids have
81, 801 or 8,001 frequencies as applicable. Load is now a phase of a validated
workflow; old parse-only timings cannot bypass numerical qualification.

Select that population explicitly:

```sh
python tools/run_paired_benchmark.py --build BUILD --benchmark bench_ths4131 \
  --reference-version 47 --spinit STOCK_SPINIT --verify-only --output NEW_DIRECTORY
python tools/report_paired_benchmark.py NEW_DIRECTORY --output NEW_REPORT_DIRECTORY
```

The default remains `--benchmark bench_comprehensive`. The wrapper inventories
the selected executable's CMake flags/link command, not another target's files.
Both use the same relative threshold1e-3 and denominator floor1e-9, failed-case
rules and output format. The legacy THS4131 timing path had no numerical
qualification; the repair adds these checks rather than weakening one.
Sampling defaults are uniformly three warmups and30 pairs (configurable),
replacing the old density-dependent run counts. File loading and output
materialization are included in their documented phases. The completed
checkpoint25 timings remain tied to that historical binary and population;
verification of a refactored harness is not a new timing measurement.

## TLV3201 population and specialized policy

Checkpoint27 adds `--benchmark bench_tlv3201` to the same runner. Its single
original switching fixture retains the 100 ns step and 30 us stop. The explicit
`tlv3201-v1` policy preserves the original strict crossing allowance of 50 ns
and DC-port formula, while requiring valid complete data and nonempty matching
directed edges. Every pair is checked and failed cases return failure status.

The reported qualification error is a fraction of the metric's allowance;
it is not the comprehensive population's pointwise normalized-error scale.
The full pointwise comparison remains a separate informational record and
fails against47 in checkpoint27. See the
[TLV3201 contract and results](tlv3201-validation.md) for exact thresholds,
required signals, negative controls and these remaining discrepancies.
Verification against47 passes the specialized policy; no new
TLV3201 performance measurement is claimed. The original 34-workload timing
population is unchanged.

## Standalone diagnostic limits

Checkpoint28 repairs `bench_solver_throughput`'s failure reporting without
changing its two generators or eight size declarations. Every warmup and sample
must return converged, nonempty named voltage output and finite named voltages
and currents. A failure stops that case before calculating its median, prints
the error, and sets the process exit status to 1; other cases are still attempted.
This integrity check is outside the timers. It is not reference agreement,
required-signal completeness, dense-result validation or an independent KCL check.
Use the comprehensive paired population for accuracy-qualified performance.

The tool times public `parse()` and `run_dc()` calls with a monotonic clock.
Their medians include their API work; metadata comes from the final sample.
Dividing median DC time by that sample's iteration count is not a measurement
of an individual iteration, LU factorization or device evaluation.

The ordering, NeoSolver and refactorization microbenchmarks now explicitly label
their timings unqualified. None checks a numerical residual or known solution.
The ordering tool's symbolic-fill checks cannot establish numeric correctness,
ngspice/KLU performance, a causal explanation or a lower bound on speedup.
Its parsed ladder supplies only sparsity: numerical values are synthetic.
These tools are not used to substantiate the paper's benchmark claims.
