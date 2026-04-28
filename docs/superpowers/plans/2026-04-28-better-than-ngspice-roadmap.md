# NeoSpice Roadmap: Beating ngspice Overall

## Purpose

NeoSpice should not aim to be a line-for-line clone of ngspice. The better target
is a smaller, clearer simulator that matches ngspice's robustness on difficult
circuits while using a more modern architecture, tighter analysis scheduling, and
better performance telemetry.

## Definition of Better

NeoSpice is better than ngspice only when it wins across four dimensions:

1. **Correctness**: results match ngspice or trusted analytical references across
   DC, AC, transient, noise, TF, sensitivity, and pole-zero.
2. **Robustness**: hard circuits converge without user hand-holding.
3. **Performance**: median and tail latency are better on a broad corpus, not just
   on small linear examples.
4. **Maintainability**: the solver remains understandable enough to evolve quickly.

Single benchmark wins are useful signals, but they are not the finish line.

## Current Position (2026-04-28)

Strengths:

- NeoSolver with AMD ordering, Gilbert-Peierls reach, and threshold pivoting is
  competitive on small and medium MNA systems.
- OPA1632 E2E (.op + .ac) is 1.1x faster than ngspice (was 35.5x slower).
- Adaptive gmin/source stepping with state-checkpoint rollback is implemented.
- Operating-point reuse (.op → .ac) with cache invalidation on param/reset.
- Global node damping (3.5V threshold) in Newton corrector iterations.
- Refactorize diagonal perturbation (±1e-12) avoids expensive numeric() fallback.
- Newton warm-start: transient reuses pivot structure via refactorize on iter 0.
- 843/843 tests passing, 13/14 benchmarks faster than ngspice.

Known gaps:

- Transient RLC benchmark is 3.2x slower than ngspice (dense tier, linear circuit).
- No convergence telemetry or solver work counters yet.
- No value-aware pivot recovery beyond perturbation.
- Stagnation detection in direct Newton is a simple 25-iter cap, not norm-based.

## Strategic Principle

Do not optimize only the sparse factorizer. In SPICE workloads, the cost is often:

- repeated failed Newton attempts,
- repeated DC solves before downstream analyses,
- device-state churn after rejected steps,
- time-step rejection loops,
- unnecessary restamping/refactorization,
- parser/model compatibility gaps that force fallback behavior.

NeoSpice should treat the simulator as a pipeline, not as only a linear solver.

## Path 1: Convergence Observability and Refinement

### Goal

Make convergence behavior measurable and tunable.

### Next Improvements

1. **Replace fixed direct-attempt cap with stagnation detection**

   Current direct Newton caps at 25 iterations. The more general solution is to
   track per-iteration progress and stop when stalled or diverging.

   Track per iteration:

   - max update norm,
   - residual norm if available,
   - worst convergence ratio,
   - whether device convergence improved,
   - repeated pivot/factorization failures.

   Then stop early only when progress stalls or diverges. Easy circuits that need
   more than the default probe should still be allowed to converge directly.

2. **Expose convergence telemetry**

   Every analysis result should be able to report:

   - convergence method used,
   - direct Newton attempts,
   - continuation steps,
   - rejected trial steps,
   - total Newton iterations,
   - solver numeric/refactorize counts,
   - worst offending node/branch when convergence fails.

   This telemetry is the key to avoiding future one-off tuning.

## Path 2: Improve Sparse Solving Without Losing Simplicity

### Goal

Keep NeoSolver fast on normal cases, but close the robustness gap to ngspice's
Sparse 1.3 on ill-conditioned nonlinear matrices.

### Next Improvements

1. **Value-aware pivot recovery**

   NeoSolver uses structural ordering plus numeric pivoting plus ±1e-12
   perturbation on near-zero refactorize pivots. Add a limited value-aware
   recovery path when pivot quality is persistently poor.

   Preferred approach:

   - keep AMD symbolic ordering as the fast path,
   - detect repeated perturbation events via the bool return from refactorize,
   - reorder or repivot only the affected trailing region/block,
   - cache the recovered order if it remains stable.

   This gives most of ngspice's Markowitz benefit without adopting its linked-list
   storage model.

