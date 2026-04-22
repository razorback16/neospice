// BSIM3 v3.3 (LEVEL=49) ngspice comparison/validation suite.
// Tests: DC operating point, NMOS IV sweep, PMOS DC, AC small-signal,
// and CMOS inverter transient — each compared against ngspice.

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
// Test fixture — shared NgspiceRunner + Simulator for all BSIM3 validation
// ============================================================================

class BSIM3Validation : public ::testing::Test {
protected:
    void SetUp() override {
        ngspice_ = std::make_unique<NgspiceRunner>(NGSPICE_BINARY);
    }
    std::unique_ptr<NgspiceRunner> ngspice_;
    Simulator sim_;
};

// ============================================================================
// 1.  NMOS DC Operating Point — compare node voltages against ngspice
// ============================================================================

TEST_F(BSIM3Validation, NMOS_DC_OperatingPoint) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/bsim3_nmos_dc.cir";

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
    // i(v1) is the branch current of V1 (drain supply), which equals -Id
    auto it = cs_result.branch_currents.find("i(v1)");
    ASSERT_NE(it, cs_result.branch_currents.end())
        << "Branch current i(v1) not found";
    double id = -it->second;  // conventional drain current
    EXPECT_GT(id, 1e-6) << "NMOS drain current should be positive and non-trivial";
    EXPECT_LT(id, 1e-2) << "NMOS drain current should be reasonable";
}

// ============================================================================
// 2.  NMOS IV Sweep — DC sweep VDS at fixed VGS, compare drain current
// ============================================================================

TEST_F(BSIM3Validation, NMOS_IV_Sweep) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/bsim3_nmos_iv.cir";

    // Run ngspice
    DCSweepResult ng_result;
    try {
        ng_result = ngspice_->run_dc_sweep(path);
    } catch (const std::exception& e) {
        GTEST_SKIP() << "ngspice not available or failed: " << e.what();
    }

    if (ng_result.sweep_values.empty()) {
        GTEST_SKIP() << "ngspice returned empty DC sweep result";
    }

    // Run neospice
    auto ckt = sim_.load(path);
    DCSweepResult cs_result = sim_.run_dc_sweep(ckt,
        {{DCSweepParam{"VDS", 0.0, 1.8, 0.05}}});

    ASSERT_FALSE(cs_result.sweep_values.empty());
    ASSERT_EQ(ng_result.sweep_values.size(), cs_result.sweep_values.size())
        << "Sweep point count mismatch: ngspice=" << ng_result.sweep_values.size()
        << " neospice=" << cs_result.sweep_values.size();

    // Compare drain current i(vds) at each sweep point
    ASSERT_TRUE(ng_result.currents.count("i(vds)") > 0)
        << "ngspice result should contain i(vds)";
    ASSERT_TRUE(cs_result.currents.count("i(vds)") > 0)
        << "neospice result should contain i(vds)";

    const auto& ng_ids = ng_result.currents.at("i(vds)");
    const auto& cs_ids = cs_result.currents.at("i(vds)");

    int mismatches = 0;
    double worst_rel_err = 0.0;
    int worst_idx = -1;

    for (size_t i = 0; i < ng_result.sweep_values.size(); ++i) {
        double ng_i = ng_ids[i];
        double cs_i = cs_ids[i];

        // Adaptive tolerance: wider near VDS=0 where current is very small
        double abstol = 1e-9;
        double reltol = 0.02;  // 2% relative tolerance

        if (std::abs(ng_i) < 1e-6) {
            abstol = 1e-7;  // wider for very small currents
        }

        double denom = std::max(std::abs(ng_i), abstol);
        double rel_err = std::abs(ng_i - cs_i) / denom;

        if (rel_err > worst_rel_err) {
            worst_rel_err = rel_err;
            worst_idx = static_cast<int>(i);
        }

        if (rel_err > reltol && std::abs(ng_i - cs_i) > abstol) {
            ++mismatches;
        }
    }

    // Allow up to 5% of points to mismatch (near threshold/triode transition)
    int max_mismatches = static_cast<int>(ng_result.sweep_values.size()) / 20;
    EXPECT_LE(mismatches, max_mismatches)
        << mismatches << "/" << ng_result.sweep_values.size()
        << " sweep points exceed tolerance. Worst relative error: "
        << worst_rel_err << " at sweep index " << worst_idx
        << " (VDS=" << ng_result.sweep_values[worst_idx]
        << ", ngspice i(vds)=" << ng_ids[worst_idx]
        << ", neospice i(vds)=" << cs_ids[worst_idx] << ")";

    // Verify basic MOSFET physics:
    // At VDS=0, drain current should be approximately zero
    EXPECT_NEAR(cs_ids[0], 0.0, 1e-6)
        << "Drain current at VDS=0 should be near zero";

    // At VDS=1.8V (saturation), current should be substantial
    double ids_sat = std::abs(cs_ids.back());
    EXPECT_GT(ids_sat, 1e-5)
        << "Drain current in saturation should be > 10uA for W=10u device";

    // Current should increase monotonically (triode->saturation)
    // Check a few key points
    double ids_at_05 = std::abs(cs_ids[10]);  // VDS=0.5V
    double ids_at_18 = std::abs(cs_ids.back()); // VDS=1.8V
    EXPECT_LE(ids_at_05, ids_at_18 + 1e-9)
        << "Drain current at VDS=0.5V should be <= current at VDS=1.8V";
}

