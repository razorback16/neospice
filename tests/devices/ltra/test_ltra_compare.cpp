// LTRA (Lossy Transmission Line) ngspice comparison suite.
// Tests: DC operating point for RC and RG line models.
// Each test runs the same .cir circuit through both ngspice and neospice
// and compares results within engineering tolerances.
//
// Note: LTRA transient convolution is not yet fully operational, so only
// DC tests are included here.

#include <gtest/gtest.h>
#include "api/neospice.hpp"
#include "framework/ngspice_runner.hpp"
#include "framework/comparator.hpp"

#include <cmath>
#include <string>

using namespace neospice;

// ============================================================================
// Test fixture — shared NgspiceRunner + Simulator for all LTRA validation
// ============================================================================

class LTRAValidation : public ::testing::Test {
protected:
    void SetUp() override {
        ngspice_ = std::make_unique<NgspiceRunner>(NGSPICE_BINARY);
    }
    std::unique_ptr<NgspiceRunner> ngspice_;
    Simulator sim_;
};

// ============================================================================
// 1.  DC Operating Point — RC lossy line
//
// Circuit: V1 (1V) -> O1 (R=50, C=100p, LEN=0.01) -> R1 (1k) -> GND
// At DC, the RC line acts as a series resistor R*LEN = 50*0.01 = 0.5 ohm.
// Expected V(out) = 1 * 1000/(1000+0.5) ≈ 0.9995 V
// ============================================================================

TEST_F(LTRAValidation, DCOperatingPointRC) {
    std::string cir_path = std::string(TEST_CIRCUITS_DIR) + "/ltra_dc_rc.cir";

    // Run ngspice
    DCResult ng_result;
    try {
        ng_result = ngspice_->run_dc(cir_path);
    } catch (const std::exception& e) {
        GTEST_SKIP() << "ngspice not available or failed: " << e.what();
    }

    // Run neospice
    auto ckt = sim_.load(cir_path);
    auto cs_result = sim_.run_dc(ckt);

    // Use compare_dc for consistent comparison.
    // Strip ngspice internal nodes (containing '#') that we name differently.
    for (auto it = ng_result.node_voltages.begin();
         it != ng_result.node_voltages.end(); ) {
        if (it->first.find('#') != std::string::npos)
            it = ng_result.node_voltages.erase(it);
        else
            ++it;
    }

    auto cmp = compare_dc(ng_result, cs_result, {1e-3, 1e-9});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;

    // Also verify absolute values are physically reasonable
    double v_out_ng = ng_result.node_voltages.count("v(out)")
                          ? ng_result.node_voltages.at("v(out)")
                          : 0.0;
    double v_out_cs = cs_result.node_voltages.count("v(out)")
                          ? cs_result.node_voltages.at("v(out)")
                          : 0.0;
    // RC line at DC: R*LEN = 0.5 ohm, load = 1k => V(out) ≈ 0.9995 V
    double expected_vout = 1.0 * 1000.0 / (1000.0 + 0.5);
    EXPECT_NEAR(v_out_ng, expected_vout, 0.01)
        << "ngspice V(out) should match analytical expectation";
    EXPECT_NEAR(v_out_cs, expected_vout, 0.01)
        << "neospice V(out) should match analytical expectation";
}

// ============================================================================
// 2.  DC Operating Point — RG lossy line
//
// Circuit: V1 (1V) -> O1 (R=100, G=0.01, LEN=0.5) -> R1 (1k) -> GND
// RG line at DC uses cosh/sinh formulation.
// The output voltage should be between 0 and 1 V due to series R and shunt G.
// ============================================================================

TEST_F(LTRAValidation, DCOperatingPointRG) {
    std::string cir_path = std::string(TEST_CIRCUITS_DIR) + "/ltra_dc_rg.cir";

    // Run ngspice
    DCResult ng_result;
    try {
        ng_result = ngspice_->run_dc(cir_path);
    } catch (const std::exception& e) {
        GTEST_SKIP() << "ngspice not available or failed: " << e.what();
    }

    // Run neospice
    auto ckt = sim_.load(cir_path);
    auto cs_result = sim_.run_dc(ckt);

    // Strip ngspice internal nodes
    for (auto it = ng_result.node_voltages.begin();
         it != ng_result.node_voltages.end(); ) {
        if (it->first.find('#') != std::string::npos)
            it = ng_result.node_voltages.erase(it);
        else
            ++it;
    }

    auto cmp = compare_dc(ng_result, cs_result, {1e-3, 1e-9});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;

    // Verify output is physically reasonable: 0 < V(out) < 1
    double v_out_cs = cs_result.node_voltages.count("v(out)")
                          ? cs_result.node_voltages.at("v(out)")
                          : -1.0;
    EXPECT_GT(v_out_cs, 0.0) << "V(out) should be positive";
    EXPECT_LT(v_out_cs, 1.0) << "V(out) should be less than source voltage";
}
