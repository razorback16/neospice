# Performance compared with ngspice 47

## Release benchmark: October 15, 2026

The release run measured a **2.03× geometric-mean speedup** across 34
accuracy-qualified workloads. neospice had the lower median total time in all
34 cases.

The run used 30 measured pairs and three warmups per workload, alternating
engine order, on a 24-core Intel Core Ultra 9 285K with no other benchmark
jobs running (one-minute load average 2.7 at the start, 2.1 at the end; a
resident inference server used well under one core). It
measured commit `368e4eb` with a clean worktree.
[Retained evidence](evidence/performance/2026-10-15-release-2/README.md) includes
the manifest, per-case summary, timing and qualification CSVs, and the generated
report. The manifest records ngspice 47, default Sparse, one simulator thread,
commands, source/build inventories, qualification, and environment
observations. All 34 cases qualified under the documented numerical checks.

Earlier runs under the same protocol:

| Run | Commit | Geometric mean | Wins |
|---|---|---:|---:|
| [October 15, before the loss fixes](evidence/performance/2026-10-15-release/README.md) | `b540d30` | 1.95× | 30 |
| [October 14](evidence/performance/2026-10-14-release/README.md) | `906930c` | 1.93× | 30 |

Between the two October 15 runs, ngspice's medians moved by −8% to +5% while
the code under it was unchanged, so treat single-workload differences of that
size as noise. `dc_mesh_141` is the most variable: 13.6×, 15.0× and 13.9×
across the three runs, all from ngspice's own median (5.26–5.89 s).
The earlier [October 3 development run](evidence/performance/2026-10-03-development/README.md)
used five pairs on a contended machine and reported 1.89×; contention inflated
`dc_mesh_141` to 22.8×. Cite the October 15 release run at `368e4eb`.

## All workload timings

These are median **total** times in milliseconds: load + analysis + cleanup.
A ratio above 1 favors neospice. A ratio below 1 favors ngspice.

| Workload | neospice (ms) | ngspice 47 (ms) | ngspice / neospice |
|---|---:|---:|---:|
| `dc_ths4131` | 0.4688 | 0.5704 | 1.22× |
| `dc_divider` | 0.0132 | 0.0456 | 3.46× |
| `ac_ths4131_10` | 0.6227 | 0.8249 | 1.32× |
| `ac_ths4131_1000` | 19.1255 | 21.7407 | 1.14× |
| `ac_rc` | 0.0242 | 0.0898 | 3.70× |
| `tran_rc` | 0.2332 | 1.2041 | 5.16× |
| `tran_rlc` | 0.2863 | 1.4768 | 5.16× |
| `tran_pulse` | 0.1221 | 0.8480 | 6.94× |
| `noise_divider` | 0.0269 | 0.1006 | 3.74× |
| `sweep_divider` | 0.2052 | 0.7111 | 3.46× |
| `op_ac_ths4131` | 0.6217 | 0.8021 | 1.29× |
| `op_ac_opa1632` | 3.9351 | 6.0141 | 1.53× |
| `dc_mesh_10` | 0.3213 | 0.4613 | 1.44× |
| `dc_mesh_32` | 5.1067 | 11.7729 | 2.31× |
| `dc_mesh_71` | 46.4506 | 240.0548 | 5.17× |
| `dc_mesh_141` | 389.6522 | 5409.6682 | 13.88× |
| `dc_diode_ladder_100` | 0.2960 | 0.5051 | 1.71× |
| `dc_diode_ladder_1000` | 2.6023 | 4.4487 | 1.71× |
| `dc_diode_ladder_5000` | 13.8014 | 25.2080 | 1.83× |
| `dc_diode_ladder_20000` | 64.4842 | 150.1589 | 2.33× |
| `ac_rc_grid_8` | 0.5221 | 0.7007 | 1.34× |
| `tran_rc_grid_8` | 2.6586 | 5.3070 | 2.00× |
| `ac_rc_grid_24` | 15.8797 | 16.0058 | 1.01× |
| `tran_rc_grid_24` | 68.3891 | 195.2941 | 2.86× |
| `ac_rc_grid_48` | 210.0154 | 227.0392 | 1.08× |
| `tran_rc_grid_48` | 617.1889 | 3169.5630 | 5.14× |
| `noise_rc_grid_24` | 17.8847 | 19.9161 | 1.11× |
| `ac_diode_rc_grid_8` | 0.8203 | 1.0721 | 1.31× |
| `tran_diode_rc_grid_8` | 9.7370 | 10.1625 | 1.04× |
| `ac_diode_rc_grid_24` | 24.2750 | 24.9814 | 1.03× |
| `tran_diode_rc_grid_24` | 263.6416 | 279.6140 | 1.06× |
| `ac_diode_rc_grid_48` | 298.3088 | 311.3253 | 1.04× |
| `tran_diode_rc_grid_48` | 3473.3580 | 3762.5985 | 1.08× |
| `noise_diode_rc_grid_24` | 28.4324 | 30.8321 | 1.08× |

