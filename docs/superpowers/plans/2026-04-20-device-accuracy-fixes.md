# Device Accuracy Fixes Implementation Plan ✅ COMPLETE

> **Status:** All 16 tasks completed and merged to `main`. Executed 2026-04-20.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix all ~40 discrepancies between neospice's custom device models and the ngspice reference implementation, organized into 4 phases by severity.

**Architecture:** Fix-by-severity with infrastructure grouping. Each task produces a tested, self-contained commit. Phase 1 fixes critical formula bugs (wrong results now). Phase 2 fixes accuracy issues (LTE, Gear-2 coefficients, defaults). Phase 3 adds missing parameters (temperature, multiplier, geometry). Phase 4 is polish (edge cases, comments).

**Tech Stack:** C++20, Google Test, ngspice comparison framework (`NgspiceRunner` + `Comparator`)

---

## Phase 1: Critical Formula/Sign Bugs

### Task 1: Fix Coupled Inductor RHS Sign Error + Double-Counting

**Files:**
- Modify: `src/devices/coupled_inductor.cpp`
- Modify: `src/devices/coupled_inductor.hpp`
- Create: `tests/circuits/coupled_inductor_transient.cir`
- Modify: `tests/unit/test_ngspice_compare.cpp`

- [ ] **Step 1: Create the ngspice comparison test circuit**

```spice
* tests/circuits/coupled_inductor_transient.cir
* Coupled inductor transient test: pulse through transformer
V1 in 0 PULSE(0 5 0 1u 1u 50u 200u)
R1 in a 10
L1 a 0 10mH
L2 b 0 10mH
K1 L1 L2 0.5
R2 b 0 100
.tran 1u 400u
.end
```

- [ ] **Step 2: Add ngspice comparison test**

In `tests/unit/test_ngspice_compare.cpp`, add after the existing transient tests:

```cpp
TEST_F(NgspiceCompareTest, CoupledInductorTransient) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/coupled_inductor_transient.cir";
    auto ng_result = ngspice_->run_transient(path);
    auto ckt = sim_.load(path);
    auto cs_result = sim_.run(ckt);
    ASSERT_TRUE(cs_result.transient.has_value());
    auto cmp = compare_transient(*cs_result.transient, ng_result, {1e-2, 1e-3});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}
```

- [ ] **Step 3: Run test to verify it fails**

Run: `cd build-release && cmake --build . --target neospice_tests -j$(nproc) && ./tests/neospice_tests --gtest_filter='*CoupledInductorTransient*'`
Expected: FAIL (sign error produces wrong waveform)

- [ ] **Step 4: Fix the trapezoidal RHS — remove double-counting and flip sign**

In `src/devices/coupled_inductor.cpp`, replace the trapezoidal branch (the `else` block starting at line 62):

```cpp
    } else {
        // Trapezoidal: R_eq_m = 2 * M / dt
        r_eq_m = 2.0 * mutual_ / dt_;

        // Companion: V_eq = R_eq_m * I_partner_prev
        // The inductor's own v_prev_ already captures the full terminal voltage
        // (including mutual contribution), so we must NOT add v_m_prev_ here.
        double v_eq_12 = r_eq_m * i2_prev_;
        double v_eq_21 = r_eq_m * i1_prev_;

        add_if_valid(mat, off_br1_br2_, -r_eq_m);
        add_if_valid(mat, off_br2_br1_, -r_eq_m);

        int32_t br1 = l1_->branch_index();
        int32_t br2 = l2_->branch_index();
        add_rhs_if_valid(rhs, br1, -v_eq_12);
        add_rhs_if_valid(rhs, br2, -v_eq_21);
    }
```

- [ ] **Step 5: Fix the Gear-2 RHS — flip sign**

In `src/devices/coupled_inductor.cpp`, in the Gear-2 branch (starting at line 47), change the RHS stamps:

```cpp
    if (integration_method_ == 1 && gear_ready_) {
        // Gear-2 (BDF2): R_eq_m = 1.5 * M / dt
        r_eq_m = 1.5 * mutual_ / dt_;

        // Gear-2 companion: V_eq = (M / (2*dt)) * (4*I_prev - I_prev2)
        double v_eq_12 = (mutual_ / (2.0 * dt_)) * (4.0 * i2_prev_ - i2_prev2_);
        double v_eq_21 = (mutual_ / (2.0 * dt_)) * (4.0 * i1_prev_ - i1_prev2_);

        add_if_valid(mat, off_br1_br2_, -r_eq_m);
        add_if_valid(mat, off_br2_br1_, -r_eq_m);

        int32_t br1 = l1_->branch_index();
        int32_t br2 = l2_->branch_index();
        add_rhs_if_valid(rhs, br1, -v_eq_12);
        add_rhs_if_valid(rhs, br2, -v_eq_21);
    }
```

- [ ] **Step 6: Remove v_m12_prev_ / v_m21_prev_ member variables**

In `src/devices/coupled_inductor.hpp`, remove lines 59-60:

```cpp
    // DELETE these two member declarations:
    // double v_m12_prev_ = 0.0;
    // double v_m21_prev_ = 0.0;
```

In `src/devices/coupled_inductor.cpp`, update `init_dc_state()` — remove lines 112-113:

```cpp
void CoupledInductor::init_dc_state(const std::vector<double>& sol) {
    int32_t br1 = l1_->branch_index();
    int32_t br2 = l2_->branch_index();
    i1_prev_ = (br1 >= 0) ? sol[br1] : 0.0;
    i2_prev_ = (br2 >= 0) ? sol[br2] : 0.0;
    i1_prev2_ = i1_prev_;
    i2_prev2_ = i2_prev_;
    gear_ready_ = false;
}
```

In `accept_step_from_solution()`, remove the trapezoidal v_m_prev update block (lines 123-130):

```cpp
void CoupledInductor::accept_step_from_solution(const std::vector<double>& sol) {
    int32_t br1 = l1_->branch_index();
    int32_t br2 = l2_->branch_index();
    double i1 = (br1 >= 0) ? sol[br1] : 0.0;
    double i2 = (br2 >= 0) ? sol[br2] : 0.0;

    // Shift history
    i1_prev2_ = i1_prev_;
    i2_prev2_ = i2_prev_;
    i1_prev_ = i1;
    i2_prev_ = i2;

    if (integration_method_ == 1 && !gear_ready_) {
        gear_ready_ = true;
    }
}
```

Also update the header comment in `coupled_inductor.hpp` (line 18-19) to reflect the corrected formula:

```cpp
///   mat[br1, br2] += -R_eq_m      rhs[br1] -= R_eq_m * I2_prev
///   mat[br2, br1] += -R_eq_m      rhs[br2] -= R_eq_m * I1_prev
```

- [ ] **Step 7: Build and run test**

Run: `cd build-release && cmake --build . --target neospice_tests -j$(nproc) && ./tests/neospice_tests --gtest_filter='*CoupledInductor*'`
Expected: PASS (both new transient test and existing DC/AC tests)

- [ ] **Step 8: Run full test suite for regressions**

Run: `cd build-release && ctest -j$(nproc) --output-on-failure`
Expected: All existing tests pass

- [ ] **Step 9: Commit**

