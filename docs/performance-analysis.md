# neospice Performance Analysis — Full KiCad Suite vs ngspice

**Date:** 2026-06-16
**Scope:** End-to-end simulation speed of neospice vs ngspice across the **entire
34,908-model KiCad SPICE Library** parity suite.

---

## 1. Executive Summary

On the full all-models sweep, **neospice is ~1.50× faster than ngspice overall**
(257 s vs 385 s total wall) and faster on **81% of circuits**. This is a large
shift from the previous 5,000-model run, which was at aggregate parity (1.02×).

Two changes drove it: **lazy model-card parsing** (the old eager-parse tax is
gone — circuits that `.include` a big vendor library and use one part are now
O(1), not O(models)) and the **AMD-ordered KLU-style sparse LU** for large linear
circuits.

The only remaining slow region is a **tiny pathological tail: ~15 op-amp
macromodels** (CLC404/420/501/505, LM6317, …) that fall into expensive DC
convergence fallbacks (OPtran / pseudo-transient) and take **1–3 s** vs ngspice's
10–20 ms. Those 15 circuits alone are **14% of neospice's entire total time**.
Fixing their convergence path is now the single highest-leverage perf item.

---

## 2. Methodology

- **Harness:** `tools/compare_kicad_models.py --max 0 --jobs 8 --save
  results/compare_full_timing.json`. Each model becomes a minimal `.op` fixture;
  both simulators run it and node voltages/branch currents are compared.
- **Timing:** subprocess wall time (startup + parse + solve), 8-way parallel.
- **Fair set:** the **25,843 circuits where both simulators converged**.
- **Caveat:** these are minimal `.op` fixtures, so per-run time is dominated by
  fixed costs (startup, parse, small solve), *not* large-circuit solver
  throughput — for that see `tests/bench/bench_solver_throughput`. Absolute
  totals are inflated by 8-way contention, but it hits both simulators equally so
  **per-circuit ratios are valid**.

---

## 3. Results (25,843 both-converged circuits)

| Metric | neospice | ngspice | ratio (ng/neo) |
|---|---:|---:|---:|
| Total | 257.3 s | 384.9 s | **1.50×** |
| Mean / circuit | 9.96 ms | 14.90 ms | 1.50× |
| Median / circuit | 6.16 ms | 8.86 ms | 1.44× |
| Median per-circuit speedup | — | — | **1.34×** |

neospice is faster on **20,943 / 25,843 (81.0%)** circuits.

**Per-circuit speedup distribution (ng/neo, >1 = neospice faster):**

| p10 | p25 | p50 | p75 | p90 |
|---:|---:|---:|---:|---:|
| 0.86× | 1.07× | **1.34×** | 1.77× | 2.49× |

The whole distribution moved right vs the old 5k run (was p10 0.50× / p50 1.10× /
p90 1.58×). The old left tail — large-library `.include`s — is resolved.

### The new slow tail

neospice is slower on 19% of circuits, but the cost is concentrated in **15
circuits with `neo_time > 100 ms`** (14 of them > 500 ms):

| Circuit | neospice | ngspice | ratio |
|---|---:|---:|---:|
| `CLC404` | 3104 ms | 9.3 ms | 0.00× |
| `CLC420` | 2877 ms | 12.5 ms | 0.00× |
| `LM6317_NSC` | 2857 ms | 17.5 ms | 0.01× |
| `CLC501_CL` | 1111 ms | 14.8 ms | 0.01× |
| *(old tail)* `FMMT458` | 6.5 ms | 10.0 ms | **1.55×** |

These are op-amp/comlin macromodels that don't converge by direct Newton or gmin
and fall through to neospice's transient-startup (`OPtran`) / pseudo-transient
fallbacks, which run a mini-transient costing seconds. ngspice's continuation
reaches the operating point in milliseconds. This is now the dominant tail cost,
replacing the old library-parse tax.

---

## 4. Status of prior recommendations

- **P1 Lazy model-card parsing — DONE.** `pass1` now stores raw `.model` token
  spans keyed by name; `ensure_model` parses + AKO-resolves + memoizes on first
  reference (`netlist_parser.cpp:880, :971`). Old tail collapsed: `FMMT458`
  18.5 → 6.5 ms, `2N5961` 18.1 → 6.7 ms, `8EWS12` 9.5 → 4.4 ms — all now faster
  than ngspice.
- **AMD/KLU sparse LU — DONE.** Auto-selected for large linear circuits
  (`AmdLuSolver`, `make_solver`); see the neospice-design doc for detail.
  Production payoff e.g. `twisted_pair1024` ~9.6 min → 0.37 s.

---

## 5. Next Steps — Prioritized

### Priority 1 — Cheaper / faster DC convergence for op-amp macromodels *(High impact)*

The remaining tail is entirely convergence-fallback cost, not solve or parse.
Targets: (a) reach these operating points without the seconds-scale transient
fallbacks (better continuation / gmin path matching ngspice's `new_gmin`),
or (b) cap/short-circuit the `OPtran` fallback so a hard circuit fails fast
instead of spending 3 s. Removing the 15-circuit tail would drop neospice's total
~257 s → ~221 s (**~1.74×** vs ngspice).

### Priority 2 — Solver-throughput tracking *(Measurement)*

`tests/bench/bench_solver_throughput.cpp` reports solve time isolated from parse
over scalable linear (resistor mesh) and nonlinear (diode ladder) circuits at
N ≈ {100, 1k, 5k, 20k} nodes. Keep it in the loop so parser wins can't mask
solver regressions. Not registered with ctest (benchmarks must not gate CI).

```bash
cd build && cmake --build . --target bench_solver_throughput -j$(nproc)
./tests/bench_solver_throughput
```

Finer per-phase timers (symbolic vs numeric factor vs triangular solve vs
device-eval) would need opt-in instrumentation hooks in `newton_solve()` /
`NeoSolver`; deferred.

---

## 6. Reproduce

```bash
cd build && cmake --build . -j$(nproc)

# Full timing sweep (writes SPEED SUMMARY + per-circuit timing JSON)
python3 tools/compare_kicad_models.py --max 0 --jobs 8 \
    --save results/compare_full_timing.json

# Per-circuit percentiles
python3 - <<'PY'
import json
rs = json.load(open('results/compare_full_timing.json'))['results']
both = [r for r in rs if r['neo_ok'] and r['ng_ok'] and r['neo_time'] > 0]
ratios = sorted(r['ng_time']/r['neo_time'] for r in both)
pct = lambda p: ratios[min(len(ratios)-1, round(p/100*(len(ratios)-1)))]
print({p: round(pct(p), 2) for p in (10, 25, 50, 75, 90)})
PY
```
