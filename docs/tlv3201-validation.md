# TLV3201 validation and open waveform discrepancy

The fixture `tests/circuits/tlv3201_switching.cir` uses its vendor model,
a 100 ns requested step, and a 30 us stop time. Select it with
`tools/run_paired_benchmark.py --benchmark bench_tlv3201`. Its population and
acceptance policy are separate from the comprehensive 34-workload benchmark.

## Edge and DC-port contract

The policy is named `tlv3201-v1` in the binary, metadata, and report.

- For `v(vcc)` and `v(inm)`, require
  `abs(actual-reference) <= 0.01 * max(abs(reference), 0.01 V)`.
  The near-zero allowance is 0.0001 V. The floor is not an additive tolerance.
- Extract output crossings with low/high parameters 0.3/3.0 V and a 1 us
  settling window. Require nonempty equal edge counts, finite crossing and
  transition times, and matching directions. Each crossing delta must be
  strictly below 50 ns.
- Require successful statuses, finite vectors, increasing axes, and completion
  of the 0–30 us request. Both outputs must contain `v(vcc)`, `v(inm)`,
  `v(inp)`, and `v(out)`. Supplied internal signals must also be valid.

Every pair uses the [paired timing boundaries](benchmark-methods.md) and runs
its comparisons outside the timers. Failed comparisons retain diagnostics and
fail the case. An earlier passing sample cannot qualify a later failed pair.

## Diagnostics

`worst_error` expresses the largest fraction of a metric's allowance:
DC-port normalized error divided by 0.01, or crossing delta divided by 50 ns.
Keep this scale separate from ordinary pointwise normalized errors. The
crossing boundary is strict at fraction 1.

Per-signal errors retain volts or seconds. Edge records retain crossing time,
signed transition time, settled value, and overshoot. Unavailable informational
metrics serialize as null. These records do not add new settling, transition,
or overshoot acceptance criteria.

`informational_waveform_comparison` records a separate pointwise check at
relative tolerance 1e-3 and denominator floor 1e-9. It does not replace the
edge/DC-port contract. Its failed signals and comparison coordinates remain
visible.

## Open finding

The recorded ngspice 47 comparison passes the specialized contract with six
matching output edges and zero DC-port error. It fails the separate strict
pointwise check. The largest normalized error is approximately 0.0045755 at an
internal node near zero. `v(out)` has approximately 4.37e-11 V maximum absolute
error and 0.00108767 worst normalized error.

Diagnose those differences before claiming full waveform agreement. Keep the
fixture, model, reference settings, and both policies unchanged. The
[report](evidence/joss/2026-09-11-tlv3201-report47/report.md) records the observed
scope. [Readiness status](joss-progress.md) tracks the remaining publication work.

C++ and tooling regressions cover invalid data, missing or opposite edges,
strict crossing limits, the denominator floor, and rejection after an earlier
successful pair. A synthetic constant-output fixture verifies that empty edge
lists fail and produce no accepted timing sample.
