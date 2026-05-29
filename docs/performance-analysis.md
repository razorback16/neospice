# neospice Performance Analysis — 5000-Model KiCad Suite vs ngspice

**Date:** 2026-05-28
**Scope:** End-to-end simulation speed of neospice vs ngspice across the KiCad
SPICE Library 5000-model parity suite, plus root-cause investigation and a
prioritized plan to improve performance.

---

## 1. Executive Summary

On the full 5000-model sweep, **neospice and ngspice are at aggregate parity**
(47.3 s vs 48.2 s total wall time; 1.02× in neospice's favor) and neospice is
**~10–20% faster on the median circuit**. But there is a real tail of circuits
where neospice runs **2.7–4× slower**.

Root cause of that tail is **not** the numerical solve and **not** process
startup — both are at parity with ngspice. It is the **eager parsing of every
`.model` card in an included library**, even models that are never
instantiated. neospice spends **~14 µs per model card** vs ngspice's **~3 µs**,
so a circuit that `.include`s a 1000-model vendor library and uses one part
pays a **~15 ms tax** before it does any real work.

**The single highest-leverage fix is lazy model-card parsing**: defer the
expensive per-card work until an instance actually references the model. This
turns the common "big catalog, one part used" case from O(models) into O(1) and
would push neospice clearly ahead of ngspice on the whole suite.

---

## 2. Methodology

- **Harness:** `tools/compare_kicad_models.py` (instrumented in this session to
  record per-circuit subprocess wall time for both simulators). Each model
  becomes a minimal `.op` test circuit; both simulators run it; node voltages
  and branch currents are compared.
- **Timing captured:** subprocess wall time only (process startup + parse +
  solve), excluding the harness's netlist-write and `.raw`-parse steps.
- **Full sweep:** `--max 5000 --jobs 8`, results in
  `results/compare_5k_timing.json`.
- **Fair comparison set:** the **3,781 circuits where both simulators
  converged** (identical work done on both sides).
- **Follow-up isolation runs:** serial (un-contended) timing with 25–40
  repetitions per circuit, median and min reported, to separate startup from
  parse from solve.

### Caveats

- These are **minimal single-device / single-subckt `.op` fixtures**. Per-run
  time is dominated by fixed costs (startup, library parse), not large-circuit
  linear-algebra throughput. This is the right metric for *"how fast does the
  5000-model sweep / a typical KiCad part lookup run"* — it is **not** a measure
  of solver throughput on large transient circuits. For that, use the
  `tests/bench/` micro-benchmarks.
- The full-sweep absolute numbers were taken under 8-way parallelism, so they
  are inflated by CPU contention. Contention affects both simulators equally, so
  **per-circuit ratios are valid**; absolute per-circuit times in §3 come from
  the serial isolation runs.

---

## 3. Results

### 3.1 Aggregate (3,781 both-converged circuits, 8-way parallel)

| Metric | neospice | ngspice | ratio (ng/neo) |
|---|---:|---:|---:|
| Total | 47.26 s | 48.22 s | **1.02×** |
| Mean / circuit | 12.50 ms | 12.75 ms | 1.02× |
| Median / circuit | 10.51 ms | 12.64 ms | **1.20×** |
| Median per-circuit speedup | — | — | **1.09×** |

neospice is faster on **2,179 / 3,781 (57.6%)** circuits. Totals tie; the median
favors neospice — meaning neospice wins the typical case but a slow tail drags
its mean up to parity.

### 3.2 Distribution of per-circuit speedup (ng_time / neo_time)

| Percentile | ng/neo | Interpretation |
|---|---:|---|
| p10 | 0.50× | worst 10%: neospice ~2× slower |
| p25 | 0.75× | |
| **p50** | **1.10×** | median: neospice ~10% faster |
| p75 | 1.39× | |
| p90 | 1.58× | best 10%: neospice ~1.5× faster |

The left tail (p10 = 0.50×) is the problem region investigated below.

### 3.3 The slow tail (serial, un-contended timing)

| Circuit | neospice | ngspice | ratio |
|---|---:|---:|---:|
| `FMMT458` (BJT.lib) | 18.46 ms | 6.51 ms | 0.35× |
| `2N5961` (BJT.lib) | 18.10 ms | 6.81 ms | 0.38× |
| `8EWS12` (DIODE2.lib) | 9.50 ms | 4.50 ms | 0.47× |
| trivial RC (no include) | 2.93 ms | 2.99 ms | **1.02×** |

The slow circuits all `.include` large vendor libraries. The trivial circuit
with no include is at parity — the first clue that the cost is in library
handling, not the engine.

---

## 4. Root Cause

### 4.1 Startup and solve are at parity — the cost is library parsing

Two controlled experiments isolate the cost:

**Experiment A — inline the single used model instead of `.include`-ing the
1040-line library:**

| Variant | neospice | ngspice |
|---|---:|---:|
| `.include BJT.lib` (1038 models) | 18.52 ms | 6.65 ms |
| inline single `FMMT458` model | **2.94 ms** | **2.91 ms** |

With the library replaced by the one model actually used, neospice ties ngspice
exactly. **100% of the slowdown is in processing the included library**, not in
parsing the circuit, setting up the device, or running Newton.

**Experiment B — scale the number of (unused) model cards in the include:**

| Models in include | neospice | ngspice |
|---:|---:|---:|
| 1 | 3.18 ms | 2.91 ms |
| 50 | 3.82 ms | 3.06 ms |
| 100 | 4.70 ms | 3.34 ms |
| 200 | 6.08 ms | 3.49 ms |
| 1038 | 17.99 ms | 6.30 ms |

Both simulators scale **linearly** with the number of model cards, but the
per-card constant differs sharply:

- **neospice: ~14.3 µs per model card**  `((18.0 − 3.18) / 1037)`
- **ngspice: ~3.3 µs per model card**  `((6.30 − 2.91) / 1037)`

neospice does **~4.3× more work per model card**, and it does this work for
**every card in the library whether or not it is ever instantiated**.

### 4.2 Why neospice is slow per card

`pass1_collect_models_params` (`src/parser/netlist_parser.cpp:695`) calls
`parse_model_card` (`src/parser/model_cards.cpp:18`) eagerly for every `.model`
line. For each card that function:

1. joins all tokens into a single `std::string`,
2. lowercases substrings (`to_lower`) for AKO / type detection,
3. builds a second normalized copy of the parameter string (spacing out `=`),
4. splits it and parses each `key=val` into an `unordered_map<string,string>`.

For a BJT card with ~30 parameters this is dozens of allocations and string
scans — paid 1038 times to use one model. ngspice stores model cards more
cheaply and defers most derived-parameter work to the instances that reference
them.

This matters far beyond the benchmark: **real KiCad usage is exactly this
pattern** — include a large vendor `.lib`, instantiate one or two parts. Every
such simulation pays the full library-parse tax, and the tax grows with library
size.

---

## 5. Next Steps — Prioritized

### Priority 1 — Lazy model-card parsing  *(High impact, Medium effort)*

**What:** Defer the expensive per-card work until an instance references the
model. In pass 1, store only the raw token span (or the `.model` line) keyed by
name. In pass 2, when an element names a model, parse that card on first use and
memoize the result.

**Why:** Converts the dominant cost from **O(models in library)** to
**O(models actually used)**. For the common "1000-model catalog, 1 part used"
case this removes essentially all of the ~15 ms tax — projecting `FMMT458` from
18.5 ms back toward the ~3 ms inline baseline, i.e. **~6× faster on these
circuits and clearly ahead of ngspice**.

**Where:** `src/parser/netlist_parser.cpp:695` (`pass1_collect_models_params`)
and the model-lookup sites in `pass2_parse_elements`
(`src/parser/netlist_parser.cpp:842+`, `:2948+`, `:3102+`).

**Watch-outs:**
- **AKO inheritance** (`netlist_parser.cpp:755-823`) resolves base→derived
  chains across the whole model set. Lazy parsing must still resolve the AKO
  base of a *used* model on demand (parse the base card too, transitively).
- **Typed model maps** (`res_models`, `cap_models`, `ind_models`) are currently
  populated eagerly; make these lazy or populate on first reference.
- Error reporting: a malformed *unused* model card currently errors at parse
  time. Lazy parsing would only surface the error if the model is used — which
  matches ngspice behavior and is arguably more correct, but confirm against the
  parity suite.

### Priority 2 — Cheaper per-card parsing  *(Medium impact, Low effort)*

Even for used cards, shave the ~14 µs constant:

- Parse parameters directly from the original token vector instead of
  re-joining into a string and re-splitting (eliminates 2–3 string builds per
  card).
- Use `std::from_chars` for numeric values and avoid the spacing-out-`=`
  normalization pass.
- `reserve()` the parameter map; avoid repeated `to_lower` of the same
  substrings (lowercase once).
- Consider a small flat vector of `(key,val)` pairs instead of
  `unordered_map<string,string>` for the typical ≤30-param card.

**Where:** `src/parser/model_cards.cpp:18`.

This compounds with Priority 1 (it makes each *used* card cheaper) and also
helps any path that still must parse many cards.

### Priority 3 — Verify there is no quadratic AKO path  *(Low effort, safety)*

The AKO resolution loop (`netlist_parser.cpp:762`) runs up to
`models.size()+1` passes, each iterating all models. For libraries with no AKO
it exits in one pass (confirmed by the linear scaling above), but a library with
long AKO chains could approach O(N²). After Priority 1 this loop should only run
over *used* models; until then, add a worklist so each pass only revisits
still-unresolved cards.

### Priority 4 — Solver throughput benchmark  *(Measurement gap)*

This study only covers tiny `.op` fixtures, so it says nothing about
large-circuit solver speed. Add a benchmark over a handful of genuinely large
transient/DC circuits (reuse `tests/bench/bench_newton_profile.cpp` and
`tests/bench/profile_tlv3201.cpp`) to track Newton-iteration cost, device-eval
cost, and matrix factor/refactor cost independently of parse overhead. Without
this, parser wins could mask solver regressions (or vice versa).

#### Implemented: `bench_solver_throughput`

`tests/bench/bench_solver_throughput.cpp` is the first cut of this benchmark. It
generates large circuits **in-memory** and reports **solve time isolated from
parse time**, so the two phases can move independently.

What it measures:

- **Two scalable circuit classes**, each at N ≈ {100, 1000, 5000, 20000} nodes:
  - **Linear** — a 2-D resistor mesh (R grid). Linear, so DC converges in a
    small fixed iteration count: isolates symbolic + numeric LU factorization
    and the triangular solve (the sparse-LU hot path).
  - **Nonlinear** — a diode+resistor ladder. Each stage adds a nonlinear
    junction, so DC needs several Newton iterations: isolates Newton iteration
    count and per-iteration device evaluation.
- **Per-phase breakdown** via the public API:
  - `parse(ms)` — `Simulator::parse()` (build + finalize),
  - `solve(ms)` — `Simulator::run_dc()` (full Newton DC solve),
  - `iter` — Newton iterations (`DCResult::status.iterations`),
  - `us/iter` — solve time per iteration, plus convergence-method annotation
    (e.g. gmin-stepping) when the solve didn't converge directly.
- **Steady numbers** — medians over N repetitions after warmup; iteration counts
  scale down automatically as circuits grow. Problem stats (nodes, vars, nnz,
  device count) are printed per size.

It links `neospice_lib` only — **no ngspice dependency** — and is **not**
registered with ctest (benchmarks must not gate CI).

Build and run:

```bash
cd build && cmake .. && cmake --build . --target bench_solver_throughput -j$(nproc)
./tests/bench_solver_throughput
```

Sample output (Release-equivalent local build):

```
LINEAR  -- 2D resistor mesh (stresses sparse LU: symbolic+numeric factor + solve)
     nodes     vars      nnz   devices  iter     parse(ms)    solve(ms)    us/iter
        99      100      457       181     3         0.147        0.213       70.9
      1023     1024     4989      1985     3         1.691        8.220     2740.2
      5040     5041    24918      9941     3        19.682      241.076    80358.7
     19880    19881    98838     39481     3       259.138     4406.233  1468744.2

NONLINEAR -- diode+R ladder (stresses Newton iterations + device eval)
     nodes     vars      nnz   devices  iter     parse(ms)    solve(ms)    us/iter
       102      103      306       202    11         0.133        0.438       39.8
      1002     1003     3006      2002    11         1.801       35.734     3248.6
      5002     5003    15006     10002    11        18.259      878.227    79838.8
     20002    20003    60006     40002    11       243.284    14079.549  1279959.0
```

Scaling observations from the sample run:

- **Solve dominates parse on large circuits.** At ~20k nodes, parse is ~0.26 s
  but solve is ~4.4 s (linear) / ~14 s (nonlinear) — i.e. solve is 17×–58×
  parse. The parser wins from Phases 1–3 are real but only matter for the
  small-fixture regime; this benchmark is where solver work shows up.
- **Per-iteration cost grows super-linearly with size** (`us/iter` rises ~20×
  from N≈5k to N≈20k for a ~4× node increase), consistent with fill-in growth
  in the sparse LU on these 2-D-connectivity meshes rather than O(n) behavior.
- **Nonlinear holds a constant 11 Newton iterations across sizes**, so its solve
  cost scales with per-iteration factor+device-eval cost rather than with
  iteration count — a clean signal for catching device-eval or refactor
  regressions.

**Follow-up — finer instrumentation.** The current breakdown stops at
parse/solve/per-iteration because the public API does not expose
symbolic-factor vs numeric-factor vs triangular-solve vs device-eval
microseconds: `newton_solve()` and `NeoSolver` return no per-phase timers.
Splitting those cleanly would require lightweight optional instrumentation hooks
inside `newton_solve()` / `NeoSolver` (e.g. an opt-in timing struct populated
only when requested, kept off the hot path by default). That is deliberately
deferred — the scratch profiler `tests/bench/bench_newton_profile.cpp` shows
what such a manual split looks like, but it re-implements the Newton loop and so
does not reflect the production solver path.

---

## 6. Expected Outcome

| Fix | Effect on `FMMT458`-class circuits | Effect on full sweep |
|---|---|---|
| P1 Lazy model parsing | 18.5 ms → ~3–4 ms (**~5×**) | neospice clearly ahead of ngspice; tail collapses |
| P2 Cheaper per-card parse | further trims used-model cost | small additional gain |
| P1 + P2 | at/below ngspice on every category | total well under ngspice's 48 s |

The left tail in §3.2 (p10 = 0.50×) is almost entirely large-library includes;
removing the unused-model tax should lift p10 from ~0.5× toward ~1.0× and move
the whole distribution right.

---

## 7. Reproduce

```bash
# Build
cd build && cmake --build . -j$(nproc)

# Full timing sweep (writes SPEED SUMMARY + per-circuit timing JSON)
python3 tools/compare_kicad_models.py --max 5000 --jobs 8 \
    --save results/compare_5k_timing.json

# Per-circuit percentiles / slowest outliers
python3 - <<'PY'
import json
rs = json.load(open('results/compare_5k_timing.json'))['results']
both = [r for r in rs if r['neo_ok'] and r['ng_ok']]
ratios = sorted(r['ng_time']/r['neo_time'] for r in both if r['neo_time'] > 0)
pct = lambda p: ratios[min(len(ratios)-1, round(p/100*(len(ratios)-1)))]
print({p: round(pct(p), 2) for p in (10, 25, 50, 75, 90)})
PY

# Isolation: include-lib vs inline-model (the root-cause experiment)
#   - replace `.include <vendor>.lib` with just the one `.model` card used
#   - serial timing with /usr/bin/time -v or python perf_counter, 25+ reps
```

The timing instrumentation lives in `tools/compare_kicad_models.py`
(`run_simulator` returns elapsed seconds; results carry `neo_time` / `ng_time`;
the `SPEED SUMMARY` block reports totals, mean, median, and per-circuit
speedup).
