# Paired performance comparison with ngspice

The current benchmark protocol is documented in [benchmark methods](benchmark-methods.md).
[Progress and evidence](joss-progress.md) distinguish completed verification,
active measurements and remaining defects. The former in-process report is
[archived](evidence/joss/2026-09-11-historical-inprocess-performance.md); its tables
and optimization explanations are not validated evidence for the current code.

## Workload population

The comprehensive harness now declares 34 paired workloads. It includes small
linear circuits, THS4131 and OPA1632 macromodels, all eight original large
resistor-mesh/diode-ladder DC cases, and additional two-dimensional RC/diode-RC
AC, transient and noise cases. Generated netlists, model dependencies, commands
and runtime settings are inventoried. Full populations and targeted diagnostic
runs are labeled separately.

Checkpoint25 measured all34 workloads against ngspice47: every pair qualifies,
with three warmups and30 measured pairs per workload. The
[complete timing CSV](evidence/joss/2026-09-11-paired-measure47-report/timings.csv)
and [figure](evidence/joss/2026-09-11-paired-measure47-report/timing-total.pdf)
retain both engines' medians and observed ranges. This experiment has22 lower
neospice total medians and12 lower ngspice total medians, with large neospice
costs on diode ladders and grid noise. No aggregate speedup is asserted.

Sampling used fixed CPU0 affinity, one simulator thread and alternating order.
The source/runtime inventories match before and after. See the method record
for machine, governor, environment observations and their limits.

## Defined measurements

| Phase | Included work |
|---|---|
| Load | Public loading, file reads and include resolution |
| Analysis | Requested analyses, deferred setup and owned output materialization |
| Cleanup | Circuit/reference-plot cleanup |
| Total | Contiguous interval encompassing those three phases |

Application/library initialization is outside the timers. Returned output
objects survive cleanup for comparison, and their destruction is outside the
timers for both engines. Setup performed by the two interfaces differs, so the
analysis phase must not be described as isolated solve time. Every sampled pair
must pass the same numerical checks; one failure disqualifies the entire case.

The reference uses its declared version's default Sparse solver. neospice's
real-solver selection depends on circuit properties; its larger linear path
and Sparse-derived path are distinct implementations. The source-based
[architecture comparison](neospice-vs-ngspice.md) describes that policy.
Complex/noise behavior and public-interface costs must not be inferred from
standalone real-matrix microbenchmarks.

## Reproduction and interpretation

Use `tools/run_paired_benchmark.py` after building the comprehensive harness.
Use `tools/report_paired_benchmark.py` on the completed output directory to
produce CSV, Markdown and vector PDF/SVG figures. The report validates input
hashes and reconstructs its summary from raw records. Exact commands and
required dependencies appear in [benchmark methods](benchmark-methods.md).

Keep per-workload medians and observed minima/maxima, failed-case diagnostics,
reference version/settings and machine observations with every claim. A ratio
of medians describes the declared paired operation; it is not an aggregate
speedup. Synthetic banded matrices do not establish general circuit scaling.
Caching, ordering, parser or convergence mechanisms require controlled
ablations before a performance difference can be attributed to them.

The paper still requires final-candidate interpretation of these measurements
and a real research workflow. Historical numbers, verification-only outputs, incomplete runs and
unqualified standalone timings cannot fill those requirements.
