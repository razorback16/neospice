# Publication readiness audit — 2026-09-10

**Verdict:** neospice contains enough substantial engineering for a research-software paper. The current repository does **not yet support submitting its headline compatibility, performance, or algorithmic-novelty claims without revision**. A narrowly scoped technical report can be drafted now. A software-paper submission should follow the evidence repairs below; a methods paper needs a controlled experiment around a specific contribution.

This assessment concerns `main` at `39bc809` (2026-08-07). The working tree was clean at the start. The audit inventoried all 857 tracked files, inspected the build, CI, public interfaces, analysis/solver implementations, model adapters, comparison infrastructure, benchmarks, saved results, and documentation. This was a repository-wide assessment with targeted source inspection, not a line-by-line proof of every translated compact model. Sanitizers, the full upstream ngspice regression suite, and cross-platform execution were not run. No simulator implementation or tolerance was changed.

**What exists today**

| Area | Evidence and limits |
|---|---|
| Simulation engine | Implemented DC operating point and sweeps, adaptive transient, AC, noise, transfer function, sensitivity, pole-zero, Fourier, measurement, and parameter-sweep paths. These are real implementations, with different coverage limits. |
| Device support | Broad SPICE3/ngspice-derived model coverage, including large BSIM and HiSIM translations; descriptor-driven migration tooling and shared compatibility shims. Model presence does not mean every analysis or option is implemented. |
| Solvers | Sparse 1.3-derived Markowitz path plus a separate minimum-degree-ordered, Gilbert–Peierls-style LU implementation. `make_solver.cpp` selects the latter automatically for linear circuits with at least 256 unknowns, with an initial-factorization fallback. Complex operations delegate to NeoSolver. |
| API and integration | C++20 library, CLI, typed circuit construction and result access, Python/nanobind bindings, NumPy outputs, measurements, and introspection. `Circuit::include()` and `Circuit::X()` already exist, including Python bindings. |
| Distribution | CMake build, Python package metadata, Linux/macOS wheel workflow, MIT license file, NOTICE, CREDITS, CONTRIBUTING, examples, and CI. Package version is `0.1.0` and its development classifier is Alpha. |
| Research plans | GPU acceleration, ML initial guesses, adjoint parameter gradients, full digital/mixed-signal simulation, PWL/POP, and Verilog-A are research/planned directions, not established contributions in this snapshot. |

Some advertised scope needs qualification. `src/core/sens.cpp` perturbs resistors and independent voltage/current sources; it does not compute sensitivity to all circuit/model parameters. `src/devices/vdmos/vdmos_device.cpp` has an empty AC stamp and unimplemented noise sources; the VDMOS validation file tests DC/IV only. MES noise is also unimplemented. The README's device count and the migration document's count differ, and MOS2 is implemented despite being listed as not migrated in part of the documentation. A device-by-analysis support matrix would be more useful than a single device count.

**Fresh verification**

| Check | Result |
|---|---|
| Fresh Release C++ configure/build | Passed with GCC 14.2, system libngspice 42, OpenBLAS 0.3.26, comparison diagnostics enabled. Reused the existing GoogleTest source checkout to avoid downloading it. |
| CTest | **1,125/1,125 passed**, no GTest skip markers in the captured run. This includes unit tests as well as reference comparisons; it is not 1,125 independent ngspice validations. |
| Fresh Python extension + both Python test directories | **63 passed**. Tested staged current Python sources and the newly built extension. |
| Migration/harness Python tests | **174 passed, 15 failed, 1 skipped**, with the local ngspice source supplied. Failures include outdated generator expectations and descriptor stubs missing fields now required by generators. The skip is for absent `jfet.yaml`. |
| Comparator negative probes | Reproduced false successes for a NaN DC result, empty DC comparisons, an empty actual transient, disjoint Python signal sets, and an infinite Python value against a finite reference. |
| KiCad first 500 generated fixtures | 371 MATCH, 53 MISMATCH, 38 NEO_ONLY, 38 NEO_TRIVIAL; neither NG_ONLY nor BOTH_FAIL. This is a sorted subset, not a representative accuracy estimate. |
| Full KiCad sweep, 34,908 fixtures | **24,452 MATCH; 1,459 MISMATCH; 1 NG_ONLY; 2,367 NEO_ONLY; 3,369 NEO_TRIVIAL; 3,260 BOTH_FAIL.** Existing harness, symmetric `ngbehavior=psa`, system ngspice 42; no missing-`spinit` errors in the saved failure records. |
| Comprehensive benchmark smoke run | Completed after the corpus run, using the fresh executable and `OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1`. Its measurement defects remain; this is not a corrected publication benchmark. |

