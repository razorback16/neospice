# BSIM4v7 Phase 2: Internal-Node Plumbing + Resistance-Model Coverage

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Enable BSIM4v7 resistance models (RDSMOD, RGATEMOD, RBODYMOD) by plumbing internal-node allocation from BSIM4setup through Circuit, and validate with ngspice-compared DC + transient tests.

**Architecture:** Add a `declare_internal_nodes(Circuit&)` phase to Device that runs before branch assignment in `Circuit::finalize()`. BSIM4v7Device moves BSIM4setup to this phase, delegating `add_internal_node` calls to `Circuit::node()` for real index allocation. `stamp_pattern` becomes a journal replay. Ghost arrays in `evaluate()` naturally cover internal nodes because max_neo_node_ is updated.

**Tech Stack:** C++17, GTest, ngspice (reference), CMake

**Baseline:** Tag `m4-phase1b-complete` (commit 0a958b8), 126/126 tests passing.

---

## File Structure

| File | Responsibility | Action |
|------|---------------|--------|
| `src/devices/device.hpp` | Device interface | Add `declare_internal_nodes` virtual |
| `src/core/circuit.cpp` | Circuit orchestration | Call `declare_internal_nodes` before branch assignment |
| `src/devices/bsim4v7/bsim4v7_shim.hpp` | UCB ↔ neospice bridge | Add node-allocation callback to `Shim::Ckt` |
| `src/devices/bsim4v7/bsim4v7_shim.cpp` | Shim implementation | Delegate `add_internal_node` to callback |
| `src/devices/bsim4v7/bsim4v7_device.hpp` | BSIM4 adapter header | Add `declare_internal_nodes` override |
| `src/devices/bsim4v7/bsim4v7_device.cpp` | BSIM4 adapter impl | Move setup to declare phase, remove Phase-1b guard |
| `tests/circuits/nmos_rdsmod.cir` | Test circuit | New: NMOS with RDSMOD=1 |
| `tests/circuits/nmos_rgatemod.cir` | Test circuit | New: NMOS with RGATEMOD=1 |
| `tests/circuits/nmos_rbodymod.cir` | Test circuit | New: NMOS with RBODYMOD=1 |
| `tests/circuits/cmos_inverter_resistance.cir` | Test circuit | New: CMOS inverter with resistance models |
| `tests/unit/test_ngspice_compare.cpp` | Ngspice comparison tests | Add 4 new test cases |
| `tests/unit/test_internal_nodes.cpp` | Unit tests | New: verify internal-node allocation mechanics |
| `tests/CMakeLists.txt` | Test registration | Add new test file |
| `tests/framework/comparator.cpp` | Oscillator comparator | Add mid-offset check (I-3) |
| `tests/unit/test_oscillator_comparator.cpp` | Comparator unit tests | Add mid-offset test case |

---

### Task 1: Device interface `declare_internal_nodes` + Circuit::finalize reorder

**Files:**
- Modify: `src/devices/device.hpp:11-62`
- Modify: `src/core/circuit.cpp:63-101`
- Test: existing 126 tests (regression only — default no-op)

- [ ] **Step 1: Add virtual method to Device**

In `src/devices/device.hpp`, add after the `stamp_pattern` declaration (line 17):

```cpp
    /// Called by Circuit::finalize() before branch assignment and sparsity
    /// build. Devices that need internal MNA nodes (e.g. BSIM4 resistance
    /// models) override this to allocate them via ckt.node().
    virtual void declare_internal_nodes(Circuit& /*ckt*/) {}
```

Add a forward declaration at the top of the file (before the `neospice` namespace):

```cpp
class Circuit;  // forward decl for declare_internal_nodes
```

- [ ] **Step 2: Update Circuit::finalize to call declare_internal_nodes first**

In `src/core/circuit.cpp`, modify `finalize()` to insert the new phase before branch assignment. The existing code at line 63-79 becomes:

