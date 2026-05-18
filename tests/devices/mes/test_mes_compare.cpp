// MES ngspice comparison suite.
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

class MesValidation : public ::testing::Test {
protected:
    void SetUp() override {
        ngspice_ = std::make_unique<NgspiceRunner>();
    }
    std::unique_ptr<NgspiceRunner> ngspice_;
    Simulator sim_;
};

// ============================================================================
// DC Operating Point
// ============================================================================

TEST_F(MesValidation, DcOpNmfCommonSource) {
    std::string cir_path = std::string(TEST_CIRCUITS_DIR) + "/mes_dc_op.cir";

    DCResult ng_result;
    try {
        ng_result = ngspice_->run_dc(cir_path);
    } catch (const std::exception& e) {
        GTEST_SKIP() << "ngspice not available or failed: " << e.what();
    }

    if (ng_result.node_voltages.empty()) {
        GTEST_SKIP() << "ngspice returned empty DC result (MES may not be compiled in)";
    }

    auto ckt = sim_.load(cir_path);
    DCResult cs_result = sim_.run_dc(ckt);

    auto cmp = compare_dc(ng_result, cs_result, {2e-13, 1e-6});
    EXPECT_TRUE(cmp.passed)
        << "DC OP comparison failed. Worst: " << cmp.worst_signal
        << " error: " << cmp.worst_error;
}

// ============================================================================
// AC Small-Signal
// ============================================================================

TEST_F(MesValidation, AcNmfCommonSource) {
    std::string cir_path = std::string(TEST_CIRCUITS_DIR) + "/mes_ac.cir";

    ACResult ng_result;
    try {
        ng_result = ngspice_->run_ac(cir_path);
    } catch (const std::exception& e) {
        GTEST_SKIP() << "ngspice not available or failed: " << e.what();
    }

    if (ng_result.frequency.empty()) {
        GTEST_SKIP() << "ngspice returned empty AC result (MES may not be compiled in)";
    }

    auto ckt = sim_.load(cir_path);
    ACResult cs_result = sim_.run_ac(ckt, AnalysisCommand::DEC, 10, 1e3, 10e9);

    ASSERT_FALSE(cs_result.frequency.empty());

    auto cmp = compare_ac(ng_result, cs_result, {5e-15, 1e-6});
    EXPECT_TRUE(cmp.passed)
        << "AC comparison failed. Worst: " << cmp.worst_signal
        << " error: " << cmp.worst_error;
}