The fresh full run reports **94.37% agreement among 25,911 both-successful fixtures**, or **70.05% matched coverage of all 34,908 fixtures**. Treat these as provisional existing-harness results because of the comparator defects below. The one NG_ONLY fixture is `uncategorized/spice_complete/harprmos.lib::RFF70N06_HA`, where neospice reports DC nonconvergence (101 iterations, reported residual 836.875). None of the recorded mismatch values is nonfinite, but successful rows do not retain enough data to rule out false matches. The full raw result is `/tmp/neospice-paper-kicad-full.json`.

The existing virtual environment's editable installation refers to the old `Codes/spice-cpp` path. The audit bypassed that stale installation when testing the fresh extension. The tooling tests additionally needed system PyYAML, which is absent from the virtual environment. These environment issues were separated from the actual 15 test failures.

**Publication blocker 1: comparison success does not always establish correctness**

In `tests/framework/comparator.cpp`, the DC comparator initializes success to true and rejects only when `err > tolerance`. NaN errors satisfy neither the failure condition nor the worst-error update. Empty expected maps also pass. The transient comparator iterates over the shorter time grid: an empty actual grid can yield a successful comparison with zero compared points even when the reference has data. Vector lengths, finiteness, time coverage, and nonempty results need explicit validation.

The standalone C++ probes returned:

```text
DC finite reference vs NaN: passed=1 points=1
Empty DC: passed=1 points=0
Nonempty reference vs empty transient: passed=1 points=0
```

In `tools/compare_kicad_models.py`, `compare_values()` compares only the signal intersection and returns true when there are no common signals. It can also accept infinity against a finite value because both the difference and tolerance become infinity. The saved JSON retains mismatches, but not successful signal values or comparison counts, so these probes establish weaknesses in the measurement system; they do **not** establish how many historical MATCH rows were affected.

There is also a direct assertion gap. `LTRAValidation.TransientRC` calculates `cmp` without asserting `cmp.passed`, then checks output voltage separately. In the fresh run it emitted:

```text
MARGIN_TRAN|i(v1)|4.281e-01|5.000e-02|0.1x
```

The test still passed. The current-error comparison exceeds its stated threshold by about 8.6 times. Other fresh normalized worst-error metrics include approximately 0.247 for VBIC switching and 0.141 for the diode rectifier, under thresholds of 0.27 and 0.15. These metrics can emphasize near-zero signals or edge interpolation; they should not be presented as whole-waveform RMS errors. Equally, the README's tightest tolerance cannot characterize all analyses.

Repair the comparators and assertions, retain existing tolerances, then fix or explicitly delimit implementation discrepancies. Publish voltage/current absolute errors, normalized errors, edge metrics where appropriate, and transient completion status. Preserve ngspice as the reference.

**Publication blocker 2: the large-corpus baseline is not yet a reproducible experiment**

The README and `docs/kicad-parity-tests.md` cite `compare_full_3bcd_v2.json`: 24,201 matches among 25,843 both-successful fixtures, or 93.65%, with 17 NG_ONLY. This is **69.33% of all 34,908 generated fixtures**, not 93.65% of the whole library. The harness generates minimal `.op` tests and normally compares external signals only. It does not validate every model across DC curves, transient, AC, noise, temperatures, and parameter settings.

The newer local `compare_full_psa_v2.json` contains 17,521 MATCH, 533 MISMATCH, 8,809 NEO_ONLY, 4,784 NEO_TRIVIAL, 3,261 BOTH_FAIL, and no NG_ONLY. Its apparent agreement is 97.05% of only 18,054 both-successful fixtures. That smaller denominator prevents interpreting the percentage as an accuracy improvement.

More seriously, 8,758 of those 8,809 NEO_ONLY rows mention missing `spinit`; 5,241 report `MIF-ERROR`. Another 1,441 NEO_TRIVIAL rows report `MIF-ERROR`. The fresh unmodified harness with `/usr/bin/ngspice` evaluates examples such as `54ALS00A` and reports a real 0 V versus 3.3 V mismatch instead. The saved failures therefore cannot all be attributed to ngspice language support or convergence. The historical JSON lacks enough environment metadata to reconstruct exactly how that run was launched.

Additional reproducibility issues:

- `results/` and `third_party/` are ignored by Git. The headline experiment's results and corpus are not part of an ordinary clone, and the documentation does not pin a corpus revision. The local library revision used here is `a8688952bcaab19f567bc4db237b60bde03ef310`.
- The 34,908 saved rows have 34,739 distinct `(file, name)` keys. Some duplicate keys may represent distinct scopes; the identity used for selection/baseline dictionaries needs to distinguish them before making uniqueness or transition claims.
- Result JSON lacks simulator/build revisions, binary hashes, library revision, effective initialization/compatibility configuration, fixture hashes, and successful comparison counts.
- Isolated/driven fallback changes the fixture and is adopted only when both simulators succeed. This is useful diagnostically, but primary and rescued fixtures need separate accounting; selection can depend on the implementation being evaluated.
- The documentation describes `--baseline` as a diff option, while the code uses it to select previously passing `OK`/`WARNING` tests. `--transition-baseline` is the transition-reporting option.
- The CMake comparison-diagnostic flag affects C++ comparators, not the independent Python KiCad comparator, despite its placement in the KiCad reproduction guide.

