# Performance analysis: current evidence and historical limitations

Updated September 11, 2026. The June16 corpus timings are
[archived historical observations](evidence/joss/2026-09-11-historical-performance-analysis.md),
not validated performance claims for the current implementation. The original
analysis and its numerical tables remain available for audit.

## Why the old speed claims are not accepted

The earlier experiment timed subprocesses with eight concurrent workers and
reported the subset where both simulators returned a solution. That subset did
not require output agreement, and the comparison/status paths subsequently
needed correctness repairs. A successful process or returned operating point
is not enough to accept a timing sample.

Contention can affect different workloads differently; it cannot be assumed to
cancel in a runtime ratio. Different convergence paths, initialization, file
loading and thread settings also affect subprocess wall time. Summing such
measurements does not establish a general speedup. The earlier attribution of
improvements to lazy parsing or solver selection was not an isolated ablation.
Neither its causal explanation nor its extrapolated speedups should be quoted
in the paper. See the [original audit](paper-readiness-audit.md) and
[repaired comparison methods](validation-methods.md).

## Current paired experiment

The canonical in-process experiment uses **34 fixed workloads**, covering the
original small/macromodel cases, all eight original large DC mesh/diode-ladder
sizes, and RC/diode-RC grids for AC, transient and noise. It retains every failure
and accepts timings only after status, signal, axis and value checks pass.
The ngspice version, active Sparse solver, one-thread setting, source/build/input
hashes and system observations are recorded.

Each pair measures public loading, analysis including result materialization,
circuit cleanup and the contiguous total. Both engines receive the same warmup
and sample counts; order alternates. Comparisons and returned-object destruction
are outside the timers. These are defined public-interface operations, not
isolated factorization or Newton-iteration costs. No fixed speedup is required.

[Benchmark methods](benchmark-methods.md) gives the commands, exact boundaries,
qualification rules and figure-generation workflow. [Current progress](joss-progress.md)
links the completed checkpoint25 measurement and its full 34-case report.
Both engines win on parts of this population; no general speedup is claimed.
Raw timing dispersion and environment observations are retained. Verification-only
runs remain separate and produce no performance samples.

## Interpretation and remaining work

The generated tables retain both engines' medians, minima and maxima, along with
failed-case diagnostics. Observed ranges are not confidence intervals. A ratio
of medians describes one declared workload and environment; it is not a
population-wide ranking or a guarantee for another circuit, machine or model.
The synthetic grids do not by themselves establish general solver scaling.

Standalone profiling programs remain diagnostic tools unless brought under the
paired qualification protocol. The eight old `bench_solver_throughput` DC
netlists are now reproduced byte-for-byte by `tools/generate_paired_circuits.py`
and executed in the comprehensive harness. Other standalone timing output must
not enter paper tables without matching boundaries and validation.

Correctness precedes optimization. Slow convergence paths must be investigated
against ngspice; shortening a failed path or bypassing convergence checks is not
a valid speed improvement. Causal performance claims require controlled
ablations, otherwise the paper should describe the observed result without
assigning a cause.
