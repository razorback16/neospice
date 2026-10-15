# Filter design: gradients, tuning and yield

This example designs doubly terminated Butterworth LC ladder low-pass filters
(50 Ω, fc = 1 MHz) entirely from Python. It runs each step in neospice and in
ngspice 47 and compares both results and wall time.

```sh
pip install neospice numpy
NGSPICE=/path/to/ngspice47 python filter_design.py 5   # 5 repetitions
```

`ladder.py` generates the netlists. `filter_design.py` writes generated
netlists to `work/` and the results to `results.json`. The retained
[results.json](results.json) is from commit `906930c`, run on a 24-core Intel
Core Ultra 9 285K. Times are medians of
five repetitions. Background load was present (one-minute load average about 6),
so treat the times as indicative.

## A. Gradient cost as the parameter count grows

The task is the complex sensitivity of v(out) to every L and C at 21
frequencies from 100 kHz to 10 MHz.

- neospice: `ns.sensitivity_ac`, an adjoint solve, timed in-process including
  netlist parse.
- ngspice: built-in `sens v(out) ac …`, and forward differences in a single
  session (`alter`, then `ac`, for each element).

The ngspice columns subtract that order's process start, parse and setup time
(2–3 ms). Note that ngspice `.sens` always computes every instance parameter,
not only the 201 element values used here.

| Order (parameters) | neospice adjoint (ms) | ngspice `.sens` (ms) | ngspice FD (ms) | `.sens` / adjoint | FD / adjoint | `.sens` vs adjoint | FD vs adjoint |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 5 | 0.15 | 2.3 | 1.4 | 16× | 9× | 6e-11 | 2e-06 |
| 11 | 0.18 | 4.2 | 1.7 | 23× | 9× | 4e-11 | 3e-06 |
| 21 | 0.18 | 8.7 | 3.3 | 47× | 18× | 8e-11 | 4e-06 |
| 51 | 0.42 | 29.0 | 10.1 | 70× | 24× | 6e-11 | 6e-06 |
| 101 | 0.74 | 70.8 | 29.0 | 95× | 39× | 9e-11 | 8e-06 |
| 201 | 1.45 | 234.1 | 86.9 | 161× | 60× | 9e-11 | 1e-05 |

The accuracy columns give the maximum relative difference from the neospice
adjoint. `.sens` is compared on c1. The FD gap is the expected
forward-difference truncation error. Node voltages match ngspice to 3e-13 or
better.

## B. Tuning after rounding inductors to E12

The seventh-order design's inductors are rounded to E12 values. The capacitors
are then re-tuned by Gauss–Newton on the dB response, using log-parameters and
eight iterations. The ladder is symmetric, so mirrored capacitors have identical
magnitude sensitivity. Each mirrored pair is tuned as one variable; the untied
Jacobian has rank 2.

Both engines' gradients take the same path. RMS error falls from 0.363 dB to
0.0773 dB, and the tuned values agree to 1.4e-8. The neospice loop takes 1.9 ms.
The ngspice loop takes 34 ms, most of it in its nine process starts.

## C. Monte Carlo yield

The tuned filter is sampled 10,000 times with a 5% Gaussian sigma on every L and
C. The spec is gain ≥ −1 dB at 0.9 fc and ≤ −40 dB at 2 fc. neospice samples
through `ns.monte_carlo`. ngspice evaluates exactly the same parameter draws
with `alter` loops.

| Engine | 1 worker / session (s) | 24 workers / sessions (s) | Yield |
|---|---:|---:|---:|
| neospice | 0.32 | 0.051 | 70.56% |
| ngspice 47 | 0.97 | 0.100 | 70.56% |

The pass/fail decision agrees on all 10,000 samples. The neospice serial and
24-worker runs return identical decisions.