Freeze and identify fixtures before the paper experiment. Separate parser failures, missing runtime model support, numerical nonconvergence, timeouts, trivial excitation, missing signals, and numerical mismatch. Archive the manifest and reproducible acquisition steps together with machine-readable results. Report both conditional agreement and total-corpus coverage. Group related model families and retain held-out circuits/vendors so tuning and evaluation are distinguishable. Agreement between implementations sharing upstream device equations establishes compatibility; it does not independently establish physical model accuracy.

**Publication blocker 3: benchmark labels and claimed causes exceed the measurement**

`tests/bench/bench_comprehensive.cpp` is useful infrastructure, but the published description does not match it:

- Timed analysis lambdas call `sim.load(path)` and ngspice `source path`; file reads, parsing, and setup are inside the timer. This is not an isolated analysis-only benchmark and not free of file I/O.
- The long AC case measures neospice 30 times and ngspice 10 times, despite the general description of 30 runs each.
- Neospice result/circuit destruction happens inside its timed lambda. ngspice reset/plot cleanup happens after the timer. Lifetime boundaries need explicit treatment.
- `NgspiceLib::command()` ignores return codes and suppresses output; the benchmark does not verify waveform agreement or successful reference execution before accepting timings.
- The published maximum, 8.6x, is a small pulse-source case. The document's own sum of non-composite cases is only 1.14x, and its long AC case slightly favors ngspice. Neither statistic is a broad workload distribution.
- KiCad throughput summaries include both MATCH and MISMATCH. A speed claim should distinguish correct paired outputs from mere successful termination.
- Concurrent subprocess timing does not guarantee equal contention for the two programs. The assertion in `performance-analysis.md` that per-circuit ratios are therefore valid needs experimental support.
- Large synthetic throughput tests exist but do not benchmark ngspice. Banded, diagonally dominant matrices do not establish performance on general, ill-conditioned circuit matrices.

The fresh smoke run on the Intel Core Ultra 9 285K also does not reproduce “faster on every benchmark except long AC”: THS4131 DC OP was 384 us versus ngspice's 361 us, its end-to-end case was 646 us versus 608 us, and long AC was 23.76 ms versus 20.88 ms. Neospice remained faster on the small transient cases and OPA1632 end-to-end (4.27 ms versus 6.05 ms); the pulse case was 6.0x. These are diagnostic observations from the uncorrected harness, not replacement headline speedups. Raw output: `/tmp/neospice-paper-benchmark.log`.