```cpp
void Circuit::finalize() {
    // 0. Let devices declare internal nodes. These get allocated from
    //    next_node_ via the normal Circuit::node() path, so they appear
    //    before branch indices in the MNA variable numbering.
    for (auto& dev : devices_) {
        dev->declare_internal_nodes(*this);
    }

    // 1. Assign branch indices for devices that carry extra MNA variables.
    //    Branch indices start right after ALL node variables (external +
    //    internal, since internal nodes were just allocated above).
    int32_t branch_idx = next_node_;
    // ... rest unchanged
```

- [ ] **Step 3: Run full test suite**

Run: `cd build && cmake .. && make -j$(nproc) && ctest`
Expected: 126/126 pass (no behavior change — default `declare_internal_nodes` is a no-op)

- [ ] **Step 4: Commit**

```bash
git add src/devices/device.hpp src/core/circuit.cpp
git commit -m "feat(core): add Device::declare_internal_nodes phase to Circuit::finalize

Devices that need internal MNA nodes (e.g. BSIM4 resistance models)
can now allocate them from Circuit::node() during finalize(), before
branch indices are assigned.  Default is a no-op; BSIM4v7Device will
override in the next commit."
```

---

### Task 2: Shim::Ckt node allocation callback + BSIM4v7Device internal-node plumbing

This is the core task: wire real node allocation through BSIM4setup, move setup from `stamp_pattern` to `declare_internal_nodes`, remove the Phase-1b guard.

**Files:**
- Modify: `src/devices/bsim4v7/bsim4v7_shim.hpp:63-106`
- Modify: `src/devices/bsim4v7/bsim4v7_shim.cpp:28-29`
- Modify: `src/devices/bsim4v7/bsim4v7_device.hpp:44-104`
- Modify: `src/devices/bsim4v7/bsim4v7_device.cpp:108-248`
- Create: `tests/unit/test_internal_nodes.cpp`
- Modify: `tests/CMakeLists.txt`
- Test: `tests/unit/test_internal_nodes.cpp` + full suite regression

- [ ] **Step 1: Add node-allocation callback to Shim::Ckt**

In `src/devices/bsim4v7/bsim4v7_shim.hpp`, add to struct Ckt (after line 105):

```cpp
    #include <functional>  // at top of file

    // Node allocation callback. When set, add_internal_node delegates
    // to this instead of incrementing the stub counter. The callback
    // receives the UCB node name and must return a UCB-convention index
    // (>= 1 for real nodes, 0 for ground).
    std::function<int(const char*)> node_alloc;
```

In `src/devices/bsim4v7/bsim4v7_shim.cpp`, update `add_internal_node`:

```cpp
int Ckt::add_internal_node(const char *name) {
    if (node_alloc) return node_alloc(name);
    return CKTinternalNodeCounter++;
}
```

- [ ] **Step 2: Add declare_internal_nodes override to BSIM4v7Device**

In `src/devices/bsim4v7/bsim4v7_device.hpp`, add to the public section (after `stamp_pattern`):

```cpp
    void declare_internal_nodes(Circuit& ckt) override;
```

- [ ] **Step 3: Implement declare_internal_nodes — move BSIM4setup here**

In `src/devices/bsim4v7/bsim4v7_device.cpp`, add the implementation. This is essentially the body of the current `stamp_pattern` up to the journal capture, with the key change that `setup_ckt.node_alloc` delegates to Circuit:

