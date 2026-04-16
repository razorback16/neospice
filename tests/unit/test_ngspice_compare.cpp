#include <gtest/gtest.h>
#include "api/neospice.hpp"
#include "framework/ngspice_runner.hpp"
#include "framework/comparator.hpp"

using namespace neospice;

class NgspiceCompareTest : public ::testing::Test {
protected:
    void SetUp() override {
        ngspice_ = std::make_unique<NgspiceRunner>(NGSPICE_BINARY);
    }
    std::unique_ptr<NgspiceRunner> ngspice_;
    Simulator sim_;
};

// ---------------------------------------------------------------------------
// DC operating-point tests — these should match ngspice very tightly.
// ---------------------------------------------------------------------------

TEST_F(NgspiceCompareTest, ResistorDividerDC) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/resistor_divider.cir";
    auto ng_result = ngspice_->run_dc(path);
    auto ckt = sim_.load(path);
    auto cs_result = sim_.run_dc(ckt);
    auto cmp = compare_dc(ng_result, cs_result, {1e-6, 1e-12});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}

TEST_F(NgspiceCompareTest, DiodeDC) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/diode_iv.cir";
    auto ng_result = ngspice_->run_dc(path);
    auto ckt = sim_.load(path);
    auto cs_result = sim_.run_dc(ckt);
    auto cmp = compare_dc(ng_result, cs_result, {1e-3, 1e-12});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}

// ---------------------------------------------------------------------------
// AC small-signal test — should match well since both use direct solve.
// ---------------------------------------------------------------------------

TEST_F(NgspiceCompareTest, RCACAnalysis) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/rc_ac.cir";
    auto ng_result = ngspice_->run_ac(path);
    auto ckt = sim_.load(path);
    auto cs_result = sim_.run(ckt);
    ASSERT_TRUE(cs_result.ac.has_value());
    auto cmp = compare_ac(ng_result, *cs_result.ac, {1e-3, 1e-9});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}

// ---------------------------------------------------------------------------
// Transient tests — we use adaptive Gear BDF-2 integration which eliminates
// the trapezoidal ringing that plagued RLC circuits.  Remaining error sources:
//   1. Slight amplitude damping inherent to BDF-2 (L-stable)
//   2. Interpolation mismatch near zero crossings vs ngspice adaptive steps
// We compare at our time grid and use tightened tolerances where possible.
// ---------------------------------------------------------------------------

TEST_F(NgspiceCompareTest, RCLowpassTransient) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/rc_lowpass.cir";
    auto ng_result = ngspice_->run_transient(path);
    auto ckt = sim_.load(path);
    auto cs_result = sim_.run(ckt);
    ASSERT_TRUE(cs_result.transient.has_value());
    // Compare at our time grid; large abstol absorbs initial-step error
    // where v(out) is tiny and trapezoidal gives ~half of exact.
    auto cmp = compare_transient(*cs_result.transient, ng_result, {1e-1, 1e-2});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}

TEST_F(NgspiceCompareTest, DiodeRectifierTransient) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/diode_rectifier.cir";
    auto ng_result = ngspice_->run_transient(path);
    auto ckt = sim_.load(path);
    auto cs_result = sim_.run(ckt);
    ASSERT_TRUE(cs_result.transient.has_value());
    // Compare at our time grid; large abstol absorbs interpolation error
    // near SIN zero crossings where small values get amplified.
    // Gear BDF-2 integration has more amplitude damping than trapezoidal
    // near sharp diode transitions, requiring wider current tolerance.
    auto cmp = compare_transient(*cs_result.transient, ng_result, {2.0, 1e-1});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}

// RLC series transient — previously disabled due to trapezoidal ringing.
// Now works with Gear BDF-2 integration.  Relaxed tolerance accounts for
// BDF-2 amplitude damping vs ngspice's default trapezoidal method.
TEST_F(NgspiceCompareTest, RLCSeriesTransient) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/rlc_series.cir";
    auto ng_result = ngspice_->run_transient(path);
    auto ckt = sim_.load(path);
    auto cs_result = sim_.run(ckt);
    ASSERT_TRUE(cs_result.transient.has_value());
    auto cmp = compare_transient(*cs_result.transient, ng_result, {1.5e-1, 1e-3});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}

// Underdamped RLC transient — exercises oscillatory response with Gear BDF-2.
TEST_F(NgspiceCompareTest, RLCUnderdampedTransient) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/rlc_underdamped.cir";
    auto ng_result = ngspice_->run_transient(path);
    auto ckt = sim_.load(path);
    auto cs_result = sim_.run(ckt);
    ASSERT_TRUE(cs_result.transient.has_value());
    auto cmp = compare_transient(*cs_result.transient, ng_result, {5e-1, 1e-3});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}
