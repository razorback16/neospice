# Milestone 3.5 — MOSFET Step-Limiting + Residual Physics Closure

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Unblock `CMOSInverterTransient` and `RingOscillator5Stage` by (a) damping Newton voltage steps at MOSFET junctions to eliminate the limit-cycle oscillation we diagnosed in M3, and (b) closing the residual ~1.84× NMOS over-current gap that currently pins the CMOS inverter's DC solution at `out=1.27V` instead of ngspice's ~1.8V.

**Architecture:** M3 established that neither limb alone is sufficient (see memory `m3-t3-t4-diagnosis-limit-cycle`). This plan treats them as a coupled fix.

(1) **MOSFET voltage step-limiting.** ngspice's `DEVlimvds`, `DEVpnjlim`, and per-device `Vgs_old`-clamps (b4v7ld.c:455-620) bound each Newton step to a physically reasonable delta — typically `|ΔVgs| ≤ 0.5V`, `|ΔVds|` bounded by Vdsat trajectory. Our current `BSIM4v7::limit_voltages` either does nothing or is too weak. Port the three ngspice helpers (or equivalent) into `src/devices/bsim4v7/bsim4v7.cpp::limit_voltages`.

(2) **Residual NMOS over-current.** The remaining 3.65× NMOS_DC_IV error from M2.5 (and the coincident 1.84× at the CMOS inverter operating point) likely comes from: missing VACLM term (channel-length-modulation output-resistance multiplier, b4v7ld.c:1957-1989), missing VADIBL (DIBL-based Early voltage, b4v7ld.c:1991-2040), and omitted velocity-overshoot factor on Vdsat (b4v7ld.c:1693-1730). Port these one at a time, verifying NMOS_DC_IV worst_error drops monotonically.

(3) **Diagnostic hook.** Make the env-gated Newton diagnostic from M3 permanent behind `SimOptions.verbose`. Future convergence debugging should not require patching the solver.

**Tech Stack:** Same as M3 — C++17, KLU, ngspice reference at `/home/subhagato/Codes/ngspice/src/spicelib/devices/bsim4v7/`.

---

## Scope Check

M3.5 does **both** solver step-limiting and residual physics because the diagnosis showed they must land together. Further physics (polysi-depletion, pocket-implant Vth shift) is still deferred — those are narrow-margin corrections and don't affect the two blocked tests' convergence or trajectory.

---

## File Structure

**Created**
- `tests/unit/test_newton_limiting.cpp` — unit test covering a single MOSFET Newton step from an overshoot state.
- `tests/unit/test_bsim4v7_vaclm.cpp` — unit test covering VACLM contribution to `gds` in saturation.

**Modified**
- `src/devices/bsim4v7/bsim4v7.cpp` — implement `limit_voltages` with ngspice-style clamping (currently a stub or too-weak).
- `src/devices/bsim4v7/bsim4v7_eval.cpp` — port VACLM and VADIBL into `r.gds`; port velocity-overshoot onto Vdsat.
- `src/core/newton.hpp`, `src/core/newton.cpp` — add `verbose` diagnostic path gated on `SimOptions.verbose`.
- `src/core/sim_options.hpp` — add `bool verbose = false;` member.
- `tests/unit/test_ngspice_compare.cpp` — re-enable `CMOSInverterTransient` and `RingOscillator5Stage`; tighten NMOS_DC_IV tolerance after physics closure.
- `tests/CMakeLists.txt` — register the two new unit tests.

---

### Task 1: Add `SimOptions.verbose` and gated Newton diagnostic

**Rationale:** M3 required patching `newton_solve` with `std::cerr` to diagnose the limit cycle. Make this permanent behind a flag so future convergence debugging is one line of setup, not a patch.

**Files:**
- Modify: `src/core/sim_options.hpp`
- Modify: `src/core/newton.cpp`

- [ ] **Step 1: Add `verbose` flag to SimOptions**

Open `src/core/sim_options.hpp`. Add a `bool verbose = false;` field (co-located with the other bool/numeric members). Keep the default `false` so no existing call site behaves differently.

- [ ] **Step 2: Add the gated diagnostic to newton_solve**

In `src/core/newton.cpp`, after the existing `NumericMatrix mat(pattern);` and RHS setup lines, add:

```cpp
    if (opts.verbose) {
        std::cerr << "[newton] gmin=" << opts.gmin << " start:";
        for (int32_t i = 0; i < num_nodes; ++i)
            std::cerr << " " << ckt.node_name(i) << "=" << solution[i];
        std::cerr << "\n";
    }
```

Inside the Newton loop, after the convergence check (and before the `if (converged) return …` block), add:

```cpp
    if (opts.verbose) {
        double max_diff = 0.0; int worst = -1;
        for (int32_t i = 0; i < n; ++i) {
            double d = std::abs(solution[i] - old_solution[i]);
            if (d > max_diff) { max_diff = d; worst = i; }
        }
        std::cerr << "[newton] iter=" << iter
                  << " max_diff=" << max_diff
                  << " worst_idx=" << worst
                  << (converged ? " (converged)" : "")
                  << "\n";
    }
```

At the fallout path (`return {false, opts.max_iter, solution};`), add:

```cpp
    if (opts.verbose)
        std::cerr << "[newton] NOT converged after " << opts.max_iter << " iter\n";
```

Include `<iostream>` at the top of the file.

- [ ] **Step 3: Build and run the full suite**

```bash
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

Expected: 107/107 enabled tests PASS (verbose is off by default).

- [ ] **Step 4: Commit**

```bash
git add src/core/sim_options.hpp src/core/newton.cpp
git commit -m "$(cat <<'EOF'
feat(solver): add SimOptions.verbose for Newton iteration diagnostics

Emits per-iteration max_diff + node voltages when enabled. Default off;
no change to existing tests. Intended for convergence-failure diagnosis
without needing to patch newton.cpp.
EOF
)"
```

---

### Task 2: Port MOSFET Newton voltage limiting (TDD)

**Rationale:** Diagnosis showed Newton steps of ~0.6V at MOSFET junctions, producing stable limit cycles at lower gmin values. ngspice's `DEVfetlim` (mosfet.c) and `DEVpnjlim` (diode.c) bound per-iteration `ΔVgs` and `ΔVds` based on the previous iterate. Port the equivalent into `BSIM4v7::limit_voltages`.

ngspice reference for `DEVfetlim` (paraphrased):
- `vgs_new` is clamped so `|vgs_new - vgs_old| ≤ max(0.5 * |vgs_old - Vth|, 0.5)`.
- `vds_new` is similarly clamped so it does not jump past `Vdsat` by more than a safe margin.

**Files:**
- Create: `tests/unit/test_newton_limiting.cpp`
- Modify: `src/devices/bsim4v7/bsim4v7.cpp` (the `limit_voltages` method — confirm its current form first)
- Modify: `tests/CMakeLists.txt` (add new source)

- [ ] **Step 1: Read the current `BSIM4v7::limit_voltages` implementation**

Open `src/devices/bsim4v7/bsim4v7.cpp`. Locate the `limit_voltages` method. If it is a stub or just copies `new_solution` to `solution`, confirm that in the report. Do not modify it yet.

- [ ] **Step 2: Write failing tests**

Create `tests/unit/test_newton_limiting.cpp`:

```cpp
#include <gtest/gtest.h>
#include "devices/bsim4v7/bsim4v7.hpp"
#include "core/circuit.hpp"
#include <vector>

using namespace neospice;

// A single BSIM4v7 device with an old solution where Vgs = 0.4V (at Vth)
// and a proposed new solution with Vgs = 2.0V (a 1.6V jump).  limit_voltages
// must clamp this to at most 0.5V per step (scaled by |Vgs_old - Vth|).
TEST(BSIM4v7Limit, LargeVgsStepClamped) {
    Circuit ckt;
    int gate   = ckt.node("g");
    int drain  = ckt.node("d");
    int source = ckt.node("s");
    int bulk   = ckt.node("b");
    ckt.finalize();

    BSIM4v7Params p{};
    p.VTH0 = 0.4;
    p.U0   = 0.04;
    p.TOXE = 2e-9;
    p.W    = 1e-6;
    p.L    = 100e-9;

    BSIM4v7 dev("m1", drain, gate, source, bulk, p);

    std::vector<double> old_sol(ckt.num_vars(), 0.0);
    old_sol[gate] = 0.4;  // Vgs = 0.4 (at Vth)
    old_sol[drain] = 0.1;
    old_sol[source] = 0.0;
    old_sol[bulk]   = 0.0;

    std::vector<double> new_sol = old_sol;
    new_sol[gate] = 2.0;  // proposed Vgs = 2.0 -> delta of 1.6V

    dev.limit_voltages(old_sol, new_sol);

    double delta = new_sol[gate] - old_sol[gate];
    EXPECT_LT(delta, 0.55);  // clamped, not the raw 1.6V
    EXPECT_GT(delta, 0.0);    // not reversed
}