// ============================================================================
// 3.  PMOS DC — Verify PMOS type works, compare against ngspice
// ============================================================================

TEST_F(BSIM3Validation, PMOS_DC_OperatingPoint) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/bsim3_pmos_dc.cir";

    // Run ngspice
    auto ng_result = ngspice_->run_dc(path);

    // Run neospice
    auto ckt = sim_.load(path);
    auto cs_result = sim_.run_dc(ckt);

    // Filter out internal nodes
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
    EXPECT_NEAR(cs_result.node_voltages["v(drain)"], -1.0, 0.01);
    EXPECT_NEAR(cs_result.node_voltages["v(gate)"], -1.0, 0.01);

    // PMOS drain current should flow in the opposite direction to NMOS.
    // i(v1) is the branch current of V1 (drain supply at -1V).
    // For PMOS with negative VDS and VGS, current flows from source to drain,
    // so V1 sources current (positive i(v1)).
    auto it = cs_result.branch_currents.find("i(v1)");
    ASSERT_NE(it, cs_result.branch_currents.end())
        << "Branch current i(v1) not found";
    double i_v1 = it->second;
    // The PMOS should be conducting (current magnitude > 0)
    EXPECT_GT(std::abs(i_v1), 1e-6)
        << "PMOS should have non-trivial drain current";
}

// ============================================================================
// 4.  AC Small-Signal — CS amplifier, compare AC gain vs ngspice
// ============================================================================

TEST_F(BSIM3Validation, NMOS_CS_Amplifier_AC) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/bsim3_nmos_ac.cir";

    // Run ngspice AC analysis
    auto ng_result = ngspice_->run_ac(path);

    // Run neospice AC analysis
    auto ckt = sim_.load(path);
    auto cs_result = sim_.run(ckt);
    ASSERT_TRUE(cs_result.ac.has_value())
        << "AC analysis result is missing — ac_stamp may not be implemented for BSIM3";

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

    // Compare AC voltage results.  BSIM3 AC should be close since both use
    // the same model and linearize at the same DC operating point.
    // Use 25% relative tolerance (same as BSIM4v7 AC test) to account for
    // sensitivity to DC bias differences at high frequency.
    auto cmp = compare_ac(ng_result, *cs_result.ac, {0.25, 1e-15});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}

// ---------------------------------------------------------------------------
// AC sanity check: NMOS AC produces non-zero output at drain
// ---------------------------------------------------------------------------
TEST_F(BSIM3Validation, NMOS_AC_NonZero_Output) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/bsim3_nmos_ac.cir";

    auto ckt = sim_.load(path);
    auto result = sim_.run(ckt);
    ASSERT_TRUE(result.ac.has_value())
        << "AC analysis result is missing";

    auto& ac = *result.ac;
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
        << "v(drain) magnitude is near zero at all frequencies — BSIM3 ac_stamp may not be working";
}

// ============================================================================
// 5.  Transient — CMOS inverter pulse response, compare against ngspice
// ============================================================================

TEST_F(BSIM3Validation, CMOS_Inverter_Transient) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/bsim3_cmos_inverter.cir";

    // Run ngspice
    auto ng_result = ngspice_->run_transient(path);

    // Run neospice
    auto ckt = sim_.load(path);
    auto cs_result = sim_.run(ckt);
    ASSERT_TRUE(cs_result.transient.has_value())
        << "Transient analysis result is missing";

    // Filter out internal nodes from ngspice result
    for (auto it = ng_result.voltages.begin();
         it != ng_result.voltages.end(); ) {
        if (it->first.find('#') != std::string::npos)
            it = ng_result.voltages.erase(it);
        else
            ++it;
    }

    ng_result.currents.clear();
    cs_result.transient->currents.clear();
    auto cmp = compare_transient(*cs_result.transient, ng_result, {2e-1, 5e-2});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}

// ---------------------------------------------------------------------------
// Transient sanity: verify CMOS inverter output transitions
// ---------------------------------------------------------------------------
TEST_F(BSIM3Validation, CMOS_Inverter_Transitions) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/bsim3_cmos_inverter.cir";

    auto ckt = sim_.load(path);
    auto cs_result = sim_.run(ckt);
    ASSERT_TRUE(cs_result.transient.has_value());

    auto& tran = *cs_result.transient;
    ASSERT_GT(tran.time.size(), 10u);
    ASSERT_TRUE(tran.voltages.count("v(out)") > 0);

    const auto& v_out = tran.voltages.at("v(out)");

    // The inverter should show output transitions:
    // When input goes high (0->1.8V), output should go low (1.8V->~0V)
    // When input goes low (1.8V->0V), output should go high (~0V->1.8V)

    // Check the output voltage range spans most of the supply
    double v_min = *std::min_element(v_out.begin(), v_out.end());
    double v_max = *std::max_element(v_out.begin(), v_out.end());

    EXPECT_LT(v_min, 0.3) << "Inverter output should reach near ground";
    EXPECT_GT(v_max, 1.5) << "Inverter output should reach near VDD";
    EXPECT_GT(v_max - v_min, 1.0)
        << "Inverter output should swing significantly";
}