```cpp
void BSIM4v7Device::declare_internal_nodes(Circuit& ckt) {
    constexpr double T_NOMINAL = 300.15;

    SparsityBuilder scratch(1);
    Shim::Ckt setup_ckt;
    setup_ckt.CKTtemp    = T_NOMINAL;
    setup_ckt.CKTnomTemp = T_NOMINAL;
    setup_ckt.CKTinternalNodeCounter = 1000;  // fallback seed (unused when callback is set)

    // Delegate internal-node allocation to Circuit::node().  The lambda
    // converts the returned neospice index (>=0) to UCB convention (>=1)
    // so BSIM4setup sees consistent coordinates.
    setup_ckt.node_alloc = [&ckt, this](const char* name) -> int {
        std::string full = "__" + name_ + "_" + name;
        int32_t neo = ckt.node(full);
        return neo + 1;  // UCB convention: ground=0, real>=1
    };

    Shim::Matrix shim_matrix(scratch);
    int states = 0;
    int rc = BSIM4setup(&shim_matrix,
                        const_cast<BSIM4v7Model*>(model_),
                        &setup_ckt, &states);
    if (rc != Shim::OK) {
        throw std::runtime_error("BSIM4setup failed with rc=" + std::to_string(rc));
    }

    // Capture the journal for stamp_pattern / assign_offsets replay.
    const auto& journal = shim_matrix.reservation_journal();
    journal_.assign(journal.begin(), journal.end());

    // Recompute max_neo_node_ to cover internal nodes.  Some journal entries
    // reference internal-node UCB indices (allocated above via Circuit::node)
    // that are larger than any external node index this device was handed at
    // make() time.  The ghost rhs/voltage arrays in evaluate() must be sized
    // to cover the widest UCB index.
    for (auto [r, c] : journal_) {
        int mx = std::max(r, c);
        if (mx > 0) {
            int32_t neo = mx - 1;  // UCB -> neospice
            if (neo > max_neo_node_) max_neo_node_ = neo;
        }
    }
}
```

- [ ] **Step 4: Simplify stamp_pattern to journal replay**

Replace the current `stamp_pattern` body in `bsim4v7_device.cpp` (lines 118-177) with:

```cpp
void BSIM4v7Device::stamp_pattern(SparsityBuilder& builder) const {
    // Journal was populated by declare_internal_nodes (BSIM4setup).
    // Replay non-ground entries into the real builder, shifting from UCB
    // coords (>=1) to neospice coords (>=0).
    for (auto [r, c] : journal_) {
        if (r <= 0 || c <= 0) continue;
        builder.add(r - 1, c - 1);
    }
}
```

This removes the entire BSIM4setup call, the Shim::Matrix/Ckt setup, the Phase-1b internal-node guard, and the scratch builder.

- [ ] **Step 5: Write unit test for internal-node allocation**

Create `tests/unit/test_internal_nodes.cpp`:

```cpp
#include <gtest/gtest.h>
#include "api/neospice.hpp"
#include "core/circuit.hpp"

namespace neospice {

// Verify that a BSIM4 device with RDSMOD=1 allocates internal nodes
// and produces a Circuit with more MNA vars than external nodes alone.
TEST(InternalNodes, RdsModAllocatesInternalDrainSource) {
    Simulator sim;
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/nmos_rdsmod.cir";
    auto ckt = sim.load(path);
    // nmos_rdsmod.cir has nodes: drain, gate, 0 (ground) → 2 external nodes.
    // RDSMOD=1 allocates dNodePrime + sNodePrime → 2 internal nodes.
    // VSource adds 2 branch vars.  Total MNA vars = 2 + 2 + 2 = 6.
    // Without internal nodes it would be 2 + 2 = 4.
    EXPECT_GT(ckt.num_vars(), 4)
        << "RDSMOD=1 should allocate internal drain/source nodes";
}

TEST(InternalNodes, IntrinsicPathNoExtraNodes) {
    Simulator sim;
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/nmos_iv.cir";
    auto ckt = sim.load(path);
    // nmos_iv.cir: drain, gate → 2 external nodes, 2 VSource branches.
    EXPECT_EQ(ckt.num_vars(), 4)
        << "Intrinsic path (no resistance models) should not add internal nodes";
}

} // namespace neospice
```

- [ ] **Step 6: Create RDSMOD test circuit**

Create `tests/circuits/nmos_rdsmod.cir`:

```spice
NMOS with RDSMOD (drain/source resistance)
V1 drain 0 1.0
V2 gate 0 1.0
M1 drain gate 0 0 NMOD W=1u L=100n
.model NMOD NMOS LEVEL=14 VTH0=0.4 U0=0.04 TOXE=2e-9 RDSMOD=1 RDSW=200
.op
.end
```

