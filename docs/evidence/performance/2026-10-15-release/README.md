# October 15 release benchmark

This directory contains the October 15 release measurement. The harness
recorded ngspice 47 (default Sparse, one simulator thread), 34 qualified
workloads, three warmups, and **30 measured pairs per workload**. The measured
source is commit `b540d30` with a clean worktree. The harness verified that the
source inventory did not change during the run.

- [manifest.json](manifest.json): commands, environment, source/build/runtime
  inventories, system observations, artifact hashes, and qualification status.
- [summary.json](summary.json): all workload results and timing summaries.
- [timings.csv](timings.csv): generated timing table for all four phases.
- [qualification.csv](qualification.csv): per-workload accuracy qualification.
- [report.md](report.md) and [timing-total.svg](timing-total.svg): output of
  `tools/report_paired_benchmark.py`.

The raw `pairs.jsonl` waveform log is about 518 MB and is not copied into this
directory. Its SHA-256 is
`b15e6d6a6bbae8c056cb8fd9784eded66a4d4b9ad0eb1e671281974cf7d4a075`. The manifest
reports `complete=true` and `evidence_valid=true`.

Host: Intel Core Ultra 9 285K (24 cores), Linux 6.17, with no other benchmark
jobs running. The one-minute load average was 1.7 at the start and 4.7 at the
end; an unrelated FPGA batch job started near the end of the run. Each pair
times both engines back to back, so shared load largely cancels in the ratio.
System observations do not prove that no other host jobs ran.

The [October 14 run](../2026-10-14-release/README.md) measured commit `906930c`,
before the fuzzing fixes, under the same protocol and gave 1.93× with the same
30 wins. 29 of 34 per-workload ratios agree with this run within 3%; the largest
changes are `dc_mesh_141` (13.6× to 15.0×) and `dc_divider` (2.98× to 3.25×).

## Summary calculation

Use all 34 `total_us` rows. For each workload, divide the ngspice median by the
neospice median. The equally weighted geometric mean of those ratios is
**1.94682839**, rounded to **1.95×**. neospice has the lower median in 30 cases.
This metric gives each workload equal weight. It is not the ratio of summed
times or a prediction for an arbitrary circuit.

```python
import csv
import statistics
from pathlib import Path

path = Path("docs/evidence/performance/2026-10-15-release/timings.csv")
with path.open() as stream:
    rows = [r for r in csv.DictReader(stream) if r["phase"] == "total_us"]
ratios = [float(r["reference_over_neo"]) for r in rows]
print(len(ratios), statistics.geometric_mean(ratios))
```

See the [comparison guide](../../../performance-analysis.md)
for the full table and [methods](../../../benchmark-methods.md) for a fresh run.
