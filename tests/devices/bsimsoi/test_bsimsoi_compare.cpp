// BSIMSOI ngspice comparison suite.
// Tests: DC operating point, AC small-signal.

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
// Test fixture
// ============================================================================

class BsimsoiValidation : public ::testing::Test {
protected:
    void SetUp() override {
        ngspice_ = std::make_unique<NgspiceRunner>();
    }
    std::unique_ptr<NgspiceRunner> ngspice_;
    Simulator sim_;
};

// ============================================================================
// DC Operating Point — Nmos
// ============================================================================

TEST_F(BsimsoiValidation, NmosOperatingPoint) {
    std::string cir_path = std::string(TEST_CIRCUITS_DIR) + "/bsimsoi_nmos_dc_op.cir";

    // Run ngspice
    DCResult ng_result;
    try {
        ng_result = ngspice_->run_dc(cir_path);
    } catch (const std::exception& e) {
        GTEST_SKIP() << "ngspice not available or failed: " << e.what();
    }

    if (ng_result.node_voltages.empty()) {
        GTEST_SKIP() << "ngspice returned empty DC result (BSIMSOI may not be compiled in)";
    }

    // Run neospice
    auto ckt = sim_.load(cir_path);
    DCResult cs_result = sim_.run_dc(ckt);

    auto cmp = compare_dc(ng_result, cs_result, {1e-2, 1e-6});
    EXPECT_TRUE(cmp.passed)
        << "DC OP comparison failed. Worst: " << cmp.worst_signal
        << " error: " << cmp.worst_error;

    // Verify basic MOSFET physics
    ASSERT_TRUE(cs_result.node_voltages.count("v(drain)") > 0);
    double v_drain = cs_result.node_voltages["v(drain)"];
    EXPECT_GT(v_drain, 0.1) << "V(drain) should be above ground";
    EXPECT_LT(v_drain, 1.8) << "V(drain) should be below Vdd";
}

// ============================================================================
// DC Operating Point — Pmos
// ============================================================================

TEST_F(BsimsoiValidation, PmosOperatingPoint) {
    std::string cir_path = std::string(TEST_CIRCUITS_DIR) + "/bsimsoi_pmos_dc_op.cir";

    // Run ngspice
    DCResult ng_result;
    try {
        ng_result = ngspice_->run_dc(cir_path);
    } catch (const std::exception& e) {
        GTEST_SKIP() << "ngspice not available or failed: " << e.what();
    }

    if (ng_result.node_voltages.empty()) {
        GTEST_SKIP() << "ngspice returned empty DC result (BSIMSOI may not be compiled in)";
    }

    // Run neospice
    auto ckt = sim_.load(cir_path);
    DCResult cs_result = sim_.run_dc(ckt);

    auto cmp = compare_dc(ng_result, cs_result, {1e-2, 1e-6});
    EXPECT_TRUE(cmp.passed)
        << "DC OP comparison failed. Worst: " << cmp.worst_signal
        << " error: " << cmp.worst_error;

    // Verify PMOS physics: with minimal .model (default thresholds) and
    // Vgs=-0.8V the PMOS is essentially off, so drain ≈ 0V through Rload.
    ASSERT_TRUE(cs_result.node_voltages.count("v(drain)") > 0);
    double v_drain = cs_result.node_voltages["v(drain)"];
    EXPECT_GE(v_drain, 0.0) << "PMOS drain voltage should be non-negative";
    EXPECT_LT(v_drain, 1.8) << "PMOS drain should be below Vdd";
}

// ============================================================================
// AC Small-Signal — Nmos
// ============================================================================

TEST_F(BsimsoiValidation, NmosAcResponse) {
    std::string cir_path = std::string(TEST_CIRCUITS_DIR) + "/bsimsoi_nmos_ac.cir";

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
    ACResult cs_result = sim_.run_ac(ckt, AnalysisCommand::DEC, 10, 100.0, 10e6);

    ASSERT_FALSE(cs_result.frequency.empty());
    ASSERT_EQ(ng_result.frequency.size(), cs_result.frequency.size())
        << "Frequency point count mismatch";

    // Filter out internal nodes from ngspice result
    for (auto it = ng_result.voltages.begin(); it != ng_result.voltages.end(); ) {
        if (it->first.find('#') != std::string::npos)
            it = ng_result.voltages.erase(it);
        else
            ++it;
    }

    auto cmp = compare_ac(ng_result, cs_result, {1e-6, 1e-9});
    EXPECT_TRUE(cmp.passed)
        << "AC comparison failed. Worst: " << cmp.worst_signal
        << " error: " << cmp.worst_error;
}
