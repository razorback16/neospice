// VDMOS (DMOS power MOSFET) ngspice comparison suite.
// Tests: DC operating point, N-channel Id-Vds IV curve (DC sweep),
//        P-channel DC operating point.
// Each test runs the same .cir circuit through both ngspice and neospice
// and compares results within engineering tolerances.

#include <gtest/gtest.h>
#include "api/neospice.hpp"
#include "framework/ngspice_runner.hpp"
#include "framework/comparator.hpp"

#include <cmath>
#include <string>

using namespace neospice;

class VDMOSValidation : public ::testing::Test {
protected:
    void SetUp() override {
        ngspice_ = std::make_unique<NgspiceRunner>();
    }
    std::unique_ptr<NgspiceRunner> ngspice_;
    Simulator sim_;
};

// ---------------------------------------------------------------------------
// 1. N-channel VDMOS DC operating point.
// ---------------------------------------------------------------------------
TEST_F(VDMOSValidation, NmosOperatingPoint) {
    std::string cir_path = std::string(TEST_CIRCUITS_DIR) + "/vdmos_nmos_dc_op.cir";

    DCResult ng_result;
    try {
        ng_result = ngspice_->run_dc(cir_path);
    } catch (const std::exception& e) {
        GTEST_SKIP() << "ngspice not available or failed: " << e.what();
    }
    if (ng_result.node_voltages.empty())
        GTEST_SKIP() << "ngspice returned empty DC result (VDMOS may not be compiled in)";

    auto ckt = sim_.load(cir_path);
    DCResult cs_result = sim_.run_dc(ckt);

    auto cmp = compare_dc(ng_result, cs_result, {1e-3, 1e-6});
    EXPECT_TRUE(cmp.passed)
        << "DC OP comparison failed. Worst: " << cmp.worst_signal
        << " error: " << cmp.worst_error;
}

// ---------------------------------------------------------------------------
// 2. N-channel VDMOS Id-Vds IV curve (DC sweep of Vds at fixed Vgs).
// ---------------------------------------------------------------------------
TEST_F(VDMOSValidation, NmosIvCurveSweep) {
    std::string cir_path = std::string(TEST_CIRCUITS_DIR) + "/vdmos_nmos_iv_sweep.cir";

    DCSweepResult ng_result;
    try {
        ng_result = ngspice_->run_dc_sweep(cir_path);
    } catch (const std::exception& e) {
        GTEST_SKIP() << "ngspice not available or failed: " << e.what();
    }
    if (ng_result.sweep_values.empty())
        GTEST_SKIP() << "ngspice returned empty DC sweep result";

    auto ckt = sim_.load(cir_path);
    DCSweepResult cs_result = sim_.run_dc_sweep(ckt,
        {{DCSweepParam{"Vds", 0.0, 10.0, 0.5}}});

    ASSERT_FALSE(cs_result.sweep_values.empty());
    ASSERT_EQ(ng_result.sweep_values.size(), cs_result.sweep_values.size());
    ASSERT_TRUE(ng_result.currents.count("i(vds)") > 0);
    ASSERT_TRUE(cs_result.currents.count("i(vds)") > 0);

    const auto& ng_ids = ng_result.current("vds");
    const auto& cs_ids = cs_result.current("vds");

    double worst_rel = 0.0;
    for (size_t i = 0; i < ng_result.sweep_values.size(); ++i) {
        double ng_i = ng_ids[i], cs_i = cs_ids[i];
        double denom = std::max(std::fabs(ng_i), 1e-9);
        double rel = std::fabs(cs_i - ng_i) / denom;
        worst_rel = std::max(worst_rel, rel);
    }
    EXPECT_LT(worst_rel, 1e-2)
        << "VDMOS Id-Vds curve deviates from ngspice by " << worst_rel;
}

// ---------------------------------------------------------------------------
// 3. P-channel VDMOS DC operating point (polarity flag vdmosp).
// ---------------------------------------------------------------------------
TEST_F(VDMOSValidation, PmosOperatingPoint) {
    std::string cir_path = std::string(TEST_CIRCUITS_DIR) + "/vdmos_pmos_dc_op.cir";

    DCResult ng_result;
    try {
        ng_result = ngspice_->run_dc(cir_path);
    } catch (const std::exception& e) {
        GTEST_SKIP() << "ngspice not available or failed: " << e.what();
    }
    if (ng_result.node_voltages.empty())
        GTEST_SKIP() << "ngspice returned empty DC result";

    auto ckt = sim_.load(cir_path);
    DCResult cs_result = sim_.run_dc(ckt);

    auto cmp = compare_dc(ng_result, cs_result, {1e-3, 1e-6});
    EXPECT_TRUE(cmp.passed)
        << "P-channel DC OP comparison failed. Worst: " << cmp.worst_signal
        << " error: " << cmp.worst_error;
}