// When the step is small (<= 0.5V), no clamping should occur.
TEST(BSIM4v7Limit, SmallVgsStepUnchanged) {
    Circuit ckt;
    int gate   = ckt.node("g");
    int drain  = ckt.node("d");
    int source = ckt.node("s");
    int bulk   = ckt.node("b");
    ckt.finalize();

    BSIM4v7Params p{};
    p.VTH0 = 0.4; p.U0 = 0.04; p.TOXE = 2e-9;
    p.W = 1e-6; p.L = 100e-9;

    BSIM4v7 dev("m1", drain, gate, source, bulk, p);

    std::vector<double> old_sol(ckt.num_vars(), 0.0);
    old_sol[gate] = 0.6;
    std::vector<double> new_sol = old_sol;
    new_sol[gate] = 0.8;  // 0.2V delta

    dev.limit_voltages(old_sol, new_sol);

    EXPECT_NEAR(new_sol[gate], 0.8, 1e-12);  // unchanged
}
```

Register in `tests/CMakeLists.txt` alphabetically.

- [ ] **Step 3: Run tests, expect them to fail**

```bash
cmake --build build -j$(nproc)
./build/tests/neospice_tests --gtest_filter='BSIM4v7Limit.*'
```

Expected: `LargeVgsStepClamped` FAILS (delta is 1.6, not clamped). `SmallVgsStepUnchanged` PASSES (no clamping in current code).

- [ ] **Step 4: Implement ngspice-style step limiting**

In `src/devices/bsim4v7/bsim4v7.cpp::limit_voltages`, replace the body with (adapt signatures to the actual method):

```cpp
void BSIM4v7::limit_voltages(const std::vector<double>& old_sol,
                             std::vector<double>& new_sol) const {
    double vg_old = (gate_   < 0 ? 0.0 : old_sol[gate_]);
    double vs_old = (source_ < 0 ? 0.0 : old_sol[source_]);
    double vd_old = (drain_  < 0 ? 0.0 : old_sol[drain_]);
    double vg_new = (gate_   < 0 ? 0.0 : new_sol[gate_]);
    double vs_new = (source_ < 0 ? 0.0 : new_sol[source_]);
    double vd_new = (drain_  < 0 ? 0.0 : new_sol[drain_]);

    double vgs_old = vg_old - vs_old;
    double vds_old = vd_old - vs_old;
    double vgs_new = vg_new - vs_new;
    double vds_new = vd_new - vs_new;

    // DEVfetlim-style Vgs clamp (ngspice mosfet.c).
    double vth = params_.VTH0;  // approximate; actual Vth depends on Vbs, OK for limiting
    double vgs_clamped = fetlim_vgs(vgs_new, vgs_old, vth);
    double vds_clamped = fetlim_vds(vds_new, vds_old);

    // Re-distribute the clamped Vgs/Vds back to absolute node voltages.
    // Keep source unchanged; move gate/drain.
    if (gate_  >= 0) new_sol[gate_]  = vs_old + vgs_clamped;
    if (drain_ >= 0) new_sol[drain_] = vs_old + vds_clamped;
}

// Helpers modelled on ngspice DEVfetlim / DEVlimvds.
static double fetlim_vgs(double vgs_new, double vgs_old, double vth) {
    double delta = vgs_new - vgs_old;
    double limit = std::max(0.5, 0.5 * std::abs(vgs_old - vth));
    if (std::abs(delta) > limit) {
        return vgs_old + (delta > 0 ? limit : -limit);
    }
    return vgs_new;
}

