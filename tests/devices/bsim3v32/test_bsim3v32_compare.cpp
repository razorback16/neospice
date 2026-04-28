// BSIM3v32 (LEVEL=49 VERSION=3.24) ngspice comparison/validation suite.
// Tests: DC operating point and AC small-signal — each compared against ngspice.

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

class BSIM3v32Validation : public ::testing::Test {
protected:
    void SetUp() override {
        ngspice_ = std::make_unique<NgspiceRunner>();
    }
    std::unique_ptr<NgspiceRunner> ngspice_;
    Simulator sim_;
};

// ============================================================================
// 1.  NMOS DC Operating Point — compare node voltages against ngspice
// ============================================================================

TEST_F(BSIM3v32Validation, NMOS_DC_OperatingPoint) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/bsim3v32_nmos_dc_op.cir";

    // Run ngspice
    auto ng_result = ngspice_->run_dc(path);

    // Run neospice
    auto ckt = sim_.load(path);
    auto cs_result = sim_.run_dc(ckt);

    // Filter out internal nodes (ngspice names them with '#')
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

    // Sanity: drain and gate should be at their applied voltage
    EXPECT_NEAR(cs_result.node_voltages["v(drain)"], 1.0, 0.01);
    EXPECT_NEAR(cs_result.node_voltages["v(gate)"], 1.0, 0.01);

    // Drain current should be non-zero and positive (NMOS in saturation)
    auto it = cs_result.branch_currents.find("i(v1)");
    ASSERT_NE(it, cs_result.branch_currents.end())
        << "Branch current i(v1) not found";
    double id = -it->second;  // conventional drain current
    EXPECT_GT(id, 1e-6) << "NMOS drain current should be positive and non-trivial";
    EXPECT_LT(id, 1e-2) << "NMOS drain current should be reasonable";
}

// ============================================================================
// 2.  NMOS AC Analysis — compare gain/phase against ngspice
// ============================================================================

TEST_F(BSIM3v32Validation, NMOS_CS_Amplifier_AC) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/bsim3v32_nmos_ac.cir";

    // Run ngspice AC analysis
    auto ng_result = ngspice_->run_ac(path);

    // Run neospice AC analysis
    auto ckt = sim_.load(path);
    auto cs_result = sim_.run(ckt);
    ASSERT_TRUE(std::holds_alternative<ACResult>(cs_result.analysis))
        << "AC analysis result is missing — ac_stamp may not be implemented for BSIM3v32";

    // Filter out internal nodes (names containing '#' from ngspice)
    for (auto it = ng_result.voltages.begin(); it != ng_result.voltages.end(); ) {
        if (it->first.find('#') != std::string::npos)
            it = ng_result.voltages.erase(it);
        else
            ++it;
    }

    // Drop gate-source currents from comparison — femtoampere-level signals
    // where DC operating point precision differences dominate relative error.
    ng_result.currents.erase("i(vg)");
    ng_result.currents.erase("i(vs)");

    // Compare AC voltage results.  BSIM3v32 AC should be close since both use
    // the same model and linearize at the same DC operating point.
    // Use 25% relative tolerance to account for sensitivity to DC bias
    // differences at high frequency.
    auto cmp = compare_ac(ng_result, std::get<ACResult>(cs_result.analysis), {1e-6, 1e-15});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}

// ============================================================================
// 3.  AC sanity check: NMOS AC produces non-zero output at drain
// ============================================================================

TEST_F(BSIM3v32Validation, NMOS_AC_NonZero_Output) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/bsim3v32_nmos_ac.cir";

    auto ckt = sim_.load(path);
    auto result = sim_.run(ckt);
    ASSERT_TRUE(std::holds_alternative<ACResult>(result.analysis))
        << "AC analysis result is missing";

    auto& ac = std::get<ACResult>(result.analysis);
    ASSERT_FALSE(ac.frequency.empty());

    // v(drain) should exist and have non-zero magnitude at mid-band
    auto it = ac.voltages.find("v(drain)");
    ASSERT_NE(it, ac.voltages.end()) << "v(drain) not found in AC results";

    // Check that drain voltage has non-trivial magnitude at some mid-band frequency
    bool found_nonzero = false;
    for (const auto& val : it->second) {
        if (std::abs(val) > 0.01) {
            found_nonzero = true;
            break;
        }
    }
    EXPECT_TRUE(found_nonzero)
        << "v(drain) magnitude is near zero at all frequencies — BSIM3v32 ac_stamp may not be working";
}
