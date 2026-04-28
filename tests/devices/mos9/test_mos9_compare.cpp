// MOS9 (Modified Level 3 MOSFET) ngspice comparison suite.
// Tests: DC operating point, NMOS AC small-signal.
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
// Test fixture -- shared NgspiceRunner + Simulator for all MOS9 validation
// ============================================================================

class MOS9Validation : public ::testing::Test {
protected:
    void SetUp() override {
        ngspice_ = std::make_unique<NgspiceRunner>(NGSPICE_BINARY);
    }
    std::unique_ptr<NgspiceRunner> ngspice_;
    Simulator sim_;
};

// ============================================================================
// 1.  DC Operating Point -- NMOS common-source with Rd=1k, Vgs=2V, Vdd=5V
//
// Circuit: Vdd(5V) -> Rd(1k) -> drain, M1(drain,gate,0,0)
// Uses LEVEL=9 model with enhanced subthreshold parameters.
// ============================================================================

TEST_F(MOS9Validation, NmosOperatingPoint) {
    std::string cir_path = std::string(TEST_CIRCUITS_DIR) + "/mos9_nmos_dc_op.cir";

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
    // MOS9 DC should be very close to ngspice since both use the same
    // Modified Level 3 equations.  Use 1% relative, 1uV absolute.
    auto cmp = compare_dc(ng_result, cs_result, {1e-2, 1e-6});
    EXPECT_TRUE(cmp.passed)
        << "DC OP comparison failed. Worst: " << cmp.worst_signal
        << " error: " << cmp.worst_error;

    // Verify basic MOSFET physics
    ASSERT_TRUE(cs_result.node_voltages.count("v(drain)") > 0);
    ASSERT_TRUE(cs_result.node_voltages.count("v(gate)") > 0);
    ASSERT_TRUE(cs_result.node_voltages.count("v(vdd)") > 0);

    double v_drain = cs_result.node_voltages["v(drain)"];
    double v_gate  = cs_result.node_voltages["v(gate)"];
    double v_vdd   = cs_result.node_voltages["v(vdd)"];

    EXPECT_NEAR(v_vdd, 5.0, 0.01);
    EXPECT_NEAR(v_gate, 2.0, 0.01);

    // Drain should be in a reasonable range (not at rail)
    EXPECT_GT(v_drain, 1.0)
        << "V(drain) should be above ground (MOSFET should not be saturating Rd)";
    EXPECT_LT(v_drain, 5.0)
        << "V(drain) should be below Vdd (current should be flowing)";
}

// ============================================================================
// 2.  AC Small-Signal -- NMOS common-source amplifier
//
// Circuit: Vdd(5V) -> Rd(1k) -> drain, M1(drain,gate,0,0), Vin AC=1 at gate.
// Model includes overlap and junction capacitances for AC response.
// ============================================================================

TEST_F(MOS9Validation, NmosAcResponse) {
    std::string cir_path = std::string(TEST_CIRCUITS_DIR) + "/mos9_nmos_ac.cir";

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

    // Filter out internal nodes (names containing '#') from ngspice result
    for (auto it = ng_result.voltages.begin(); it != ng_result.voltages.end(); ) {
        if (it->first.find('#') != std::string::npos)
            it = ng_result.voltages.erase(it);
        else
            ++it;
    }

    // Compare AC results. MOS9 AC should be close since both use the same
    // linearized model. Use 5% relative tolerance, 1e-9 absolute.
    auto cmp = compare_ac(ng_result, cs_result, {1e-6, 1e-9});
    EXPECT_TRUE(cmp.passed)
        << "AC comparison failed. Worst: " << cmp.worst_signal
        << " error: " << cmp.worst_error;

    // Verify basic AC physics
    ASSERT_TRUE(cs_result.voltages.count("v(drain)") > 0);
    const auto& v_drain_ac = cs_result.voltages.at("v(drain)");

    // Low-frequency gain: |Av| = gm * Rd should be > 1 for a CS amplifier
    double gain_low = std::abs(v_drain_ac.front());
    EXPECT_GT(gain_low, 0.5)
        << "CS amplifier gain at low frequency should be > 0.5";
    EXPECT_LT(gain_low, 20.0)
        << "CS amplifier gain should not be unreasonably high for this circuit";

    // Phase at low frequency: CS amplifier inverts, so phase should be
    // near +/-180 degrees.
    double phase_low_deg = std::arg(v_drain_ac.front()) * 180.0 / M_PI;
    EXPECT_GT(std::abs(phase_low_deg), 90.0)
        << "CS amp should show inverting phase (|phase| > 90 deg) at low frequency";
}
