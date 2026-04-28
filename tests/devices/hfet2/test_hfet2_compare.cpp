// HFET2 (Level 6 heterojunction FET) ngspice comparison suite.
// Tests: DC operating point, NHFET IV curve (DC sweep),
//        AC small-signal, transient pulse response.
// Each test runs the same .cir circuit through both ngspice and neospice
// and compares results within engineering tolerances.

#include <gtest/gtest.h>
#include "api/neospice.hpp"
#include "framework/ngspice_runner.hpp"
#include "framework/comparator.hpp"

#include <cmath>
#include <complex>
#include <string>
#include <algorithm>

using namespace neospice;

// ============================================================================
// Test fixture -- shared NgspiceRunner + Simulator for all HFET2 validation
// ============================================================================

class HFET2Validation : public ::testing::Test {
protected:
    void SetUp() override {
        ngspice_ = std::make_unique<NgspiceRunner>();
    }
    std::unique_ptr<NgspiceRunner> ngspice_;
    Simulator sim_;
};

// ============================================================================
// 1.  DC Operating Point -- NHFET with Vgs=0.3V, Vds=1V
//
// Circuit: Vds(1V) on drain, Vgs(0.3V) on gate, Z1 drain/gate/0
// Expected: device is biased above threshold (vto=0.13V) in saturation.
// ============================================================================

TEST_F(HFET2Validation, NhfetOperatingPoint) {
    std::string cir_path = std::string(TEST_CIRCUITS_DIR) + "/hfet2_nfet_dc_op.cir";

    // Run ngspice
    DCResult ng_result;
    try {
        ng_result = ngspice_->run_dc(cir_path);
    } catch (const std::exception& e) {
        GTEST_SKIP() << "ngspice not available or failed: " << e.what();
    }

    if (ng_result.node_voltages.empty()) {
        GTEST_SKIP() << "ngspice returned empty DC result";
    }

    // Run neospice
    auto ckt = sim_.load(cir_path);
    DCResult cs_result = sim_.run_dc(ckt);

    // Compare all node voltages and branch currents.
    // HFET2 DC should be close to ngspice. Use 1% relative, 1uV absolute.
    auto cmp = compare_dc(ng_result, cs_result, {1e-2, 1e-6});
    EXPECT_TRUE(cmp.passed)
        << "DC OP comparison failed. Worst: " << cmp.worst_signal
        << " error: " << cmp.worst_error;

    // Verify drain and gate voltages are applied
    ASSERT_TRUE(cs_result.node_voltages.count("v(drain)") > 0);
    ASSERT_TRUE(cs_result.node_voltages.count("v(gate)") > 0);

    double v_drain = cs_result.node_voltages["v(drain)"];
    double v_gate  = cs_result.node_voltages["v(gate)"];

    EXPECT_NEAR(v_drain, 1.0, 0.01);
    EXPECT_NEAR(v_gate, 0.3, 0.01);
}

// ============================================================================
// 2.  NHFET IV Curve -- DC sweep Vds from 0 to 2V at fixed Vgs=0.3V
//
// Measures drain current vs Vds. Should show output characteristics
// transitioning from linear to saturation region.
// ============================================================================

