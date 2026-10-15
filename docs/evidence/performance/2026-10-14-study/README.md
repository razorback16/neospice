# October 14 study-throughput benchmark

`tools/run_study_benchmark.py` measured many-sample studies at commit `906930c`
on a 24-core Intel Core Ultra 9 285K. Each workload varies one resistor over N
evenly spaced values. Every sample's probe value matched the serial ngspice
session to within the recorded `max_rel_vs_ngspice_serial`. No sample failed.

- [study-10000.json](study-10000.json): N = 10,000 and five repetitions. This
  is the primary result.
- [study-1000.json](study-1000.json): N = 1,000 and five repetitions. At this
  size, fixed per-batch and per-session startup dominates the 24-way rows for
  both engines.

The 1-minute load average was 1.1 before the 10,000-sample run and 3.4 after
it. See [parallel studies](../../../parallel-studies.md#throughput-compared-with-ngspice-47)
for the table and its interpretation.