2. **Matrix scaling and equilibration**

   Add optional row/column scaling for badly scaled systems. This should be
   telemetry-driven and off for matrices where it does not help.

3. **Better block handling**

   BTF did not help OPA1632 much, but it can help other netlists. Keep improving
   block detection and singleton elimination so large mixed circuits do less work.

4. **Strengthen complex AC reuse**

   AC sweeps should minimize per-frequency setup:

   - reuse symbolic work,
   - refactorize complex matrices when the pattern is unchanged,
   - cache per-device frequency-independent small-signal stamps,
   - specialize common passive-only or source-only cases.

5. **Track solver work explicitly**

   Add counters for:

   - symbolic calls,
   - numeric calls,
   - refactorize calls,
   - refactorize fallbacks (perturbation events),
   - complex numeric/refactorize calls,
   - pivot recovery events,
   - solve calls.

   Solver performance should be explainable from logs, not inferred from wall time.

## Path 3: Treat Analysis Scheduling as a Performance Feature

### Goal

Avoid recomputing results that are already valid.

### Next Improvements

1. **Analysis planner for netlist execution**

   `Simulator::run()` should plan analysis dependencies:

   - `.ac` requires a DC operating point,
   - `.noise` requires a DC operating point and small-signal state,
   - `.tf` requires a DC operating point,
   - `.pz` requires a DC operating point,
   - `.tran` may require a transient operating point unless `uic` is used.

   The planner should compute each dependency once.

2. **Cross-analysis small-signal reuse**

   AC, noise, TF, sensitivity, and pole-zero often share small-signal device data.
   Reuse when the operating point and analysis mode permit it.

## Path 4: Improve Transient Performance and Robustness

### Goal

Close the transient gap while preserving accuracy. The benchmark currently shows
NeoSpice winning RC lowpass and pulse source but losing the RLC series case (3.2x).

### Next Improvements

1. **Time-step rejection telemetry**

   Track accepted/rejected steps, Newton iterations per time point, LTE failures,
   breakpoint restarts, and ringing-mode switches.

2. **Fast paths for linear time-invariant circuits**

   RLC/passive linear networks should avoid unnecessary nonlinear machinery.
   Detect all-linear transient circuits and use a tighter linear stepping path.
   This is the primary fix for the RLC regression.

3. **Improve breakpoint handling**

   Source breakpoints should be scheduled precisely without over-restarting the
   integrator. This matters for pulse, PWL, EXP, SFFM, and AM sources.

4. **Better adaptive order/method control**

   Trap and Gear should be chosen by measured behavior:

   - trap for smooth low-damping cases where it is accurate,
   - Gear when ringing or stiffness demands damping,
   - explicit hysteresis to avoid mode thrashing.

5. **State rollback for rejected transient steps**

   Rejected time steps must restore the full state ring and device internal
   states exactly. This should be tested with capacitors, inductors, BSIM devices,
   and behavioral sources with `ddt`/`idt`.

## Path 5: Device Model Quality and Limiting

### Goal

Make convergence robust because devices stamp stable equations, not because the
global solver compensates for bad device behavior.

### Next Improvements

1. **Device-local limiting audit**

   For each nonlinear device family, document and test:

   - terminal voltage limiting,
   - current limiting,
   - charge limiting,
   - convergence flag behavior,
   - temperature update behavior,
   - state pointer handling.

2. **Golden model comparison**

   Continue device-by-device comparison against ngspice for DC, AC, transient,
   and noise. Add model-card edge cases and temperature sweeps.

3. **State consistency tests**

   Stateful devices must tolerate:

   - continuation rollback,
   - transient rejection,
   - repeated small-signal init passes,
   - analysis chaining,
   - parameter changes.

4. **Behavioral source stability**

   Behavioral sources often create hard nonlinear systems. Improve derivative
   quality, discontinuity handling, and smoothing support where SPICE-compatible.

## Path 6: Build a Real Performance Corpus

### Goal

Avoid optimizing for one benchmark. Build a corpus that reflects real SPICE usage.

### Corpus Buckets