```bash
git add src/devices/coupled_inductor.cpp src/devices/coupled_inductor.hpp tests/circuits/coupled_inductor_transient.cir tests/unit/test_ngspice_compare.cpp
git commit -m "fix: correct coupled inductor RHS sign error and double-counting"
```

---

### Task 2: Add Current Source AC Excitation

**Files:**
- Modify: `src/core/ac.cpp`
- Modify: `src/devices/isource.hpp` (add node accessors)
- Create: `tests/circuits/isrc_ac.cir`
- Modify: `tests/unit/test_ngspice_compare.cpp`

- [ ] **Step 1: Create test circuit**

```spice
* tests/circuits/isrc_ac.cir
* AC analysis driven by current source
I1 0 in AC 1m
R1 in out 1k
C1 out 0 100n
.ac dec 10 1 1Meg
.end
```

- [ ] **Step 2: Add ngspice comparison test**

```cpp
TEST_F(NgspiceCompareTest, IsrcACAnalysis) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/isrc_ac.cir";
    auto ng_result = ngspice_->run_ac(path);
    auto ckt = sim_.load(path);
    auto cs_result = sim_.run(ckt);
    ASSERT_TRUE(cs_result.ac.has_value());
    auto cmp = compare_ac(ng_result, *cs_result.ac, {1e-3, 1e-9});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}
```

- [ ] **Step 3: Run test to verify it fails**

Run: `cd build-release && cmake --build . --target neospice_tests -j$(nproc) && ./tests/neospice_tests --gtest_filter='*IsrcAC*'`
Expected: FAIL (all AC voltages are zero)

- [ ] **Step 4: Add node accessors to ISource**

In `src/devices/isource.hpp`, add public accessors after `ac_phase_rad()`:

```cpp
    int32_t pos_node() const { return np_; }
    int32_t neg_node() const { return nn_; }
```

- [ ] **Step 5: Add ISource AC excitation in ac.cpp**

In `src/core/ac.cpp`, add `#include "devices/isource.hpp"` at the top (after the existing includes), then after line 133 (end of VSource AC loop), add:

```cpp
    for (auto& dev : ckt.devices()) {
        if (auto* is = dynamic_cast<ISource*>(dev.get())) {
            if (is->ac_mag() != 0.0) {
                auto exc = std::polar(is->ac_mag(), is->ac_phase_rad());
                int32_t np = is->pos_node();
                int32_t nn = is->neg_node();
                if (np >= 0 && np < n) ac_rhs[np] -= exc;
                if (nn >= 0 && nn < n) ac_rhs[nn] += exc;
            }
        }
    }
```

- [ ] **Step 6: Build and run test**

Run: `cd build-release && cmake --build . --target neospice_tests -j$(nproc) && ./tests/neospice_tests --gtest_filter='*IsrcAC*'`
Expected: PASS

- [ ] **Step 7: Run full test suite**

Run: `cd build-release && ctest -j$(nproc) --output-on-failure`
Expected: All tests pass

- [ ] **Step 8: Commit**

```bash
git add src/core/ac.cpp src/devices/isource.hpp tests/circuits/isrc_ac.cir tests/unit/test_ngspice_compare.cpp
git commit -m "fix: add current source AC excitation to solve_ac()"
```

---

### Task 3: Replace Switch Smooth Step with ngspice Hysteresis Model

**Files:**
- Modify: `src/devices/switch.hpp`
- Modify: `src/devices/switch.cpp`
- Create: `tests/circuits/switch_hysteresis.cir`
- Modify: `tests/unit/test_ngspice_compare.cpp`

- [ ] **Step 1: Create test circuit**

```spice
* tests/circuits/switch_hysteresis.cir
* Switch with hysteresis driven by triangle wave
V1 ctrl 0 PULSE(-2 2 0 5u 5u 0.1u 10u)
Vin in 0 DC 5
S1 in out ctrl 0 SMOD
R1 out 0 100
.model SMOD SW Vt=0 Vh=1 Ron=1 Roff=1e6
.tran 0.1u 40u
.end
```

- [ ] **Step 2: Add test**

```cpp
TEST_F(NgspiceCompareTest, SwitchHysteresisTransient) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/switch_hysteresis.cir";
    auto ng_result = ngspice_->run_transient(path);
    auto ckt = sim_.load(path);
    auto cs_result = sim_.run(ckt);
    ASSERT_TRUE(cs_result.transient.has_value());
    auto cmp = compare_transient(*cs_result.transient, ng_result, {5e-2, 1e-2});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}
```

- [ ] **Step 3: Run test to verify it fails**

Run: `cd build-release && cmake --build . --target neospice_tests -j$(nproc) && ./tests/neospice_tests --gtest_filter='*SwitchHysteresis*'`
Expected: FAIL (smooth step produces different switching times than hard hysteresis)

- [ ] **Step 4: Rewrite switch.hpp with ngspice hysteresis model**

Replace the entire `src/devices/switch.hpp`:

```cpp
#pragma once
#include "devices/device.hpp"
#include "devices/vsource.hpp"
#include <string>

namespace neospice {

struct SwitchModel {
    std::string name;
    bool is_voltage_controlled = true;
    double Vt  = 0.0;
    double Vh  = 0.0;
    double Ron  = 1.0;
    double Roff = 1e12;
};

enum class SwitchState { REALLY_OFF = 0, REALLY_ON = 1, HYST_OFF = 2, HYST_ON = 3 };

inline bool switch_is_on(SwitchState s) {
    return s == SwitchState::REALLY_ON || s == SwitchState::HYST_ON;
}

class VSwitch : public Device {
public:
    VSwitch(std::string name,
            int32_t node_pos, int32_t node_neg,
            int32_t node_ctrl_pos, int32_t node_ctrl_neg,
            const SwitchModel& model,
            bool initial_on = false);

    void stamp_pattern(SparsityBuilder& builder) const override;
    void assign_offsets(const SparsityPattern& pattern) override;
    void evaluate(const std::vector<double>& voltages,
                  NumericMatrix& mat, std::vector<double>& rhs) override;
    void ac_stamp(const std::vector<double>& voltages,
                  NumericMatrix& G, NumericMatrix& C) override;

    bool device_converged() const override { return !state_changed_; }

    const SwitchModel& model() const { return model_; }

private:
    int32_t np_, nn_;
    int32_t ncp_, ncn_;
    SwitchModel model_;
    bool initial_on_ = false;

    SwitchState current_state_;
    SwitchState previous_state_;
    bool state_changed_ = false;
    double last_g_ = 0.0;

    MatrixOffset off_pp_ = -1, off_pn_ = -1, off_np_ = -1, off_nn_ = -1;

    SwitchState compute_state(double v_ctrl, SwitchState prev, bool init) const;
};

class CSwitch : public Device {
public:
    CSwitch(std::string name,
            int32_t node_pos, int32_t node_neg,
            const VSource* sense,
            const SwitchModel& model,
            bool initial_on = false);

    void stamp_pattern(SparsityBuilder& builder) const override;
    void assign_offsets(const SparsityPattern& pattern) override;
    void evaluate(const std::vector<double>& voltages,
                  NumericMatrix& mat, std::vector<double>& rhs) override;
    void ac_stamp(const std::vector<double>& voltages,
                  NumericMatrix& G, NumericMatrix& C) override;

    bool device_converged() const override { return !state_changed_; }

    const SwitchModel& model() const { return model_; }

private:
    int32_t np_, nn_;
    const VSource* sense_;
    SwitchModel model_;
    bool initial_on_ = false;

    SwitchState current_state_;
    SwitchState previous_state_;
    bool state_changed_ = false;
    double last_g_ = 0.0;

    MatrixOffset off_pp_ = -1, off_pn_ = -1, off_np_ = -1, off_nn_ = -1;

    SwitchState compute_state(double ctrl, SwitchState prev, bool init) const;
};

} // namespace neospice
```

