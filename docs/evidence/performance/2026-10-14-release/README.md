# October 14 release benchmark

This directory contains the October 14 release measurement. The harness
recorded ngspice 47 (default Sparse, one simulator thread), 34 qualified
workloads, three warmups, and **30 measured pairs per workload**. The measured
source is commit `906930c` with a clean worktree. The harness verified that the
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
`f35d19f393e12a9b635ebfb9caade7adc3d7d8c7f5255717fb6b5fb6a94c1d3e`. The manifest
reports `complete=true` and `evidence_valid=true`.

Host: Intel Core Ultra 9 285K (24 cores), Linux 6.17, with no other benchmark
jobs running. The one-minute load average was 2.8 at the start and 2.0 at the
end. System observations do not prove that no other host jobs ran.

## Summary calculation

Use all 34 `total_us` rows. For each workload, divide the ngspice median by the
neospice median. The equally weighted geometric mean of those ratios is
**1.93181650**, rounded to **1.93×**. neospice has the lower median in 30 cases.
This metric gives each workload equal weight. It is not the ratio of summed
times or a prediction for an arbitrary circuit.

```python
import csv
import statistics
from pathlib import Path

path = Path("docs/evidence/performance/2026-10-14-release/timings.csv")
with path.open() as stream:
    rows = [r for r in csv.DictReader(stream) if r["phase"] == "total_us"]
ratios = [float(r["reference_over_neo"]) for r in rows]
print(len(ratios), statistics.geometric_mean(ratios))
```

See the [comparison guide](../../../performance-analysis.md)
for the full table and [methods](../../../benchmark-methods.md) for a fresh run.
