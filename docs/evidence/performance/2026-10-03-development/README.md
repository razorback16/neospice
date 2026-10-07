# October 3 development benchmark

This directory contains the October 3 development measurement. The harness
recorded ngspice 47,
34 qualified workloads, one warmup, and five measured pairs per workload.

- [manifest.json](manifest.json): commands, environment, source/build/runtime
  inventories, system observations, artifact hashes, and qualification status.
- [summary.json](summary.json): all workload results and timing summaries.
- [timings.csv](timings.csv): generated timing table for all four phases.

The original `pairs.jsonl` waveform log is about 107 MB and is not copied into
this directory. The manifest retains its SHA-256 and original artifact names.
Consequently, this snapshot supports inspection of the recorded summaries, but
does not supply the complete raw input for the report generator. The retained
manifest reports `complete=true`, `evidence_valid=true`, and
`all_workloads_qualified=true` for the original run.

The machine was contended and this run used five samples. Treat its timings as
a development observation, not a final release benchmark. Repeat the full
protocol with 30 samples on an idle machine for release evidence. The source
inventory identifies the measured implementation, which predates the October 6
API and browser changes.

## Summary calculation

Use all 34 `total_us` rows. For each workload, divide the ngspice median by the
neospice median. The equally weighted geometric mean of those ratios is
**1.88873457**, rounded to **1.89×**. neospice has the lower median in 30 cases.
This metric gives each workload equal weight. It is not the ratio of summed
times or a prediction for an arbitrary circuit.

```python
import csv
import statistics
from pathlib import Path

path = Path("docs/evidence/performance/2026-10-03-development/timings.csv")
with path.open() as stream:
    rows = [r for r in csv.DictReader(stream) if r["phase"] == "total_us"]
ratios = [float(r["reference_over_neo"]) for r in rows]
print(len(ratios), statistics.geometric_mean(ratios))
```

See the [comparison guide](../../../performance-analysis.md)
for the full table and [methods](../../../benchmark-methods.md) for a fresh run.
