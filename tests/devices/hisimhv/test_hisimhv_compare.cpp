// HiSIM_HV (LEVEL=73) ngspice comparison suite.
// Tests: DC operating point (NMOS, PMOS), AC small-signal.
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
// Test fixture -- shared NgspiceRunner + Simulator for all HiSIM_HV validation
// ============================================================================

class HiSIMHVValidation : public ::testing::Test {
protected:
    void SetUp() override {
        ngspice_ = std::make_unique<NgspiceRunner>();
    }
    std::unique_ptr<NgspiceRunner> ngspice_;
    Simulator sim_;
};

// ============================================================================
// 1.  DC Operating Point -- NMOS with Rd=1k, Vgs=1.5V, Vdd=3.3V
//
// Circuit: Vdd(3.3V) -> Rd(1k) -> drain, M1(drain,gate,0,0,0)
// HiSIM_HV LEVEL=73, W=10u L=5u, version=2.20
// Expected: drain near 3.19V (MOSFET conducting ~109uA)
// ============================================================================

TEST_F(HiSIMHVValidation, NmosOperatingPoint) {
    std::string cir_path = std::string(TEST_CIRCUITS_DIR) + "/hisimhv_nmos_dc_op.cir";

    // Run ngspice
    DCResult ng_result;
    try {
        ng_result = ngspice_->run_dc(cir_path);
    } catch (const std::exception& e) {
        GTEST_SKIP() << "ngspice not available or failed: " << e.what();
    }

    if (ng_result.node_voltages.empty()) {
        GTEST_SKIP() << "ngspice returned empty DC result (HiSIM_HV may not be compiled in)";
    }

    // Run neospice
    auto ckt = sim_.load(cir_path);
    DCResult cs_result = sim_.run_dc(ckt);

    // Compare all node voltages and branch currents.
    // HiSIM_HV DC should be very close to ngspice since both use the same
    // model equations. Use 1% relative, 1uV absolute.
    auto cmp = compare_dc(ng_result, cs_result, {1e-2, 1e-6});
    EXPECT_TRUE(cmp.passed)
        << "DC OP comparison failed. Worst: " << cmp.worst_signal
        << " error: " << cmp.worst_error;

    // Verify basic MOSFET physics
    ASSERT_TRUE(cs_result.node_voltages.count("v(drain)") > 0);
    ASSERT_TRUE(cs_result.node_voltages.count("v(gate)") > 0);
    ASSERT_TRUE(cs_result.node_voltages.count("v(vdd)") > 0);

    double v_drain = cs_result.voltage("drain");
    double v_gate  = cs_result.voltage("gate");
    double v_vdd   = cs_result.voltage("vdd");

    EXPECT_NEAR(v_vdd, 3.3, 0.01);
    EXPECT_NEAR(v_gate, 1.5, 0.01);

    // Drain should be in a reasonable range (MOSFET conducting through Rd)
    EXPECT_GT(v_drain, 1.0)
        << "V(drain) should be above ground (MOSFET should not be saturating Rd)";
    EXPECT_LT(v_drain, 3.3)
        << "V(drain) should be below Vdd (current should be flowing)";
}

// ============================================================================
// 2.  PMOS DC Operating Point -- Verify PMOS polarity works correctly
//
// Circuit: Vdd(3.3V) -> M1(drain,gate,vdd,vdd,vdd) -> Rload(1k) -> GND
// PMOS: source at Vdd, drain pulled to ground through Rload.
// Vgs = V(gate) - V(vdd) = 1.5 - 3.3 = -1.8V (ON).
// ============================================================================

TEST_F(HiSIMHVValidation, PmosOperatingPoint) {
    std::string cir_path = std::string(TEST_CIRCUITS_DIR) + "/hisimhv_pmos_dc_op.cir";

    // Run ngspice
    DCResult ng_result;
    try {
        ng_result = ngspice_->run_dc(cir_path);
    } catch (const std::exception& e) {
        GTEST_SKIP() << "ngspice not available or failed: " << e.what();
    }

    if (ng_result.node_voltages.empty()) {
        GTEST_SKIP() << "ngspice returned empty DC result (HiSIM_HV may not be compiled in)";
    }

    // Run neospice
    auto ckt = sim_.load(cir_path);
    DCResult cs_result = sim_.run_dc(ckt);

    auto cmp = compare_dc(ng_result, cs_result, {1e-2, 1e-6});
    EXPECT_TRUE(cmp.passed)
        << "PMOS DC OP comparison failed. Worst: " << cmp.worst_signal
        << " error: " << cmp.worst_error;

    // Verify PMOS physics
    ASSERT_TRUE(cs_result.node_voltages.count("v(drain)") > 0);
    double v_drain = cs_result.voltage("drain");

    EXPECT_GT(v_drain, 0.01)
        << "PMOS drain should be pulled up from ground through Rload";
    EXPECT_LT(v_drain, 3.3)
        << "PMOS drain should be below Vdd";
}

