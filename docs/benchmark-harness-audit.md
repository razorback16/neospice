# Benchmark harness audit

Source inspection, September 11, 2026. The validated paired protocol is
[benchmark-methods.md](benchmark-methods.md). That protocol does not certify
other executables simply because they share an engine or reference wrapper.

| Harness | Current boundary and remaining issue |
|---|---|
| `bench_comprehensive` | Declares a fixed population, checks every pair and records load/analysis/cleanup/total phases. Its wrapper and report validate provenance, protocol and raw evidence. Checkpoint25 measurements completed with all34 workloads qualified. |
| `bench_ths4131` | Repaired in checkpoint26 using the shared paired driver. Retains the original circuit and all DEC10/100/1000 sweep densities, validates every pair, records consistent load/analysis/cleanup/total phases, and exposes failures through exit status. Five workflows verified against47; no new timing claim. |
| `bench_tlv3201` | Repaired in checkpoint27 with the shared paired driver and a distinct edge/DC-port policy. Requires status, complete finite data, four named voltages, matching nonempty directed edges and the original crossing/port limits. Keeps strict pointwise waveform differences informational and explicit; this is not full waveform certification. Verification passes47; no new timing claim. |
| `bench_solver_throughput` | Checkpoint28 checks every warmup/sample for converged, nonempty named voltage output and finite named voltages/currents. A failed case emits no median and makes the process fail, while remaining cases still run. All eight original generators/sizes are unchanged and have counterparts in the comprehensive paired population. This single-engine diagnostic does not establish reference agreement or KCL accuracy. |
| `bench_neo_solver` | Synthetic matrix microbenchmark. No residual or known-solution check validates its solves; checkpoint28 corrects the earlier audit's implication otherwise and labels the output unqualified. Do not extrapolate circuit performance. |
| `bench_amd_refactor` | Factorization/reuse microbenchmark over diagonally dominant meshes and scaled values. No solve/residual validation is performed and samples run in separate blocks. Checkpoint28 states these limits in source/output and removes the per-solve speedup claim. |
| `bench_ordering_spike` | Exploratory ordering/factorization experiment with symbolic-fill sanity checks, but no numerical residual/solution validation. Checkpoint28 removes unsupported ngspice-KLU speed, causal and lower-bound claims. It identifies the in-tree ordering and synthetic values, including those applied to the ladder's parsed sparsity pattern. Output is explicitly unqualified. |
| `bench_newton_profile` | Reimplements a Newton loop to attribute costs; it is not the production analysis driver. Its timings cannot be presented as a production-path ablation. |
| `profile_tlv3201`, `debug_tlv3201` | Local diagnostics, not independently qualified benchmark evidence. |

## Required next work

THS4131 and TLV3201 now use the shared paired measurement contract, retaining
their original circuits and declared metric scopes. Each accepted sample
validates its own outputs; a previously passing sample cannot authorize later timings.
Preserve failures and make process status reflect them. Keep performance
observations separate from specialized switching diagnostics and expose both.

Do not treat the checkpoint25 34-case timing experiment as covering TLV3201
or all standalone THS4131 density workflows. Checkpoint26 declares and verifies
THS4131 separately; checkpoint27 separately verifies TLV3201 under its original
edge/DC-port policy. Their new measurements remain pending. Additional workloads need a
separate declared experiment and provenance, without rewriting historical populations.

Checkpoint28 removes the identified unsupported matrix publication claims and
documents their narrower purpose. These tools remain numerically unqualified;
their symbolic-fill checks do not validate the numeric factors or solutions.
If a causal claim is retained in the paper,
validate and measure the relevant mechanism on the declared workload rather
than inferring it from a scratch profiler. Existing files and failed fixtures
must not be removed merely to make the benchmark audit appear complete.

The older audit incorrectly called the separate TLV3201 reference run a CLI
run. It used `NgspiceRunner` backed by `NgspiceLib`. The actual defect was checking
a separate run instead of each sampled pair. See the
[erratum](evidence/joss/2026-09-11-benchmark-audit-erratum.json).