- Tiny linear circuits: resistor dividers, RC, RLC.
- Medium analog IC macromodels: THS4131, OPA1632, LM358-like models.
- Discrete devices: diode rectifiers, BJT amplifiers, JFET/HFET examples.
- MOS circuits: CMOS inverters, chains, amplifiers, ring oscillators.
- Behavioral circuits: ASRC, table, polynomial, `ddt`, `idt`.
- Transmission lines: TLINE/LTRA.
- Noise-heavy circuits.
- DC sweeps and nested parameter sweeps.
- Long AC sweeps.
- Transient circuits with dense breakpoints.

### Required Metrics

For each corpus entry, record:

- parse/finalize time,
- operating-point time,
- analysis time,
- total wall time,
- Newton iteration counts,
- rejected continuation/time steps,
- solver symbolic/numeric/refactorize counts,
- memory usage if available,
- result error versus reference.

The result should be a dashboard, not only a text benchmark.

## Path 7: CI Gates for Performance and Correctness

### Goal

Make regressions hard to introduce.

### Gates

1. **Correctness gate**

   All unit and ngspice-comparison tests must pass.

2. **Performance smoke gate**

   A small benchmark subset runs in CI and fails on large regressions.

3. **Nightly full performance gate**

   The full corpus compares current branch against:

   - previous main,
   - ngspice,
   - any optional reference solver.

4. **Telemetry diff gate**

   If wall time changes, require a diff of solver/convergence counters. This
   makes it clear whether a regression came from parser, device load, Newton,
   linear solve, or analysis scheduling.

## What Not To Do

- Do not hardcode behavior by circuit name, device name, or model name.
- Do not tune constants solely to OPA1632.
- Do not port ngspice's linked-list sparse package wholesale unless profiling
  proves the current sparse structure cannot catch up.
- Do not add device-specific convergence hacks before proving the device equation
  or limiting behavior is the problem.
- Do not optimize a benchmark if the telemetry shows the cost is elsewhere.
- Do not accept performance wins that reduce convergence reliability.

## Prioritized Execution Plan

### Phase 1: Convergence Telemetry and Stagnation Detection

- Add convergence telemetry to `SimStatus`.
- Replace fixed 25-iter direct-attempt cap with progress/stagnation detection.
- Add focused rollback tests for stateful devices.
- Add tests proving `.op -> .ac` reuse invalidates on parameter/source/temp change.

### Phase 2: Transient Performance

- Add transient telemetry (accepted/rejected steps, Newton iterations per point).
- Build fast path for linear time-domain circuits (fix RLC 3.2x regression).
- Improve breakpoint and method/order control.

### Phase 3: Analysis Planner and Cache

- Centralize analysis dependency planning.
- Reuse small-signal init across AC/noise/TF/PZ.
- Make cache invalidation explicit and tested.

### Phase 4: Sparse Solver Recovery

- Add pivot-quality metrics.
- Add value-aware pivot recovery for persistently unstable blocks.
- Add optional scaling/equilibration for badly scaled matrices.
- Validate against hard nonlinear and large AC workloads.

### Phase 5: Product-Level Superiority

- Build a benchmark dashboard.
- Document convergence traces in user-facing diagnostics.
- Make NeoSpice faster to debug than ngspice, not just faster to run.

## Success Criteria

NeoSpice can credibly claim to be better than ngspice overall when:

- It passes the full correctness and ngspice comparison suite.
- It is faster than ngspice on the median corpus entry.
- It is no worse than ngspice on the important tail cases, or the regression is
  understood and tracked.
- It converges the hard analog macromodel and MOS/device suites without manual
  options.
- Every slow case has telemetry explaining the bottleneck.
- The public API is easier to embed and inspect than ngspice's command interface.

## Bottom Line

The path to beating ngspice is not one magic solver change. It is a layered
strategy:

1. adaptive nonlinear convergence,
2. rollback-safe device state handling,
3. analysis dependency reuse,
4. sparse numeric robustness where measured,
5. transient-specific fast paths,
6. continuous benchmark telemetry.

NeoSpice's advantage is that it can implement these ideas with a cleaner design
than ngspice. The priority is to keep that clarity while adding the robustness
that makes ngspice hard to beat.