static double fetlim_vds(double vds_new, double vds_old) {
    double delta = vds_new - vds_old;
    if (std::abs(delta) > 0.5) {
        return vds_old + (delta > 0 ? 0.5 : -0.5);
    }
    return vds_new;
}
```

(Adjust member names if the class uses different field names like `g_node_`, `d_node_`.)

- [ ] **Step 5: Run tests**

```bash
cmake --build build -j$(nproc)
./build/tests/neospice_tests --gtest_filter='BSIM4v7Limit.*'
```

Expected: both tests PASS.

- [ ] **Step 6: Run full suite, check for regression**

```bash
ctest --test-dir build --output-on-failure
```

Expected: 107/107 enabled tests still pass. NMOS_DC_IV must still converge within tolerance 5.0.

- [ ] **Step 7: Commit**

```bash
git add src/devices/bsim4v7/bsim4v7.cpp tests/unit/test_newton_limiting.cpp tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(bsim4v7): port DEVfetlim/DEVlimvds-style Newton step limiting

MOSFET Vgs/Vds steps are clamped per Newton iteration so that
|ΔVgs| ≤ max(0.5, 0.5·|Vgs_old - Vth|) and |ΔVds| ≤ 0.5. This breaks
the two-state limit cycles diagnosed in M3 where Newton oscillated
between overshoot states (e.g. CMOS inverter out=1.27V ↔ 1.90V).
EOF
)"
```

---

### Task 3: Port VACLM / VADIBL into gds (TDD)

**Rationale:** The residual ~3.65× NMOS_DC_IV error (1.84× at the CMOS operating point) is output-resistance-limited: our model has too little `gds` in saturation, so the transistor conducts too much current for a given Vds. ngspice's b4v7ld.c:1957-2040 computes two Early-voltage contributions: `VACLM` (from channel-length modulation) and `VADIBL` (from DIBL). Port both into the existing `gds` computation in `bsim4v7_eval.cpp`.

**Files:**
- Create: `tests/unit/test_bsim4v7_vaclm.cpp`
- Modify: `src/devices/bsim4v7/bsim4v7_eval.cpp` (in the CLM/gds region, currently ~lines 151-204)
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Read ngspice reference**

Read `/home/subhagato/Codes/ngspice/src/spicelib/devices/bsim4v7/b4v7ld.c` lines 1957 through 2040. Copy the VACLM and VADIBL formulas into the task notes here (or inline into the implementation comments). Specifically:

```c
// VACLM (b4v7ld.c:1957-1989):
//   VACLM = Abulk · Esat · Leff / PCLM · ( (Vgst_eff + 2·Vt) / Abulk ) · (Vds - Vdseff) / (Esat·Leff + Vgst_eff + 2·Vt)
// VADIBL (b4v7ld.c:1991-2040):
//   VADIBL = (Vgst_eff + 2·Vt) / (PDIBLC1 · (1 + PDROUT·Leff/λt) + PDIBLC2) · (1 + PVAG·Vgst/Esat/Leff) · ...
// Effective Early voltage: 1/Va = 1/VACLM + 1/VADIBL
// gds_early = Ids / Va
```

- [ ] **Step 2: Write a failing test**

Create `tests/unit/test_bsim4v7_vaclm.cpp`:

```cpp
#include <gtest/gtest.h>
#include "devices/bsim4v7/bsim4v7_eval.hpp"
using namespace neospice;

