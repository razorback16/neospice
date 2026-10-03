# ngspice 47 compatibility

ngspice 47 is neospice's sole compatibility target. Required C++ comparisons,
corpus evaluation, benchmarks and migration-tool source tests use this release.
There is no maintained older-version baseline or per-fixture reference switch.

## Reproducible reference

The release archive is pinned to SHA-256
`894e649651f1838a14095e5a5439e7d3aa63e87ede14d283173fda4fcdef675f`.
The [reference build script](../scripts/build-ngspice47-reference.sh) verifies
this checksum and builds separate CLI and shared-library installations from
unmodified source. It preserves the release's runtime defaults. See
[building](building.md) for online/offline setup and migration-test commands.

Both builds use `--without-x --with-readline=no --disable-debug` and
`CFLAGS='-O2 -g'`; the shared build additionally uses `--with-ngshared`.
XSPICE, KLU, OSDI and OpenMP are enabled by release defaults. Sparse is the
selected solver unless explicitly changed; compiled KLU support does not mean
KLU was used. Stock `spinit` supplies code-model paths and disables its OSDI
branch. Its `num_threads=8` setting overrides the corpus runner's OpenMP
thread environment. Concurrent corpus elapsed times are not performance data.
Paired benchmarks explicitly enforce one simulator thread.

CMake requires `ngspice=47` when tests are enabled. The corpus preflight checks
the actual CLI version and analytical divider/POLY probes. The paired benchmark
runner defaults to reference47 and checks the loaded library's recorded version.
CI uses the extracted47 source for migration tests too and rejects skipped
required tests. The local reference-build script and library have been tested;
remote Actions and macOS execution remain unverified.

The BSIM4 preprocessing and drain-current goldens are captured from this same
release, selecting BSIM4 `VERSION=4.7.0` to match the native model implementation.
The [capture instructions](../tests/goldens/README.md) and checked-in debugger
script reproduce the values without patching ngspice. Older simulator versions
appear in negative tests only to verify rejection, and in archived evidence.

## Current verified behavior

- AM-source parameter order, defaults and phases follow47.
- AC/noise frequency grids follow47, including sub-decade and short LIN sweeps.
- MES `AREA` and `M` are independent inputs.
- PULSE corners and transient restart scheduling follow the tested47 behavior.
- Diode grading coefficients retain values above0.9. Instance `TEMP`/`DTEMP`
  and series-resistance noise follow47.
- True-gmin slow-step reduction retains47's factor floor3; dynamic gmin uses1.00005.
- BJT/VBIC `OFF` reaches junction initialization without disabling the final
  biased operating point; see the [OFF investigation](bjt-off-investigation.md).
- Ordinary VBIC initialization reverse-biases the intrinsic base–collector
  junction as in 47; [the VBIC audit](vbic-compatibility.md) also records the
  delay implementation and thermal-interface limits.
- UIC startup skips the DC solve and preserves capacitor node initial conditions.
- Noise uses the common operating-point fallback sequence; VBIC delay noise
  follows the tested NPN/PNP behavior described in the option audit.
- Mixed model-card syntax such as `NPN LEVEL=4(IS=1e-14) TD=1u` preserves
  the model type and all parameters. Missing or incompatible Q models and
  unsupported BJT levels fail explicitly; see [model-card compatibility](model-card-compatibility.md).

See [source compatibility](source-compatibility.md),
[capabilities](capabilities.md), and [diode investigation](diode-coolmos-investigation.md).
These are tested behaviors, not a claim that every ngspice feature is supported.

## Remaining compatibility work

The current candidate 37 passes 1,265 of 1,266 C++ tests against 47; the
required RFF70N06 operating-point regression remains failing. It is retained
even though the reference also fails this fixture. The full frozen corpus
contains 67,359 fixture runs (34,908 declaration cases: 34,908 primary runs plus
32,451 isolated driven variants); remaining mismatches, model binding, option
coverage and evaluation grouping require investigation. The [VDMOS
audit](vdmos-compatibility.md) distinguishes repaired terminal syntax from
broader option coverage. Candidate 37's reference regressions pass for the
model/node scope collision and temperature-expression assignment precedence; its
full-corpus, fresh-wheel, sanitizer and benchmark validation remain pending.

All34 comprehensive benchmark workloads pass their accuracy gates. TLV3201
passes its original edge/DC-port contract but retains a strict pointwise
waveform discrepancy. VDMOS AC/noise are explicitly unsupported. The focused
13-test diode sanitizer suite passes with leak detection; this does not establish
that every reference/device path is leak-free. A previously reproduced reference
T-line leak remains separate from those passing tests.

The [progress tracker](joss-progress.md) links current evidence and blockers.
Earlier experiment records remain archived provenance and are not active
compatibility targets.
