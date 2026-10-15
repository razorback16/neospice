# Parallel sweeps and Monte Carlo

`Simulator::run_sweep` executes independent netlist jobs in a bounded C++ worker
pool. The batch reads the netlist, resolves includes, tokenizes and extracts
subcircuit definitions once, because none of these steps depends on sample
values. Each job then applies its overrides, expands instances and owns its
circuit, models, state and results. Results stay in input order. `workers=0` uses hardware concurrency capped at the number
of jobs. `workers=1`
executes jobs serially on one isolated worker.

Each netlist must contain exactly one analysis directive. Use separate batches
for different analyses. Nested `.step` is rejected. Text input uses `parse`
semantics. File input (`from_file=true`) resolves includes relative to the file.
Source and include files are read once when the batch starts. If they cannot
be read, every sample reports the same error.

## C++

```cpp
neospice::Simulator sim;
std::vector<neospice::SweepPoint> points(3);
for (int i = 0; i < 3; ++i) {
    points[i].parameters["rload"] = 1000.0 * (i + 1);
    points[i].temperature_celsius = 27.0 + 25.0 * i;
}
auto batch = sim.run_sweep("divider.cir", points, {.workers = 4}, true);
for (const auto& sample : batch.samples) {
    if (!sample.error.empty()) throw std::runtime_error(sample.error);
    auto v = std::get<neospice::DCResult>(sample.result->analysis).voltage("out");
}
```

`parameters` overrides **declared top-level `.param` names**, case-insensitively,
prior to dependency resolution and subcircuit expansion. Unknown names fail the
job. Multi-assignment cards and dependent expressions are preserved. Subcircuit
local parameters retain local scope. `device_values` instead changes a supported
primitive's value through the existing `Circuit::set_param` interface.
Temperature uses Celsius, unlike `Circuit::options.temp`, which uses Kelvin.
Process corners can use top-level parameters in model-card expressions.

## Python

```python
import neospice as ns

text = '''Divider
.param rload=1k
V1 in 0 6
R1 in out 1k
R2 out 0 {rload}
.op
.end
'''
batch = ns.sweep(text, [{"rload": 1000}, {"rload": 2000}], workers=2)

variation = ns.ParameterVariation()
variation.parameter = "rload"
variation.nominal = 1000
variation.spread = 50
variation.distribution = ns.VariationDistribution.Gaussian
mc = ns.monte_carlo(text, [variation], n=1000, seed=42, workers=4)
errors = [(i, s.error) for i, s in enumerate(mc.samples) if s.error]
if errors:
    raise RuntimeError(errors)
values = [s.result.dc.voltage("out") for s in mc.samples]
stats = ns.summarize_samples(values, lower=2.8, upper=3.2, bins=20)
print(stats.mean, stats.standard_deviation, stats.yield_fraction)
```

The Python calls release the GIL while the batch runs. A `SweepPoint` can be
passed instead of a dictionary for device-value or temperature overrides.
Sample/result references keep their owning batch alive.

## Randomness and statistics

Gaussian spread is absolute standard deviation. Uniform spread is absolute
half-width around nominal. No clipping is applied: an invalid sampled circuit
is a failed job, not a replacement draw. Optional correlation matrices are
symmetric positive semidefinite with unit diagonal and apply to Gaussian
parameters only. Perfect correlation and anticorrelation are supported.

Sampling and netlist expression random generators are seeded per job. Samples
and results are reproducible across worker counts for the same build and input.
C++ distribution implementations can differ between standard libraries, so
cross-platform bitwise identity is not promised. Input job order is part of the
seed mapping. The caller's expression RNG is not modified.

`summarize_samples` accepts explicit finite scalar values. Standard deviation
uses `n-1` (zero for one sample). Yield counts values within inclusive limits.
Histograms span the data minimum/maximum, with the final bin including the
maximum. Constant data occupies the first bin with coincident bin edges.
Failed jobs must be handled explicitly before aggregation. Dropping failures
can bias yield estimates. Aggregate a chosen voltage, measurement or other
scalar from any supported analysis.

## Throughput compared with ngspice 47

`tools/run_study_benchmark.py` runs the same N-sample resistor sweep three
ways in ngspice 47 and two ways in neospice:

- ngspice, one session: a single batch process loops over `alter` and the
  analysis. This is ngspice's fastest in-process path.
- ngspice, 24 sessions: 24 such processes, each handling a disjoint chunk.
- ngspice, one process per sample: run 24 at a time, which is typical
  scripted use.
- neospice: `run_sweep` with 1 and 24 workers. The param rows vary the
  top-level `.param`. Device-value rows are within 5% of them.

Samples per second at N = 10,000, median of five repetitions, on a 24-core Intel
Core Ultra 9 285K:

| Workload | ngspice 1 session | neospice 1 worker | ngspice 24 sessions | neospice 24 workers | ngspice process per sample |
|---|---:|---:|---:|---:|---:|
| LT1001 `.op`, 500 KB vendor library | 18,146 | 12,250 | 118,587 | **192,667** | 787 |
| THS4131 `.ac`, 81 points | **1,902** | 1,581 | **26,376** | 16,645 | 4,374 |
| RC pulse `.tran`, 50 µs | 246 | **1,069** | 3,923 | **20,751** | 2,671 |

