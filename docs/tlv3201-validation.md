# TLV3201 benchmark validation

Checkpoint27 repairs the standalone benchmark while retaining
`tests/circuits/tlv3201_switching.cir`, its vendor-model include, 100 ns requested
step and 30 us stop time. Its separate population is selected with
`tools/run_paired_benchmark.py --benchmark bench_tlv3201`. It is not appended
to the historical34-workload timing experiment.

## Original acceptance contract

The original benchmark used output crossing deltas strictly below50 ns and
DC-port agreement with `Tolerance{1e-2,1e-2}`. The second value is a denominator
floor, despite the old printed label suggesting a10 mV absolute allowance.
The repaired implementation preserves the actual formula:

- For `v(vcc)` and `v(inm)`, require
  `abs(actual-reference) <= 0.01 * max(abs(reference), 0.01 V)` at the comparison
  coordinates. This is a0.0001 V allowance when the reference magnitude is below
  0.01 V, not a0.01 V additive tolerance. Reference/actual order is now correct.
- Extract output crossings with the original low/high parameters0.3/3.0 V and
  settle window1 us. Require a nonempty equal edge count, finite crossing and
  transition times, and matching directions. Each crossing delta must remain
  strictly below50 ns. Equal empty edge lists cannot qualify.
- Require successful statuses, finite vectors of valid lengths, strictly
  increasing axes and completion of the0–30 us request. Both outputs must
  contain `v(vcc)`, `v(inm)`, `v(inp)` and `v(out)`; no signal intersection is
  used to infer success. Supplied internal voltage/current data also must be
  structurally valid and finite.

The paired driver preserves load/analysis/cleanup/total boundaries, holds owned
results for validation outside the timers, and checks each pair in alternating
order. Exceptions or failed comparison produce failed-case records and exit1;
invalid invocation/protocol errors remain distinct. No earlier passing sample
can qualify a later failed pair. The default sampling policy is3 warmups and30
pairs, replacing the legacy3/20 counts while retaining equal treatment of engines.

## Evidence scale and diagnostics

The policy is explicitly named `tlv3201-v1` in the binary description, raw
metadata and report. Wrapper validation rejects altered thresholds, unknown
policies, wrong fixture/command declarations and inconsistent edge/port evidence.
The benchmark's `worst_error` is the maximum fraction of a metric's allowance:
DC-port normalized error divided by0.01, or crossing delta divided by50 ns.
The report labels this scale; it must not be pooled with ordinary pointwise
normalized errors. The crossing boundary remains strict even at fraction1.

Per-signal absolute errors retain their units: volts for DC ports and seconds
for crossings. Coordinates are the relevant comparison time or reference
crossing time. Counts include DC-port comparison points plus one point per
matched edge. Separate `reference_edges`/`actual_edges` records retain crossing,
signed transition time, settled value and overshoot; unavailable informational
metrics serialize as null. Direction/completeness checks do not introduce new
rise-time, settling or overshoot accuracy tolerances.

A full voltage/current pointwise comparison at relative1e-3 and denominator
floor1e-9 is retained as `informational_waveform_comparison`. It does not replace
or silently weaken the original edge/DC-port policy. Its absolute and normalized
errors, coordinates and compared values remain visible, including failures.
The removed claim that internal differences were caused by GEAR switching had
not been demonstrated by an ablation.

## Checkpoint27 results and limits

The ngspice47 comparison has six matched output edges and zero DC-port error.
The maximum crossing difference is approximately5.59e-20 s. It passes the
original specialized policy but **fails the separately recorded strict
pointwise waveform check.** The largest normalized
error is approximately0.0045755 at an internal node near zero; `v(out)` has
approximately4.37e-11 V maximum absolute error and0.00108767 worst normalized
error. These are retained discrepancies requiring interpretation;
no full waveform, compact-model or general compatibility certification follows
from the specialized pass. No tolerances were loosened and no reference
configuration was changed to conceal them.

Six new C++ tests cover invalid data, missing/empty/opposite edges, crossing
limits, the signed reference-denominator distinction, the near-zero port floor,
and rejection after an earlier successful pair. Eight new Python tests cover
policy integrity, evidence requirements, the exact strict crossing boundary
and report labeling. A separate synthetic constant-output fixture exercises
the real executable: it returns1 for missing edges and emits no accepted timing
pair. That fixture is explicitly a negative control, not the vendor experiment.
The focused36-test comparator/TLV sanitizer executable passes with leak
detection; this does not certify unrelated reference-library paths.

Current C++ and Python/tooling suite results, with required failures retained,
are recorded in
[JOSS progress](joss-progress.md) for provenance and the remaining correctness,
measurement, research-use and submission work.