- [ ] **Step 5: Rewrite switch.cpp with ngspice hysteresis logic**

Replace the entire `src/devices/switch.cpp`:

```cpp
#include "devices/switch.hpp"
#include <stdexcept>
#include <cmath>

namespace neospice {

// Shared hysteresis state machine matching ngspice swload.c
static SwitchState compute_switch_state(double ctrl, double Vt, double Vh,
                                         SwitchState prev, bool init, bool initial_on) {
    if (init) {
        if (initial_on) {
            if (Vh >= 0) {
                return (ctrl > Vt + Vh) ? SwitchState::REALLY_ON : SwitchState::HYST_ON;
            } else {
                return (ctrl > Vt - Vh) ? SwitchState::REALLY_ON : SwitchState::HYST_ON;
            }
        } else {
            if (Vh >= 0) {
                return (ctrl < Vt - Vh) ? SwitchState::REALLY_OFF : SwitchState::HYST_OFF;
            } else {
                return (ctrl < Vt + Vh) ? SwitchState::REALLY_OFF : SwitchState::HYST_OFF;
            }
        }
    }

    if (Vh > 0) {
        if (ctrl > Vt + Vh) return SwitchState::REALLY_ON;
        if (ctrl < Vt - Vh) return SwitchState::REALLY_OFF;
        return prev;
    } else if (Vh < 0) {
        if (ctrl > Vt - Vh) return SwitchState::REALLY_ON;
        if (ctrl < Vt + Vh) return SwitchState::REALLY_OFF;
        if (prev == SwitchState::HYST_OFF || prev == SwitchState::HYST_ON)
            return prev;
        if (prev == SwitchState::REALLY_ON) return SwitchState::HYST_OFF;
        if (prev == SwitchState::REALLY_OFF) return SwitchState::HYST_ON;
        return prev;
    } else {
        return (ctrl > Vt) ? SwitchState::REALLY_ON : SwitchState::REALLY_OFF;
    }
}

// ==== VSwitch ====

VSwitch::VSwitch(std::string name,
                 int32_t node_pos, int32_t node_neg,
                 int32_t node_ctrl_pos, int32_t node_ctrl_neg,
                 const SwitchModel& model, bool initial_on)
    : Device(std::move(name))
    , np_(node_pos), nn_(node_neg)
    , ncp_(node_ctrl_pos), ncn_(node_ctrl_neg)
    , model_(model), initial_on_(initial_on)
    , current_state_(initial_on ? SwitchState::HYST_ON : SwitchState::HYST_OFF)
    , previous_state_(current_state_)
    , last_g_(1.0 / model_.Roff)
{}

void VSwitch::stamp_pattern(SparsityBuilder& builder) const {
    stamp_if_not_ground(builder, np_, np_);
    stamp_if_not_ground(builder, np_, nn_);
    stamp_if_not_ground(builder, nn_, np_);
    stamp_if_not_ground(builder, nn_, nn_);
}

void VSwitch::assign_offsets(const SparsityPattern& pattern) {
    off_pp_ = offset_if_not_ground(pattern, np_, np_);
    off_pn_ = offset_if_not_ground(pattern, np_, nn_);
    off_np_ = offset_if_not_ground(pattern, nn_, np_);
    off_nn_ = offset_if_not_ground(pattern, nn_, nn_);
}

SwitchState VSwitch::compute_state(double v_ctrl, SwitchState prev, bool init) const {
    return compute_switch_state(v_ctrl, model_.Vt, model_.Vh, prev, init, initial_on_);
}

void VSwitch::evaluate(const std::vector<double>& voltages,
                       NumericMatrix& mat, std::vector<double>& /*rhs*/) {
    double Vcp = (ncp_ >= 0) ? voltages[ncp_] : 0.0;
    double Vcn = (ncn_ >= 0) ? voltages[ncn_] : 0.0;
    double v_ctrl = Vcp - Vcn;

    bool init = false;
    if (tls_integrator_ctx) {
        constexpr int MODEINITFIX = 0x400;
        constexpr int MODEINITJCT = 0x200;
        init = (tls_integrator_ctx->mode & (MODEINITFIX | MODEINITJCT)) != 0;
    }

    SwitchState old_state = current_state_;
    current_state_ = compute_state(v_ctrl, previous_state_, init);
    state_changed_ = (current_state_ != old_state);

    if (init) previous_state_ = current_state_;

    double g = switch_is_on(current_state_) ? (1.0 / model_.Ron) : (1.0 / model_.Roff);
    last_g_ = g;

    add_if_valid(mat, off_pp_,  g);
    add_if_valid(mat, off_pn_, -g);
    add_if_valid(mat, off_np_, -g);
    add_if_valid(mat, off_nn_,  g);
}

void VSwitch::ac_stamp(const std::vector<double>& /*voltages*/,
                       NumericMatrix& G, NumericMatrix& /*C*/) {
    add_if_valid(G, off_pp_,  last_g_);
    add_if_valid(G, off_pn_, -last_g_);
    add_if_valid(G, off_np_, -last_g_);
    add_if_valid(G, off_nn_,  last_g_);
}

// ==== CSwitch ====

CSwitch::CSwitch(std::string name,
                 int32_t node_pos, int32_t node_neg,
                 const VSource* sense,
                 const SwitchModel& model, bool initial_on)
    : Device(std::move(name))
    , np_(node_pos), nn_(node_neg)
    , sense_(sense), model_(model), initial_on_(initial_on)
    , current_state_(initial_on ? SwitchState::HYST_ON : SwitchState::HYST_OFF)
    , previous_state_(current_state_)
    , last_g_(1.0 / model_.Roff)
{
    if (!sense_) throw std::invalid_argument("CSwitch: sense pointer must not be null");
}

void CSwitch::stamp_pattern(SparsityBuilder& builder) const {
    stamp_if_not_ground(builder, np_, np_);
    stamp_if_not_ground(builder, np_, nn_);
    stamp_if_not_ground(builder, nn_, np_);
    stamp_if_not_ground(builder, nn_, nn_);
}

void CSwitch::assign_offsets(const SparsityPattern& pattern) {
    off_pp_ = offset_if_not_ground(pattern, np_, np_);
    off_pn_ = offset_if_not_ground(pattern, np_, nn_);
    off_np_ = offset_if_not_ground(pattern, nn_, np_);
    off_nn_ = offset_if_not_ground(pattern, nn_, nn_);
}

SwitchState CSwitch::compute_state(double ctrl, SwitchState prev, bool init) const {
    return compute_switch_state(ctrl, model_.Vt, model_.Vh, prev, init, initial_on_);
}

void CSwitch::evaluate(const std::vector<double>& voltages,
                       NumericMatrix& mat, std::vector<double>& /*rhs*/) {
    int32_t bidx = sense_->branch_index();
    double ctrl = (bidx >= 0 && bidx < static_cast<int32_t>(voltages.size()))
                  ? voltages[bidx] : 0.0;

    bool init = false;
    if (tls_integrator_ctx) {
        constexpr int MODEINITFIX = 0x400;
        constexpr int MODEINITJCT = 0x200;
        init = (tls_integrator_ctx->mode & (MODEINITFIX | MODEINITJCT)) != 0;
    }

    SwitchState old_state = current_state_;
    current_state_ = compute_state(ctrl, previous_state_, init);
    state_changed_ = (current_state_ != old_state);

    if (init) previous_state_ = current_state_;

    double g = switch_is_on(current_state_) ? (1.0 / model_.Ron) : (1.0 / model_.Roff);
    last_g_ = g;

    add_if_valid(mat, off_pp_,  g);
    add_if_valid(mat, off_pn_, -g);
    add_if_valid(mat, off_np_, -g);
    add_if_valid(mat, off_nn_,  g);
}

void CSwitch::ac_stamp(const std::vector<double>& /*voltages*/,
                       NumericMatrix& G, NumericMatrix& /*C*/) {
    add_if_valid(G, off_pp_,  last_g_);
    add_if_valid(G, off_pn_, -last_g_);
    add_if_valid(G, off_np_, -last_g_);
    add_if_valid(G, off_nn_,  last_g_);
}

} // namespace neospice
```

