// MOS3 (Level 3 MOSFET) ngspice comparison suite.
// Tests: DC operating point, NMOS IV curve (DC sweep), AC small-signal.
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
// Test fixture -- shared NgspiceRunner + Simulator for all MOS3 validation
// ============================================================================

class MOS3Validation : public ::testing::Test {
protected:
    void SetUp() override {
        ngspice_ = std::make_unique<NgspiceRunner>();
    }
    std::unique_ptr<NgspiceRunner> ngspice_;
    Simulator sim_;
};

// ============================================================================
// 1.  DC Operating Point -- NMOS common-source with Rd=1k, Vgs=2V, Vdd=5V
//
// Circuit: Vdd(5V) -> Rd(1k) -> drain, M1(drain,gate,0,0)
// MOS3 uses Level 3 model with short-channel effects (theta, eta, kappa).
// ============================================================================

TEST_F(MOS3Validation, NmosOperatingPoint) {
    std::string cir_path = std::string(TEST_CIRCUITS_DIR) + "/mos3_nmos_dc_op.cir";

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
    // MOS3 DC should be very close to ngspice since both use the same
    // Level 3 equations.  Use 1% relative, 1uV absolute.
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
// 2.  NMOS IV Curve -- DC sweep Vds from 0 to 5V at fixed Vgs=2.5V
//
// Measures drain current vs Vds. Should show linear region at low Vds
// transitioning to saturation.
// ============================================================================

TEST_F(MOS3Validation, NmosIvCurveSweep) {
    std::string cir_path = std::string(TEST_CIRCUITS_DIR) + "/mos3_nmos_iv_sweep.cir";

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

    // Run neospice on the same circuit
    auto ckt = sim_.load(cir_path);
    DCSweepResult cs_result = sim_.run_dc_sweep(ckt,
        {{DCSweepParam{"Vds", 0.0, 5.0, 0.05}}});

    ASSERT_FALSE(cs_result.sweep_values.empty());

    // Both should have the same number of sweep points
    ASSERT_EQ(ng_result.sweep_values.size(), cs_result.sweep_values.size())
        << "ngspice and neospice should have the same number of sweep points";

    // Compare drain current i(vds) at each sweep point.
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

        // Use adaptive tolerance
        double abstol = 1e-9;  // 1nA absolute floor
        double reltol = 0.02;  // 2% relative tolerance

        // Near Vds=0 where current is very small, widen tolerance
        if (std::abs(ng_i) < 1e-6) {
            abstol = 1e-7;
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

    // Allow up to 5% of points to mismatch (near transitions)
    int max_mismatches = static_cast<int>(ng_result.sweep_values.size()) / 20;
    EXPECT_LE(mismatches, max_mismatches)
        << mismatches << "/" << ng_result.sweep_values.size()
        << " sweep points exceed tolerance. Worst relative error: "
        << worst_rel_err << " at sweep index " << worst_idx
        << " (Vds=" << ng_result.sweep_values[worst_idx]
        << ", ngspice i(vds)=" << ng_ids[worst_idx]
        << ", neospice i(vds)=" << cs_ids[worst_idx] << ")";

    // Verify basic IV physics: current should increase with Vds in linear
    // region and saturate in saturation region.
    // At Vds=0, Id should be near zero.
    EXPECT_NEAR(std::abs(cs_ids[0]), 0.0, 1e-6)
        << "Drain current should be near zero at Vds=0";

    // At Vds=5V (well into saturation), current should be significant.
    double ids_at_5v = std::abs(cs_ids.back());
    EXPECT_GT(ids_at_5v, 0.1e-3)
        << "Saturation drain current should be > 0.1mA at Vds=5V";
    EXPECT_LT(ids_at_5v, 10e-3)
        << "Drain current should be < 10mA at Vds=5V";
}

// ============================================================================
// 3.  PMOS DC Operating Point — Verify PMOS polarity works correctly
//
// Circuit: Vdd(5V) -> M1(drain,gate,vdd,vdd) -> Rload(1k) -> GND
// PMOS: source at Vdd, drain pulled to ground through Rload.
// Vgs = V(gate) - V(vdd) = 3 - 5 = -2V (which is < Vto = -0.7V, so ON).
// ============================================================================

TEST_F(MOS3Validation, PmosOperatingPoint) {
    std::string cir_path = std::string(TEST_CIRCUITS_DIR) + "/mos3_pmos_dc_op.cir";

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

    auto cmp = compare_dc(ng_result, cs_result, {1e-2, 1e-6});
    EXPECT_TRUE(cmp.passed)
        << "PMOS DC OP comparison failed. Worst: " << cmp.worst_signal
        << " error: " << cmp.worst_error;

    // Verify PMOS physics
    ASSERT_TRUE(cs_result.node_voltages.count("v(drain)") > 0);
    double v_drain = cs_result.node_voltages["v(drain)"];

    EXPECT_GT(v_drain, 0.1)
        << "PMOS drain should be pulled up from ground";
    EXPECT_LT(v_drain, 5.0)
        << "PMOS drain should be below Vdd";
}

// ============================================================================
// 4.  AC Small-Signal -- NMOS common-source amplifier
//
// Circuit: Vdd(5V) -> Rd(1k) -> drain, M1(drain,gate,0,0), Vin AC=1 at gate.
// Model includes overlap and junction capacitances for AC response.
// ============================================================================

TEST_F(MOS3Validation, NmosAcResponse) {
    std::string cir_path = std::string(TEST_CIRCUITS_DIR) + "/mos3_nmos_ac.cir";

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

    // Compare AC results. MOS3 AC should be close since both use the same
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

    // Gain at high frequency should be lower due to capacitances
    double gain_high = std::abs(v_drain_ac.back());
    EXPECT_LT(gain_high, gain_low * 1.1)
        << "Gain at highest frequency should not exceed low-frequency gain";

    // Phase at low frequency: CS amplifier inverts, so phase should be
    // near +/-180 degrees.
    double phase_low_deg = std::arg(v_drain_ac.front()) * 180.0 / M_PI;
    EXPECT_GT(std::abs(phase_low_deg), 90.0)
        << "CS amp should show inverting phase (|phase| > 90 deg) at low frequency";
}

// ============================================================================
// 5.  Transient — NMOS inverter pulse response
//
// Circuit: Vdd(5V) -> Rd(1k) -> drain, M1(drain,gate,0,0), Cl=1pF on drain
// Input: PULSE(0, 5, 0, 100ns, 100ns, 2us, 5us)
// Tests truncation error control (compute_trunc) with charge storage.
// ============================================================================

TEST_F(MOS3Validation, NmosTransientPulse) {
    std::string cir_path = std::string(TEST_CIRCUITS_DIR) + "/mos3_nmos_transient.cir";

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
    TransientResult cs_result = sim_.run_transient(ckt, 50e-9, 15e-6);

    ASSERT_FALSE(cs_result.time.empty());
    ASSERT_GT(cs_result.time.back(), 10e-6)
        << "Simulation should reach near final time";

    // Compare v(drain) only — skip v(gate) which is the pulse source and
    // has sharp edges that cause interpolation mismatch between simulators.
    TransientResult ng_filtered;
    ng_filtered.time = ng_result.time;
    if (ng_result.voltages.count("v(drain)") > 0) {
        ng_filtered.voltages["v(drain)"] = ng_result.voltages.at("v(drain)");
    }

    auto cmp = compare_transient(ng_filtered, cs_result, {5e-2, 5e-3});
    EXPECT_TRUE(cmp.passed)
        << "Transient comparison failed. Worst: " << cmp.worst_signal
        << " error: " << cmp.worst_error;

    // Verify basic transient physics
    ASSERT_TRUE(cs_result.voltages.count("v(drain)") > 0);
    const auto& v_drain = cs_result.voltages.at("v(drain)");

    int idx_high = -1;
    for (size_t i = 0; i < cs_result.time.size(); ++i) {
        if (cs_result.time[i] >= 1e-6) {
            idx_high = static_cast<int>(i);
            break;
        }
    }
    ASSERT_GE(idx_high, 0) << "Could not find t=1us time point";
    EXPECT_LT(v_drain[idx_high], 2.0)
        << "V(drain) should be pulled low when gate is high (MOSFET on)";
}