Use matching boundaries for parse, initialization, numeric solve, output materialization, and cleanup. Validate before timing; retain raw samples; alternate execution order; control thread counts; report dispersion and circuit-size/analysis strata. Keep the frozen ngspice-42 baseline for reproducibility and add a newer ngspice baseline: upstream documents ngspice 46, released March 29, 2026. Solver comparisons may explicitly include default Sparse and a separately labeled KLU configuration; never change reference settings to make a failed accuracy comparison disappear. [ngspice release history](https://ngspice.sourceforge.io/news.html)

**Publication blocker 4: distinguish architecture from new numerical methods**

The lineage attribution in README/NOTICE/CREDITS is a strength. The paper should describe a reimplementation/adaptation of established SPICE methods with new software architecture and compatibility engineering, and identify original contributions precisely.

Several existing comparisons should not enter a paper as written:

- `docs/neospice-vs-ngspice.md` says ngspice relies solely on terminal variable convergence. Its local `niiter.c` checks `CKTnoncon`, and device loads such as `b4v7ld.c` set it. Device-level convergence is not absent from ngspice.
- The same document suggests ngspice must transpose/refactor a separate matrix for noise. Its `NInzIter` calls `SMPcaSolve`, whose Sparse path calls `spSolveTransposed` on the existing factors. Neospice's separate transpose pattern is a design difference, not an established improvement over that path.
- Exact behavioral-source derivatives are not absent from ngspice: `inpptree.c` differentiates expression trees. Neospice's expression implementation can still be valuable without claiming to introduce the capability.
- `src/core/amd.cpp` explicitly constructs fill cliques and scans remaining vertices for minimum degree. That is not SuiteSparse AMD's approximate-degree/quotient-graph implementation. Claims that it “matches SuiteSparse AMD” need correction or a precisely defined equivalence claim and evidence.
- BTF code exists, but the production solver implementations inspected do not call `btf_decompose()`. Architecture documents currently describe it as part of the active solver path.

Python integration also has established alternatives: PySpice interfaces Python with ngspice and Xyce. The stronger distinction is ownership and embedding of the simulation engine, its data structures and device extension interface, rather than merely exposing SPICE through Python. Xyce provides an established independent, high-performance simulator against which broader architecture claims should be positioned. [PySpice documentation](https://pyspice.fabrice-salvaire.fr/releases/v1.4/index.html), [Xyce](https://xyce.sandia.gov/)

**Recommended paper and submission path**

The closest defensible paper is a research-software article, provisionally titled **“neospice: An embeddable C++20 circuit simulator with Python bindings and differential SPICE validation.”** Its contribution would be the architecture, migration/extension mechanism, actual research workflow, and reproducible compatibility evaluation. A focused example should show a real application such as repeated circuit characterization or optimization, including what embedding enables beyond existing interfaces. The repository demonstrates an oscillator notebook, but this audit did not establish external research adoption or publications using neospice.

JOSS is a plausible eventual target, with a material timing constraint. Its current submission criteria require more than six months of public development history and evidence of research use. The local history starts April 15, 2026, less than five months before this audit; even if public from inception, six months is reached around October 15. Public availability from that date has not been verified. It also requires an AI usage disclosure; the repository documents AI-assisted development. Authors must supply their actual tool-use history and human validation account. [JOSS submission requirements](https://joss.readthedocs.io/en/latest/submitting.html)

A methods paper needs a narrower hypothesis and ablations. The most interesting candidate is **how sparse ordering, device initialization, and continuation affect ngspice-compatible nonlinear operating-point selection, and how to obtain speedups without changing the selected solution**. Compare the compatibility-preserving default, forced ordering choices, and individual continuation changes on a frozen corpus. Report residuals, selected operating points, convergence methods, iteration counts, correctness, and runtime, including failures. Unique mathematical solutions on linear circuits do not guarantee bit-identical floating-point results, so the solver-selection rationale should be phrased accordingly.

This audit does not establish a novel algorithm sufficient for a competitive EDA methods submission. Implementing ML, GPU execution, or a new solver is not necessary for the software paper; adding an unvalidated research feature would expand the burden of proof.

**Work to complete before submission, in order**

1. Make comparisons reject invalid/empty/incomplete results, assert all intended comparisons, and fix the 15 tooling failures. Add the tooling suite and both Python test directories to regular CI. Existing push/PR CI runs CTest only; the wheel workflow tests only `tests/python`.
2. Publish an accurate device-by-analysis support matrix and repair contradictory README, migration, hierarchy, solver, sensitivity, and benchmark descriptions. Unsupported analyses should be explicit rather than silently returning an incomplete stamp.
3. Freeze the corpus, reference runtime, compatibility settings, and fixture policy. Rerun the full comparison after measurement fixes, investigate failures against ngspice, and archive provenance plus raw evidence. Do not relabel unverified reference failures as neospice correctness wins.
4. Produce accuracy-validated paired benchmarks and, for any mechanism-level claim, ablations. Include realistic nonlinear circuits and larger circuits alongside the existing small examples.
5. Document one actual research workflow and its users; prepare the manuscript, bibliography, citation metadata, release notes, and an archival release. Preserve upstream attribution and record authors' contributions. No manuscript or citation metadata was found in the tracked snapshot.

The main submission work is measurement integrity, scope accuracy, and research evidence. The underlying implementation is already substantial.

**Local audit artifacts and reproduction**

Build and logs are under `/tmp/neospice-paper-*`; they are session artifacts, not an archival paper release. The C++ build was configured with:

```sh
cmake -S . -B /tmp/neospice-paper-audit-build \
  -DCMAKE_BUILD_TYPE=Release -DNEOSPICE_DEBUG_COMPARE=ON \
  -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST="$PWD/build/_deps/googletest-src"
cmake --build /tmp/neospice-paper-audit-build -j8
ctest --test-dir /tmp/neospice-paper-audit-build \
  -j8 --output-on-failure --timeout 120

PYTHONPATH=/usr/lib/python3/dist-packages \
NGSPICE_DIR=/home/subhagato/Codes/ngspice \
  .venv/bin/python -m pytest tools/tests -q -rs

python3 tools/compare_kicad_models.py --jobs 8 \
  --neospice /tmp/neospice-paper-audit-build/neospice \
  --save /tmp/neospice-paper-kicad-full.json
```

For Python verification, the same build was subsequently configured with `NEOSPICE_BUILD_PYTHON=ON`, the local virtual-environment interpreter and nanobind CMake path; `_core` was built and staged together with current `python/neospice/*.py` under `/tmp/neospice-paper-python-stage`. The test interpreter bypassed the stale editable-install hook and ran `tests/python` and `python/tests` against that stage. The original C++ tests ran against the initial fresh Release build.
