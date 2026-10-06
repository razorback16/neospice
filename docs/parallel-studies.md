# Parallel sweeps and Monte Carlo

`Simulator::run_sweep` executes independent netlist jobs in a bounded C++ worker
pool. Each job parses and owns its circuit, models, state and results. Results
stay in input order. `workers=0` uses hardware concurrency capped at the number
of jobs; this is a default, not an empirically optimal worker count. `workers=1`
executes jobs serially on one isolated worker.

Each netlist must contain exactly one analysis directive. Use separate batches
for different analyses. Nested `.step` is rejected. Text input uses `parse`
semantics; file input (`from_file=true`) resolves includes relative to the file.
Keep source/include files unchanged during a batch.

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

Gaussian spread is absolute standard deviation; uniform spread is absolute
half-width around nominal. No clipping is applied: an invalid sampled circuit
is a failed job, not a replacement draw. Optional correlation matrices are
symmetric positive semidefinite with unit diagonal and apply to Gaussian
parameters only; perfect correlation and anticorrelation are supported.

Sampling and netlist expression random generators are seeded per job. Samples
and results are reproducible across worker counts for the same build and input;
C++ distribution implementations can differ between standard libraries, so
cross-platform bitwise identity is not promised. Input job order is part of the
seed mapping. The caller's expression RNG is not modified.

`summarize_samples` accepts explicit finite scalar values. Standard deviation
uses `n-1` (zero for one sample); yield counts values within inclusive limits.
Histograms span the data minimum/maximum, with the final bin including the
maximum. Constant data occupies the first bin with coincident bin edges.
Failed jobs must be handled explicitly before aggregation; dropping failures
can bias yield estimates. Aggregate a chosen voltage, measurement or other
scalar from any supported analysis.

## Concurrency scope

Built-in migrated node scratch storage and HiSIM scratch variables are isolated
per thread. The BSIM4 legacy `bsim4.out` model-check log is serialized; it records
the last model check, not a per-job history. Console diagnostics may interleave.
Do not mutate the global device registry, process environment, simulator
configuration or a shared circuit while other threads use them. This API owns
its job circuits; it does not make concurrent operations on one circuit safe.
Nested parallelism in external math libraries can oversubscribe a machine;
measure worker counts appropriate for the workload. No scaling claim is made.

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

`-no-pie` is a Linux-specific workaround for this host's sanitizer mappings;
other platforms may need different sanitizer flags. On the development host,
ThreadSanitizer intermittently failed before execution with `unexpected memory
mapping` (and once with no output and SIGSEGV). A subsequent initialized run
completed all 19 fixtures with exit 0 and no race report. Runtime initialization
failures are not passing tests. This scope does not certify every model option.