- [ ] **Step 7: Wire test into CMakeLists.txt**

Add to `tests/CMakeLists.txt` in the source list:

```cmake
    unit/test_internal_nodes.cpp
```

- [ ] **Step 8: Build and run tests**

Run: `cd build && cmake .. && make -j$(nproc) && ctest`
Expected: All previous 126 tests pass + 2 new tests pass = 128 total. If InternalNodes.RdsModAllocatesInternalDrainSource fails, the RDSMOD=1 path is not allocating internal nodes correctly — debug from there.

- [ ] **Step 9: Commit**

```bash
git add src/devices/bsim4v7/bsim4v7_shim.hpp src/devices/bsim4v7/bsim4v7_shim.cpp \
        src/devices/bsim4v7/bsim4v7_device.hpp src/devices/bsim4v7/bsim4v7_device.cpp \
        tests/unit/test_internal_nodes.cpp tests/circuits/nmos_rdsmod.cir tests/CMakeLists.txt
git commit -m "feat(bsim4v7): wire internal-node allocation through Circuit

Shim::Ckt::add_internal_node now delegates to a callback that allocates
real MNA variables via Circuit::node().  BSIM4setup runs during the new
declare_internal_nodes phase (before branch assignment), and stamp_pattern
replays the captured journal.  The Phase-1b internal-node guard is
removed.  Ghost arrays size correctly because max_neo_node_ is recomputed
from the journal after setup.

RDSMOD=1 now allocates dNodePrime + sNodePrime as real Circuit nodes.
Verified by new InternalNodes unit tests."
```

---

### Task 3: RDSMOD DC ngspice comparison

**Files:**
- Test circuit: `tests/circuits/nmos_rdsmod.cir` (created in T2)
- Modify: `tests/unit/test_ngspice_compare.cpp`

- [ ] **Step 1: Add ngspice DC comparison test for RDSMOD**

In `tests/unit/test_ngspice_compare.cpp`, add after the existing NMOS_DC_IV test:

```cpp
TEST_F(NgspiceCompareTest, NMOS_DC_RDSMOD) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/nmos_rdsmod.cir";
    auto ng_result = ngspice_->run_dc(path);
    auto ckt = sim_.load(path);
    auto cs_result = sim_.run(ckt);
    ASSERT_TRUE(cs_result.dc.has_value());
    auto cmp = compare_dc(*cs_result.dc, ng_result, {1e-3, 1e-9});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}
```

- [ ] **Step 2: Run the test**

Run: `cd build && make -j$(nproc) && ctest -R NMOS_DC_RDSMOD -V`
Expected: PASS — DC node voltages match ngspice within 1e-3 relative tolerance. If it fails, inspect the worst signal: the drain voltage should differ slightly from the no-resistance case due to the voltage drop across Rds.

- [ ] **Step 3: Commit**

```bash
git add tests/unit/test_ngspice_compare.cpp
git commit -m "test(bsim4v7): NMOS DC with RDSMOD=1 matches ngspice"
```

---

### Task 4: RGATEMOD DC ngspice comparison

**Files:**
- Create: `tests/circuits/nmos_rgatemod.cir`
- Modify: `tests/unit/test_ngspice_compare.cpp`

- [ ] **Step 1: Create RGATEMOD test circuit**

Create `tests/circuits/nmos_rgatemod.cir`:

```spice
NMOS with RGATEMOD (gate resistance)
V1 drain 0 1.0
V2 gate 0 1.0
M1 drain gate 0 0 NMOD W=1u L=100n
.model NMOD NMOS LEVEL=14 VTH0=0.4 U0=0.04 TOXE=2e-9 RGATEMOD=1 RSHG=10
.op
.end
```

Note: RGATEMOD=1 creates one internal node (gNodePrime). RSHG provides the gate sheet resistance. Value of 10 ohm/sq is typical.

- [ ] **Step 2: Add ngspice comparison test**

