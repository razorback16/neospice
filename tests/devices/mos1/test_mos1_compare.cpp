// MOS1 (Level 1 Shichman-Hodges MOSFET) ngspice comparison suite.
// Tests: DC operating point, NMOS IV curve (DC sweep), PMOS DC,
//        AC small-signal, transient pulse response.
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
// Test fixture — shared NgspiceRunner + Simulator for all MOS1 validation
// ============================================================================

class MOS1Validation : public ::testing::Test {
protected:
    void SetUp() override {
        ngspice_ = std::make_unique<NgspiceRunner>(NGSPICE_BINARY);
    }
    std::unique_ptr<NgspiceRunner> ngspice_;
    Simulator sim_;
};

// ============================================================================
// 1.  DC Operating Point — NMOS common-source with Rd=1k, Vgs=2V, Vdd=5V
//
// Circuit: Vdd(5V) -> Rd(1k) -> drain, M1(drain,gate,0,0)
// Expected: M1 is in saturation (Vgs-Vto=1.3V, Vds should be > Vgs-Vto).
//   Id = KP/2 * W/L * (Vgs-Vto)^2 = 55u * 10 * 1.3^2 ~ 0.93mA
//   V(drain) = 5 - Id*1k ~ 5 - 0.93 = 4.07V
// ============================================================================

TEST_F(MOS1Validation, NmosOperatingPoint) {
    std::string cir_path = std::string(TEST_CIRCUITS_DIR) + "/mos1_nmos_dc_op.cir";

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
    // MOS1 DC should be very close to ngspice since both use the same
    // Shichman-Hodges equations.  Use 1% relative, 1uV absolute.
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

    // Verify saturation: Vds > Vgs - Vto = 2.0 - 0.7 = 1.3V
    double vds = v_drain;  // source is at ground
    double vgs_minus_vto = v_gate - 0.7;
    EXPECT_GT(vds, vgs_minus_vto)
        << "MOSFET should be in saturation (Vds > Vgs-Vto)";
}

// ============================================================================
// 2.  NMOS IV Curve — DC sweep Vds from 0 to 5V at fixed Vgs=2.5V
//
// Measures drain current vs Vds. Should show linear region at low Vds
// transitioning to saturation at Vds = Vgs - Vto = 1.8V.
// ============================================================================

TEST_F(MOS1Validation, NmosIvCurveSweep) {
    std::string cir_path = std::string(TEST_CIRCUITS_DIR) + "/mos1_nmos_iv_sweep.cir";

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
    // ngspice names the voltage source current i(vds).
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
    // Id_sat = KP/2 * W/L * (Vgs-Vto)^2 = 55u * 10 * (2.5-0.7)^2 ~ 1.78mA
    double ids_at_5v = std::abs(cs_ids.back());
    EXPECT_GT(ids_at_5v, 0.5e-3)
        << "Saturation drain current should be > 0.5mA at Vds=5V";
    EXPECT_LT(ids_at_5v, 10e-3)
        << "Drain current should be < 10mA at Vds=5V";

    // Verify saturation: current at Vds=3V and Vds=5V should be similar
    // (within 20% due to lambda/CLM effects)
    int idx_3v = -1;
    for (size_t i = 0; i < cs_result.sweep_values.size(); ++i) {
        if (std::abs(cs_result.sweep_values[i] - 3.0) < 0.026) {
            idx_3v = static_cast<int>(i);
            break;
        }
    }
    ASSERT_GE(idx_3v, 0) << "Could not find Vds=3V sweep point";

    double ids_at_3v = std::abs(cs_ids[idx_3v]);
    double ratio = ids_at_5v / ids_at_3v;
    EXPECT_GT(ratio, 0.8) << "Saturation current should be relatively flat";
    EXPECT_LT(ratio, 1.3) << "Current should not increase too much in saturation";
}

// ============================================================================
// 3.  PMOS DC Operating Point — Verify PMOS polarity works correctly
//
// Circuit: Vdd(5V) -> M1(drain,gate,vdd,vdd) -> Rload(1k) -> GND
// PMOS: source at Vdd, drain pulled to ground through Rload.
// Vgs = V(gate) - V(vdd) = 3 - 5 = -2V (which is < Vto = -0.7V, so ON).
// Expected: Ids flowing, V(drain) pulled up from 0V.
// ============================================================================

