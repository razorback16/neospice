# BSIM4v7 UCB Z-Port — Phase 1b (Load path + Device wire-up)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Translate `b4ld.c` (the per-timestep residual/Jacobian loader) into `bsim4v7_load.cpp`, extend the neospice `Device`/`Shim::Ckt` interfaces with the state-vector and integrator-coefficient machinery `BSIM4load` requires, ship a `BSIM4v7Device` adapter that plugs the UCB kernel into the simulator, wire the parser M-card to it, and re-enable the three parked MOSFET tests (`NMOS_DC_IV`, `CMOSInverterTransient`, `RingOscillator5Stage`).

**Architecture:**
- Keep the Phase-1a mechanical-translation discipline. `b4ld.c` (5601 lines, one giant `BSIM4LoadOMP` body + helpers `BSIM4polyDepletion` and `BSIM4LoadRhsMat`) is translated verbatim to `bsim4v7_load.cpp` with only the same syntactic substitutions Phase-1a used: `GENmodel* → BSIM4v7Model*`, `GENinstance* → BSIM4v7Instance*`, `CKTcircuit* → Shim::Ckt*`, UCB field names preserved, constants duplicated. The single new rewrite is `*(inst->BSIM4xPtr) += v` → `mat.add(inst->BSIM4xPtr, v)` because our `BSIM4xPtr` fields are already `MatrixOffset` (Task 4 of Phase 1a), not `double*`.
- `Shim::Matrix` keeps its two-phase identity. Phase-1a currently returns `0` as a sentinel from `make_elt`; Task 4 of this plan replaces that sentinel with a lookup against the finalized `SparsityPattern` so offsets held on the instance are real matrix indices the translated `BSIM4load` can stamp into directly.
- `Shim::Ckt` grows just enough to serve the translated code: 3-buffer state ring (`CKTstate0/1/2`), integrator coefficient array (`CKTag[0..7]`), full `CKTmode` bit vocabulary, `CKTrhs` / `CKTrhsOld` pointers, `CKTdelta`, `CKTdeltaOld[8]`, `CKTorder`. These are filled by the Simulator each Newton iteration before calling `BSIM4v7Device::evaluate`, and read by the translated code verbatim.
- `BSIM4v7Device : public neospice::Device` owns one `BSIM4v7Instance`, references a shared `BSIM4v7Model`, and adapts the neospice `Device` API to the UCB setup→temp→load pipeline. It is the only file in `src/devices/bsim4v7/` that is *not* a mechanical translation — it is the boundary.
- The parser gains an `M` handler that (a) looks up the named `.model` card, (b) dispatches through `BSIM4param` / `BSIM4mParam` to populate instance+model, and (c) adds a `BSIM4v7Device` to the circuit. LEVEL=14 is the Z-port's model level; other LEVELs remain unsupported in this milestone.

**Tech Stack:** C++17, CMake, GoogleTest, KLU sparse solver, UCB BSIM 4.7.0.

**Source inventory:**

| UCB file | LOC | Output | Task |
|---|---|---|---|
| `b4ld.c` | 5601 | `bsim4v7_load.cpp` | 6 |
| (new) | ~220 | `bsim4v7_device.hpp` / `bsim4v7_device.cpp` | 7 |
| (modify) | — | `bsim4v7_shim.hpp`, `bsim4v7_shim.cpp` | 2 |
| (modify) | — | `bsim4v7_setup.cpp` | 4 |
| (modify) | — | `src/devices/device.hpp` | 3 |
| (modify) | — | `src/core/circuit.{hpp,cpp}`, `src/core/transient.cpp`, `src/core/dc.cpp` | 5 |
| (modify) | — | `src/parser/netlist_parser.cpp`, `src/parser/model_cards.{hpp,cpp}` | 8 |

**Out of scope (defer to Phase 2):** `b4acld.c` (AC small-signal — independent code path), `b4trunc.c` (LTE estimator for adaptive transient — our Gear controller has its own), `b4cvtest.c`, `b4noi.c`, `b4pzld.c`. AC analysis of MOSFETs remains unsupported after this milestone. Transient still works because `BSIM4load` handles the BE/TRAP/Gear stamping itself via `CKTag[0]`.

---

## Mechanical translation rules (inherited from Phase 1a)

All 15 rules from `2026-04-16-milestone4-bsim4-ucb-z-port-phase1a.md` apply unchanged. The additions specific to `b4ld.c`:

16. **RHS stamping**: UCB writes `*(ckt->CKTrhs + here->BSIM4dNodePrime) -= ceqdrn;`. In the port, `ckt->CKTrhs` is a `double*` set by the caller to `&rhs[0]` before every load call, so the line translates verbatim. Same for `ckt->CKTrhsOld` (which UCB reads as the previous Newton iterate — we point it at `voltages[]` the simulator passes to `evaluate`).
17. **Matrix stamping**: Every `*(here->BSIM4DdPtr) += v;` → `mat.add(here->BSIM4DdPtr, v);`. `BSIM4DdPtr` is already a `MatrixOffset` in our port. Because `mat.add` is a member of `neospice::NumericMatrix` and `here` lives in the `neospice::bsim4v7` namespace, the translated call site references a shim-provided `mat` reference placed on `Shim::Ckt` or passed as a parameter — Task 2 adds a `Shim::Ckt::mat` field bound by the device adapter just before calling `BSIM4load`.
18. **State array indexing**: `*(ckt->CKTstate0 + here->BSIM4vds)` stays verbatim. `CKTstate0/1/2` are `double*` in `Shim::Ckt`; the adapter binds them to the circuit's state buffers.
19. **Integrator coefficients**: `ckt->CKTag[0]` is used to scale dQ/dV terms into the matrix. For BE it is `1/h`, for trapezoidal `2/h`, for Gear BDF-2 `3/(2h)`. Our `TimeStepController` already computes these scalings for the capacitor/inductor devices; Task 5 exposes them through `Shim::Ckt::CKTag[]`.
20. **Bypass (`MODEBYPASS`)**: UCB supports a convergence-bypass shortcut if ΔV is tiny. For the first port, leave the `ByPass` branch in the translated code intact — it checks `CKTmode & MODEBYPASS` which our simulator never sets, so the fast path is dead code. **Do not** rip the ByPass branch out; the rule is mechanical translation and dead branches stay.
21. **PREDICTOR macro**: UCB's `b4ld.c` is compiled with `#define PREDICTOR` as the ngspice default. At the top of `bsim4v7_load.cpp` add `#define PREDICTOR` so all `#ifndef PREDICTOR` branches are elided the same way ngspice does. Matches Phase-1a's `#define NEWCONV` policy.
22. **USE_OMP**: UCB wraps the outer model/instance loops with `#ifdef USE_OMP`. **Do not** define `USE_OMP` in the C++ port. The non-OMP path at `b4ld.c:100-101` (`BSIM4model *model = (BSIM4model*)inModel; BSIM4instance *here;`) is the one we take; translate that branch only (per rule 3 — stripping ngspice build-system features). Document this as the single deliberate rule-6 deviation in the commit message.

---

## File structure

```
src/devices/bsim4v7/
    bsim4v7_load.cpp       (NEW — from b4ld.c, ~5600 lines)
    bsim4v7_device.hpp     (NEW — adapter class)
    bsim4v7_device.cpp     (NEW — adapter impl)
    bsim4v7_shim.hpp       (MODIFIED — CKTstate/CKTag/CKTrhs/CKTmode flags)
    bsim4v7_shim.cpp       (MODIFIED — helpers if needed)
    bsim4v7_setup.cpp      (MODIFIED — two-phase TSTALLOC resolution)

src/devices/
    device.hpp             (MODIFIED — state_vars, set_state_ptrs)

src/core/
    circuit.{hpp,cpp}      (MODIFIED — state buffer allocation)
    transient.cpp          (MODIFIED — rotate state buffers, set CKTag)
    dc.cpp                 (MODIFIED — set CKTmode MODEDC / MODEINITJCT)

src/parser/
    netlist_parser.cpp     (MODIFIED — M-card handler)
    model_cards.{hpp,cpp}  (MODIFIED — LEVEL=14 → BSIM4v7 dispatch)

tests/unit/
    test_bsim4v7_ucb_load.cpp   (NEW — Task 9: DC op-point golden)
    test_ngspice_compare.cpp    (MODIFIED — un-skip / un-disable three tests)
```

---

## Task 1: Branch bookkeeping and scope announcement

**Files:**
- None (git operations only)

- [ ] **Step 1: Verify Phase 1a is shipped at head**

```bash
git log --oneline -5
git tag --list "m4-phase1a-complete"
```

Expected: the tag exists and HEAD is the `m4-phase1a-complete` commit (or a descendant). If HEAD has drifted, re-base this work onto `m4-phase1a-complete`.

- [ ] **Step 2: Confirm baseline test state**

```bash
cmake --build build -j 2>&1 | tail -5
ctest --test-dir build -j --output-on-failure 2>&1 | tail -10
```

Expected: all 102 tests pass, 1 skipped (`NMOS_DC_IV`), 2 disabled (`DISABLED_CMOSInverterTransient`, `DISABLED_RingOscillator5Stage`). Record the PASS count; we will assert no regression at Task 12.

- [ ] **Step 3: No worktree branching**

Phase 1a landed on `main`. This milestone continues on `main`. If the user explicitly requests a worktree, use `superpowers:using-git-worktrees` and revise this step — otherwise proceed on `main`.

---

## Task 2: Extend `Shim::Ckt` for load-path fields

**Files:**
- Modify: `src/devices/bsim4v7/bsim4v7_shim.hpp`
- Modify: `src/devices/bsim4v7/bsim4v7_shim.cpp`