- [ ] **Step 6: Update parser for new VSwitch/CSwitch constructor signature**

Search for VSwitch and CSwitch construction in `src/parser/netlist_parser.cpp`. The constructors now take an extra `bool initial_on` parameter (defaults to `false`). Check if the parser already parses ON/OFF keywords — if so, pass the parsed value. If not, the default `false` matches existing behavior and no parser changes are needed for now.

Run: `grep -n "VSwitch\|CSwitch" src/parser/netlist_parser.cpp` to find the construction sites and update them if needed.

- [ ] **Step 7: Build and run test**

Run: `cd build-release && cmake --build . --target neospice_tests -j$(nproc) && ./tests/neospice_tests --gtest_filter='*Switch*'`
Expected: PASS

- [ ] **Step 8: Run full test suite**

Run: `cd build-release && ctest -j$(nproc) --output-on-failure`
Expected: All tests pass

- [ ] **Step 9: Commit**

```bash
git add src/devices/switch.hpp src/devices/switch.cpp tests/circuits/switch_hysteresis.cir tests/unit/test_ngspice_compare.cpp
git commit -m "fix: replace smooth switch model with ngspice 4-state hysteresis"
```

---

### Task 4: Fix Transmission Line DC Short-Circuit and History Initialization

**Files:**
- Modify: `src/devices/tline.cpp`
- Modify: `src/devices/tline.hpp`
- Create: `tests/circuits/tline_dc.cir`
- Modify: `tests/unit/test_ngspice_compare.cpp`

- [ ] **Step 1: Create DC test circuit**

```spice
* tests/circuits/tline_dc.cir
* TL should pass DC: V(out) should equal V(in) * R2/(R1+R2)
V1 in 0 DC 10
R1 in a 50
T1 a 0 b 0 Z0=50 TD=10n
R2 b 0 100
.op
.end
```

- [ ] **Step 2: Add tests**

```cpp
TEST_F(NgspiceCompareTest, TlineDC) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/tline_dc.cir";
    auto ng_result = ngspice_->run_dc(path);
    auto ckt = sim_.load(path);
    auto cs_result = sim_.run_dc(ckt);
    auto cmp = compare_dc(ng_result, cs_result, {1e-3, 1e-6});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}
```

- [ ] **Step 3: Run test to verify it fails**

Run: `cd build-release && cmake --build . --target neospice_tests -j$(nproc) && ./tests/neospice_tests --gtest_filter='*TlineDC*'`
Expected: FAIL (ports are isolated at DC)

- [ ] **Step 4: Add cross-port DC coupling sparsity**

In `src/devices/tline.hpp`, add cross-port matrix offsets:

```cpp
    // Cross-port coupling offsets for DC (short-circuit model)
    MatrixOffset off_p1p_p2p_ = -1, off_p1p_p2n_ = -1;
    MatrixOffset off_p1n_p2p_ = -1, off_p1n_p2n_ = -1;
    MatrixOffset off_p2p_p1p_ = -1, off_p2p_p1n_ = -1;
    MatrixOffset off_p2n_p1p_ = -1, off_p2n_p1n_ = -1;
```

In `stamp_pattern()` in `tline.cpp`, after the existing port shunt stamps, add:

```cpp
    // Cross-port coupling (for DC short-circuit model)
    stamp_if_not_ground(builder, p1p_, p2p_);
    stamp_if_not_ground(builder, p1p_, p2n_);
    stamp_if_not_ground(builder, p1n_, p2p_);
    stamp_if_not_ground(builder, p1n_, p2n_);
    stamp_if_not_ground(builder, p2p_, p1p_);
    stamp_if_not_ground(builder, p2p_, p1n_);
    stamp_if_not_ground(builder, p2n_, p1p_);
    stamp_if_not_ground(builder, p2n_, p1n_);
```

In `assign_offsets()`, add:

```cpp
    off_p1p_p2p_ = offset_if_not_ground(pattern, p1p_, p2p_);
    off_p1p_p2n_ = offset_if_not_ground(pattern, p1p_, p2n_);
    off_p1n_p2p_ = offset_if_not_ground(pattern, p1n_, p2p_);
    off_p1n_p2n_ = offset_if_not_ground(pattern, p1n_, p2n_);
    off_p2p_p1p_ = offset_if_not_ground(pattern, p2p_, p1p_);
    off_p2p_p1n_ = offset_if_not_ground(pattern, p2p_, p1n_);
    off_p2n_p1p_ = offset_if_not_ground(pattern, p2n_, p1p_);
    off_p2n_p1n_ = offset_if_not_ground(pattern, p2n_, p1n_);
```

- [ ] **Step 5: Implement DC short-circuit in evaluate()**

Replace the DC section of `evaluate()` (lines 115-121) with:

```cpp
    if (transient_ && tls_integrator_ctx) {
        double t_now = tls_integrator_ctx->current_time;
        update_delayed_values(t_now - td_);
    } else {
        // DC: TL is a short circuit. Tie p1+↔p2+ and p1-↔p2- with
        // large conductance to enforce V(p1+)=V(p2+), V(p1-)=V(p2-).
        double g_dc = 1e9;  // ~1 GS (very low resistance tie)
        // Conductance between p1p and p2p:
        add_if_valid(mat, off_p1pp_,    g_dc);   // (p1p,p1p) += g_dc
        add_if_valid(mat, off_p2pp_,    g_dc);   // (p2p,p2p) += g_dc
        add_if_valid(mat, off_p1p_p2p_, -g_dc);  // (p1p,p2p) -= g_dc
        add_if_valid(mat, off_p2p_p1p_, -g_dc);  // (p2p,p1p) -= g_dc
        // Conductance between p1n and p2n:
        add_if_valid(mat, off_p1nn_,    g_dc);   // (p1n,p1n) += g_dc
        add_if_valid(mat, off_p2nn_,    g_dc);   // (p2n,p2n) += g_dc
        add_if_valid(mat, off_p1n_p2n_, -g_dc);  // (p1n,p2n) -= g_dc
        add_if_valid(mat, off_p2n_p1n_, -g_dc);  // (p2n,p1n) -= g_dc
        e1_ = 0.0;
        e2_ = 0.0;
        return;
    }
```

