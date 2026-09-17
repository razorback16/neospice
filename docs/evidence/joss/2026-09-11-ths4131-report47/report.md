# Paired benchmark report: ngspice 47

Population: 5 declared workloads; 5 qualified.

Reference: default Sparse, one simulator thread. Separate populations and reference versions must not be pooled.

Qualification uses all attempted comparisons, including validation/warmup pairs. Invalid runs are rejected.

Verification only; no timing samples or performance claim.

Only accuracy-qualified cases receive timing rows. Min–max bars describe observed spread; they do not establish absence of contention or general scaling.

| Workload | Qualified | Maximum normalized error | Failure |
|---|---|---|---|
| dc_ths4131 | True | 3.53965e-10 |  |
| ac_ths4131_10 | True | 1.29952e-08 |  |
| op_ac_ths4131_10 | True | 1.29952e-08 |  |
| op_ac_ths4131_100 | True | 1.35369e-08 |  |
| op_ac_ths4131_1000 | True | 1.37284e-08 |  |
