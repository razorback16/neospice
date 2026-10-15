# Performance compared with ngspice 47

## Release benchmark: October 15, 2026

The release run measured a **1.95× geometric-mean speedup** across 34
accuracy-qualified workloads. neospice had the lower median total time in 30
cases.

The run used 30 measured pairs and three warmups per workload, alternating
engine order, on a 24-core Intel Core Ultra 9 285K with no other benchmark
jobs running (one-minute load average 1.7 at the start, 4.7 at the end after an
unrelated batch job started). It measured
commit `b540d30` with a clean worktree.
[Retained evidence](evidence/performance/2026-10-15-release/README.md) includes
the manifest, per-case summary, timing and qualification CSVs, and the generated
report. The manifest records ngspice 47, default Sparse, one simulator thread,
commands, source/build inventories, qualification, and environment
observations. All 34 cases qualified under the documented numerical checks.

The [October 14 run](evidence/performance/2026-10-14-release/README.md) used
the same protocol on commit `906930c`, before the fuzzing fixes, and measured
1.93× with the same 30 wins. 29 of 34 per-workload ratios agree within 3%. The
largest changes are `dc_mesh_141` (13.6× to 15.0×) and the 14 µs `dc_divider`
(2.98× to 3.25×); no workload changed sides.
The earlier [October 3 development run](evidence/performance/2026-10-03-development/README.md)
used five pairs on a contended machine and reported 1.89×. Contention inflated
its largest ratio. `dc_mesh_141` measured 22.8× then, against 13.6× and 15.0×
in the two release-protocol runs, because ngspice's median fell from 9.30 s to
5.26–5.89 s. Cite the October 15 run.

## All workload timings

These are median **total** times in milliseconds: load + analysis + cleanup.
A ratio above 1 favors neospice. A ratio below 1 favors ngspice.

| Workload | neospice (ms) | ngspice 47 (ms) | ngspice / neospice |
|---|---:|---:|---:|
| `dc_ths4131` | 0.4690 | 0.5670 | 1.21× |
| `dc_divider` | 0.0137 | 0.0447 | 3.25× |
| `ac_ths4131_10` | 0.6448 | 0.8399 | 1.30× |
| `ac_ths4131_1000` | 18.5163 | 20.7896 | 1.12× |
| `ac_rc` | 0.0236 | 0.0869 | 3.69× |
| `tran_rc` | 0.2678 | 1.2358 | 4.61× |
| `tran_rlc` | 0.3399 | 1.4833 | 4.36× |
| `tran_pulse` | 0.1180 | 0.8253 | 7.00× |
| `noise_divider` | 0.0261 | 0.1001 | 3.83× |
| `sweep_divider` | 0.2198 | 0.6968 | 3.17× |
| `op_ac_ths4131` | 0.6351 | 0.7983 | 1.26× |
| `op_ac_opa1632` | 4.1368 | 6.2126 | 1.50× |
| `dc_mesh_10` | 0.3300 | 0.4800 | 1.45× |
| `dc_mesh_32` | 5.4049 | 12.3480 | 2.28× |
| `dc_mesh_71` | 48.1340 | 245.5426 | 5.10× |
| `dc_mesh_141` | 391.9637 | 5891.9907 | 15.03× |
| `dc_diode_ladder_100` | 0.2911 | 0.5035 | 1.73× |
| `dc_diode_ladder_1000` | 2.6295 | 4.4650 | 1.70× |
| `dc_diode_ladder_5000` | 14.3963 | 25.7617 | 1.79× |
| `dc_diode_ladder_20000` | 69.3940 | 160.5111 | 2.31× |
| `ac_rc_grid_8` | 0.5428 | 0.7269 | 1.34× |
| `tran_rc_grid_8` | 3.7447 | 5.3883 | 1.44× |
| `ac_rc_grid_24` | 16.3714 | 16.9833 | 1.04× |
| `tran_rc_grid_24` | 78.9374 | 198.5269 | 2.51× |
| `ac_rc_grid_48` | 217.6769 | 236.4100 | 1.09× |
| `tran_rc_grid_48` | 651.7602 | 3179.5197 | 4.88× |
| `noise_rc_grid_24` | 18.1549 | 20.0723 | 1.11× |
| `ac_diode_rc_grid_8` | 0.8258 | 1.0684 | 1.29× |
| `tran_diode_rc_grid_8` | 12.1127 | 10.3787 | 0.86× |
| `ac_diode_rc_grid_24` | 25.4836 | 24.9736 | 0.98× |
| `tran_diode_rc_grid_24` | 283.3074 | 279.7791 | 0.99× |
| `ac_diode_rc_grid_48` | 352.9213 | 316.9371 | 0.90× |
| `tran_diode_rc_grid_48` | 3649.5414 | 3871.6837 | 1.06× |
| `noise_diode_rc_grid_24` | 28.8877 | 31.0916 | 1.08× |

The headline is the geometric mean of all 34 per-workload ratios, with equal
weight per workload. It is 1.94682839 before rounding. The
[CSV](evidence/performance/2026-10-15-release/timings.csv) retains each
phase's medians and observed minima/maxima. Observed ranges are not confidence
intervals. This population-specific metric does not establish a universal
speedup or isolate the contribution of an individual optimization.

## Where ngspice is faster

All four losses are mid-size RC grids with diodes. Load is 1.7–1.9× faster
than ngspice in each case. The analysis phase is slower.

- **Transient (`tran_diode_rc_grid_8`, 0.86×; `tran_diode_rc_grid_24`, 0.99×).**
  Factorization and device evaluation match ngspice when timed in isolation.
  The remaining cost is per-step overhead. Diode truncation-error estimation
  takes about 2.3× ngspice's `trunc` time for the same formula. The cause is not
  yet identified.
- **AC (`ac_diode_rc_grid_24`, 0.98×; `ac_diode_rc_grid_48`, 0.90×).** The
  complex refactorization per frequency point is about 12% slower than ngspice's
  Sparse. Fill-in is identical to the real factorization.

Commit `87b9bd1` removed most of the transient overhead. It reuses unchanged
factors in linear transients, buffers output rows, and skips devices that
contribute no truncation error. That change turned `tran_rc_grid_8` from 0.75×
into 1.47× (1.44× in the October 15 run).

## Timing boundaries

| Phase | Included work |
|---|---|
| Load | Public loading, file reads, and include resolution |
| Analysis | Requested analyses, deferred setup, and owned output materialization |
| Cleanup | Circuit/reference-plot cleanup |
| Total | Contiguous interval containing those three phases |

Initialization is outside the timers. Returned output objects survive cleanup
for comparison. Their destruction is outside both engines' timers. Every pair
must pass status, signal, axis, and value checks. A failed pair disqualifies its
case. The analysis phase is not isolated factorization or Newton time.

## Reproduce

Follow [benchmark methods](benchmark-methods.md) to build the comprehensive
harness and run `tools/run_paired_benchmark.py`. Use
`tools/report_paired_benchmark.py` on the full raw output directory to generate
CSV, Markdown, and figures. Preserve the manifest and raw records with results.

See [architecture](neospice-design.md) for the current engine design.
