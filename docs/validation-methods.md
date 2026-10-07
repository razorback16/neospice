# Validation methods

The verification harness compares neospice with **ngspice 47**. Analytical tests
provide independent checks. Regression tests preserve reproduced failures and
check fixes. Use the [pinned reference build](ngspice47-reference.md).

## Verification harness

| Level | Checks | Entry point |
|---|---|---|
| Devices and circuits | Operating points, DC sweeps, AC, transient waveforms, and noise | [Reference runner](../tests/framework/ngspice_runner.hpp), [comparators](../tests/framework/comparator.hpp) |
| KiCad libraries | 34,908 generated declaration cases, plus separate driven variants | [Corpus runner](../tools/compare_kicad_models.py), [frozen experiment](kicad-experiment.md) |
| Browser | Circuit Lab examples at their default settings | [Gallery checks](../web/scripts/check-gallery.ts), [reference bridge](../tests/wasm/gallery_reference.cpp) |
| Performance | Numerical checks for every validation, warmup, and measured pair | [Paired harness](../tests/bench/paired_benchmark.hpp), [benchmark methods](benchmark-methods.md) |

The [support matrix](support-matrix.md) separates reference comparisons,
analytical checks, and other regression coverage. [Native CI](../.github/workflows/ci.yml)
builds the pinned reference and rejects skipped required tests.

Gallery slider endpoints have separate convergence checks without a reference
comparison. See [Circuit Lab validation](circuit-lab.md#validation) for browser test scope.

## Required data

- ngspice defines the required signals. Tests can explicitly exclude named
  simulator-private signals. Missing public signals fail the comparison.
- C++ comparisons reject unsuccessful status, empty required data, nonfinite
  values, malformed vectors, and invalid axes. The reference runner also checks
  emitted error diagnostics.
- Transient times must increase strictly. neospice must cover the reference
  interval. AC, noise, and DC-sweep coordinates must match within a roundoff allowance.
- Operating-point comparisons require an operating-point plot. They cannot use
  the first AC or transient sample as a substitute.

DC-sweep coordinates can descend or repeat for nested sweeps.
`validate_dc_sweep_data()` checks data structure and alignment. Numerical
agreement and completion of the requested sweep require separate checks.

## Error formulas

Standard C++ comparators use:

```text
error = abs(reference - actual) / max(abs(reference), absolute_floor)
pass when error <= relative_tolerance at every compared value
```

`Tolerance.absolute` is the denominator floor. Near zero, the allowed error is
`relative_tolerance * absolute_floor`. Each test sets its tolerances.

| Analysis | Comparison |
|---|---|
| DC and DC sweep | Scalar values at corresponding coordinates |
| AC | Full complex difference, including phase |
| Transient | Both adaptive time grids within the reference interval, with interpolation between stored samples |
| Noise | Input and output amplitude spectra, after taking the square root of neospice power spectra |

Transient interpolation uses local polynomial estimates with linear fallbacks.
It does not bound error between samples. Negative noise densities fail.
Some device sweep tests use separate acceptance rules, including allowances for
mismatching points. Their test code defines those rules.

The Python corpus runner uses:

```text
abs(neo - ng) <= 1e-3 * max(abs(neo), abs(ng)) + absolute_tolerance
```

The absolute tolerance is **1e-6 V** for voltage and **1e-9 A** for current.
See the [frozen experiment](kicad-experiment.md) for case counts and outcomes.

## Specialized waveform checks

Oscillator tests compare period, amplitude, midpoint, and DC level. Edge tests
compare crossing time, transition duration, settled value, and overshoot.
These checks do not establish pointwise waveform agreement. The
[TLV3201 guide](tlv3201-validation.md) defines its edge and DC-port requirements.

Tests that check rejection of unsupported operations are separate from
numerical comparisons. A passing test covers its selected circuit, analysis,
options, signals, and acceptance rules.

## Memory checks

The [recorded sanitizer run](evidence/joss/2026-09-17-checkpoint39-milestone3.json)
covered 1,268 tests. All 19 leak-related failures traced allocations to the
ngspice reference library. No reported leak stack contained a neospice frame.
A separate [noise validation record](evidence/joss/2026-09-11-validation-24.json)
reproduced a 120-byte ngspice `TRAsetup` leak.

These results apply to the recorded builds and paths. A separate UBSan warning
at neospice thread-local guard resets remains unresolved. See
[open findings](joss-progress.md#open-numerical-findings).

## Run checks and inspect errors

First configure the reference paths as shown in the [build guide](building.md).
Then enable comparison diagnostics:

```sh
cmake -S . -B build -DNEOSPICE_DEBUG_COMPARE=ON
cmake --build build --parallel
ctest --test-dir build --verbose
```

`CompareResult.signals` records point counts, maximum absolute and normalized
errors, and the reference and actual values at the worst coordinate.
Debug output includes `MARGIN_*` and `DETAIL_*` records for supported comparators.
See [JOSS readiness](joss-progress.md) for remaining evidence work.