- [ ] **Step 6: Add DC history initialization**

Add an `init_dc_state` method to `TransmissionLine`. In `tline.hpp`:

```cpp
    void init_dc_state(const std::vector<double>& sol);
```

In `tline.cpp`:

```cpp
void TransmissionLine::init_dc_state(const std::vector<double>& sol) {
    double vp1p = (p1p_ >= 0) ? sol[p1p_] : 0.0;
    double vp1n = (p1n_ >= 0) ? sol[p1n_] : 0.0;
    double vp2p = (p2p_ >= 0) ? sol[p2p_] : 0.0;
    double vp2n = (p2n_ >= 0) ? sol[p2n_] : 0.0;
    double v1 = vp1p - vp1n;
    double v2 = vp2p - vp2n;
    double i1 = g0_ * v1 - g0_ * (v2 + z0_ * 0.0);  // at DC steady state
    double i2 = g0_ * v2 - g0_ * (v1 + z0_ * 0.0);

    // Seed history with DC values at t=0, t=-TD, t=-2*TD
    history_.clear();
    for (int k = 2; k >= 0; --k) {
        HistoryPoint hp;
        hp.time = -static_cast<double>(k) * td_;
        hp.v1 = v1; hp.i1 = i1;
        hp.v2 = v2; hp.i2 = i2;
        history_.push_back(hp);
    }
}
```

Then in `src/core/transient.cpp`, find the block where `tl->set_transient(true)` is called, and add `tl->init_dc_state(solution)` right after it. Search for `TransmissionLine` in that file to find the exact location.

- [ ] **Step 7: Build and run tests**

Run: `cd build-release && cmake --build . --target neospice_tests -j$(nproc) && ./tests/neospice_tests --gtest_filter='*Tline*'`
Expected: PASS (DC and existing matched-load transient)

- [ ] **Step 8: Run full test suite**

Run: `cd build-release && ctest -j$(nproc) --output-on-failure`
Expected: All tests pass

- [ ] **Step 9: Commit**

```bash
git add src/devices/tline.cpp src/devices/tline.hpp src/core/transient.cpp tests/circuits/tline_dc.cir tests/unit/test_ngspice_compare.cpp
git commit -m "fix: implement TL DC short-circuit and history initialization from DC OP"
```

---

## Phase 2: Accuracy-Impacting Missing Features

### Task 5: Add LTE compute_trunc for Capacitor and Inductor

**Files:**
- Modify: `src/devices/capacitor.hpp`
- Modify: `src/devices/capacitor.cpp`
- Modify: `src/devices/inductor.hpp`
- Modify: `src/devices/inductor.cpp`

- [ ] **Step 1: Add charge history to Capacitor**

In `src/devices/capacitor.hpp`, add after `i_prev2_`:

```cpp
    double q_prev_ = 0.0;   // Q(n-1) = C * v_prev
    double q_prev2_ = 0.0;  // Q(n-2) = C * v_prev2
    double q_prev3_ = 0.0;  // Q(n-3)
    double dt_prev_ = 0.0;  // timestep at previous accepted step
```

- [ ] **Step 2: Override compute_trunc in Capacitor**

In `src/devices/capacitor.hpp`, add:

```cpp
    double compute_trunc(const IntegratorCtx& ctx,
                         const SimOptions& opts) const override;
```

In `src/devices/capacitor.cpp`, add:

```cpp
double Capacitor::compute_trunc(const IntegratorCtx& /*ctx*/,
                                const SimOptions& opts) const {
    if (!transient_ || dt_ <= 0.0) return 1e30;

    double q_now = cap_ * v_prev_;
    double dd1 = q_now - q_prev_;
    double dd2 = q_prev_ - q_prev2_;
    double dd3 = q_prev2_ - q_prev3_;
    double d2q = dd1 - dd2;
    double d3q = (dd1 - 2.0 * dd2 + dd3);

    double lte_coeff = (integration_method_ == 1) ? (2.0 / 9.0) : (1.0 / 12.0);
    double lte = std::abs(d2q) * lte_coeff;
    double tol = opts.trtol * opts.chgtol;

    if (lte <= 0.0 || tol <= 0.0) return 1e30;
    double ratio = tol / lte;
    double factor = 0.8 * std::pow(ratio, 1.0 / 3.0);
    factor = std::min(factor, 2.0);
    factor = std::max(factor, 0.25);
    return dt_ * factor;
}
```

- [ ] **Step 3: Update Capacitor accept_step to track charge history**

In `Capacitor::accept_step()`, add at the beginning (before the existing code):

```cpp
    q_prev3_ = q_prev2_;
    q_prev2_ = q_prev_;
    q_prev_ = cap_ * v_prev_;
    dt_prev_ = dt_;
```

In `Capacitor::init_dc_state()`, add:

```cpp
    q_prev_ = cap_ * v_prev_;
    q_prev2_ = q_prev_;
    q_prev3_ = q_prev_;
```

- [ ] **Step 4: Add flux history and compute_trunc to Inductor**

Same pattern as capacitor. In `src/devices/inductor.hpp`, add:

```cpp
    double phi_prev_ = 0.0;   // flux(n-1) = L * i_prev
    double phi_prev2_ = 0.0;  // flux(n-2)
    double phi_prev3_ = 0.0;  // flux(n-3)
    double dt_prev_ = 0.0;
```

Add override declaration:

```cpp
    double compute_trunc(const IntegratorCtx& ctx,
                         const SimOptions& opts) const override;
```

In `src/devices/inductor.cpp`, add the implementation (same formula using flux instead of charge):

```cpp
double Inductor::compute_trunc(const IntegratorCtx& /*ctx*/,
                               const SimOptions& opts) const {
    if (!transient_ || dt_ <= 0.0) return 1e30;

    double phi_now = inductance_ * i_prev_;
    double dd1 = phi_now - phi_prev_;
    double dd2 = phi_prev_ - phi_prev2_;
    double d2phi = dd1 - dd2;

    double lte_coeff = (integration_method_ == 1) ? (2.0 / 9.0) : (1.0 / 12.0);
    double lte = std::abs(d2phi) * lte_coeff;
    double tol = opts.trtol * opts.chgtol;

    if (lte <= 0.0 || tol <= 0.0) return 1e30;
    double ratio = tol / lte;
    double factor = 0.8 * std::pow(ratio, 1.0 / 3.0);
    factor = std::min(factor, 2.0);
    factor = std::max(factor, 0.25);
    return dt_ * factor;
}
```

Update `Inductor::accept_step()` and `init_dc_state()` to track flux history (same pattern as capacitor).

- [ ] **Step 5: Build and run test suite**

Run: `cd build-release && cmake --build . --target neospice_tests -j$(nproc) && ctest -j$(nproc) --output-on-failure`
Expected: All tests pass (LTE makes timesteps smaller but more accurate — existing tolerances should still hold)