// In saturation (Vds > Vdsat), gds should be Ids / Va where Va is
// finite (~5-20V). Without VACLM/VADIBL our gds is near zero and
// the Ids/gds ratio blows up.
TEST(BSIM4v7VACLM, SaturationGdsReasonable) {
    BSIM4v7Params p{};
    p.VTH0 = 0.4; p.U0 = 0.04; p.TOXE = 2e-9;
    p.W = 1e-6; p.L = 100e-9; p.PCLM = 1.3;
    p.PDIBLC1 = 0.39; p.PDIBLC2 = 0.0086; p.DROUT = 0.56;
    p.VSAT = 1e5; p.DELTA = 0.01;

    auto r = bsim4v7_evaluate(1.0, 1.8, 0.0, p, 300.0);

    EXPECT_GT(r.Ids, 1e-5);
    EXPECT_GT(r.gds, 0.0);
    // Va = Ids/gds should be in (1, 50) V for a 100nm device.
    double Va = r.Ids / r.gds;
    EXPECT_GT(Va, 1.0);
    EXPECT_LT(Va, 50.0);
}
```

Register in `tests/CMakeLists.txt`.

- [ ] **Step 3: Run to verify it fails**

```bash
cmake --build build -j$(nproc)
./build/tests/neospice_tests --gtest_filter='BSIM4v7VACLM.*'
```

Expected: FAILS — Va currently is astronomical because `gds` comes only from FD and is near zero in saturation.

- [ ] **Step 4: Port VACLM and VADIBL**

In `src/devices/bsim4v7/bsim4v7_eval.cpp`, after the CLM block (current line 154) and before the FD block (current line 164), add the VACLM/VADIBL computation following the ngspice reference. Write directly into a `Va` local that is then folded into `r.gds`:

```cpp
    // --- Early-voltage output resistance (ngspice b4v7ld.c:1957-2040) ---
    double VACLM = 0.0;
    if (p.PCLM > 0.0 && Vds > Vdsat) {
        double T0 = Vds - Vdsat;
        VACLM = Abulk * EsatL / p.PCLM * ( (Vgst_eff + 2.0 * Vt) / Abulk ) * T0 / (EsatL + Vgst_eff + 2.0 * Vt);
    }
    double VADIBL = 0.0;
    if (p.PDIBLC1 > 0.0 || p.PDIBLC2 > 0.0) {
        double thetaRout = p.PDIBLC1 * (1.0 + p.DROUT * Leff / (1e-6)) + p.PDIBLC2;  // 1e-6 is a coarse λ_t; refine if test fails
        if (thetaRout > 1e-18)
            VADIBL = (Vgst_eff + 2.0 * Vt) / thetaRout;
    }
    double Va_inv = 0.0;
    if (VACLM  > 1e-12) Va_inv += 1.0 / VACLM;
    if (VADIBL > 1e-12) Va_inv += 1.0 / VADIBL;
    double Va = (Va_inv > 1e-18) ? 1.0 / Va_inv : 1e18;
    double gds_early = Ids / Va;
    // Fold gds_early into r.gds AFTER the FD block below completes.
```

Then, where `r.gds = Ids_dVd * CLM;` is assigned, adjust to `r.gds = Ids_dVd * CLM + gds_early;`. Make sure the line ordering keeps `Va`/`gds_early` in scope.

Confirm the signs are correct: for PMOS the eval already flips inputs, so `Ids > 0` at the eval level; VACLM/VADIBL are positive; `gds_early > 0` is the desired direction.

- [ ] **Step 5: Run tests**

```bash
cmake --build build -j$(nproc)
./build/tests/neospice_tests --gtest_filter='BSIM4v7VACLM.*'
./build/tests/neospice_tests --gtest_filter='NgspiceCompareTest.NMOS_DC_IV'
```

Expected: `BSIM4v7VACLM.SaturationGdsReasonable` PASSES. `NMOS_DC_IV` still passes, and the worst_error should drop significantly (expected: from 3.65× to below 2×). Record the new worst_error.

- [ ] **Step 6: Commit**

```bash
git add src/devices/bsim4v7/bsim4v7_eval.cpp tests/unit/test_bsim4v7_vaclm.cpp tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(bsim4v7): port VACLM + VADIBL Early-voltage contributions to gds