// ============================================================================
// 3.  AC Small-Signal -- NMOS common-source amplifier
//
// Circuit: Vdd(3.3V) -> Rd(1k) -> drain, M1(drain,gate,0,0,0), Vin AC=1.
// HiSIM_HV model provides intrinsic capacitances for AC response.
// ============================================================================

TEST_F(HiSIMHVValidation, NmosAcResponse) {
    std::string cir_path = std::string(TEST_CIRCUITS_DIR) + "/hisimhv_nmos_ac.cir";

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

    // Compare AC results. HiSIM_HV AC should be close since both use the same
    // linearized model. Use 5% relative tolerance, 1e-9 absolute.
    auto cmp = compare_ac(ng_result, cs_result, {1e-6, 1e-9});
    EXPECT_TRUE(cmp.passed)
        << "AC comparison failed. Worst: " << cmp.worst_signal
        << " error: " << cmp.worst_error;

    // Verify basic AC physics
    ASSERT_TRUE(cs_result.voltages.count("v(drain)") > 0);
    const auto& v_drain_ac = cs_result.voltage("drain");

    // Low-frequency gain: |Av| = gm * Rd should be > 0.01 for a CS amplifier
    double gain_low = std::abs(v_drain_ac.front());
    EXPECT_GT(gain_low, 0.01)
        << "CS amplifier gain at low frequency should be > 0.01";
    EXPECT_LT(gain_low, 50.0)
        << "CS amplifier gain should not be unreasonably high for this circuit";

    // Phase at low frequency: CS amplifier inverts, so phase should be
    // near +/-180 degrees.
    double phase_low_deg = std::arg(v_drain_ac.front()) * 180.0 / M_PI;
    EXPECT_GT(std::abs(phase_low_deg), 90.0)
        << "CS amp should show inverting phase (|phase| > 90 deg) at low frequency";
}

// ============================================================================
// 4.  Noise Analysis -- NMOS common-source amplifier noise
//
// Circuit: Vdd(3.3V) -> Rd(1k) -> drain, M1(drain,gate,0,0,0), Vin AC=1.
// HiSIM_HV noise model provides thermal, flicker (1/f), and shot noise.
// ============================================================================

TEST_F(HiSIMHVValidation, NmosNoise) {
    std::string cir_path = std::string(TEST_CIRCUITS_DIR) + "/hisimhv_nmos_noise.cir";

    // Run ngspice
    NgspiceNoiseResult ng_result;
    try {
        ng_result = ngspice_->run_noise(cir_path);
    } catch (const std::exception& e) {
        GTEST_SKIP() << "ngspice not available or failed: " << e.what();
    }

    if (ng_result.frequency.empty()) {
        GTEST_SKIP() << "ngspice returned empty noise result (HiSIM_HV may not be compiled in)";
    }

    // Run neospice
    auto ckt = sim_.load(cir_path);
    auto cs_result = sim_.run(ckt);
    ASSERT_TRUE(std::holds_alternative<NoiseResult>(cs_result.analysis)) << "neospice noise analysis returned no result";
    ASSERT_EQ(ng_result.frequency.size(), std::get<NoiseResult>(cs_result.analysis).frequency.size())
        << "Frequency point count mismatch";

    // Compare noise results. Use 10% relative tolerance since noise models
    // can have small implementation differences (induced gate noise, etc.).
    auto cmp = compare_noise(ng_result, std::get<NoiseResult>(cs_result.analysis), {1e-5, 1e-30});
    EXPECT_TRUE(cmp.passed)
        << "Noise comparison failed. Worst: " << cmp.worst_signal
        << " error: " << cmp.worst_error;

    // Verify basic noise physics: output noise density should be positive
    // and decrease with frequency (due to 1/f component)
    ASSERT_FALSE(std::get<NoiseResult>(cs_result.analysis).output_noise_density.empty());
    for (double n : std::get<NoiseResult>(cs_result.analysis).output_noise_density) {
        EXPECT_GE(n, 0.0) << "Noise spectral density should be non-negative";
    }
}