Every sample's probe value matches the serial ngspice session within
4e-13 relative error. No sample failed.
[Evidence](evidence/performance/2026-10-14-study/README.md) includes both the
10,000- and 1,000-sample runs.

How to read the table:

- **Against ngspice's in-session `alter` loop, the result depends on the
  workload.** neospice is 5.3× faster on the transient study and 1.6× faster on
  the 24-way operating-point study. It is 0.63× on the 24-way THS4131 AC study.
  It is also slower serially on the two op-amp workloads: 0.68× on the
  operating point and 0.83× on AC.
- **Why the serial op-amp rows lose.** `run_sweep` expands subcircuits and sets
  up devices again for every sample. An `alter` loop changes one value in a
  circuit that is already set up and solves it again. When each solve is short,
  that per-sample setup is a large share of the time.
- **Why AC scales worse.** neospice reaches 10.9× on 24 workers for AC, while
  ngspice reaches 13.9×. This processor mixes performance and efficiency cores,
  so neither engine scales linearly.
- **Large speedups hold only against one process per sample.** The usual way to
  script ngspice is one process per sample. neospice is 245× faster than that on
  the operating-point study. Do not quote that ratio against ngspice's
  in-session baseline.

To reproduce:

```sh
python3 tools/run_study_benchmark.py --build build --ngspice /path/to/ngspice \
  --kicad-models third_party/KiCad-Spice-Library/Models \
  --output study-run --samples 10000
```

## Concurrency scope

Built-in migrated node scratch storage and HiSIM scratch variables are isolated
per thread. The BSIM4 legacy `bsim4.out` model-check log is serialized. It records
the last model check, not a per-job history. Console diagnostics may interleave.
Do not mutate the global device registry, process environment, simulator
configuration or a shared circuit while other threads use them. This API owns
its job circuits. It does not make concurrent operations on one circuit safe.
External math libraries can oversubscribe the machine with nested parallelism.
Measure suitable worker counts for your workload.

## Reproducibility check

`neospice_study_repro` hashes every sample's full result: all node voltages at
every time or frequency point, plus the sample's parameter values or error. It
runs each Monte Carlo batch twice at 1, 2, 4, 8, 16 and 24 workers and compares
every batch with the first, bit for bit. With `standalone`, it also re-parses
the netlist with each sample's parameter values on one thread, simulates it,
and compares that result too.

```sh
cmake -S . -B build -DNEOSPICE_BUILD_SWEEP_STRESS=ON
cmake --build build --target neospice_study_repro
cd tests/stress/repro
../../../build/neospice_study_repro ths_ac.cir rf 1000 10 1000 standalone
../../../build/neospice_study_repro rc_agauss.cir - 0 0 1000
```

Results on October 14, 2026 (24-core x86-64 Linux, GCC 14), 1,000 samples per
workload:

| Netlist | Randomness | Distinct samples | Bit-identical batches | Standalone mismatches |
|---|---|---:|---:|---:|
| `lt1001_op.cir` (500 KB vendor library, `.op`) | sampled `.param` | 1,000 | 12 of 12 | 0 |
| `ths_ac.cir` (THS4131, `.ac`) | sampled `.param` | 1,000 | 12 of 12 | 0 |
| `rc_tran.cir` (`.tran`) | sampled `.param` | 1,000 | 12 of 12 | 0 |
| `rc_agauss.cir` (`.tran`) | netlist `agauss()` only | 1,000 | 12 of 12 | not run |

Results do not depend on worker count or scheduling. A sample run alone
matches the same sample run inside a parallel batch. The standalone check is
not run for `rc_agauss.cir`, because its random values come from per-job
expression seeds that a standalone parse does not reproduce. This measurement
covers one build and platform. As noted above, other standard libraries can
draw different values.

## Race-test harness

The standalone harness does not link ngspice or need GoogleTest. It compares 32
serial and eight-worker temperature points for 19 built-in device fixtures.

```sh
cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=Debug \
  -DNEOSPICE_BUILD_TESTS=OFF -DNEOSPICE_BUILD_CLI=OFF \
  -DNEOSPICE_BUILD_SWEEP_STRESS=ON -DCMAKE_DISABLE_FIND_PACKAGE_OpenMP=TRUE \
  -DCMAKE_CXX_FLAGS='-O1 -g -fsanitize=thread' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=thread -no-pie'
cmake --build build-tsan -j8
TSAN_OPTIONS=halt_on_error=1 ./build-tsan/neospice_sweep_stress tests/circuits
```

`-no-pie` addresses sanitizer mapping conflicts on the development Linux host.
Other platforms may need different flags.
ThreadSanitizer can fail before execution with `unexpected memory mapping`.
An initialization failure is not a passing test.
A completed development run reported no races across these 19 fixtures.
This result does not certify every model option.
