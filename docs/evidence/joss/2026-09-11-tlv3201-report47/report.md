# Paired benchmark report: ngspice 47

Population: 1 declared workloads; 1 qualified.

TLV3201 specialized qualification: output crossing error <50 ns with matching nonempty edge directions; v(vcc)/v(inm) error <=0.01*max(abs(reference),0.01 V). Reported normalized errors are fractions of these separate allowances; waveform/internal-node comparisons are informational and retained in raw records. This is not pointwise waveform certification.

Reference: default Sparse, one simulator thread. Separate populations and reference versions must not be pooled.

Qualification uses all attempted comparisons, including validation/warmup pairs. Invalid runs are rejected.

Verification only; no timing samples or performance claim.

Only accuracy-qualified cases receive timing rows. Min–max bars describe observed spread; they do not establish absence of contention or general scaling.

| Workload | Qualified | Maximum allowance fraction | Failure |
|---|---|---|---|
| tran_tlv3201 | True | 1.11808e-12 |  |