In `tests/unit/test_ngspice_compare.cpp`:

```cpp
TEST_F(NgspiceCompareTest, NMOS_DC_RGATEMOD) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/nmos_rgatemod.cir";
    auto ng_result = ngspice_->run_dc(path);
    auto ckt = sim_.load(path);
    auto cs_result = sim_.run(ckt);
    ASSERT_TRUE(cs_result.dc.has_value());
    auto cmp = compare_dc(*cs_result.dc, ng_result, {1e-3, 1e-9});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}
```

- [ ] **Step 3: Run and verify**

Run: `cd build && make -j$(nproc) && ctest -R NMOS_DC_RGATEMOD -V`
Expected: PASS. Gate resistance at DC is just a series R in the gate path; for DC op-point the gate current is ~0 so voltage drop is negligible, but the internal node exists and the matrix is correctly sized.

- [ ] **Step 4: Add internal-node count verification**

In `tests/unit/test_internal_nodes.cpp`, add:

```cpp
TEST(InternalNodes, RgateModAllocatesInternalGate) {
    Simulator sim;
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/nmos_rgatemod.cir";
    auto ckt = sim.load(path);
    // drain, gate = 2 external nodes + 1 internal (gNodePrime) + 2 VSource branches = 5
    EXPECT_GT(ckt.num_vars(), 4)
        << "RGATEMOD=1 should allocate internal gate node";
}
```

- [ ] **Step 5: Run full suite and commit**

Run: `cd build && make -j$(nproc) && ctest`
Expected: All tests pass.

```bash
git add tests/circuits/nmos_rgatemod.cir tests/unit/test_ngspice_compare.cpp \
        tests/unit/test_internal_nodes.cpp
git commit -m "test(bsim4v7): NMOS DC with RGATEMOD=1 matches ngspice"
```

---

### Task 5: RBODYMOD DC ngspice comparison

**Files:**
- Create: `tests/circuits/nmos_rbodymod.cir`
- Modify: `tests/unit/test_ngspice_compare.cpp`
- Modify: `tests/unit/test_internal_nodes.cpp`

- [ ] **Step 1: Create RBODYMOD test circuit**

Create `tests/circuits/nmos_rbodymod.cir`:

```spice
NMOS with RBODYMOD (body resistance network)
V1 drain 0 1.0
V2 gate 0 1.0
M1 drain gate 0 0 NMOD W=1u L=100n
.model NMOD NMOS LEVEL=14 VTH0=0.4 U0=0.04 TOXE=2e-9 RBODYMOD=1 RBDB=100 RBSB=100 RBPB=100 RBPS=100 RBPD=100
.op
.end
```

RBODYMOD=1 creates 3 internal nodes: dbNode, bNodePrime, sbNode. The RB* parameters provide body resistance values.

- [ ] **Step 2: Add ngspice comparison test**

In `tests/unit/test_ngspice_compare.cpp`:

```cpp
TEST_F(NgspiceCompareTest, NMOS_DC_RBODYMOD) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/nmos_rbodymod.cir";
    auto ng_result = ngspice_->run_dc(path);
    auto ckt = sim_.load(path);
    auto cs_result = sim_.run(ckt);
    ASSERT_TRUE(cs_result.dc.has_value());
    auto cmp = compare_dc(*cs_result.dc, ng_result, {1e-3, 1e-9});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}
```

- [ ] **Step 3: Add internal-node count verification**

In `tests/unit/test_internal_nodes.cpp`:

```cpp
TEST(InternalNodes, RbodyModAllocatesInternalBodyNodes) {
    Simulator sim;
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/nmos_rbodymod.cir";
    auto ckt = sim.load(path);
    // drain, gate = 2 external + 3 internal (dbNode, bNodePrime, sbNode) + 2 VSource = 7
    EXPECT_GT(ckt.num_vars(), 4)
        << "RBODYMOD=1 should allocate 3 internal body nodes";
}
```

- [ ] **Step 4: Run full suite and commit**

