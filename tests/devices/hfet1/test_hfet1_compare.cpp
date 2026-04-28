// HFET1 (Level 5 heterojunction FET) ngspice comparison suite.
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

class HFET1Validation : public ::testing::Test {
protected:
    void SetUp() override {
        ngspice_ = std::make_unique<NgspiceRunner>();
    }
    std::unique_ptr<NgspiceRunner> ngspice_;
    Simulator sim_;
};

// ============================================================================
// 1.  DC Operating Point -- NHFET LEVEL=5 with Vgs=0.3V, Vds=1V
// ============================================================================

TEST_F(HFET1Validation, NhfetOperatingPoint) {
    std::string cir_path = std::string(TEST_CIRCUITS_DIR) + "/hfet1_nfet_dc_op.cir";

    DCResult ng_result;
    try {
        ng_result = ngspice_->run_dc(cir_path);
    } catch (const std::exception& e) {
        GTEST_SKIP() << "ngspice not available or failed: " << e.what();
    }

    if (ng_result.node_voltages.empty()) {
        GTEST_SKIP() << "ngspice returned empty DC result";
    }

    auto ckt = sim_.load(cir_path);
    DCResult cs_result = sim_.run_dc(ckt);

    auto cmp = compare_dc(ng_result, cs_result, {1e-2, 1e-6});
    EXPECT_TRUE(cmp.passed)
        << "DC OP comparison failed. Worst: " << cmp.worst_signal
        << " error: " << cmp.worst_error;

    ASSERT_TRUE(cs_result.node_voltages.count("v(drain)") > 0);
    ASSERT_TRUE(cs_result.node_voltages.count("v(gate)") > 0);

    double v_drain = cs_result.node_voltages["v(drain)"];
    double v_gate  = cs_result.node_voltages["v(gate)"];

    EXPECT_NEAR(v_drain, 1.0, 0.01);
    EXPECT_NEAR(v_gate, 0.3, 0.01);
}

// ============================================================================
// 2.  AC Small-Signal -- NHFET common-source amplifier
// ============================================================================

TEST_F(HFET1Validation, NhfetAcResponse) {
    std::string cir_path = std::string(TEST_CIRCUITS_DIR) + "/hfet1_nfet_ac.cir";

    ACResult ng_result;
    try {
        ng_result = ngspice_->run_ac(cir_path);
    } catch (const std::exception& e) {
        GTEST_SKIP() << "ngspice not available or failed: " << e.what();
    }

    if (ng_result.frequency.empty()) {
        GTEST_SKIP() << "ngspice returned empty AC result";
    }

    auto ckt = sim_.load(cir_path);
    ACResult cs_result = sim_.run_ac(ckt, AnalysisCommand::DEC, 10, 100.0, 10e6);

    ASSERT_FALSE(cs_result.frequency.empty());
    ASSERT_EQ(ng_result.frequency.size(), cs_result.frequency.size());

    for (auto it = ng_result.voltages.begin(); it != ng_result.voltages.end(); ) {
        if (it->first.find('#') != std::string::npos)
            it = ng_result.voltages.erase(it);
        else
            ++it;
    }

    auto cmp = compare_ac(ng_result, cs_result, {1e-3, 1e-9});
    EXPECT_TRUE(cmp.passed)
        << "AC comparison failed. Worst: " << cmp.worst_signal
        << " error: " << cmp.worst_error;

    ASSERT_TRUE(cs_result.voltages.count("v(drain)") > 0);
    const auto& v_drain_ac = cs_result.voltages.at("v(drain)");

    double gain_low = std::abs(v_drain_ac.front());
    EXPECT_GT(gain_low, 0.01)
        << "CS amplifier should have measurable gain at low frequency";
}
