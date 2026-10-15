# October 15 release benchmark (after the loss fixes)

This directory contains the second October 15 release measurement. The harness
recorded ngspice 47 (default Sparse, one simulator thread), 34 qualified
workloads, three warmups, and **30 measured pairs per workload**. The measured
source is commit `368e4eb` with a clean worktree. It includes the three fixes
for the former losing workloads: the truncation-error store-forwarding stall
(`b83d973`), AC reuse of the operating-point pivot order (`92f2f83`), and the
trimmed Newton bookkeeping (`368e4eb`). The harness verified that the source
inventory did not change during the run.

- [manifest.json](manifest.json): commands, environment, source/build/runtime
  inventories, system observations, artifact hashes, and qualification status.
- [summary.json](summary.json): all workload results and timing summaries.
- [timings.csv](timings.csv): generated timing table for all four phases.
- [qualification.csv](qualification.csv): per-workload accuracy qualification.
- [report.md](report.md) and [timing-total.svg](timing-total.svg): output of
  `tools/report_paired_benchmark.py`.

The raw `pairs.jsonl` waveform log is about 518 MB and is not copied into this
directory. Its SHA-256 is
`ed29571a24b4b05a115c2bcd3dfd83e6a42749de08bb4fc3928f269fa90390d4`. The
manifest reports `complete=true` and `evidence_valid=true`.

Host: Intel Core Ultra 9 285K (24 cores), Linux 6.17, with no other benchmark
jobs running. The one-minute load average was 2.7 at the start and 2.1 at the
end. An idle inference server was resident. System observations do not prove
that no other host jobs ran.

The [first October 15 run](../2026-10-15-release/README.md) measured commit
`b540d30`, before these fixes, under the same protocol and gave 1.95× with 30
wins.

## Summary calculation

Use all 34 `total_us` rows. For each workload, divide the ngspice median by the
neospice median. The equally weighted geometric mean of those ratios is
**2.02990878**, rounded to **2.03×**. neospice has the lower median in all 34
cases. This metric gives each workload equal weight. It is not the ratio of
summed times or a prediction for an arbitrary circuit.

```python
import csv
import statistics
from pathlib import Path

path = Path("docs/evidence/performance/2026-10-15-release-2/timings.csv")
with path.open() as stream:
    rows = [r for r in csv.DictReader(stream) if r["phase"] == "total_us"]
ratios = [float(r["reference_over_neo"]) for r in rows]
print(len(ratios), statistics.geometric_mean(ratios))
```

See the [comparison guide](../../../performance-analysis.md)
for the full table and [methods](../../../benchmark-methods.md) for a fresh run.