The headline is the geometric mean of all 34 per-workload ratios, with equal
weight per workload. It is 2.02990878 before rounding. The
[CSV](evidence/performance/2026-10-15-release-2/timings.csv) retains each
phase's medians and observed minima/maxima. Observed ranges are not confidence
intervals. This population-specific metric does not establish a universal
speedup or isolate the contribution of an individual optimization.

## Former losses

Before `368e4eb`, ngspice was faster on four mid-size RC grids with diodes.
`perf` hardware-counter profiles of both engines, pinned to one performance
core, traced each loss to a specific cost. Load was already 1.7–1.9× faster;
the analysis phase was slower.

| Workload | Before (`b540d30`) | Release (`368e4eb`) |
|---|---:|---:|
| `tran_diode_rc_grid_8` | 0.86× | 1.04× |
| `tran_diode_rc_grid_24` | 0.99× | 1.06× |
| `ac_diode_rc_grid_24` | 0.98× | 1.03× |
| `ac_diode_rc_grid_48` | 0.90× | 1.04× |

- **Truncation-error estimate (`b83d973`).** Both engines make the same
  216,876 `exp` calls on `tran_diode_rc_grid_8`, so they follow the same Newton
  path, and factorization costs the same. The estimate cost 9.4 M cycles per
  run against 4.1 M for ngspice's `CKTterr`, with the same formula. GCC's
  `-O3` vectorizer had turned the divided-difference loop into stack stores
  followed by overlapping vector loads, which the CPU cannot forward from the
  store buffer. Orders 1 and 2 are now scalar code with the same operations in
  the same order (bit-identical on 10⁷ random inputs); the estimate costs
  2.9 M cycles.
- **AC pivot order (`92f2f83`).** AC built a fresh solver, so its first
  frequency point ran a complete Markowitz ordering of the complex matrix:
  182 M cycles per run on `ac_diode_rc_grid_48`, about twice the whole gap.
  ngspice factors AC on the matrix the operating point already ordered and
  keeps every pivot that passes the threshold test, so its first point costs
  about one factorization. AC now adopts the operating point's pivot order.
  This also matches ngspice's rounding: of 173 ngspice comparisons that report
  margins, five AC errors shrank (two to exactly zero) and none grew.
- **Newton bookkeeping (`368e4eb`).** `newton_solve` spent 5.3 M cycles per
  run on its own work against 0.4 M in ngspice's `NIiter`. Transient steps no
  longer scan every equation for the residual norm, which only DC reads, or
  copy `state0` for operating-point damping, and the never-overridden
  per-device `limit_voltages` hook is gone.

The transient changes also help linear grids: `tran_rc_grid_8` went from 1.44×
to 2.00× and `tran_rc_grid_24` from 2.51× to 2.86×.

In the pinned profile, the analysis phase of `tran_diode_rc_grid_8` is still
about 7% slower than ngspice's; the total is ahead because load is faster. The
largest identified difference is the intermediate matrix that devices stamp
into before it is copied into the sparse matrix, where ngspice stamps
directly. The narrowest wins are now `ac_rc_grid_24` (1.01×) and
`ac_diode_rc_grid_24` (1.03×).

Commit `87b9bd1` earlier removed most other per-step transient overhead. It
reuses unchanged factors in linear transients, buffers output rows, and skips
devices that contribute no truncation error.

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