Run: `cd build && make -j$(nproc) && ctest`
Expected: All tests pass.

```bash
git add tests/circuits/nmos_rbodymod.cir tests/unit/test_ngspice_compare.cpp \
        tests/unit/test_internal_nodes.cpp
git commit -m "test(bsim4v7): NMOS DC with RBODYMOD=1 matches ngspice"
```

---

### Task 6: CMOS inverter transient with resistance models

**Files:**
- Create: `tests/circuits/cmos_inverter_resistance.cir`
- Modify: `tests/unit/test_ngspice_compare.cpp`

- [ ] **Step 1: Create CMOS inverter with resistance models**

Create `tests/circuits/cmos_inverter_resistance.cir`:

```spice
CMOS Inverter with drain/source and gate resistance
VDD vdd 0 1.8
VIN in 0 PULSE(0 1.8 100p 100p 100p 1n 2.2n)
M1 out in vdd vdd PMOD W=2u L=100n
M2 out in 0 0 NMOD W=1u L=100n
.model NMOD NMOS LEVEL=14 VTH0=0.4 U0=0.04 TOXE=2e-9 RDSMOD=1 RDSW=200 RGATEMOD=1 RSHG=10
.model PMOD PMOS LEVEL=14 VTH0=-0.4 U0=0.02 TOXE=2e-9 RDSMOD=1 RDSW=200 RGATEMOD=1 RSHG=10
.tran 10p 4n
.end
```

This exercises internal nodes under transient simulation with the Gear-2 integrator. Each MOSFET has 3 internal nodes (dPrime, sPrime, gPrime), total 6 internal nodes for 2 devices.

- [ ] **Step 2: Add transient comparison test**

In `tests/unit/test_ngspice_compare.cpp`:

```cpp
TEST_F(NgspiceCompareTest, CMOSInverterTransientWithResistance) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/cmos_inverter_resistance.cir";
    auto ng_result = ngspice_->run_transient(path);
    auto ckt = sim_.load(path);
    auto cs_result = sim_.run(ckt);
    ASSERT_TRUE(cs_result.transient.has_value());
    // Gear-2 vs Trap mismatch + resistance-model RC delay — use same
    // tolerance as the intrinsic CMOS inverter test.
    auto cmp = compare_transient(*cs_result.transient, ng_result, {5e-1, 5e-2});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}
```

- [ ] **Step 3: Run and verify**

Run: `cd build && make -j$(nproc) && ctest -R CMOSInverterTransientWithResistance -V`
Expected: PASS. The resistance models add small RC delays to the switching edges compared to the intrinsic case. If it fails with worse error than expected, inspect the waveform; it may need slightly looser tolerance for the additional model complexity.

- [ ] **Step 4: Run full suite and commit**

Run: `cd build && make -j$(nproc) && ctest`
Expected: All tests pass.

```bash
git add tests/circuits/cmos_inverter_resistance.cir tests/unit/test_ngspice_compare.cpp
git commit -m "test(bsim4v7): CMOS inverter transient with RDSMOD+RGATEMOD matches ngspice"
```

---

### Task 7: Oscillator comparator mid-offset check (I-3 from Phase 1b review)

**Files:**
- Modify: `tests/framework/comparator.cpp`
- Modify: `tests/framework/comparator.hpp` (if OscillatorTolerance needs a field)
- Modify: `tests/unit/test_oscillator_comparator.cpp`

- [ ] **Step 1: Add dc_mid_tolerance to OscillatorTolerance**

In `tests/framework/comparator.hpp`, add to `OscillatorTolerance`:

```cpp
    double mid_absolute = 1e-1;    // 100 mV: max allowed |mid_expected - mid_actual| for oscillating signals
```

- [ ] **Step 2: Add mid-offset comparison to compare_transient_oscillator**

In `tests/framework/comparator.cpp`, in the oscillating-signal comparison path (after period and amplitude checks pass), add:

```cpp
    // Mid-level (DC bias) offset check — catches a bug where the waveform
    // oscillates at the right frequency/amplitude but is shifted up/down.
    double mid_err = std::abs(exp_info.mid - act_info.mid);
    if (mid_err > tol.mid_absolute) {
        result.passed = false;
        if (mid_err > result.worst_error) {
            result.worst_error = mid_err;
            result.worst_signal = sig_name + " (mid-offset)";
        }
        continue;
    }
```

Where `exp_info.mid` and `act_info.mid` are the midpoint levels already computed by `analyze_signal`.

- [ ] **Step 3: Add unit test for mid-offset detection**

In `tests/unit/test_oscillator_comparator.cpp`, add:

```cpp
TEST(OscillatorComparator, DetectsMidOffsetShift) {
    // Two 1 MHz sines with same period and amplitude, but one is DC-shifted
    // by 0.5 V (mid = 0 vs mid = 0.5).
    TransientResult expected, actual;
    expected.time = /* 4 us of 1 MHz sine around 0 */;
    actual.time   = /* 4 us of 1 MHz sine around 0.5 */;
    // ... (generate with sin(2pi*1e6*t) vs 0.5 + sin(2pi*1e6*t))
    OscillatorTolerance tol;
    tol.mid_absolute = 0.1;  // 100 mV tolerance — 500 mV shift should fail
    auto cmp = compare_transient_oscillator(expected, actual, tol);
    EXPECT_FALSE(cmp.passed);
    EXPECT_NE(cmp.worst_signal.find("mid-offset"), std::string::npos);
}
```

- [ ] **Step 4: Run tests and commit**

Run: `cd build && make -j$(nproc) && ctest`
Expected: All tests pass including the new mid-offset test and existing ring oscillator test (ring osc mid-level is VDD/2 for both simulators, well within 100 mV).

```bash
git add tests/framework/comparator.hpp tests/framework/comparator.cpp \
        tests/unit/test_oscillator_comparator.cpp
git commit -m "feat(tests): add mid-offset check to oscillator comparator (I-3)

Oscillating signals now also compare their DC bias (midpoint level).
Catches a class of bugs where period+amplitude match but the waveform
is vertically shifted.  Default tolerance: 100 mV."
```

---

### Task 8: Final review + tag `m4-phase2-complete`

**Files:** All files touched in T1–T7

- [ ] **Step 1: Run full test suite**

Run: `cd build && cmake .. && make -j$(nproc) && ctest`
Expected: All tests pass. Count should be ~134+ (126 baseline + 4 ngspice-compare + 3 internal-node + 1 mid-offset = 134).

- [ ] **Step 2: Verify line-count parity**

Run: `wc -l src/devices/bsim4v7/bsim4v7_load.cpp third_party/bsim4_4.7.0/code/b4ld.c`
Expected: ±100 lines (should be unchanged from Phase 1b — no load.cpp modifications in Phase 2).

- [ ] **Step 3: Verify internal-node circuits work with CLI**

```bash
echo "NMOS rdsmod DC:" && echo ".op" | cat tests/circuits/nmos_rdsmod.cir - | ./build/neospice /dev/stdin 2>&1 | head -5
```

Expected: CLI produces output without crashing (it links bsim4v7_obj since Phase 1b).

- [ ] **Step 4: Dispatch final code reviewer**

Use `superpowers:code-reviewer` subagent to review the full Phase 2 diff:
- BASE_SHA: `0a958b8` (m4-phase1b-complete)
- HEAD_SHA: current HEAD
- Focus: internal-node lifecycle, ghost-array sizing correctness, no UB in the UCB→neospice index translation, exception safety of the node_alloc callback

- [ ] **Step 5: Fix any review issues**

Address Critical and Important issues before tagging.

- [ ] **Step 6: Tag**

```bash
git tag -a m4-phase2-complete -m "Milestone 4 BSIM4v7 Phase 2: internal-node plumbing + resistance models"
```

- [ ] **Step 7: Save memory entry**

Record `m4-phase2-complete` memory entry linking to `m4-phase1b-complete`.