TEST_F(HFET2Validation, NhfetIvCurveSweep) {
    std::string cir_path = std::string(TEST_CIRCUITS_DIR) + "/hfet2_nfet_iv_sweep.cir";

    // Run ngspice
    DCSweepResult ng_result;
    try {
        ng_result = ngspice_->run_dc_sweep(cir_path);
    } catch (const std::exception& e) {
        GTEST_SKIP() << "ngspice not available or failed: " << e.what();
    }

    if (ng_result.sweep_values.empty()) {
        GTEST_SKIP() << "ngspice returned empty DC sweep result";
    }

    // Run neospice
    auto ckt = sim_.load(cir_path);
    DCSweepResult cs_result = sim_.run_dc_sweep(ckt,
        {{DCSweepParam{"Vds", 0.0, 2.0, 0.05}}});

    ASSERT_FALSE(cs_result.sweep_values.empty());

    // Compare sweep point count
    size_t n_ng = ng_result.sweep_values.size();
    size_t n_cs = cs_result.sweep_values.size();
    EXPECT_EQ(n_ng, n_cs) << "Sweep point count mismatch";

    size_t n = std::min(n_ng, n_cs);

    // Compare i(vds) current at each sweep point
    ASSERT_TRUE(ng_result.currents.count("i(vds)") > 0)
        << "ngspice missing i(vds)";
    ASSERT_TRUE(cs_result.currents.count("i(vds)") > 0)
        << "neospice missing i(vds)";

    const auto& ng_ids = ng_result.currents.at("i(vds)");
    const auto& cs_ids = cs_result.currents.at("i(vds)");

    double worst_err = 0;
    for (size_t i = 0; i < n; ++i) {
        double ref = ng_ids[i];
        double act = cs_ids[i];
        double err = std::abs(ref - act);
        double denom = std::max(std::abs(ref), 1e-9);
        double rel = err / denom;
        worst_err = std::max(worst_err, rel);
    }
    EXPECT_LT(worst_err, 1e-2)
        << "Id-Vds sweep worst relative error: " << worst_err;
}

// ============================================================================
// 3.  NHFET Transient -- Pulse input on common-source amplifier
//
// Vgs pulse from 0 to 0.4V, drain loaded with 200-ohm to 2V supply.
// ============================================================================

TEST_F(HFET2Validation, NhfetTransientPulse) {
    std::string cir_path = std::string(TEST_CIRCUITS_DIR) + "/hfet2_nfet_transient.cir";

    // Run ngspice
    TransientResult ng_result;
    try {
        ng_result = ngspice_->run_transient(cir_path);
    } catch (const std::exception& e) {
        GTEST_SKIP() << "ngspice not available or failed: " << e.what();
    }

    if (ng_result.time.empty()) {
        GTEST_SKIP() << "ngspice returned empty transient result";
    }

    // Run neospice
    auto ckt = sim_.load(cir_path);
    TransientResult cs_result = sim_.run_transient(ckt, 0.01e-9, 4e-9);

    ASSERT_FALSE(cs_result.time.empty());

    // Compare transient results with 5% relative, 10mV absolute tolerance
    auto cmp = compare_transient(ng_result, cs_result, {3e-2, 5e-3});
    EXPECT_TRUE(cmp.passed)
        << "Transient comparison failed. Worst: " << cmp.worst_signal
        << " error: " << cmp.worst_error;
}

// ============================================================================
// 4.  NHFET AC Small-Signal -- gain vs frequency
//
// Common-source with AC stimulus on gate, measure drain.
// ============================================================================

TEST_F(HFET2Validation, NhfetAcSmallSignal) {
    std::string cir_path = std::string(TEST_CIRCUITS_DIR) + "/hfet2_nfet_ac.cir";

    // Run ngspice
    ACResult ng_result;
    try {
        ng_result = ngspice_->run_ac(cir_path);
    } catch (const std::exception& e) {
        GTEST_SKIP() << "ngspice not available or failed: " << e.what();
    }

    if (ng_result.frequency.empty()) {
        GTEST_SKIP() << "ngspice returned empty AC result";
    }

    // Run neospice
    auto ckt = sim_.load(cir_path);
    ACResult cs_result = sim_.run_ac(ckt, AnalysisCommand::DEC, 10, 1e6, 1e12);

    ASSERT_FALSE(cs_result.frequency.empty());

    // Compare AC magnitude and phase with 5% relative, 1e-6 absolute tolerance
    auto cmp = compare_ac(ng_result, cs_result, {1e-4, 1e-6});
    EXPECT_TRUE(cmp.passed)
        << "AC comparison failed. Worst: " << cmp.worst_signal
        << " error: " << cmp.worst_error;
}