The translated `BSIM4load` reads 4 families of fields from `CKTcircuit` that the Phase-1a `Shim::Ckt` does not yet provide. Add them here so Task 6's translation compiles without stubs.

- [ ] **Step 1: Write a failing smoke test**

Create `tests/unit/test_bsim4v7_shim_load_fields.cpp`:

```cpp
#include <gtest/gtest.h>
#include "devices/bsim4v7/bsim4v7_shim.hpp"

using namespace neospice::bsim4v7;

TEST(BSIM4v7ShimCkt, HasLoadPathFields) {
    Shim::Ckt ckt;
    // State ring
    EXPECT_EQ(nullptr, ckt.CKTstate0);
    EXPECT_EQ(nullptr, ckt.CKTstate1);
    EXPECT_EQ(nullptr, ckt.CKTstate2);
    // Integrator coeffs (8 slots per ngspice)
    EXPECT_EQ(0.0, ckt.CKTag[0]);
    EXPECT_EQ(0.0, ckt.CKTag[7]);
    // RHS pointers (bound by the device adapter per evaluate)
    EXPECT_EQ(nullptr, ckt.CKTrhs);
    EXPECT_EQ(nullptr, ckt.CKTrhsOld);
    // Matrix binding (null until adapter binds)
    EXPECT_EQ(nullptr, ckt.mat);
    // Order (Gear order; 1 = BE, 2 = trap/Gear2)
    EXPECT_EQ(1, ckt.CKTorder);
    // MODE flag values mirror UCB bit layout
    EXPECT_EQ(0x1,   Shim::MODEDC);
    EXPECT_EQ(0x2,   Shim::MODEAC);
    EXPECT_EQ(0x20,  Shim::MODETRAN);
    EXPECT_NE(0,     Shim::MODEINITJCT);
    EXPECT_NE(0,     Shim::MODEINITFIX);
    EXPECT_NE(0,     Shim::MODEINITTRAN);
    EXPECT_NE(0,     Shim::MODEINITPRED);
    EXPECT_NE(0,     Shim::MODEINITSMSIG);
    EXPECT_NE(0,     Shim::MODEUIC);
    EXPECT_NE(0,     Shim::MODEDCTRANCURVE);
    EXPECT_NE(0,     Shim::MODETRANOP);
}
```

Add to `tests/CMakeLists.txt` next to `test_bsim4v7_ucb_setup.cpp`.

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build build --target neospice_tests -j
ctest --test-dir build -R BSIM4v7ShimCkt --output-on-failure
```

Expected: compile error — the fields don't exist.

- [ ] **Step 3: Extend `bsim4v7_shim.hpp`**

In the `Shim::Ckt` struct, replace the Phase-1a stub block (currently around lines 70-78) with the full set:

```cpp
struct Ckt {
    double CKTtemp       = 300.15;
    double CKTnomTemp    = 300.15;
    double CKTgmin       = 1e-12;
    double CKTreltol     = 1e-3;
    double CKTabstol     = 1e-12;
    double CKTvoltTol    = 1e-6;
    int    CKTmode       = 0;
    int    CKTbadMos3    = 0;
    int    CKTnumStates  = 0;

    // Transient integrator state
    double  CKTdelta        = 0.0;
    double  CKTdeltaOld[8]  = {};   // UCB uses indices [0..5]
    double  CKTag[8]        = {};   // integrator coefficients (UCB reads [0..1])
    int     CKTorder        = 1;

    // State vector ring (bound by the device adapter each load call).
    // Length = total state size across all devices; indexed by inst->BSIM4states + offset.
    double *CKTstate0 = nullptr;
    double *CKTstate1 = nullptr;
    double *CKTstate2 = nullptr;

    // Residual / previous-iterate pointers (bound by adapter each load call).
    double *CKTrhs    = nullptr;
    double *CKTrhsOld = nullptr;

    // Matrix binding (bound by adapter each load call). UCB uses ckt->CKTmatrix
    // indirectly; our translated code stamps through mat directly.
    neospice::NumericMatrix *mat = nullptr;

    // Internal node registrar
    int CKTinternalNodeCounter = 1000;
    int add_internal_node(const char *name);
};
```

Below the struct, add the mode flag constants (bit values copied from `$NGSPICE_DIR/src/include/ngspice/cktdefs.h` lines 164-184):

```cpp
namespace Shim {
    constexpr int MODE             = 0x3;        // AC | TRAN mask
    constexpr int MODETRAN         = 0x1;
    constexpr int MODEAC           = 0x2;
    constexpr int MODEDC           = 0x70;       // DCOP | TRANOP | DCTRANCURVE
    constexpr int MODEDCOP         = 0x10;
    constexpr int MODETRANOP       = 0x20;
    constexpr int MODEDCTRANCURVE  = 0x40;
    constexpr int INITF            = 0x3f00;     // MODEINIT* mask
    constexpr int MODEINITFLOAT    = 0x100;
    constexpr int MODEINITJCT      = 0x200;
    constexpr int MODEINITFIX      = 0x400;
    constexpr int MODEINITSMSIG    = 0x800;
    constexpr int MODEINITTRAN     = 0x1000;
    constexpr int MODEINITPRED     = 0x2000;
    constexpr int MODEUIC          = 0x10000;
    constexpr int MODEBYPASS       = 0x1000000;  // not in ngspice CKTmode; kept for translated code compat
} // namespace Shim
```

These must match ngspice's bit layout exactly — the test's `EXPECT_EQ(0x1, Shim::MODETRAN)` etc. assert this. If any value disagrees with `$NGSPICE_DIR/src/include/ngspice/cktdefs.h`, update both the constant and the test.

- [ ] **Step 4: Run to verify the test passes**

```bash
cmake --build build --target neospice_tests -j
ctest --test-dir build -R BSIM4v7ShimCkt --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/devices/bsim4v7/bsim4v7_shim.hpp \
        tests/unit/test_bsim4v7_shim_load_fields.cpp \
        tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(bsim4v7/shim): extend Shim::Ckt with load-path fields

Adds the state-vector ring (CKTstate0/1/2), integrator coefficients
(CKTag[8]), RHS pointers (CKTrhs/CKTrhsOld), matrix binding (mat),
integration order (CKTorder), and the full UCB CKTmode flag vocabulary
(MODEDC/MODEAC/MODETRAN/MODEINIT*/etc.).

Values are null/zero by default; the BSIM4v7Device adapter (Task 7)
binds CKTstate*, CKTrhs*, mat, CKTag, and CKTmode per evaluate() call.
Bit values mirror ngspice cktdefs.h so translated b4ld.c lines like
`if (ckt->CKTmode & MODEINITJCT)` map 1:1.
EOF
)"
```

---

## Task 3: Extend the `Device` interface with state and CKTag hooks

**Files:**
- Modify: `src/devices/device.hpp`

UCB stores ~30 state-vector slots per instance (see `bsim4v7_def.hpp:430` — `BSIM4numStates 29`). Existing devices (Capacitor, Inductor, Diode) have no state needs, so their overrides return 0 and opt out of state binding. BSIM4v7Device returns `29`.

- [ ] **Step 1: Write the failing test**

Create `tests/unit/test_device_state_interface.cpp`:

```cpp
#include <gtest/gtest.h>
#include "devices/device.hpp"
#include "devices/diode.hpp"
#include "devices/capacitor.hpp"

using namespace neospice;

TEST(DeviceInterface, DefaultStateCountIsZero) {
    DiodeModel m;
    Diode d("D1", 1, 0, m);
    EXPECT_EQ(0, d.state_vars());
    Capacitor c("C1", 1, 0, 1e-9);
    EXPECT_EQ(0, c.state_vars());
}

TEST(DeviceInterface, SetStatePtrsDefaultIsNoop) {
    DiodeModel m;
    Diode d("D1", 1, 0, m);
    // Must not crash / throw for a stateless device when given nullptrs
    d.set_state_ptrs(nullptr, nullptr, nullptr, 0);
}
```

Add to `tests/CMakeLists.txt`.

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build build --target neospice_tests -j
```

Expected: compile error — `state_vars` / `set_state_ptrs` do not exist.

- [ ] **Step 3: Extend `device.hpp`**

Add below `extra_vars()`:

```cpp
/// Number of BSIM-style state slots per instance (0 for stateless devices).
/// Summed by Circuit during finalize() to size the per-circuit state buffers.
virtual int32_t state_vars() const { return 0; }

/// Bind three rotating state buffers and the per-instance base offset.
/// state0 is the latest iterate; state1/state2 are previous timesteps
/// (for PREDICTOR and integrator history). Default is a no-op.
virtual void set_state_ptrs(double* /*state0*/, double* /*state1*/,
                            double* /*state2*/, int32_t /*base*/) {}
```

- [ ] **Step 4: Run to verify the test passes**

```bash
cmake --build build --target neospice_tests -j
ctest --test-dir build -R DeviceInterface --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/devices/device.hpp tests/unit/test_device_state_interface.cpp tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(device): add state_vars()/set_state_ptrs() for BSIM-style state

Defaults return 0 / no-op so stateless devices (R/L/C/V/I/D) are
unchanged. BSIM4v7Device (Task 7) overrides state_vars() to return 29
and consumes the bound pointers inside evaluate().
EOF
)"
```

---

## Task 4: Two-phase matrix offset resolution

**Files:**
- Modify: `src/devices/bsim4v7/bsim4v7_shim.hpp`
- Modify: `src/devices/bsim4v7/bsim4v7_shim.cpp`
- Modify: `src/devices/bsim4v7/bsim4v7_setup.cpp`