Both contributions enter gds as Ids/Va with 1/Va = 1/VACLM + 1/VADIBL.
Closes a significant fraction of the M2.5 NMOS_DC_IV gap (3.65×) by
giving the transistor realistic output resistance in saturation.
EOF
)"
```

---

### Task 3b: Fix DIBL Vth-shift to use bounded theta0vb0 (TDD)

**Rationale:** T4's initial attempt (Sonnet, BLOCKED) uncovered a physics bug that predates M3.5: `src/devices/bsim4v7/bsim4v7_eval.cpp:33` adds `-p.DSUB * Vds_clamped` as if DSUB were a second linear Vth-shift coefficient. It is not. In BSIM4 (ngspice b4v7temp.c:1446-1456 and b4v7ld.c:1145-1168), **DSUB only shapes the exponential decay of a precomputed factor `theta0vb0`**, and the real Vth-shift from DIBL is:

```
DIBL_Sft = (ETA0 + ETAB·Vbs) · theta0vb0 · Vds
```

Our current line effectively double-counts DIBL. With BSIM4 defaults `ETA0=0.08`, `DSUB=0.56`, the existing code produces `-0.64·Vds` as the DIBL-Vth shift; at `Vds=1.8V` on the CMOS inverter NMOS, the shift is `−1.15 V`, driving `Vth_eff = 0.4 − 1.15 = −0.75 V`. NMOS conducts ~1 mA at `Vgs=0`; the DC equilibrium becomes an unstable fixed point and Newton diverges. ngspice's `theta0vb0` is exponentially small for `Leff >> litl`-scale, so its `ETA0·theta0vb0·Vds` is bounded by what the device physics actually supports.

This task ports the bounded form. Expected outcome: CMOS inverter NMOS-off state recovers (`Vth_eff` stays positive at `Vgs=0`, `Vds=1.8V`), which unblocks T4/T5.

**Files:**
- Create: `tests/unit/test_bsim4v7_dibl_clamp.cpp`
- Modify: `src/devices/bsim4v7/bsim4v7_eval.cpp` (Vth assembly, lines 27-33)
- Modify: `tests/CMakeLists.txt`

**ngspice reference (paraphrased, the exact form to port):**
```c
// In b4v7temp.c (precomputed per device):
//   tmp          = sqrt(EPSSUB/EPSOX · TOXE · Xdep0)       // litl-like length
//   T0           = DSUB · Leff / tmp
//   if (T0 < EXP_THRESHOLD):
//       T1 = exp(T0); T2 = T1 - 1; T4 = T2² + 2·T1·MIN_EXP
//       theta0vb0 = T1 / T4
//   else:
//       theta0vb0 = 1 / (MAX_EXP - 2)
//
// In b4v7ld.c:1145-1155 (per evaluate call):
//   T3       = ETA0 + ETAB · Vbs
//   DIBL_Sft = T3 · theta0vb0 · Vds
//   Vth     -= DIBL_Sft
```

We adopt the same `EXP_THRESHOLD = 34.0` guard already used for VADIBL in T3.

- [ ] **Step 1: Write the failing test**

Create `tests/unit/test_bsim4v7_dibl_clamp.cpp`:

```cpp
#include <gtest/gtest.h>
#include "devices/bsim4v7/bsim4v7_eval.hpp"
#include <cmath>

using namespace neospice;

// With BSIM4 defaults DSUB=0.56, ETA0=0.08 and Leff=100nm, theta0vb0 is
// small enough that the DIBL Vth-shift at Vds=1.8V cannot exceed ~0.2V.
// The device must remain OFF at Vgs=0, Vds=1.8V (Ids < 1 nA).
TEST(BSIM4v7DIBLClamp, NmosOffAtVgsZeroHighVds) {
    BSIM4v7Params p{};
    p.VTH0  = 0.4;
    p.U0    = 0.04;
    p.TOXE  = 2e-9;
    p.W     = 1e-6;
    p.L     = 100e-9;
    p.nf    = 1.0;
    p.K1    = 0.5;
    p.NFACTOR = 1.0;
    p.NDEP  = 1.7e17;
    p.XJ    = 1.5e-7;
    p.A0    = 1.0;
    p.PCLM  = 1.3;
    p.ETA0  = 0.08;
    p.DSUB  = 0.56;

    auto r = bsim4v7_evaluate(0.0, 1.8, 0.0, p, 300.0);

    // Before the fix: Vth_eff ~ -0.75V, Ids > 1e-4 A (NMOS strongly on).
    // After the fix: Vth_eff > 0.15V, subthreshold leakage only.
    EXPECT_LT(r.Ids, 1e-9) << "NMOS should be OFF at Vgs=0, Vds=1.8V";
}

