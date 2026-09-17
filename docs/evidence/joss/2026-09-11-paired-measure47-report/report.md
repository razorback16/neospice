# Paired benchmark report: ngspice 47

Population: 34 declared workloads; 34 qualified.

Reference: default Sparse, one simulator thread. Separate populations and reference versions must not be pooled.

Qualification uses all attempted comparisons, including validation/warmup pairs. Invalid runs are rejected.

Timing table: equal paired samples and warmups, alternating engine order. Load/analysis/cleanup/total boundaries follow docs/benchmark-methods.md. Median and min–max are descriptive statistics, not confidence intervals. Ratios are reference median divided by neospice median; no aggregate speedup is computed.

Only accuracy-qualified cases receive timing rows. Min–max bars describe observed spread; they do not establish absence of contention or general scaling.

| Workload | Qualified | Maximum normalized error | Failure |
|---|---|---|---|
| dc_ths4131 | True | 3.53965e-10 |  |
| dc_divider | True | 0 |  |
| ac_ths4131_10 | True | 1.29952e-08 |  |
| ac_ths4131_1000 | True | 1.37284e-08 |  |
| ac_rc | True | 0 |  |
| tran_rc | True | 0 |  |
| tran_rlc | True | 7.47856e-12 |  |
| tran_pulse | True | 0 |  |
| noise_divider | True | 0 |  |
| sweep_divider | True | 0 |  |
| op_ac_ths4131 | True | 1.29952e-08 |  |
| op_ac_opa1632 | True | 0.00040134 |  |
| dc_mesh_10 | True | 0 |  |
| dc_mesh_32 | True | 8.8613e-14 |  |
| dc_mesh_71 | True | 9.93125e-13 |  |
| dc_mesh_141 | True | 4.42583e-12 |  |
| dc_diode_ladder_100 | True | 0 |  |
| dc_diode_ladder_1000 | True | 0 |  |
| dc_diode_ladder_5000 | True | 0 |  |
| dc_diode_ladder_20000 | True | 0 |  |
| ac_rc_grid_8 | True | 0 |  |
| tran_rc_grid_8 | True | 0 |  |
| ac_rc_grid_24 | True | 0 |  |
| tran_rc_grid_24 | True | 1.89965e-11 |  |
| ac_rc_grid_48 | True | 0 |  |
| tran_rc_grid_48 | True | 3.19604e-10 |  |
| noise_rc_grid_24 | True | 1.93424e-13 |  |
| ac_diode_rc_grid_8 | True | 0 |  |
| tran_diode_rc_grid_8 | True | 0 |  |
| ac_diode_rc_grid_24 | True | 0 |  |
| tran_diode_rc_grid_24 | True | 0 |  |
| ac_diode_rc_grid_48 | True | 0 |  |
| tran_diode_rc_grid_48 | True | 0 |  |
| noise_diode_rc_grid_24 | True | 3.10533e-12 |  |