Phase-1a `Shim::Matrix::make_elt` returns the sentinel `0` because offsets can't be resolved until the `SparsityPattern` is finalized. Phase 1b needs real offsets on the instance. Strategy: `make_elt` remembers the `(row, col)` pair in an internal journal and returns a *reservation ID*; a new `resolve_offsets(const SparsityPattern&)` call replays the journal and returns the real `MatrixOffset` vector in reservation order. The device adapter (Task 7) stores the IDs on the instance during `stamp_pattern`, then swaps them for resolved offsets during `assign_offsets`.

- [ ] **Step 1: Write the failing test**

Create `tests/unit/test_bsim4v7_shim_matrix.cpp`:

```cpp
#include <gtest/gtest.h>
#include "devices/bsim4v7/bsim4v7_shim.hpp"
#include "core/matrix.hpp"

using namespace neospice;
using namespace neospice::bsim4v7;

TEST(Bsim4ShimMatrix, TwoPhaseResolution) {
    SparsityBuilder b(4);
    Shim::Matrix mat(b);

    // Phase 1: reservations. IDs are sequential, starting at 0.
    auto id_00 = mat.make_elt(0, 0);
    auto id_11 = mat.make_elt(1, 1);
    auto id_01 = mat.make_elt(0, 1);
    auto id_gd = mat.make_elt(-1, 2); // ground row — sentinel
    EXPECT_EQ(0, id_00);
    EXPECT_EQ(1, id_11);
    EXPECT_EQ(2, id_01);
    EXPECT_EQ(-1, id_gd);

    // Phase 2: resolve against finalized pattern.
    SparsityPattern pat = b.build();
    auto offsets = mat.resolve_offsets(pat);
    ASSERT_EQ(4u, offsets.size());
    EXPECT_GE(offsets[0], 0);
    EXPECT_GE(offsets[1], 0);
    EXPECT_GE(offsets[2], 0);
    EXPECT_EQ(-1, offsets[3]);     // ground stays -1

    EXPECT_EQ(offsets[0], pat.offset(0, 0));
    EXPECT_EQ(offsets[1], pat.offset(1, 1));
    EXPECT_EQ(offsets[2], pat.offset(0, 1));
}
```

Add to `tests/CMakeLists.txt`.

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build build --target neospice_tests -j
```

Expected: compile error — `resolve_offsets` does not exist; `make_elt` returns `0`.

- [ ] **Step 3: Rewrite `Shim::Matrix`**

In `bsim4v7_shim.hpp`:

```cpp
class Matrix {
public:
    Matrix(neospice::SparsityBuilder &builder) : builder_(builder) {}

    /// Reserve a (row, col) position. Returns a sequential reservation ID
    /// (0, 1, 2, ...) for later resolution, or -1 if either index is ground.
    /// NOTE: in Phase-1a semantics the return was a "MatrixOffset" but it was
    /// a 0-sentinel. In Phase-1b it is a distinct ID into the journal.
    neospice::MatrixOffset make_elt(int row, int col);

    /// After the SparsityPattern is built, replay the journal and return
    /// the real MatrixOffset for each reservation (ground reservations get -1).
    std::vector<neospice::MatrixOffset>
    resolve_offsets(const neospice::SparsityPattern &pat) const;

    /// Reset the journal — useful if the same Shim::Matrix is reused across
    /// multiple stamp_pattern() calls (it is not, but leave the hook).
    void clear() { journal_.clear(); }

private:
    neospice::SparsityBuilder &builder_;
    std::vector<std::pair<int,int>> journal_;  // in reservation order
};
```

In `bsim4v7_shim.cpp`, replace the Phase-1a stub:

```cpp
neospice::MatrixOffset Matrix::make_elt(int row, int col) {
    if (row < 0 || col < 0) {
        journal_.emplace_back(-1, -1);
        return -1;
    }
    builder_.add(row, col);
    neospice::MatrixOffset id = static_cast<neospice::MatrixOffset>(journal_.size());
    journal_.emplace_back(row, col);
    return id;
}

std::vector<neospice::MatrixOffset>
Matrix::resolve_offsets(const neospice::SparsityPattern &pat) const {
    std::vector<neospice::MatrixOffset> out;
    out.reserve(journal_.size());
    for (auto &[r, c] : journal_) {
        if (r < 0 || c < 0) out.push_back(-1);
        else out.push_back(pat.offset(r, c));
    }
    return out;
}
```

- [ ] **Step 4: Patch `BSIM4setup` to record reservation IDs**

The existing `bsim4v7_setup.cpp` TSTALLOC macro:

```cpp
#define TSTALLOC(ptr, first, second) \
    { here->ptr = matrix->make_elt(here->first, here->second); }
```

Does not change — but the interpretation of `here->ptr` now shifts from "sentinel 0" to "reservation ID". This is a semantic change, not a syntactic one — no edit required in the 67 `TSTALLOC` call sites.

Add a single comment at the top of `bsim4v7_setup.cpp`, above `BSIM4setup`:

```cpp
// Phase 1b note: TSTALLOC records a reservation ID on here->BSIM4*Ptr.
// The device adapter (see bsim4v7_device.cpp) resolves these IDs to real
// MatrixOffsets in assign_offsets() after the SparsityPattern is built.
```

- [ ] **Step 5: Run to verify the test passes**

```bash
cmake --build build --target neospice_tests -j
ctest --test-dir build -R Bsim4ShimMatrix --output-on-failure
```

Expected: PASS.

- [ ] **Step 6: Regression-check Phase-1a preproc test**

```bash
ctest --test-dir build -R BSIM4v7UCBSetup --output-on-failure
```

Expected: still PASS (it exercises only `BSIM4setup`+`BSIM4temp` — unaffected by the semantic shift of `make_elt`'s return value because the test never dereferences any `BSIM4*Ptr`).

- [ ] **Step 7: Commit**

```bash
git add src/devices/bsim4v7/bsim4v7_shim.hpp \
        src/devices/bsim4v7/bsim4v7_shim.cpp \
        src/devices/bsim4v7/bsim4v7_setup.cpp \
        tests/unit/test_bsim4v7_shim_matrix.cpp \
        tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(bsim4v7/shim): two-phase MatrixOffset resolution

make_elt() now journals (row, col) reservations and returns a sequential
ID. resolve_offsets(pattern) replays the journal against a finalized
SparsityPattern to produce real MatrixOffsets. The device adapter
(Task 7) uses ID→offset mapping in assign_offsets() so the translated
BSIM4load can stamp via mat.add(here->BSIM4DdPtr, v) directly.
EOF
)"
```

---

## Task 5: State buffers and integrator coefficients on `Circuit`

**Files:**
- Modify: `src/core/circuit.hpp`
- Modify: `src/core/circuit.cpp`
- Modify: `src/core/transient.cpp`
- Modify: `src/core/dc.cpp`

The circuit owns the three rotating state buffers (length = `Σ dev.state_vars()` across all devices). The transient driver rotates them each accepted step and fills `CKTag[]` from the integrator. DC sets `CKTmode = MODEDC | MODEDCOP` (plus `MODEINITJCT` on the first Newton iter, `MODEINITFIX` on subsequent ones).

- [ ] **Step 1: Write the failing test**

Create `tests/unit/test_circuit_state_allocation.cpp`:

```cpp
#include <gtest/gtest.h>
#include "core/circuit.hpp"
#include "devices/diode.hpp"
#include "devices/device.hpp"

using namespace neospice;

namespace {
// Dummy device with non-zero state_vars() to drive allocation.
struct FakeStateDev : public Device {
    FakeStateDev(std::string n, int32_t nv) : Device(std::move(n)), nv_(nv) {}
    void stamp_pattern(SparsityBuilder&) const override {}
    void assign_offsets(const SparsityPattern&) override {}
    void evaluate(const std::vector<double>&, NumericMatrix&, std::vector<double>&) override {}
    int32_t state_vars() const override { return nv_; }
    void set_state_ptrs(double* s0, double* s1, double* s2, int32_t base) override {
        bound0_ = s0; bound1_ = s1; bound2_ = s2; base_ = base;
    }
    int32_t nv_;
    double *bound0_ = nullptr, *bound1_ = nullptr, *bound2_ = nullptr;
    int32_t base_ = -1;
};
}

TEST(CircuitState, AllocatesRingAndBinds) {
    Circuit ckt;
    ckt.node("a"); ckt.node("b");
    auto *fa = new FakeStateDev("X1", 29);
    auto *fb = new FakeStateDev("X2", 12);
    ckt.add_device(std::unique_ptr<Device>(fa));
    ckt.add_device(std::unique_ptr<Device>(fb));
    ckt.finalize();

    EXPECT_EQ(41, ckt.num_states());
    EXPECT_NE(nullptr, fa->bound0_);
    EXPECT_NE(nullptr, fb->bound0_);
    EXPECT_EQ(0,  fa->base_);
    EXPECT_EQ(29, fb->base_);
    // state0/1/2 are distinct buffers
    EXPECT_NE(fa->bound0_, fa->bound1_);
    EXPECT_NE(fa->bound1_, fa->bound2_);
}
```

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build build --target neospice_tests -j
```

Expected: compile error — `num_states` missing, `set_state_ptrs` not wired.

- [ ] **Step 3: Extend `Circuit`**

In `circuit.hpp`, add:

```cpp
int32_t num_states() const { return num_states_; }

double* state0() { return state0_.data(); }
double* state1() { return state1_.data(); }
double* state2() { return state2_.data(); }

void rotate_state();  // state2 <- state1 <- state0; rebinds device ptrs
```

and the private members:

```cpp
int32_t num_states_ = 0;
std::vector<double> state0_, state1_, state2_;
```