// At Vds=0, DIBL shift must vanish regardless of DSUB, ETA0.
TEST(BSIM4v7DIBLClamp, NoDIBLShiftAtZeroVds) {
    BSIM4v7Params p{};
    p.VTH0 = 0.4; p.U0 = 0.04; p.TOXE = 2e-9;
    p.W = 1e-6; p.L = 100e-9; p.nf = 1.0;
    p.K1 = 0.5; p.NFACTOR = 1.0; p.NDEP = 1.7e17; p.XJ = 1.5e-7;
    p.ETA0 = 0.08; p.DSUB = 0.56;

    // At Vds ≈ 0 and Vgs = VTH0, Ids should be a small strong-inversion
    // linear-region current (~nA-to-µA), not the runaway subthreshold
    // current that would indicate Vth was shifted by DIBL.
    auto r = bsim4v7_evaluate(0.4, 1e-4, 0.0, p, 300.0);
    EXPECT_LT(r.Ids, 1e-5);
}
```

Register in `tests/CMakeLists.txt` alphabetically.

- [ ] **Step 2: Build and run — expect `NmosOffAtVgsZeroHighVds` to FAIL**

```bash
cmake --build build -j$(nproc)
./build/tests/neospice_tests --gtest_filter='BSIM4v7DIBLClamp.*'
```

Expected: `NmosOffAtVgsZeroHighVds` FAILS with `r.Ids` at milliamp level (~1e-3).

- [ ] **Step 3: Port theta0vb0 and replace the DSUB term**

In `src/devices/bsim4v7/bsim4v7_eval.cpp`, locate the Vth assembly (around lines 27-33):

```cpp
    double sqrtPhis = std::sqrt(std::max(0.4, 0.4 - Vbs));
    double Vds_clamped = std::max(0.0, std::min(Vds, 5.0));
    double Vth = p.VTH0 + p.K1 * sqrtPhis - p.K2 * Vbs
                 - p.ETA0 * Vds_clamped - p.DSUB * Vds_clamped;
```

Replace with:

```cpp
    double sqrtPhis = std::sqrt(std::max(0.4, 0.4 - Vbs));
    double Vds_clamped = std::max(0.0, std::min(Vds, 5.0));

    // --- DIBL Vth-shift (ngspice b4v7temp.c:1446-1456 + b4v7ld.c:1145-1155) ---
    // theta0vb0 shapes the exponential decay of the DIBL contribution as a
    // function of Leff/litl_like. DSUB is the exponent coefficient; it does
    // NOT enter Vth linearly.
    double sqrtPhi = std::sqrt(0.4);  // 2*phi_s at zero bias, simplified
    double Xdep0 = std::sqrt(2.0 * EPSSUB * 0.4 / (Q_ELEC * p.NDEP * 1.0e6))
                 * sqrtPhi;
    double tmp_dibl = std::sqrt(EPSSUB / EPSOX * p.TOXE * Xdep0);
    double theta0vb0;
    if (tmp_dibl > 1e-30) {
        double T0_dibl = p.DSUB * Leff / tmp_dibl;
        if (T0_dibl < 34.0) {
            double T1_dibl = std::exp(T0_dibl);
            double T2_dibl = T1_dibl - 1.0;
            // MIN_EXP ≈ exp(-34); T4 = (T1-1)² + 2·T1·MIN_EXP keeps the
            // denominator finite for small T0_dibl.
            double T4_dibl = T2_dibl * T2_dibl + 2.0 * T1_dibl * 1.713908e-15;
            theta0vb0 = T1_dibl / T4_dibl;
        } else {
            theta0vb0 = 0.0;  // exponentially small
        }
    } else {
        theta0vb0 = 0.0;
    }
    double DIBL_Sft = p.ETA0 * theta0vb0 * Vds_clamped;

    double Vth = p.VTH0 + p.K1 * sqrtPhis - p.K2 * Vbs - DIBL_Sft;
