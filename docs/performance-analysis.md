# Performance compared with ngspice 47

## Release benchmark: October 14, 2026

The release run measured a **1.93× geometric-mean speedup** across 34
accuracy-qualified workloads. neospice had the lower median total time in 30
cases.

The run used 30 measured pairs and three warmups per workload, alternating
engine order, on a 24-core Intel Core Ultra 9 285K with no other benchmark
jobs running (one-minute load average 2–3). It measured
commit `906930c` with a clean worktree.
[Retained evidence](evidence/performance/2026-10-14-release/README.md) includes
the manifest, per-case summary, timing and qualification CSVs, and the generated
report. The manifest records ngspice 47, default Sparse, one simulator thread,
commands, source/build inventories, qualification, and environment
observations. All 34 cases qualified under the documented numerical checks.

The earlier [October 3 development run](evidence/performance/2026-10-03-development/README.md)
used five pairs on a contended machine and reported 1.89×. Contention inflated
its largest ratio. `dc_mesh_141` measured 22.8× then and 13.6× on the quiet
machine, because ngspice's median fell from 9.30 s to 5.26 s. Cite the October
14 run.

## All workload timings

These are median **total** times in milliseconds: load + analysis + cleanup.
A ratio above 1 favors neospice. A ratio below 1 favors ngspice.

| Workload | neospice (ms) | ngspice 47 (ms) | ngspice / neospice |
|---|---:|---:|---:|
| `dc_ths4131` | 0.4930 | 0.5748 | 1.17× |
| `dc_divider` | 0.0166 | 0.0496 | 2.98× |
| `ac_ths4131_10` | 0.6487 | 0.8370 | 1.29× |
| `ac_ths4131_1000` | 18.8815 | 22.0111 | 1.17× |
| `ac_rc` | 0.0245 | 0.0895 | 3.65× |
| `tran_rc` | 0.2662 | 1.2339 | 4.63× |
| `tran_rlc` | 0.3437 | 1.4852 | 4.32× |
| `tran_pulse` | 0.1241 | 0.8726 | 7.03× |
| `noise_divider` | 0.0270 | 0.1037 | 3.84× |
| `sweep_divider` | 0.2271 | 0.7286 | 3.21× |
| `op_ac_ths4131` | 0.6426 | 0.7972 | 1.24× |
| `op_ac_opa1632` | 4.0215 | 5.8848 | 1.46× |
| `dc_mesh_10` | 0.3258 | 0.4730 | 1.45× |
| `dc_mesh_32` | 5.2936 | 12.0942 | 2.28× |
| `dc_mesh_71` | 48.3860 | 246.8540 | 5.10× |
| `dc_mesh_141` | 385.6873 | 5260.1082 | 13.64× |
| `dc_diode_ladder_100` | 0.2885 | 0.4924 | 1.71× |
| `dc_diode_ladder_1000` | 2.6822 | 4.5656 | 1.70× |
| `dc_diode_ladder_5000` | 13.9635 | 24.8068 | 1.78× |
| `dc_diode_ladder_20000` | 64.8563 | 145.2807 | 2.24× |
| `ac_rc_grid_8` | 0.5618 | 0.7308 | 1.30× |
| `tran_rc_grid_8` | 3.6849 | 5.4116 | 1.47× |
| `ac_rc_grid_24` | 15.7834 | 16.4862 | 1.04× |
| `tran_rc_grid_24` | 77.2576 | 193.5771 | 2.51× |
| `ac_rc_grid_48` | 208.2875 | 227.1702 | 1.09× |
| `tran_rc_grid_48` | 638.2532 | 3090.0444 | 4.84× |
| `noise_rc_grid_24` | 17.2044 | 19.2087 | 1.12× |
| `ac_diode_rc_grid_8` | 0.7958 | 1.0302 | 1.29× |
| `tran_diode_rc_grid_8` | 11.3065 | 9.8974 | 0.88× |
| `ac_diode_rc_grid_24` | 24.6425 | 23.9946 | 0.97× |
| `tran_diode_rc_grid_24` | 282.8896 | 275.0942 | 0.97× |
| `ac_diode_rc_grid_48` | 350.6459 | 314.4945 | 0.90× |
| `tran_diode_rc_grid_48` | 3582.3794 | 3805.3085 | 1.06× |
| `noise_diode_rc_grid_24` | 28.5511 | 31.2468 | 1.09× |

The headline is the geometric mean of all 34 per-workload ratios, with equal
weight per workload. It is 1.93181650 before rounding. The
[CSV](evidence/performance/2026-10-14-release/timings.csv) retains each
phase's medians and observed minima/maxima. Observed ranges are not confidence
intervals. This population-specific metric does not establish a universal
speedup or isolate the contribution of an individual optimization.

## Where ngspice is faster

All four losses are mid-size RC grids with diodes. Load is about 1.9× faster
than ngspice in each case. The analysis phase is slower.

- **Transient (`tran_diode_rc_grid_8`, 0.88×; `tran_diode_rc_grid_24`, 0.97×).**
  Factorization and device evaluation match ngspice when timed in isolation.
  The remaining cost is per-step overhead. Diode truncation-error estimation
  takes about 2.3× ngspice's `trunc` time for the same formula. The cause is not
  yet identified.
- **AC (`ac_diode_rc_grid_24`, 0.97×; `ac_diode_rc_grid_48`, 0.90×).** The
  complex refactorization per frequency point is about 12% slower than ngspice's
  Sparse. Fill-in is identical to the real factorization.

Commit `87b9bd1` removed most of the transient overhead. It reuses unchanged
factors in linear transients, buffers output rows, and skips devices that
contribute no truncation error. That change turned `tran_rc_grid_8` from 0.75×
into 1.47×.

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
