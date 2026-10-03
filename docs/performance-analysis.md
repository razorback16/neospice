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

## Scaling defects removed after checkpoint25 (October 3, 2026)

Profiling the checkpoint25 losses found four implementation defects. ngspice has
none of them. Each fix is a separate commit, so each one can be measured on its
own:

| Commit | Defect | Workloads affected |
|---|---|---|
| `c4a0d84` | Every device instance ran its model's temperature routine over the whole instance list: O(N²) passes for N instances sharing a model | diode ladders |
| `d3070f5` | Minimum-degree ordering scanned every vertex at each elimination step | large meshes (load) |
| `02a4f98` | Diagonal and dead-node classification scanned every stamped entry once per variable | large meshes and ladders (load) |
| `799f4a6` | Noise factored two real 2n×2n systems per frequency instead of one complex factorization reused for a transposed adjoint solve | noise |

Verification: 1,274 C++ tests and 413 Python/tooling tests passed. The
paired harness accepted all 34 workloads against ngspice 47.

- **Byte-identical output.** `d3070f5` and `02a4f98` produce byte-identical
  output on all 224 repository and benchmark netlists that write output.
  `c4a0d84` does too.
- **Noise moved closer to ngspice.** After `799f4a6`, noise follows ngspice's
  `NIacIter`/`NInzIter` sequence. Its worst normalized error against ngspice 47
  fell from 1.9e-13 to 1.9e-15 (`noise_rc_grid_24`) and from 3.1e-12 to 2.7e-15
  (`noise_diode_rc_grid_24`).
- **KiCad corpus.** In a full rerun of the 34,908-fixture KiCad corpus against
  the previous build, neospice values changed in 15 fixtures. All came from
  `c4a0d84` through MOS2. Like ngspice, `MOS2temp` resets every instance's
  `MOS2von` and `MOS2mode`. The old per-instance reruns repeated that reset after
  earlier instances had already loaded. That changed the next iteration's
  limiting and departed from ngspice, where the temperature pass runs once before
  any load. The 14 matching fixtures still match at under 1e-4 of tolerance, and
  the one mismatch is unchanged. Every other status difference between the two
  runs came from ngspice-side variation or from timeouts under machine load.

No new timing is claimed here. An indicative paired run on a contended
machine (5 samples) is not evidence. A qualified 30-sample paired measurement on
an idle machine must replace checkpoint25 before any figure is reported.