```

Note: the existing `-p.DSUB * Vds_clamped` term is **deleted**, not replaced — DSUB is now consumed only inside `theta0vb0`. The `-p.ETA0 * Vds_clamped` is also replaced by the bounded `DIBL_Sft` expression.

- [ ] **Step 4: Run unit tests — expect PASS**

```bash
cmake --build build -j$(nproc)
./build/tests/neospice_tests --gtest_filter='BSIM4v7DIBLClamp.*:BSIM4v7VACLM.*:BSIM4v7Subthreshold.*'
```

Expected: all PASS. The three existing BSIM4v7 unit test families must keep passing — if `BSIM4v7Subthreshold` or `BSIM4v7VACLM` regress, the new `DIBL_Sft` is mis-computed.

- [ ] **Step 5: Run full ngspice-comparison suite — watch NMOS_DC_IV**

```bash
./build/tests/neospice_tests --gtest_filter='NgspiceCompareTest.NMOS_DC_IV'
ctest --test-dir build --output-on-failure
```

Expected:
- `NMOS_DC_IV` still passes within tolerance 5.0. **Record the new `worst_error`** — with DIBL no longer double-counted, it should drop further (T6 may now have more headroom).
- 112/112 enabled tests still pass.

- [ ] **Step 6: Commit**

```bash
git add src/devices/bsim4v7/bsim4v7_eval.cpp tests/unit/test_bsim4v7_dibl_clamp.cpp tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
fix(bsim4v7): port bounded DIBL Vth-shift using theta0vb0

Previous code applied DSUB as a second linear Vth coefficient
(-DSUB*Vds). BSIM4 actually uses DSUB only to shape the exponential
decay of a precomputed factor theta0vb0, with the real Vth shift
being ETA0*theta0vb0*Vds (ngspice b4v7temp.c:1446-1456,
b4v7ld.c:1145-1155). The bug made NMOS conduct at Vgs=0 when Vds
was large, making the CMOS inverter operating point unstable under
Newton. Unblocks M3.5 T4 / T5.
EOF
)"
```

---

### Task 4: Re-enable CMOSInverterTransient

Same structure as M3 T3. After Tasks 1-3 land, run:

```bash
./build/tests/neospice_tests --gtest_filter='NgspiceCompareTest.DISABLED_CMOSInverterTransient' --gtest_also_run_disabled_tests
```

Three branches as before:
- **(a) PASSES**: re-enable directly.
- **(b) FAILS with tolerance**: relax at most 2×; if insufficient, escalate.
- **(c) FAILS with ConvergenceError**: add `.nodeset V(out)=1.8` to the circuit, re-run.

(Full task text follows the M3 Task 3 template — omitted here for brevity; re-use it.)

---

### Task 5: Re-enable RingOscillator5Stage

Same structure as M3 T4, same branching. The circuit already has `.ic` hints; Task 1's verbose flag helps debug if convergence still fails.

(Full task text follows the M3 Task 4 template — omitted here for brevity; re-use it.)

---

### Task 6: Tighten NMOS_DC_IV tolerance

After VACLM/VADIBL land (Task 3), run `NMOS_DC_IV`, record worst_error, tighten tolerance in `tests/unit/test_ngspice_compare.cpp` if there is ≥ 10% headroom.

---

## Self-Review Checklist

Run this yourself after writing the plan.

1. **Spec coverage.** M3 diagnosis identified (a) Newton step overshoot and (b) residual physics gap. Task 1 adds verbose hook. Task 2 adds step-limiting. Task 3 ports VACLM/VADIBL. Tasks 4-6 re-enable + re-tighten. Both root causes addressed.

2. **Placeholder scan.** Tasks 4 and 5 reference the M3 Task 3/4 templates rather than repeating them verbatim — a judgment call that trades plan brevity for a re-read burden. Flag: if the executing agent has trouble finding the M3 template, copy it inline before dispatching.

3. **Type consistency.** The VACLM/VADIBL snippet uses `p.PCLM`, `p.PDIBLC1`, `p.PDIBLC2`, `p.DROUT`, `Vgst_eff`, `Vt`, `EsatL`, `Abulk`, `Vdsat`, `Vds` — all names already present in `bsim4v7_eval.cpp` from M2.5. `fetlim_vgs`, `fetlim_vds` are new static helpers. The `BSIM4v7::limit_voltages` method signature should be verified against the current class definition before implementation.

4. **Risks.** The VACLM `thetaRout` formula uses `λ_t ≈ 1e-6` as a placeholder — ngspice computes `λ_t` from `TOXE`, `EPSSUB`, `XDEP`. If Task 3 Step 5's worst_error doesn't drop, revisit the `λ_t` expression before giving up.

---

## Execution Handoff

After writing, offer subagent-driven vs inline. Given the physics risk (VACLM/VADIBL port can easily regress NMOS_DC_IV), Opus is the right default for Tasks 2 and 3.