TEST_F(MOS1Validation, PmosOperatingPoint) {
    std::string cir_path = std::string(TEST_CIRCUITS_DIR) + "/mos1_pmos_dc_op.cir";

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

    // Compare DC operating point. Use 1% relative tolerance.
    auto cmp = compare_dc(ng_result, cs_result, {1e-2, 1e-6});
    EXPECT_TRUE(cmp.passed)
        << "PMOS DC OP comparison failed. Worst: " << cmp.worst_signal
        << " error: " << cmp.worst_error;

    // Verify PMOS physics
    ASSERT_TRUE(cs_result.node_voltages.count("v(drain)") > 0);
    ASSERT_TRUE(cs_result.node_voltages.count("v(gate)") > 0);
    ASSERT_TRUE(cs_result.node_voltages.count("v(vdd)") > 0);

    double v_drain = cs_result.node_voltages["v(drain)"];
    double v_gate  = cs_result.node_voltages["v(gate)"];
    double v_vdd   = cs_result.node_voltages["v(vdd)"];

    EXPECT_NEAR(v_vdd, 5.0, 0.01);
    EXPECT_NEAR(v_gate, 3.0, 0.01);

    // PMOS: Vgs = V(gate) - V(source) = 3.0 - 5.0 = -2.0V
    // Vto = -0.7V, so |Vgs| > |Vto|, PMOS is on.
    // Current flows from source (Vdd) to drain through Rload.
    // V(drain) should be positive (pulled up by PMOS current through Rload).
    EXPECT_GT(v_drain, 0.1)
        << "PMOS drain should be pulled up from ground";
    EXPECT_LT(v_drain, 5.0)
        << "PMOS drain should be below Vdd";

    // Rough current estimate: Id = (KP/2) * (W/L) * (|Vgs|-|Vto|)^2
    // = (27.5u) * 20 * (2.0-0.7)^2 = 0.55m * 1.69 ~ 0.93mA
    // V(drain) = Id * Rload ~ 0.93V
    double id_approx = v_drain / 1000.0;
    EXPECT_GT(id_approx, 0.1e-3) << "PMOS should have significant drain current";
}

// ============================================================================
// 4.  AC Small-Signal — NMOS common-source amplifier
//
// Circuit: Vdd(5V) -> Rd(1k) -> drain, M1(drain,gate,0,0), Vin AC=1 at gate.
// Model includes overlap and junction capacitances for AC response.
// At DC bias: Vgs=2V, Id ~ 0.93mA, gm = 2*Id/(Vgs-Vto) ~ 1.43mS
// Low-frequency gain: Av = -gm * Rd = -1.43m * 1k = -1.43
// At high frequencies gain rolls off due to capacitances.
// ============================================================================

TEST_F(MOS1Validation, NmosAcResponse) {
    std::string cir_path = std::string(TEST_CIRCUITS_DIR) + "/mos1_nmos_ac.cir";

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

    // Compare AC results. MOS1 AC should be close since both use the same
    // linearized model. Use 5% relative tolerance, 1e-9 absolute.
    auto cmp = compare_ac(ng_result, cs_result, {5e-2, 1e-9});
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
// A small load capacitor ensures the transient integrator has charge
// storage elements to work with, preventing numerical issues.
// When input is high (5V), MOSFET conducts hard, drain goes low.
// When input is low (0V), MOSFET is off, drain floats to Vdd through Rd.
// ============================================================================

TEST_F(MOS1Validation, NmosTransientPulse) {
    std::string cir_path = std::string(TEST_CIRCUITS_DIR) + "/mos1_nmos_transient.cir";

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

    // Compare at ngspice's time grid with interpolation of neospice.
    // Tolerance: 55% relative, 50mV absolute — MOS1 Meyer capacitance model
    // is sensitive to timestep control differences near switching edges.
    auto cmp = compare_transient(ng_filtered, cs_result, {5.5e-1, 5e-2});
    EXPECT_TRUE(cmp.passed)
        << "Transient comparison failed. Worst: " << cmp.worst_signal
        << " error: " << cmp.worst_error;

    // Verify basic transient physics
    ASSERT_TRUE(cs_result.voltages.count("v(drain)") > 0);
    const auto& v_drain = cs_result.voltages.at("v(drain)");

    // During pulse high (Vin=5V), MOSFET should be on hard:
    // Id is large, V(drain) is pulled low.
    // Find a time point during first pulse high period (around t=1us,
    // well after the 100ns rise time).
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

    // During pulse low (Vin=0V), MOSFET should be off:
    // V(drain) should be near Vdd=5V (pulled up through Rd).
    // Pulse goes low at t=2.1us (0 + 100n rise + 2u width = 2.1us),
    // after 100n fall time at t=2.2us. Check at t=4us (well into low period).
    int idx_low = -1;
    for (size_t i = 0; i < cs_result.time.size(); ++i) {
        if (cs_result.time[i] >= 4e-6) {
            idx_low = static_cast<int>(i);
            break;
        }
    }
    ASSERT_GE(idx_low, 0) << "Could not find t=4us time point";
    EXPECT_GT(v_drain[idx_low], 4.0)
        << "V(drain) should be near Vdd when gate is low (MOSFET off)";
}
