# Simulation workflows goal

Requested sequence: parallel parameter sweeps / Monte Carlo, adjoint sensitivity
/ gradients, incremental re-simulation, then WebAssembly browser simulation.
Work directly on `main`. This goal takes priority over the roadmap's historical
phase ordering. Existing numerical release blockers remain tracked separately;
new features must not weaken their tests or ngspice 47 comparison tolerances.

## Completion gates

Each stage needs a usable public API, failure handling, meaningful analytical
and regression tests, examples, and documentation of supported scope and limits.
Record actual validation commands/results here. Do not claim speedups or platform
support without measurements or execution. Keep stages sequential.

1. **Parallel sweeps and Monte Carlo — complete.** Independent netlist jobs,
   parameter overrides, temperature corners, bounded worker pool, stable result
   order and per-job errors. Seeded Gaussian/uniform variations, correlation,
   statistical aggregation and Python convenience functions. Audit shared device
   state; compare serial/parallel results and exercise race detection.
2. **Adjoint sensitivity and gradients — initial supported scope complete.** DC and AC output derivatives
   for explicitly supported component parameters, with clear rejection of
   unsupported requests. Compare analytical gradients and independent numerical
   differences; preserve existing `.sens` behavior unless equivalence is proven.
3. **Incremental re-simulation — initial supported scope complete.** Validated value mutation, cache
   invalidation, reusable symbolic factorization and numeric re-solve. Verify
   results against fresh circuits, including topology/cache invalidation cases.
4. **WebAssembly — initial supported scope complete.** Emscripten configuration, browser JS/TypeScript API,
   interactive example, reproducible build instructions and executable smoke
   tests. Audit native dependencies and document browser limitations.

## Evidence

- Initial repository: clean `main`.
- Local pinned reference CLI reports ngspice 47; extracted source `configure`
  declares `PACKAGE_VERSION='47'`.
- Initial inspection found shared temporary nodes in migrated device shims;
  parallel execution must address these before being advertised as supported.

### Stage 1 validation (2026-10-06)

- Release C++ build in `/tmp/neospice-workflows-build`: `ctest -j8
  --output-on-failure` passed **1,288/1,288** tests against the pinned ngspice 47
  library. This includes 14 new sweep/Monte Carlo tests, analytical divider and
  distribution checks, an unchanged-tolerance diode comparison to 47, and AC /
  transient serial/parallel checks. These observed results supersede older
  counts for this checkout only; the separate corpus/publication audit is not
  re-certified by this run.
- Freshly built Python module: **74/74** binding tests passed. Combined with
  focused shim-generator and support-matrix tests: **112/112** passed. The
  pre-existing `.venv` editable hook points at an old checkout, so validation
  loaded a copied package/module from `/tmp/neospice-workflows-python` with that
  hook excluded. System PyYAML supplied the missing tooling dependency. A full
  tooling run was attempted but collection lacked PyYAML before the focused run;
  a complete tooling-suite pass is not claimed.
- ThreadSanitizer harness: **19 fixtures × 32 points**, serial versus eight
  workers, completed with exit 0 and no race report after fixing shared shim
  nodes, HiSIM scratch storage, BSIMSOI setup globals and an unused VBIC global
  return-code store. BSIM4 check-log writes are serialized. See
  [race-test reproduction](parallel-studies.md#race-test-harness), including
  host sanitizer startup failures and scope limits.
- Source parameter evaluation regression fixed: independent-source DC values,
  AC magnitudes and phases now evaluate parameter expressions. The new diode
  reference test originally exposed a silent zero source; the implementation
  was corrected without loosening its tolerance.
- `git diff --check` passed. No performance/scaling claim, commit, or push.

### Stage 2 validation

- **1,301/1,301 C++ tests** passed, including 13 new adjoint tests: analytic
  divider/controlled-source/RC/RL derivatives, diode DC central differences,
  ngspice 47 `.sens` comparison, temperature factors, failure status and real
  transpose solves with equilibration. **115/115 Python/focused tooling tests**
  passed. The optimization example reached R2=1500 ohm and V(out)=6 V.
- Implemented DC R/C/L/source-value gradients (with nonlinear circuit
  linearization), and linear-circuit AC R/C/uncoupled-L/source gradients.
  [Excluded derivative domains](adjoint-gradients.md) raise explicit errors.
  Full semiconductor-parameter AD and nonlinear AC gradients remain extensions.
- Complex singular factorization now fails before solving; checked updates also
  invalidate temperature preparation, preventing lost TC/multiplier scaling.

### Stage 3 validation

- Ten incremental tests pass for changed R/C/source values, nonlinear circuits,
  temperature, DC/AC grids, cache moves/resets/ownership, interleaved analyses,
  invalid-update atomicity and recovery after DC/AC failures.
- Symbolic-analysis counters stay at one across successful same-topology runs.
  Full results are compared against fresh circuits. No latency claim is made.
- [Supported incremental scope](incremental-simulation.md) is DC and full AC
  sweeps with retained solver workspaces. Partial restamping, frequency skipping
  and transient resume remain extensions.

### Stage 4 validation

- Emscripten **6.0.11** Release build succeeded. Node API smoke tests passed,
  including six semiconductor-circuit comparisons against the native build.
  The generated module is about 2.9 MiB uncompressed.
- Headless Chrome passed the actual Web Worker demo checks: loading,
  DC/AC/transient analysis, resistor-slider updates, errors and worker reset.
  The AC plot was also visually inspected. Safari and Firefox were not tested.
- TypeScript **5.9.3** strict NodeNext checking passed for the public declarations.
- [Build instructions, API examples and limitations](webassembly.md) include
  reproduction commands. The browser build is single-threaded; external model
  files and the native batch API are not exposed. Pole-zero analysis is excluded
  because it requires LAPACK. Other browser API exclusions are listed there.
- Added a pinned Emscripten GitHub Actions build, Node smoke test and artifact
  upload. The workflow has not been executed on GitHub; the equivalent local
  build and smoke test passed. No website was deployed.

## Final scope

Final native Release validation passed **1,312/1,312 C++ tests**. The final
Python bindings and focused tooling validation passed **116/116 tests**. The
final native and WASM rebuilds include a regression fix that invalidates stale
real and complex solver factors when complex factorization fails. The Node
smoke test and all six native/WASM comparisons passed again after that fix.
`git diff --check` and `python3 tools/support_matrix.py --check` passed.

All four stages provide usable initial implementations in the requested order.
The extensions identified above remain roadmap work, not completed features.
The ngspice 47 reference and comparison tolerances are unchanged. No performance
or scaling improvement is claimed. Implementation and validation were completed
on `main` before the separately requested commit and push.