In `circuit.cpp`, extend `Circuit::finalize()` after the existing pattern build:

```cpp
num_states_ = 0;
for (auto &dev : devices_) num_states_ += dev->state_vars();
state0_.assign(num_states_, 0.0);
state1_.assign(num_states_, 0.0);
state2_.assign(num_states_, 0.0);

int32_t base = 0;
for (auto &dev : devices_) {
    int32_t n = dev->state_vars();
    if (n > 0) {
        dev->set_state_ptrs(state0_.data(), state1_.data(), state2_.data(), base);
        base += n;
    }
}
```

And the rotation helper:

```cpp
void Circuit::rotate_state() {
    state2_.swap(state1_);
    state1_ = state0_;   // copy (state0 still the accepted iterate)
    // Pointers to state0_.data(), state1_.data(), state2_.data() are unchanged
    // because swap() preserves the backing buffer. Do NOT rebind devices.
    // (state1_ = state0_ copies *values*, same backing buffer.)
}
```

Note: `state2_.swap(state1_)` plus `state1_ = state0_` (copy assignment — same capacity) keeps all three `.data()` pointers stable across rotations, so the devices never need to be rebound. This is a deliberate choice; `set_state_ptrs` is called exactly once, at `finalize()`.

- [ ] **Step 4: Run to verify the test passes**

```bash
cmake --build build --target neospice_tests -j
ctest --test-dir build -R CircuitState --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Wire `CKTag[]` from the transient integrator**

We'll thread this through a new accessor so `BSIM4v7Device::evaluate` can read it. The trap/Gear/BE computation already lives in `core/transient.cpp`. Add a small struct carried on `Circuit` that the integrator writes and the device adapter reads:

In `circuit.hpp` add:

```cpp
/// Populated by the transient driver before each Newton load, read by
/// state-storing devices (BSIM4v7). DC leaves mode=MODEDC|MODEINITJCT/FIX.
struct IntegratorCtx {
    int    mode  = 0;       // Shim-style CKTmode bitfield
    double ag[8] = {};      // UCB integrator coeffs (BE/Trap/Gear2)
    double delta = 0.0;
    double delta_old[8] = {};
    int    order = 1;
};
IntegratorCtx integrator_ctx;
```

(In `circuit.hpp`, `IntegratorCtx` can stay a nested type; no separate header needed.)

- [ ] **Step 6: Set `integrator_ctx` from DC and Transient drivers**

In `core/dc.cpp` — wherever the Newton loop sets up, write:

```cpp
ckt.integrator_ctx.mode = /* MODEDC | MODEDCOP */ 0x41;
ckt.integrator_ctx.mode |= /* MODEINITJCT */ 0x2000;  // first iter only
// On later iterations:
ckt.integrator_ctx.mode = (0x41 | 0x4000 /* MODEINITFIX */);
```

(Use literal values here; the shim-side `Shim::MODEDC` etc. match them. Task 2's smoke test enforces the bit layout.)

In `core/transient.cpp`, around `timestep.cpp`'s coefficient computation, expose `ag[0]` and `ag[1]` per-step. For Gear-2 with step `h` and previous step `h_old`:

```cpp
double h = ctrl.current_dt();
// BE (order 1)
if (ctrl.order() == 1) {
    ckt.integrator_ctx.ag[0] = 1.0 / h;
    ckt.integrator_ctx.ag[1] = -1.0 / h;
}
// Gear-2 (order 2, fixed ratio)
else {
    // ag[0] = (1 + 2*(h/h_old)) / (h*(1 + h/h_old))
    double r = h / h_old;
    ckt.integrator_ctx.ag[0] = (1.0 + 2.0*r) / (h * (1.0 + r));
    ckt.integrator_ctx.ag[1] = -(1.0 + r) / (h * r);  // etc.
}
ckt.integrator_ctx.order = ctrl.order();
ckt.integrator_ctx.delta = h;
ckt.integrator_ctx.delta_old[1] = h_old;
ckt.integrator_ctx.mode = /* MODETRAN */ 0x20 | (first_step ? 0x10000 /*MODEINITTRAN*/ : 0);
```

(If `TimeStepController` does not yet expose `order()`, add a `int order() const { return order_; }` method and wire it. For Gear-2 used today, `order_` = 2 after the first two steps.)

- [ ] **Step 7: Regression sanity**

```bash
cmake --build build -j
ctest --test-dir build --output-on-failure 2>&1 | tail -15
```

Expected: baseline suite still 102 pass / 1 skip / 2 disabled. (New state allocator is a no-op for existing devices because `state_vars()` defaults to 0.)

- [ ] **Step 8: Commit**

```bash
git add src/core/circuit.hpp src/core/circuit.cpp \
        src/core/transient.cpp src/core/dc.cpp \
        tests/unit/test_circuit_state_allocation.cpp \
        tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(core): circuit-wide state ring + integrator context

Circuit::finalize() now sizes state0/1/2 by summing Device::state_vars()
and binds per-device base offsets. The transient driver fills
integrator_ctx.{ag,mode,order,delta} per Newton load; DC fills
integrator_ctx.mode with MODEDC | MODEINITJCT/FIX. BSIM4v7Device (Task 7)
reads integrator_ctx inside evaluate() and mirrors it onto Shim::Ckt.
EOF
)"
```

---

## Task 6: Translate `b4ld.c` → `bsim4v7_load.cpp` (scripted)

**Files:**
- Create: `tools/bsim4_translate.py` — deterministic UCB C → C++ translator
- Create: `src/devices/bsim4v7/bsim4v7_load.cpp`
- Modify: `src/devices/bsim4v7/CMakeLists.txt` (append `bsim4v7_load.cpp` to the `bsim4v7_obj` target)

`b4ld.c` is 5601 lines — too large for a careful human mechanical port, but small enough that the translation rules from Phase 1a are mostly regex-mechanical: type renames, include swaps, error-code rewrites, `*(ptr) +=/-= v` → `mat.add(ptr, ±v)` stamping, `#ifdef USE_OMP` branch stripping, namespace wrapping. A Python script captures those rules once and applies them deterministically.

**Strategy:**
1. Write `tools/bsim4_translate.py` that encodes the Phase-1a+1b translation rules.
2. **Validate** the script by running it on already-translated UCB sources (`b4temp.c` and `b4set.c`) and diffing against the hand-translated Phase-1a files. Iterate on script rules until the generated output matches the committed Phase-1a files structurally (whitespace/comment-order differences OK; semantic differences NOT OK).
3. Run the script on `b4ld.c` → `bsim4v7_load.cpp`.
4. Hand-fix residue: anything the script couldn't deterministically translate (multi-line macro expansions, the two K&R-era helper signatures at the tail, the ByPass `goto` target if any, the `using namespace Shim;` scoping).
5. Compile + commit.

**Why this beats a human port:** deterministic, reviewable as a script + output diff rather than as 5601 hand-written lines, reusable for `b4acld.c` in Phase 2.

- [ ] **Step 1: Write the translator script**

Create `tools/bsim4_translate.py`. The script reads a UCB `.c` file and emits a C++ `.cpp` file. Rules it must encode (complete list — if a rule isn't listed, don't apply it):

**A. Preprocessor-level:**
1. `#include "ngspice/..."` lines → delete all.
2. `#include "bsim4def.h"` → delete.
3. Strip `#ifdef USE_OMP ... #else ... #endif` blocks: keep the `#else` branch verbatim, delete the `#ifdef` branch. Handle nested preprocessor blocks via a stack. When `#ifdef USE_OMP` has no `#else`, the whole block is deleted.
4. Leave `#ifdef PREDICTOR`, `#ifndef PREDICTOR`, `#ifdef NEWCONV`, etc. verbatim — the translated file `#define`s them at the top.
5. `#define` blocks (MAX_EXPL etc.) → keep verbatim.

**B. Token substitutions (whole-word only — do NOT match inside strings or comments):**
- `struct sBSIM4instance` → `struct BSIM4v7Instance` *(handle before the next rule)*
- `struct sBSIM4model` → `struct BSIM4v7Model`
- `BSIM4instance` → `BSIM4v7Instance`
- `BSIM4model` → `BSIM4v7Model`
- `GENmodel` → `BSIM4v7Model`
- `GENinstance` → `BSIM4v7Instance`
- `CKTcircuit` → `Shim::Ckt`
- `IFvalue` → `Shim::IfValue`
- `IFuid` → `const char *`
- `SMPmatrix` → `Shim::Matrix`

**C. Casts to drop:**
- `(BSIM4model*)inModel` → `inModel` (the assignment it feeds already has the right type after substitution B)
- `(BSIM4instance*)inInst` → `inInst`

**D. Error codes:**
- `return(OK)` / `return OK` → `return 0;`
- `return(E_BADPARM)` → `return Shim::E_BADPARM;`
- `return(E_PARMRANGE)` → `return Shim::E_PARMRANGE;`

**E. Error reporting:**
- `SPfrontEnd->IFerror(ERR_WARNING, <fmt>, ...)` → `Shim::report_error(Shim::ERR_WARNING, <fmt>, ...)`
- `SPfrontEnd->IFerror(ERR_FATAL, ...)` → `Shim::report_error(Shim::ERR_FATAL, ...)`

**F. Memory:**
- `FREE(x)` → `Shim::FREE(x)` (the shim provides a templated FREE that accepts any pointer type).
- `tmalloc(sizeof(double) * n)` → `Shim::tmalloc<double>(n)`

**G. Matrix stamping (b4ld-specific — the interesting one):**
- `*(here->BSIM4<X>Ptr) += <expr>;` → `mat.add(here->BSIM4<X>Ptr, <expr>);`
- `*(here->BSIM4<X>Ptr) -= <expr>;` → `mat.add(here->BSIM4<X>Ptr, -(<expr>));`
- The `<expr>` may span multiple lines. Parse up to the matching `;`. Wrap negated expressions in parens if they aren't already a single identifier.

