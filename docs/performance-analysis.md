# Performance compared with ngspice 47

## Development benchmark: October 3, 2026

The recorded development run measured a **1.89× geometric-mean speedup** across
34 accuracy-qualified workloads. neospice had the lower median total time in
30 cases.

The result uses five measured pairs and one warmup per workload on a contended
machine. It is an indicative development measurement, not a final release
benchmark. The recorded source predates the October 6 workflow and browser
changes. A fresh 30-sample run on an idle machine remains required for release
and paper figures.

[Retained evidence](evidence/performance/2026-10-03-development/README.md)
includes the original manifest, per-case summary, and timing CSV. The manifest
records ngspice 47, default Sparse, one simulator thread, commands, source/build
inventories, qualification, and environment observations. All 34 cases qualified
under the documented numerical checks.

## All workload timings

These are median **total** times in milliseconds: load + analysis + cleanup.
A ratio above 1 favors neospice. A ratio below 1 favors ngspice.

| Workload | neospice (ms) | ngspice 47 (ms) | ngspice / neospice |
|---|---:|---:|---:|
| `dc_ths4131` | 0.3940 | 0.4458 | 1.13× |
| `dc_divider` | 0.0170 | 0.0553 | 3.26× |
| `ac_ths4131_10` | 0.6448 | 0.8392 | 1.30× |
| `ac_ths4131_1000` | 20.1148 | 24.9612 | 1.24× |
| `ac_rc` | 0.0270 | 0.1018 | 3.77× |
| `tran_rc` | 0.3525 | 1.2950 | 3.67× |
| `tran_rlc` | 0.4339 | 1.5125 | 3.49× |
| `tran_pulse` | 0.1693 | 0.8831 | 5.22× |
| `noise_divider` | 0.0295 | 0.1181 | 4.00× |
| `sweep_divider` | 0.2977 | 0.7551 | 2.54× |
| `op_ac_ths4131` | 0.6504 | 0.8485 | 1.30× |
| `op_ac_opa1632` | 4.2691 | 6.3141 | 1.48× |
| `dc_mesh_10` | 0.3270 | 0.4950 | 1.51× |
| `dc_mesh_32` | 5.3819 | 12.4984 | 2.32× |
| `dc_mesh_71` | 50.2106 | 262.7946 | 5.23× |
| `dc_mesh_141` | 408.5199 | 9301.8706 | 22.77× |
| `dc_diode_ladder_100` | 0.2909 | 0.5249 | 1.80× |
| `dc_diode_ladder_1000` | 2.6592 | 4.6070 | 1.73× |
| `dc_diode_ladder_5000` | 15.0102 | 28.7565 | 1.92× |
| `dc_diode_ladder_20000` | 75.6713 | 197.4065 | 2.61× |
| `ac_rc_grid_8` | 0.5368 | 0.7392 | 1.38× |
| `tran_rc_grid_8` | 7.1136 | 5.3212 | 0.75× |
| `ac_rc_grid_24` | 16.8138 | 17.2591 | 1.03× |
| `tran_rc_grid_24` | 96.0727 | 202.5554 | 2.11× |
| `ac_rc_grid_48` | 221.4157 | 244.0397 | 1.10× |
| `tran_rc_grid_48` | 749.0788 | 3299.4564 | 4.40× |
| `noise_rc_grid_24` | 17.9952 | 20.0957 | 1.12× |
| `ac_diode_rc_grid_8` | 0.8441 | 1.0850 | 1.29× |
| `tran_diode_rc_grid_8` | 13.9814 | 10.2039 | 0.73× |
| `ac_diode_rc_grid_24` | 26.3072 | 25.1145 | 0.95× |
| `tran_diode_rc_grid_24` | 303.1352 | 277.5809 | 0.92× |
| `ac_diode_rc_grid_48` | 349.0437 | 356.6907 | 1.02× |
| `tran_diode_rc_grid_48` | 3910.6627 | 4505.6011 | 1.15× |
| `noise_diode_rc_grid_24` | 28.4163 | 31.0930 | 1.09× |

The headline is the geometric mean of all 34 per-workload ratios, with equal
weight per workload. It is 1.88873457 before rounding. The
[CSV](evidence/performance/2026-10-03-development/timings.csv) retains each
phase's medians and observed minima/maxima. Observed ranges are not confidence
intervals. This population-specific metric does not establish a universal
speedup or isolate the contribution of an individual optimization.

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