- [ ] **Step 6: Commit**

```bash
git add src/devices/capacitor.hpp src/devices/capacitor.cpp src/devices/inductor.hpp src/devices/inductor.cpp
git commit -m "feat: add LTE-based timestep control for capacitors and inductors"
```

---

### Task 6: Fix Variable-Timestep Gear-2 Coefficients

**Files:**
- Modify: `src/devices/capacitor.cpp`
- Modify: `src/devices/inductor.cpp`
- Modify: `src/devices/coupled_inductor.cpp`

- [ ] **Step 1: Add dt_prev tracking to set_transient and accept_step**

In capacitor, inductor, and coupled inductor, ensure `dt_prev_` is updated in `accept_step*()` and set to `dt_` initially.

- [ ] **Step 2: Fix Gear-2 coefficients in Capacitor**

In `src/devices/capacitor.cpp`, replace the Gear-2 branch in `evaluate()`:

```cpp
    if (integration_method_ == 1 && gear_ready_) {
        double r = (dt_prev_ > 0.0) ? dt_prev_ / dt_ : 1.0;
        double ag0 = (1.0 + 2.0 * r) / ((1.0 + r) * dt_);
        double c1 = -(1.0 + 2.0 * r) / (r * dt_);
        double c2 = (1.0 + 2.0 * r) / (r * (1.0 + r) * dt_);
        g_eq = ag0 * cap_;
        // Q = C*V, so i_eq = c1*C*v_prev + c2*C*v_prev2
        i_eq = cap_ * (c1 * v_prev_ + c2 * v_prev2_);
    }
```

Do the same for the Gear-2 branch in `accept_step()` (the current computation).

- [ ] **Step 3: Fix Gear-2 coefficients in Inductor**

Same pattern in `src/devices/inductor.cpp`:

```cpp
    if (integration_method_ == 1 && gear_ready_) {
        double r = (dt_prev_ > 0.0) ? dt_prev_ / dt_ : 1.0;
        double ag0 = (1.0 + 2.0 * r) / ((1.0 + r) * dt_);
        r_eq = ag0 * inductance_;
        double c1 = -(1.0 + 2.0 * r) / (r * dt_);
        double c2 = (1.0 + 2.0 * r) / (r * (1.0 + r) * dt_);
        v_eq = inductance_ * (c1 * i_prev_ + c2 * i_prev2_);
    }
```

- [ ] **Step 4: Fix Gear-2 coefficients in CoupledInductor**

Same pattern for the mutual coupling terms.

- [ ] **Step 5: Build and run test suite**

Run: `cd build-release && cmake --build . --target neospice_tests -j$(nproc) && ctest -j$(nproc) --output-on-failure`
Expected: All tests pass

- [ ] **Step 6: Commit**

```bash
git add src/devices/capacitor.cpp src/devices/inductor.cpp src/devices/coupled_inductor.cpp
git commit -m "fix: use variable-timestep BDF-2 coefficients for Gear integration"
```

---

### Task 7: Fix PULSE/SIN Default Parameter Values

**Files:**
- Modify: `src/devices/vsource.hpp`
- Modify: `src/devices/vsource.cpp`
- Modify: `src/devices/isource.hpp`
- Modify: `src/core/transient.cpp`
- Create: `tests/circuits/pulse_defaults.cir`
- Modify: `tests/unit/test_ngspice_compare.cpp`

- [ ] **Step 1: Change PulseParams/SinParams defaults to sentinel -1**

In `src/devices/vsource.hpp`:

```cpp
struct PulseParams {
    double v1 = 0, v2 = 0, td = 0, tr = -1, tf = -1, pw = -1, per = -1;
};

struct SinParams {
    double v0 = 0, va = 0, freq = -1, td = 0, theta = 0, phase = 0;
};
```

- [ ] **Step 2: Add resolve_defaults methods to VSource and ISource**

In `src/devices/vsource.hpp`, add public method:

```cpp
    void resolve_defaults(double tstep, double tstop);
```

In `src/devices/vsource.cpp`:

```cpp
void VSource::resolve_defaults(double tstep, double tstop) {
    if (func_ == SourceFunction::PULSE) {
        if (pulse_.tr < 0) pulse_.tr = tstep;
        if (pulse_.tf < 0) pulse_.tf = tstep;
        if (pulse_.pw < 0) pulse_.pw = tstop;
        if (pulse_.per < 0) pulse_.per = tstop;
    } else if (func_ == SourceFunction::SIN) {
        if (sin_.freq < 0) sin_.freq = (tstop > 0) ? 1.0 / tstop : 0.0;
    }
}
```

Add the same method to ISource (`isource.hpp`/`isource.cpp`).

- [ ] **Step 3: Call resolve_defaults in transient solver preamble**

In `src/core/transient.cpp`, find where sources are configured before the transient loop. After the `set_transient()` calls on reactive devices, add:

```cpp
    for (auto& dev : ckt.devices()) {
        if (auto* vs = dynamic_cast<VSource*>(dev.get()))
            vs->resolve_defaults(tstep, tstop);
        else if (auto* is = dynamic_cast<ISource*>(dev.get()))
            is->resolve_defaults(tstep, tstop);
    }
```

- [ ] **Step 4: Create test and verify**

```spice
* tests/circuits/pulse_defaults.cir
* PULSE with omitted TR/TF/PW/PER — should use ngspice defaults
V1 in 0 PULSE(0 5 1u)
R1 in 0 1k
.tran 1u 20u
.end
```

- [ ] **Step 5: Build, test, commit**

```bash
git add src/devices/vsource.hpp src/devices/vsource.cpp src/devices/isource.hpp src/devices/isource.cpp src/core/transient.cpp tests/circuits/pulse_defaults.cir tests/unit/test_ngspice_compare.cpp
git commit -m "fix: match ngspice PULSE/SIN default parameter values"
```

---

### Task 8: Transmission Line AC Model (Frequency-Dependent)

**Files:**
- Modify: `src/devices/device.hpp`
- Modify: `src/devices/tline.cpp`
- Modify: `src/devices/tline.hpp`
- Modify: `src/core/ac.cpp`
- Create: `tests/circuits/tline_ac.cir`
- Modify: `tests/unit/test_ngspice_compare.cpp`

- [ ] **Step 1: Add ac_stamp_freq virtual to Device**

In `src/devices/device.hpp`, add after `ac_stamp`:

```cpp
    virtual bool ac_stamp_freq(double omega,
                               std::vector<double>& ax, int32_t nnz,
                               std::vector<std::complex<double>>& ac_rhs) {
        return false;  // not handled; caller falls back to G + jwC
    }
```

- [ ] **Step 2: Implement ac_stamp_freq in TransmissionLine**

Override in `tline.hpp` and implement in `tline.cpp`:

```cpp
bool TransmissionLine::ac_stamp_freq(double omega,
                                      std::vector<double>& ax, int32_t /*nnz*/,
                                      std::vector<std::complex<double>>& /*ac_rhs*/) {
    // Self-port terms: G0 (already stamped via ac_stamp into G matrix)
    // Cross-port terms: -G0 * exp(-j*omega*td)
    auto delay = std::polar(1.0, -omega * td_);
    double cross_re = -g0_ * delay.real();
    double cross_im = -g0_ * delay.imag();

    // Stamp cross-port entries directly into the complex ax array
    auto stamp_cross = [&](MatrixOffset off, double re, double im) {
        if (off >= 0) {
            ax[2 * off]     += re;
            ax[2 * off + 1] += im;
        }
    };

    // Port 1←2: +cross on (p1p,p2p), (p1n,p2n); -cross on (p1p,p2n), (p1n,p2p)
    stamp_cross(off_p1p_p2p_,  cross_re,  cross_im);
    stamp_cross(off_p1p_p2n_, -cross_re, -cross_im);
    stamp_cross(off_p1n_p2p_, -cross_re, -cross_im);
    stamp_cross(off_p1n_p2n_,  cross_re,  cross_im);

    // Port 2←1: symmetric
    stamp_cross(off_p2p_p1p_,  cross_re,  cross_im);
    stamp_cross(off_p2p_p1n_, -cross_re, -cross_im);
    stamp_cross(off_p2n_p1p_, -cross_re, -cross_im);
    stamp_cross(off_p2n_p1n_,  cross_re,  cross_im);

    return true;
}
```

- [ ] **Step 3: Integrate into AC frequency sweep**

In `src/core/ac.cpp`, in the frequency loop (line 218-224), after building `ax` from `g_vals + omega*c_vals`, add:

```cpp
        for (auto& dev : ckt.devices()) {
            dev->ac_stamp_freq(omega, ax, nnz, ac_rhs);
        }
```

This way, the TL adds its frequency-dependent cross-coupling on top of the base G+jwC entries.

- [ ] **Step 4: Create test, build, test, commit**

Create `tests/circuits/tline_ac.cir`, add ngspice comparison test, verify.

```bash
git commit -m "feat: add frequency-dependent AC model for transmission line"
```

---

### Task 9: Transmission Line Quadratic Interpolation

**Files:**
- Modify: `src/devices/tline.cpp`

- [ ] **Step 1: Replace linear interpolation with 3-point Lagrange**

In `update_delayed_values()`, replace the linear interpolation block (lines 82-102) with:

```cpp
    // Find 3 bounding history points for quadratic interpolation.
    auto it = std::upper_bound(history_.begin(), history_.end(), t_delayed,
        [](double t, const HistoryPoint& hp) { return t < hp.time; });

    // Need at least 2 points to interpolate; 3 for quadratic
    if (it == history_.begin()) {
        // t_delayed is before first point
        e1_ = 0.0; e2_ = 0.0;
        return;
    }

    // Quadratic (3-point Lagrange) if possible, else linear fallback
    const HistoryPoint* p0;
    const HistoryPoint* p1;
    const HistoryPoint* p2;

    if (it == history_.end()) {
        // Extrapolate from last 3
        if (history_.size() >= 3) {
            p0 = &history_[history_.size()-3];
            p1 = &history_[history_.size()-2];
            p2 = &history_[history_.size()-1];
        } else {
            const auto& h = history_.back();
            e1_ = h.v2 + z0_ * h.i2;
            e2_ = h.v1 + z0_ * h.i1;
            return;
        }
    } else if (it - history_.begin() >= 2) {
        // Normal case: it points past t_delayed, use (it-2, it-1, it)
        p0 = &*(it - 2);
        p1 = &*(it - 1);
        p2 = &*it;
    } else if (it - history_.begin() == 1 && it + 1 != history_.end()) {
        p0 = &*(it - 1);
        p1 = &*it;
        p2 = &*(it + 1);
    } else {
        // Only 2 points — linear fallback
        const auto& h1 = *(it - 1);
        const auto& h2 = *it;
        double alpha = (h2.time - h1.time > 1e-300)
                       ? (t_delayed - h1.time) / (h2.time - h1.time) : 0.0;
        alpha = std::max(0.0, std::min(1.0, alpha));
        double v1_d = h1.v1 + alpha * (h2.v1 - h1.v1);
        double i1_d = h1.i1 + alpha * (h2.i1 - h1.i1);
        double v2_d = h1.v2 + alpha * (h2.v2 - h1.v2);
        double i2_d = h1.i2 + alpha * (h2.i2 - h1.i2);
        e1_ = v2_d + z0_ * i2_d;
        e2_ = v1_d + z0_ * i1_d;
        return;
    }

    // Lagrange basis polynomials
    double t0 = p0->time, t1 = p1->time, t2 = p2->time;
    double t = t_delayed;
    double f0 = ((t-t1)*(t-t2)) / ((t0-t1)*(t0-t2));
    double f1 = ((t-t0)*(t-t2)) / ((t1-t0)*(t1-t2));
    double f2 = ((t-t0)*(t-t1)) / ((t2-t0)*(t2-t1));

    double v1_d = f0*p0->v1 + f1*p1->v1 + f2*p2->v1;
    double i1_d = f0*p0->i1 + f1*p1->i1 + f2*p2->i1;
    double v2_d = f0*p0->v2 + f1*p1->v2 + f2*p2->v2;
    double i2_d = f0*p0->i2 + f1*p1->i2 + f2*p2->i2;

    e1_ = v2_d + z0_ * i2_d;
    e2_ = v1_d + z0_ * i1_d;
```

- [ ] **Step 2: Build, run existing TL tests, commit**

```bash
git commit -m "feat: upgrade transmission line to quadratic interpolation"
```

---

### Task 10: ASRC Convergence Test and Numerical Safeguards

**Files:**
- Modify: `src/devices/asrc/asrc_device.cpp`
- Modify: `src/devices/asrc/asrc_device.hpp`
- Modify: `src/devices/asrc/expression_ast.cpp`

- [ ] **Step 1: Add previous value storage to ASRCDevice**

In `asrc_device.hpp`, add member:

```cpp
    double prev_value_ = 0.0;
    bool has_prev_value_ = false;
```

- [ ] **Step 2: Store value in evaluate and implement convergence check**

At the end of `evaluate()`, store the expression value. Override `device_converged()`:

```cpp
bool ASRCDevice::device_converged() const {
    if (!has_prev_value_) return true;
    double tol = (mode_ == Mode::VOLTAGE)
        ? 1e-3 * std::max(std::abs(prev_value_), std::abs(current_value_)) + 1e-6
        : 1e-3 * std::max(std::abs(prev_value_), std::abs(current_value_)) + 1e-12;
    return std::abs(current_value_ - prev_value_) <= tol;
}
```

- [ ] **Step 3: Add numerical safeguards to expression_ast.cpp**

In the division handler, wrap with protection:

```cpp
// Replace raw a/b with:
double denom = b.value;
if (std::abs(denom) < 1e-32)
    denom = std::copysign(1e-32, denom == 0.0 ? 1.0 : denom);
result.value = a.value / denom;
```

For sqrt of negative: `result.value = std::sqrt(std::abs(arg.value));`
For log of negative: `result.value = std::log(std::abs(arg.value));`
For pow with negative base: `result.value = std::copysign(std::pow(std::abs(base.value), exp.value), base.value);`

- [ ] **Step 4: Build, run tests, commit**

```bash
git commit -m "feat: add ASRC convergence test and numerical safeguards"
```

---

## Phase 3: Missing Parameter Support

### Task 11: Temperature Coefficients for R/C/L