**H. K&R rewrites (`b4set.c` had these; `b4ld.c` may not):**
- Detect the pattern:
  ```c
  int Foo(a, b)
  int a;
  int b;
  {
  ```
  and rewrite to `int Foo(int a, int b) {` (single line or multi-line). For `b4ld.c`, the two helper functions at the tail are already ISO C++, so this may be unused — but include the rule for the `b4temp.c` / `b4set.c` validation step.

**I. Header/footer:**
- Prepend the UCB banner comment block (lines 1 through the first `#include` — copy verbatim from the input).
- Below the banner, insert:
  ```cpp
  // Translated to C++ for neospice on 2026-04-16 by tools/bsim4_translate.py.
  // See third_party/bsim4_4.7.0/B4TERMS_OF_USE.

  #include "devices/bsim4v7/bsim4v7_def.hpp"
  #include "devices/bsim4v7/bsim4v7_shim.hpp"
  #include <cmath>
  #include <cstdio>

  namespace neospice::bsim4v7 {

  using namespace Shim;  // MODE* flags, error codes, IfValue, IfParm
  ```
- Append at EOF: `} // namespace neospice::bsim4v7`
- For `b4ld.c` specifically, also emit `#define PREDICTOR` above the `#include`s (per plan rule 21).

**J. Identity rules (do NOT rewrite):**
- `exp`, `log`, `sqrt`, `pow`, `fabs`, `tanh`, etc. (C++ `<cmath>` resolves via ADL)
- `ckt->CKT<anything>` — verbatim (our `Shim::Ckt` mirrors UCB field names)
- `here->BSIM4<anything>` — verbatim (our instance POD mirrors UCB field names)
- `MODE*` flags — verbatim (`using namespace Shim;` at file top resolves them)

**Script structure:**
```python
#!/usr/bin/env python3
"""Mechanical translator: UCB BSIM4 C source → neospice C++ port.
Usage: bsim4_translate.py INPUT.c OUTPUT.cpp [--define PREDICTOR ...]
"""
import re, sys, argparse

RULES = [
    # (description, pattern, replacement, flags)
    # ...filled per the list above
]

def strip_omp_blocks(src: str) -> str:
    """Walk preprocessor lines, keeping only the #else branch of USE_OMP."""
    ...

def protect_literals(src: str):
    """Replace string literals and comments with placeholders, return
    (protected_src, unprotect_fn) so regex rules don't rewrite inside them."""
    ...

def apply_token_subs(src: str) -> str:
    """Whole-word identifier substitutions from rule B/C/D/E/F."""
    ...

def rewrite_matrix_stamps(src: str) -> str:
    """Rule G: *(here->BSIM4xPtr) +=/-= expr; → mat.add(here->BSIM4xPtr, expr);
    State-machine based — finds `*(` ... `)` `+=|-=` up to matching `;`."""
    ...

def rewrite_knr_signatures(src: str) -> str:
    """Rule H: K&R-era int foo(a,b) \n int a; int b; → int foo(int a, int b)."""
    ...

def wrap_namespace_and_banner(src: str, defines: list[str]) -> str:
    """Rule I: banner + includes + namespace open/close."""
    ...

def main():
    ...
```

Keep the script self-contained — no external dependencies beyond the Python stdlib.

- [ ] **Step 2: Validate against Phase-1a files**

This is the critical trust-builder. If the script's output on already-translated files matches what's committed, the script is correct.

```bash
# Golden comparison on b4temp.c (already ported in Phase 1a T10):
python3 tools/bsim4_translate.py \
    third_party/bsim4_4.7.0/code/b4temp.c /tmp/b4temp_generated.cpp
diff -u src/devices/bsim4v7/bsim4v7_temp.cpp /tmp/b4temp_generated.cpp

# Same for b4set.c (Phase 1a T11):
python3 tools/bsim4_translate.py \
    third_party/bsim4_4.7.0/code/b4set.c /tmp/b4set_generated.cpp
diff -u src/devices/bsim4v7/bsim4v7_setup.cpp /tmp/b4set_generated.cpp
```

Expected: diffs exist (whitespace, comment-order, TSTALLOC macro which the script won't emit identically) but NO semantic differences. Acceptable diff classes:
- Whitespace around rewritten matrix stamps.
- Comment placement (Phase 1a preserved comments verbatim; the script can too).
- The TSTALLOC macro — b4set.c uses a UCB macro that expands to matrix stamping + error check; Phase 1a hand-rewrote it to `here->BSIM4xPtr = matrix->make_elt(...)` without the error check. The script does NOT need to reproduce this hand-rewrite — if b4set.c hits `TSTALLOC`, the script should emit a `/* TODO(translator): TSTALLOC macro unhandled */` comment and we accept the diff.

Unacceptable diffs:
- A `*(ptr) += expr` that was rewritten differently.
- A type substitution missed or over-applied.
- A missed USE_OMP branch.

If any unacceptable diff appears, fix the rule and re-run until the validation diffs are bounded to the acceptable classes. Document the accepted diffs in `tools/bsim4_translate.py`'s module docstring.

- [ ] **Step 3: Run the script on b4ld.c**

```bash
python3 tools/bsim4_translate.py \
    third_party/bsim4_4.7.0/code/b4ld.c \
    src/devices/bsim4v7/bsim4v7_load.cpp \
    --define PREDICTOR
wc -l src/devices/bsim4v7/bsim4v7_load.cpp
```

Expected: output file exists, line count within ±200 of the 5601-line input (accounting for deleted OMP branches and added namespace wrapping).

- [ ] **Step 4: Hand-fix compile residue**

```bash
cmake --build build --target bsim4v7_obj -j 2>&1 | tee /tmp/b4ld_build.log
```

Expected residue to hand-fix (estimate ~10-30 spots):
- `mat` not declared: add `auto &mat = *ckt->mat;` at the top of `BSIM4load` and `BSIM4LoadRhsMat`.
- `BSIM4polyDepletion` forward declaration missing → add at top of file.
- `using namespace Shim;` scope for `MODE*` constants — already injected by rule I.
- Any `IFerror` calls the rule E regex missed (unusual formatting).
- Any `TSTALLOC` artefacts (none expected in b4ld.c — this macro lives in b4set.c).

Fix each error, re-run `cmake --build`, iterate to zero errors.

- [ ] **Step 5: Run Phase-1a regression tests**

```bash
ctest --test-dir build -R BSIM4v7UCBSetup --output-on-failure
```

Expected: PASS (untouched by the load translation).

- [ ] **Step 6: Add to CMake**

In `src/devices/bsim4v7/CMakeLists.txt`, append `bsim4v7_load.cpp` to the `bsim4v7_obj` OBJECT library sources list, next to `bsim4v7_setup.cpp`.

- [ ] **Step 7: Commit**

```bash
git add tools/bsim4_translate.py \
        src/devices/bsim4v7/bsim4v7_load.cpp \
        src/devices/bsim4v7/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(bsim4v7): port b4ld.c to bsim4v7_load.cpp (scripted, Phase 1b core)

Mechanical 1:1 translation of UCB b4ld.c (5601 lines) to
bsim4v7_load.cpp. Translation rules from milestone 4 Phase 1a apply,
plus the following Phase-1b additions:

  - Matrix stamping: *(here->BSIM4DdPtr) += v → mat.add(..., v).
    mat is bound by the device adapter via ckt->mat before each load.
  - State array indexing: *(ckt->CKTstate0 + here->BSIM4vds) stays
    verbatim; CKTstate0/1/2 are double* on Shim::Ckt, also bound per
    load.
  - RHS stamping: *(ckt->CKTrhs + node) -= ceq stays verbatim; CKTrhs
    is a double* on Shim::Ckt bound per load.
  - Integrator coefficients: ckt->CKTag[0] and CKTag[1] stay verbatim.

Deliberate deviations documented inline:
  - #define PREDICTOR at file top (ngspice default).
  - USE_OMP NOT defined; the OMP branches in UCB source are deleted
    rather than translated (rule 22, Phase 1b).

No tests wired yet — device adapter in Task 7 drives the new function.
EOF
)"
```

---

## Task 7: `BSIM4v7Device` adapter class

**Files:**
- Create: `src/devices/bsim4v7/bsim4v7_device.hpp`
- Create: `src/devices/bsim4v7/bsim4v7_device.cpp`
- Modify: `src/devices/bsim4v7/CMakeLists.txt`

`BSIM4v7Device` is the boundary between the neospice `Device` interface and the UCB-shaped code. It owns one `BSIM4v7Instance` and holds a non-owning pointer to a shared `BSIM4v7Model` (shared because many `.model NMOD NMOS ...` instances can reference the same card). It is the only file in `src/devices/bsim4v7/` not governed by the mechanical-translation rules.

- [ ] **Step 1: Write the failing DC op-point golden test**

Create `tests/unit/test_bsim4v7_ucb_load.cpp`:

```cpp
// Phase-1b golden: end-to-end BSIM4v7Device DC Id at fixed biases, cross
// -checked against ngspice on the same NMOS probe card used in Phase-1a.
// Model: NMOD NMOS LEVEL=14 VTH0=0.4 U0=0.04 TOXE=2e-9. W=1u L=100n.

#include <gtest/gtest.h>
#include "devices/bsim4v7/bsim4v7_device.hpp"
#include "core/circuit.hpp"
#include "core/klu_solver.hpp"
#include "core/newton.hpp"

using namespace neospice;

TEST(BSIM4v7UCBLoad, NmosDcOpMatchesNgspice) {
    Circuit ckt;
    int d = ckt.node("d"), g = ckt.node("g"), b = ckt.node("b");
    // Stubs: VDD d 0 0.1; VGS g 0 0.8; VBS b 0 0; M1 d g 0 b NMOD W=1u L=100n
    // Use the real parser in Task 10's wire-up — for this unit test,
    // construct devices manually via BSIM4v7Device::make_nmos(...).
    BSIM4v7ModelCard card{ /* VTH0=0.4, U0=0.04, TOXE=2e-9, type=NMOS, level=14 */ };
    auto dev = BSIM4v7Device::make(
        "M1", /*nd=*/d, /*ng=*/g, /*ns=*/-1 /* ground */, /*nb=*/b,
        BSIM4v7Instance::Geom{1e-6, 1e-7, 1.0 /*NF*/}, card);
    ckt.add_device(std::make_unique<VSource>("VDD", d, -1, 0.1));
    ckt.add_device(std::make_unique<VSource>("VGS", g, -1, 0.8));
    ckt.add_device(std::make_unique<VSource>("VBS", b, -1, 0.0));
    ckt.add_device(std::move(dev));
    ckt.finalize();

    KLUSolver solver;
    solver.symbolic(ckt.pattern());
    std::vector<double> sol(ckt.num_vars(), 0.0);
    auto r = newton_solve(ckt, solver, sol, ckt.options);
    ASSERT_TRUE(r.converged);

    // Golden Id from ngspice op analysis of the same card (record after
    // running ngspice tests/goldens/probe.cir and grepping `@m1[id]`).
    // Expected: |Id| ~ O(1e-6) A at VGS=0.8, VDS=0.1 for U0=0.04 weak-inv device.
    // Replace placeholder with actual value from `ngspice -b probe.cir | grep @m.id`.
    const double Id_golden = /* TBD — capture from ngspice at step 2 */ 0.0;
    const double Id_measured = /* read through a VSource branch current probe */ 0.0;
    EXPECT_NEAR(Id_measured, Id_golden, std::max(std::abs(Id_golden) * 1e-3, 1e-15));
}
```

> **NOTE on the golden value**: Before committing this test, run `ngspice -b tests/goldens/probe.cir` and capture the operating-point Id for M1. Replace the `TBD` placeholder with the captured value and drop this note. The placeholder-at-commit-time state is **not** acceptable per plan rule "no placeholders" — this step must complete before Step 4.

- [ ] **Step 2: Capture the golden Id**

```bash
ngspice -b ./tests/goldens/probe.cir 2>&1 \
    | grep -E 'id|@m1'
```

Record the reported drain current. Replace the `Id_golden` placeholder in the test with the exact value. Commit the test with a comment `// Golden from ngspice op() on tests/goldens/probe.cir, date 2026-04-16`.

- [ ] **Step 3: Design the adapter header**

```cpp
#pragma once
#include "devices/device.hpp"
#include "devices/bsim4v7/bsim4v7_def.hpp"
#include "devices/bsim4v7/bsim4v7_shim.hpp"
#include <memory>

namespace neospice {

// Shared model — many BSIM4v7Device instances point at one of these, matching
// UCB's single .model card with many M-card instances.
struct BSIM4v7ModelCard {
    bsim4v7::BSIM4v7Model ucb{};   // aggregate of all UCB model fields
};

class BSIM4v7Device : public Device {
public:
    struct Geom { double W = 1e-6; double L = 1e-7; double NF = 1.0;
                  double AD = 0, AS = 0, PD = 0, PS = 0;
                  double NRD = 0, NRS = 0; double SA = 0, SB = 0, SD = 0; };

    static std::unique_ptr<BSIM4v7Device> make(
        std::string name, int32_t nd, int32_t ng, int32_t ns, int32_t nb,
        const Geom &geom, BSIM4v7ModelCard &shared_card);

    void stamp_pattern(SparsityBuilder&) const override;
    void assign_offsets(const SparsityPattern&) override;
    void evaluate(const std::vector<double>& voltages,
                  NumericMatrix& mat, std::vector<double>& rhs) override;
    int32_t state_vars() const override { return 29; /* BSIM4numStates */ }
    void set_state_ptrs(double *s0, double *s1, double *s2, int32_t base) override;

private:
    BSIM4v7Device(std::string name) : Device(std::move(name)) {}

    bsim4v7::BSIM4v7Instance inst_{};
    bsim4v7::BSIM4v7Model *model_ = nullptr;
    bsim4v7::Shim::Matrix shim_matrix_builder_{/* ... */};  // wrapping SparsityBuilder
    std::vector<MatrixOffset> reservation_to_offset_;
    // Pointers bound by set_state_ptrs()
    double *state0_ = nullptr, *state1_ = nullptr, *state2_ = nullptr;
    int32_t state_base_ = -1;
};

} // namespace neospice
```

- [ ] **Step 4: Implement the adapter**

In `bsim4v7_device.cpp`:

```cpp
#include "devices/bsim4v7/bsim4v7_device.hpp"
#include <cstring>

namespace neospice {
namespace b4 = bsim4v7;

std::unique_ptr<BSIM4v7Device> BSIM4v7Device::make(
    std::string name, int32_t nd, int32_t ng, int32_t ns, int32_t nb,
    const Geom &geom, BSIM4v7ModelCard &card)
{
    auto dev = std::unique_ptr<BSIM4v7Device>(new BSIM4v7Device(std::move(name)));
    dev->model_ = &card.ucb;
    auto &h = dev->inst_;
    h.BSIM4name       = dev->name().c_str();
    h.BSIM4dNode      = nd; h.BSIM4dNodeGiven = 1;
    h.BSIM4gNodeExt   = ng; h.BSIM4gNodeExtGiven = 1;
    h.BSIM4sNode      = ns;
    h.BSIM4bNode      = nb;
    h.BSIM4w  = geom.W;  h.BSIM4wGiven  = 1;
    h.BSIM4l  = geom.L;  h.BSIM4lGiven  = 1;
    h.BSIM4nf = geom.NF; h.BSIM4nfGiven = 1;
    h.BSIM4ad = geom.AD; h.BSIM4as = geom.AS;
    h.BSIM4pd = geom.PD; h.BSIM4ps = geom.PS;
    h.BSIM4nrd = geom.NRD; h.BSIM4nrs = geom.NRS;
    h.BSIM4sa = geom.SA;   h.BSIM4sb = geom.SB; h.BSIM4sd = geom.SD;
    h.BSIM4modPtr       = dev->model_;
    h.BSIM4nextInstance = dev->model_->BSIM4instances;
    dev->model_->BSIM4instances = &h;
    return dev;
}

void BSIM4v7Device::stamp_pattern(SparsityBuilder &builder) const {
    // Cast-away: stamp_pattern is const on the Device interface, but our adapter
    // needs to run BSIM4setup which records reservations. Use a mutable thunk.
    auto &self = const_cast<BSIM4v7Device&>(*this);
    b4::Shim::Ckt ckt;   // local: BSIM4setup doesn't need the full context
    b4::Shim::Matrix mat(builder);
    int states = 0;
    int rc = b4::BSIM4setup(&mat, self.model_, &ckt, &states);
    if (rc != 0) throw std::runtime_error("BSIM4setup failed");
    // Capture the journal from mat for later resolution in assign_offsets.
    // Because stamp_pattern is called once per finalize(), and the builder
    // outlives this call, we cache the journal on self_.
    self.reservation_to_offset_.clear();
    // (See Task 4: Matrix::resolve_offsets consumes the journal.)
    // Defer resolve to assign_offsets; cache the Matrix by moving its state.
    self.captured_shim_matrix_ = std::move(mat);   // add a field on the class
}

void BSIM4v7Device::assign_offsets(const SparsityPattern &pat) {
    // Resolve reservations on the instance's BSIM4*Ptr fields.
    auto offsets = captured_shim_matrix_.resolve_offsets(pat);
    // The translated BSIM4setup stored reservation IDs into inst_.BSIM4DdPtr
    // et al. Walk every BSIM4*Ptr field and replace the ID with the real offset.
    // Helper macro:
    #define RESOLVE(field) \
        if (inst_.field >= 0) inst_.field = offsets[inst_.field];
    RESOLVE(BSIM4DdPtr)
    RESOLVE(BSIM4GPgpPtr)
    RESOLVE(BSIM4SspPtr)
    RESOLVE(BSIM4BbPtr)
    // ... repeat for every BSIM4*Ptr field on BSIM4v7Instance — there are
    //     precisely 67 in the Phase-1a def (matching the 67 TSTALLOC sites).
    //     An engineer should list them by greping `BSIM4.*Ptr` in
    //     src/devices/bsim4v7/bsim4v7_setup.cpp and emitting one
    //     RESOLVE(...) line per field found in `here->...` expressions.
    #undef RESOLVE
}

void BSIM4v7Device::set_state_ptrs(double *s0, double *s1, double *s2, int32_t base) {
    state0_ = s0; state1_ = s1; state2_ = s2; state_base_ = base;
    // Phase-1a's BSIM4setup set inst_.BSIM4states to base via `*states += 29`.
    // Confirm parity:
    // EXPECT_EQ(inst_.BSIM4states, base);
}

void BSIM4v7Device::evaluate(const std::vector<double> &voltages,
                             NumericMatrix &mat, std::vector<double> &rhs)
{
    // Reach into the owning Circuit's integrator_ctx. We don't have a back-pointer,
    // so the Circuit passes it through a thread_local or via rhs[] augmentation.
    // Cleanest: Circuit::evaluate_all() sets a thread_local pointer before calling
    // Device::evaluate — add that indirection in Task 5 if not present.
    extern thread_local const Circuit::IntegratorCtx *tls_integrator_ctx;
    const auto &ic = *tls_integrator_ctx;

    b4::Shim::Ckt ckt;
    ckt.CKTtemp    = /* room temp */ 300.15;  // TODO: thread from SimOptions
    ckt.CKTnomTemp = 300.15;
    ckt.CKTgmin    = 1e-12;
    ckt.CKTreltol  = 1e-3;
    ckt.CKTabstol  = 1e-12;
    ckt.CKTvoltTol = 1e-6;
    ckt.CKTmode    = ic.mode;
    ckt.CKTorder   = ic.order;
    ckt.CKTdelta   = ic.delta;
    std::memcpy(ckt.CKTdeltaOld, ic.delta_old, sizeof(ic.delta_old));
    std::memcpy(ckt.CKTag,       ic.ag,        sizeof(ic.ag));
    ckt.CKTstate0  = state0_;
    ckt.CKTstate1  = state1_;
    ckt.CKTstate2  = state2_;
    ckt.CKTrhs     = rhs.data();
    ckt.CKTrhsOld  = const_cast<double*>(voltages.data());
    ckt.mat        = &mat;

    // Lazy temp() on first evaluate — BSIM4temp fills pParam.
    if (inst_.pParam == nullptr) {
        int rc = b4::BSIM4temp(model_, &ckt);
        if (rc != 0) throw std::runtime_error("BSIM4temp failed");
    }

    int rc = b4::BSIM4load(model_, &ckt);
    if (rc != 0) throw std::runtime_error("BSIM4load failed");
}

} // namespace neospice
```

- [ ] **Step 5: Thread-local integrator context**

In `core/circuit.hpp` add `extern thread_local const Circuit::IntegratorCtx *tls_integrator_ctx;`, define it in `core/circuit.cpp` as `thread_local const Circuit::IntegratorCtx *tls_integrator_ctx = nullptr;`, and set it inside the Newton load helper that iterates `ckt.devices()` (likely `newton.cpp` or `circuit.cpp::stamp_all`) — search for where `dev->evaluate(...)` is called and wrap:

```cpp
tls_integrator_ctx = &ckt.integrator_ctx;
for (auto &dev : ckt.devices()) dev->evaluate(voltages, mat, rhs);
tls_integrator_ctx = nullptr;
```

- [ ] **Step 6: Run the DC op-point test**

```bash
cmake --build build --target neospice_tests -j
ctest --test-dir build -R BSIM4v7UCBLoad --output-on-failure
```

Expected: PASS within 1e-3 relative tolerance of ngspice's recorded Id.

- [ ] **Step 7: Commit**

```bash
git add src/devices/bsim4v7/bsim4v7_device.hpp \
        src/devices/bsim4v7/bsim4v7_device.cpp \
        src/devices/bsim4v7/CMakeLists.txt \
        src/core/circuit.hpp src/core/circuit.cpp src/core/newton.cpp \
        tests/unit/test_bsim4v7_ucb_load.cpp tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(bsim4v7): BSIM4v7Device adapter + DC op-point golden

BSIM4v7Device plugs the UCB kernel (setup/temp/load) into the Device
interface. stamp_pattern() runs BSIM4setup against a reservation-mode
Shim::Matrix; assign_offsets() resolves the 67 BSIM4*Ptr fields against
the finalized SparsityPattern; evaluate() binds Shim::Ckt to the
simulator's state/RHS/matrix/integrator context and calls BSIM4load.

Test NmosDcOpMatchesNgspice exercises the full pipeline on the
tests/goldens/probe.cir card and asserts Id within 1e-3 rel tol of
ngspice's operating-point value.
EOF
)"
```

---

## Task 8: Parser wire-up for M-card + LEVEL=14 model dispatch

**Files:**
- Modify: `src/parser/netlist_parser.cpp`
- Modify: `src/parser/model_cards.hpp`
- Modify: `src/parser/model_cards.cpp`

`netlist_parser.cpp:359-366` currently prints a warning and skips every `M` card. Replace that with the real handler.

- [ ] **Step 1: Write the parser smoke test**

Create `tests/unit/test_parser_mosfet.cpp`:

```cpp
#include <gtest/gtest.h>
#include "parser/netlist_parser.hpp"

using namespace neospice;

TEST(ParserMosfet, Level14NmosCardInstantiatesDevice) {
    const std::string netlist =
        "* NMOS probe\n"
        "VDD d 0 0.1\n"
        "VGS g 0 0.8\n"
        "VBS b 0 0\n"
        "M1 d g 0 b NMOD W=1u L=100n\n"
        ".model NMOD NMOS LEVEL=14 VTH0=0.4 U0=0.04 TOXE=2e-9\n"
        ".op\n.end\n";
    NetlistParser p;
    Circuit ckt = p.parse(netlist);
    // Expect a single BSIM4v7Device in devices().
    int mosfet_count = 0;
    for (auto &d : ckt.devices())
        if (dynamic_cast<BSIM4v7Device*>(d.get())) ++mosfet_count;
    EXPECT_EQ(1, mosfet_count);
}

TEST(ParserMosfet, UnsupportedLevelThrows) {
    const std::string netlist =
        "M1 d g s b M1\n"
        ".model M1 NMOS LEVEL=1 VT0=0.7\n"
        ".end\n";
    NetlistParser p;
    EXPECT_THROW(p.parse(netlist), ParseError);
}
```

- [ ] **Step 2: Replace the M-card handler**

In `netlist_parser.cpp:359` replace the skip-and-warn block with:

```cpp
} else if (elem_type == 'm') {
    if (tokens.size() < 6) {
        throw ParseError("Line " + std::to_string(line.line_number) +
                         ": M card requires name, nd, ng, ns, nb, modelname");
    }
    std::string name = tokens[0];
    int32_t nd = ckt.node(tokens[1]);
    int32_t ng = ckt.node(tokens[2]);
    int32_t ns = ckt.node(tokens[3]);
    int32_t nb = ckt.node(tokens[4]);
    std::string model_name = tokens[5];

    BSIM4v7Device::Geom geom{};
    for (size_t i = 6; i < tokens.size(); ++i) {
        auto eq = tokens[i].find('=');
        if (eq == std::string::npos) continue;
        std::string key = to_lower(tokens[i].substr(0, eq));
        double val = parse_number(tokens[i].substr(eq + 1));
        if      (key == "w")  geom.W  = val;
        else if (key == "l")  geom.L  = val;
        else if (key == "nf") geom.NF = val;
        else if (key == "ad") geom.AD = val;
        else if (key == "as") geom.AS = val;
        else if (key == "pd") geom.PD = val;
        else if (key == "ps") geom.PS = val;
        else if (key == "nrd") geom.NRD = val;
        else if (key == "nrs") geom.NRS = val;
        else if (key == "sa") geom.SA = val;
        else if (key == "sb") geom.SB = val;
        else if (key == "sd") geom.SD = val;
        // silently ignore the rest (UCB has ~20 more but they default cleanly)
    }
    deferred_mosfets.push_back(
        {name, nd, ng, ns, nb, model_name, geom, line.line_number});
}
```

Add a `deferred_mosfets` vector next to `deferred_diodes` and a resolution loop after the diode resolution loop:

```cpp
for (const auto &m : deferred_mosfets) {
    auto it = models.find(m.model_name);
    if (it == models.end()) {
        throw ParseError("Line " + std::to_string(m.line_number) +
                         ": Unknown model '" + m.model_name + "'");
    }
    BSIM4v7ModelCard &card = get_or_create_bsim4_card(it->second);
    ckt.add_device(BSIM4v7Device::make(m.name, m.nd, m.ng, m.ns, m.nb, m.geom, card));
}
```

`get_or_create_bsim4_card` lives in `parser/model_cards.cpp` and maps one `ModelCard` (the parsed `.model` line) to one `BSIM4v7ModelCard` (owning the `BSIM4v7Model` POD). The mapping is N:1 — duplicate references to the same `.model` share one card. Implementation: `std::unordered_map<std::string, std::unique_ptr<BSIM4v7ModelCard>>` keyed by model name, owned by the `NetlistParser` or by the `Circuit` (circuit takes ownership at the end).

- [ ] **Step 3: Implement `to_bsim4_card`**

In `model_cards.cpp`, add a function that walks `ModelCard::params` and calls the Phase-1a `BSIM4mParam` once per parameter. The parameter ID → UCB keyword mapping is already in `bsim4v7_mpar.cpp` (the `BSIM4mPTable`). For each `(key, value)` in the parsed ModelCard:

```cpp
// Look up the parameter by keyword in BSIM4mPTable to get its ID.
int id = lookup_mparm_id(to_upper(key));   // returns -1 if unknown
if (id < 0) {
    // Unknown key — warn and skip.
    fprintf(stderr, "Unknown BSIM4 model parameter: %s\n", key.c_str());
    continue;
}
bsim4v7::Shim::IfValue v;
// Set v.rValue / v.iValue / v.sValue based on the parameter's dataType.
// For LEVEL=14 the NMOS/PMOS type also needs to be set via BSIM4typeGiven.
int rc = bsim4v7::BSIM4mParam(id, &v, &card.ucb);
if (rc != 0) throw ParseError("BSIM4mParam failed on " + key);
```

LEVEL=14 is enforced in the TYPE token parsing:

```cpp
if (to_upper(model_card.type) == "NMOS") {
    card.ucb.BSIM4type = 1;     // NMOS sign convention
    card.ucb.BSIM4typeGiven = 1;
} else if (to_upper(model_card.type) == "PMOS") {
    card.ucb.BSIM4type = -1;
    card.ucb.BSIM4typeGiven = 1;
} else {
    throw ParseError("Unsupported MOS type: " + model_card.type);
}
int level = (int) get_or_default(model_card.params, "level", 14);
if (level != 14) {
    throw ParseError("Only LEVEL=14 (BSIM4v7) is supported; got LEVEL=" +
                     std::to_string(level));
}
```

- [ ] **Step 4: Run the tests**

```bash
cmake --build build --target neospice_tests -j
ctest --test-dir build -R ParserMosfet --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/parser/netlist_parser.cpp src/parser/model_cards.hpp \
        src/parser/model_cards.cpp tests/unit/test_parser_mosfet.cpp \
        tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(parser): M-card → BSIM4v7Device, LEVEL=14 → BSIM4v7ModelCard

Replaces the Phase-1a skip-and-warn stub at netlist_parser.cpp:359 with
a real handler that parses W/L/NF/AD/AS/PD/PS/NRD/NRS/SA/SB/SD, resolves
the named .model card to a shared BSIM4v7ModelCard via BSIM4mParam, and
emits a BSIM4v7Device. Only LEVEL=14 (BSIM4v7) is accepted; other LEVELs
throw ParseError.
EOF
)"
```

---

## Task 9: Re-enable `NMOS_DC_IV`

**Files:**
- Modify: `tests/unit/test_ngspice_compare.cpp`

- [ ] **Step 1: Remove the SKIP and run**

Delete the `GTEST_SKIP()` line at `test_ngspice_compare.cpp:125`:

```cpp
TEST_F(NgspiceCompareTest, NMOS_DC_IV) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/nmos_iv.cir";
    auto ng_result = ngspice_->run_dc(path);
    // ... (rest unchanged)
}
```

- [ ] **Step 2: Run**

```bash
cmake --build build -j
ctest --test-dir build -R NMOS_DC_IV --output-on-failure
```

Expected outcomes:
- **PASS**: move on to Task 10.
- **Numerical mismatch near tol=5.0**: likely a missing `BSIM4mParam` key for a param the `.cir` sets. Grep `tests/circuits/nmos_iv.cir` for all `.model` keys and confirm each one in `bsim4v7_mpar.cpp::BSIM4mPTable`.
- **Convergence failure**: check `integrator_ctx.mode` includes `MODEINITJCT` on first Newton iter; check `BSIM4setup` allocated pParam (via `BSIM4temp` lazy-first-call in the adapter).
- **Segfault**: null `CKTrhs`/`CKTstate0` pointer — adapter didn't bind; check thread_local `tls_integrator_ctx` is set before `Device::evaluate`.

- [ ] **Step 3: Commit**

```bash
git add tests/unit/test_ngspice_compare.cpp
git commit -m "$(cat <<'EOF'
test(bsim4v7): re-enable NMOS_DC_IV comparison against ngspice

BSIM4v7Device adapter (Task 7) and parser wire-up (Task 8) land the
full DC path; ID/VGS sweep at tol={5.0, 1e-6} now converges and tracks
ngspice.
EOF
)"
```

---

## Task 10: Re-enable `CMOSInverterTransient`

**Files:**
- Modify: `tests/unit/test_ngspice_compare.cpp`

Same pattern as Task 9: drop the `DISABLED_` prefix at `test_ngspice_compare.cpp:135` so the test runs. Expected failure modes:

- **Wrong Id at t=0**: operating-point seed vs transient seed. Check `.ic` is applied BEFORE the DC op used to seed transient (already fixed in M3 T2).
- **Integrator coeff wrong**: confirm `integrator_ctx.ag[0]` is populated by `transient.cpp` per step (Task 5 Step 6).
- **State rotation off-by-one**: confirm `Circuit::rotate_state()` is called between accepted steps, not before Newton.

- [ ] **Step 1: Un-disable**

Rename `DISABLED_CMOSInverterTransient` → `CMOSInverterTransient`.

- [ ] **Step 2: Run, debug, repeat**

```bash
ctest --test-dir build -R CMOSInverterTransient --output-on-failure
```

- [ ] **Step 3: Commit**

```bash
git add tests/unit/test_ngspice_compare.cpp
git commit -m "test(bsim4v7): re-enable CMOSInverterTransient"
```

---

## Task 11: Re-enable `RingOscillator5Stage`

**Files:**
- Modify: `tests/unit/test_ngspice_compare.cpp`

The historical comment at line 146-154 says this circuit fails DC from all-zero seed and needs `.ic`-as-Newton-seed. That was landed in M3 T2. Plus M3.5 added step-limiting. Plus Phase-1a/1b actually wires BSIM4. Un-disable and see what breaks.

- [ ] **Step 1: Un-disable**

Rename `DISABLED_RingOscillator5Stage` → `RingOscillator5Stage`.

- [ ] **Step 2: Run, debug, repeat**

```bash
ctest --test-dir build -R RingOscillator --output-on-failure
```

Likely issues:
- Subthreshold convergence on the unseeded nodes. Fallbacks: gmin-stepping, source-stepping, pseudo-transient. Should already be exercised by the existing DC driver.
- Feedback loop oscillation starting condition. `.ic V(n1..n5)=...` should hand a physical seed to Newton.

- [ ] **Step 3: Commit**

```bash
git add tests/unit/test_ngspice_compare.cpp
git commit -m "test(bsim4v7): re-enable RingOscillator5Stage"
```

---

## Task 12: Final review and milestone tag

**Files:** none (git operations only)

- [ ] **Step 1: Full test suite**

```bash
cmake --build build -j
ctest --test-dir build --output-on-failure 2>&1 | tail -20
```

Expected: all previously-passing tests still pass; previously-skipped `NMOS_DC_IV` now passes; `CMOSInverterTransient` and `RingOscillator5Stage` now pass. Zero skipped, zero disabled among BSIM4 tests.

- [ ] **Step 2: Line-count parity check**

```bash
wc -l src/devices/bsim4v7/bsim4v7_load.cpp
wc -l third_party/bsim4_4.7.0/code/b4ld.c 2>/dev/null \
  || wc -l $NGSPICE_DIR/src/spicelib/devices/bsim4/b4ld.c
```

Expected: within ±100 lines (banner + namespace open/close + USE_OMP branch deletion account for the delta).

- [ ] **Step 3: Final code review (subagent)**

Use `superpowers:requesting-code-review` and dispatch a reviewer against the full Phase 1b diff:

```bash
git log --oneline "m4-phase1a-complete..HEAD"
```

Reviewer checks: mechanical-translation-rule adherence on `bsim4v7_load.cpp`, absence of scope creep on the adapter, test coverage across DC/transient/feedback regime, no secrets committed, no un-reviewed TODO markers.

- [ ] **Step 4: Tag**

```bash
git tag -a m4-phase1b-complete -m "$(cat <<'EOF'
BSIM4v7 UCB Z-port Phase 1b complete

- b4ld.c ported to bsim4v7_load.cpp (5601-line mechanical translation)
- Device interface extended with state_vars()/set_state_ptrs()
- Shim::Ckt grew state ring, CKTag, CKTrhs, mat binding
- Two-phase MatrixOffset resolution replaces the Phase-1a sentinel
- BSIM4v7Device adapter + parser M-card wire-up
- NMOS_DC_IV, CMOSInverterTransient, RingOscillator5Stage all passing
  against ngspice goldens
EOF
)"
```

- [ ] **Step 5: Close milestone memory**

```
memory_add {
  space: "projects/spice-cpp",
  name: "m4-phase1b-complete",
  content: "Phase 1b of the BSIM4v7 UCB Z-port. b4ld.c translated to
            bsim4v7_load.cpp; BSIM4v7Device adapter bridges UCB kernel
            to neospice Device interface; parser M-card handler wires
            LEVEL=14 models; 3 previously-parked circuit tests now pass
            against ngspice. See tag m4-phase1b-complete.",
  tags: ["area:bsim4v7", "cat:decision", "milestone:4", "status:complete"],
  links_to: ["m4-phase1a-ucb-port-complete"]
}
```

---

## Self-review checklist

1. **Spec coverage:** Goal names three deliverables — b4ld.c port (Task 6), Device wire-up (Tasks 3+7+8), re-enable three tests (Tasks 9-11). ✅
2. **Placeholder scan:**
   - Task 7 Step 1 intentionally contains a TBD for the ngspice-captured golden Id; Step 2 resolves it before Step 4. Acceptable because the plan explicitly marks it and gates the commit on resolution.
   - Task 7 Step 4 `// ... repeat for every BSIM4*Ptr field` — acceptable because the preceding sentence gives the exact procedure (grep `BSIM4.*Ptr` in `bsim4v7_setup.cpp`, emit one `RESOLVE` per match). Engineer has a deterministic derivation, not a judgment call.
   - Task 6 Step 3 `// ... (transcribe all UCB defines here, top of b4ld.c)` — acceptable because the source file is the reference and the engineer copies verbatim; the preceding banner block is shown explicitly.
3. **Type consistency:** `BSIM4v7Device`, `BSIM4v7ModelCard`, `Shim::Ckt`, `Shim::Matrix::make_elt`, `Shim::Matrix::resolve_offsets`, `Device::state_vars`, `Device::set_state_ptrs`, `Circuit::IntegratorCtx`, `tls_integrator_ctx` — used consistently across Tasks 2-8.
4. **Risk check:** Task 6 is the biggest risk (5601-line translation). Mitigation: Phase-1a established the translation pattern on a 2521-line file (`b4set.c`), so the process is proven; pick Opus; allow 3-6 hours; ship as one commit; no mid-translation review gate because the file won't compile until the whole function is translated.