**Files:**
- Modify: `src/devices/resistor.hpp`, `src/devices/resistor.cpp`
- Modify: `src/devices/capacitor.hpp`, `src/devices/capacitor.cpp`
- Modify: `src/devices/inductor.hpp`, `src/devices/inductor.cpp`
- Modify: `src/parser/netlist_parser.cpp`
- Create: `tests/circuits/resistor_temp.cir`
- Modify: `tests/unit/test_ngspice_compare.cpp`

- [ ] **Step 1: Add TC params to Resistor, Capacitor, Inductor**

To each class, add members `tc1_`, `tc2_`, `scale_` (default 1.0), `temp_` (default -1 sentinel), `dtemp_` (default 0), and a `process_temperature(double sim_temp, double sim_tnom)` method that computes the effective value:

```cpp
void Resistor::process_temperature(double sim_temp, double sim_tnom) {
    double tnom = (tnom_ > 0) ? tnom_ : sim_tnom;
    double temp = (temp_ > 0) ? temp_ : sim_temp;
    double dt = (temp + dtemp_) - tnom;
    double factor = 1.0 + tc1_ * dt + tc2_ * dt * dt;
    resistance_eff_ = resistance_nom_ * factor * scale_;
}
```

- [ ] **Step 2: Use effective value in evaluate/ac_stamp**

Change `1.0 / resistance_` to `1.0 / resistance_eff_` (or `cap_eff_` / `inductance_eff_`).

- [ ] **Step 3: Extend parser to handle inline tc1=, tc2=, scale= on R/C/L**

In the R-element parsing section of `netlist_parser.cpp`, after reading the resistance value, scan remaining tokens for `tc1=`, `tc2=`, `scale=`, `temp=`, `dtemp=` key-value pairs.

- [ ] **Step 4: Create test, verify, commit**

```bash
git commit -m "feat: add temperature coefficients TC1/TC2 for R/C/L devices"
```

---

### Task 12: Multiplier (m) Parameter

**Files:**
- Modify: `src/devices/resistor.hpp/cpp`, `capacitor.hpp/cpp`, `inductor.hpp/cpp`, `vccs.hpp/cpp`, `cccs.hpp/cpp`
- Modify: `src/parser/netlist_parser.cpp`

- [ ] **Step 1: Add m_ field to each device**

Add `double m_ = 1.0;` and a setter `void set_multiplier(double m) { m_ = m; }`.

- [ ] **Step 2: Apply m in stamps**

- Resistor: `g = m_ / resistance_eff_`
- Capacitor: `g_eq *= m_`, `i_eq *= m_`, AC `cap_ * m_`
- Inductor: use `inductance_ / m_` as effective inductance
- VCCS: `gm_ * m_` in stamps
- CCCS: `gain_ * m_` in stamps
- Noise: multiply spectral density by m_

- [ ] **Step 3: Parse m= from netlist**

In parser, scan for `m=` on device lines.

- [ ] **Step 4: Test with m=2 against ngspice, commit**

```bash
git commit -m "feat: add multiplier (m) parameter to passive and controlled source devices"
```

---

### Task 13: ASRC Missing Functions and Ternary Fix

**Files:**
- Modify: `src/devices/asrc/expression_ast.cpp`

- [ ] **Step 1: Add missing functions to expression parser**

In `expression_ast.cpp`, in the function dispatch table, add handlers for:

- `sgn(x)`: value = `(x > 0) - (x < 0)`, deriv = 0
- `u(x)` / `ustep(x)`: value = `(x >= 0) ? 1.0 : 0.0`, deriv = 0
- `uramp(x)`: value = `std::max(0.0, x)`, deriv = `(x > 0) ? 1.0 : 0.0`
- `acosh(x)`: value = `std::acosh(x)`, deriv = `1/sqrt(x*x-1)`
- `asinh(x)`: value = `std::asinh(x)`, deriv = `1/sqrt(x*x+1)`
- `atanh(x)`: value = `std::atanh(x)`, deriv = `1/(1-x*x)`
- `ceil(x)`, `floor(x)`, `nint(x)`: standard rounding, deriv = 0
- `pwr(x,y)`: value = `copysign(pow(abs(x), y), x)`, deriv via chain rule

- [ ] **Step 2: Fix ternary condition semantics**

Find the ternary evaluation (search for `> 0.0` in the ternary handler), change to `!= 0.0`.

- [ ] **Step 3: Add unit tests for each function, commit**

```bash
git commit -m "feat: add missing ASRC functions (sgn, ustep, uramp, etc.) and fix ternary semantics"
```

---

## Phase 4: Polish and Edge Cases

### Task 14: Resistor Zero-Resistance Guard

**Files:**
- Modify: `src/devices/resistor.cpp`

- [ ] **Step 1: Add guard in constructor or process_temperature**

```cpp
if (std::abs(resistance_eff_) < 1e-3)
    resistance_eff_ = 1e-3;
```

- [ ] **Step 2: Commit**

```bash
git commit -m "fix: clamp near-zero resistance to 1 milliohm"
```

---

### Task 15: Initial Conditions (IC) for C/L

**Files:**
- Modify: `src/devices/capacitor.hpp/cpp`
- Modify: `src/devices/inductor.hpp/cpp`
- Modify: `src/parser/netlist_parser.cpp`
- Modify: `src/core/transient.cpp`

- [ ] **Step 1: Add ic_ field and setter**

```cpp
double ic_ = std::nan(""); // NaN = not specified
void set_ic(double v) { ic_ = v; }
bool has_ic() const { return !std::isnan(ic_); }
```

- [ ] **Step 2: Parse IC= on C/L device lines**

In parser, scan for `IC=` after the capacitance/inductance value.

- [ ] **Step 3: Apply IC in transient init when UIC mode active**

In transient.cpp, in the initialization block, check `cap->has_ic()` and override `v_prev_`.

- [ ] **Step 4: Test, commit**

```bash
git commit -m "feat: add IC= initial condition support for capacitors and inductors"
```

---

### Task 16: Fix VCCS/CCCS Header Comment Sign Errors

**Files:**
- Modify: `src/devices/vccs.hpp`
- Modify: `src/devices/cccs.hpp`

- [ ] **Step 1: Correct comments**

In `vccs.hpp`, fix the MNA stamp comments to match the actual code (positive gm at np,ncp not negative). Same for `cccs.hpp`.

- [ ] **Step 2: Commit**

```bash
git commit -m "docs: correct MNA stamp sign documentation in VCCS and CCCS headers"
```

---

## Deferred Items (Not in This Plan)

These items from the spec are deferred to future work as they require significant parser infrastructure or have low practical impact:

- **Geometry models for R/C** (Task 3.3 in spec): Requires full model card infrastructure for R/C types. Defer until parser model card system is more mature.
- **Switch timestep control** (Task 3.4): Lower priority since the hysteresis model fix in Task 3 is the critical fix.
- **TL breakpoints**: Requires refactoring the breakpoint collection interface.
- **TL initial conditions** (IC=V1,I1,V2,I2): Rarely used.
- **AC resistance for resistors**: Very rarely used.
- **TEMPER/HERTZ ASRC variables**: Requires threading temperature/frequency state into expression evaluation.
- **PWL function in ASRC**: Requires special multi-argument parsing.
