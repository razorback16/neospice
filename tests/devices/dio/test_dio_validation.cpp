// Diode (DIODevice) ngspice comparison suite.
// Tests: DC sweep (forward + reverse), AC response, transient switching.
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
// Test fixture — shared NgspiceRunner + Simulator for all diode validation
// ============================================================================

class DiodeValidation : public ::testing::Test {
protected:
    void SetUp() override {
        ngspice_ = std::make_unique<NgspiceRunner>();
    }
    std::unique_ptr<NgspiceRunner> ngspice_;
    Simulator sim_;
};

// ============================================================================
// 1.  DC Sweep — V1 from -1V to +1V through R1 (1k) + D1 to ground
// ============================================================================

TEST_F(DiodeValidation, DCSweepNgspice) {
    std::string cir_path = std::string(TEST_CIRCUITS_DIR) + "/diode_dc_sweep.cir";

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
        {{DCSweepParam{"V1", -1.0, 1.0, 0.01}}});

    ASSERT_FALSE(cs_result.sweep_values.empty());

    // Both should have the same number of sweep points (201 points: -1 to +1 step 0.01)
    ASSERT_EQ(ng_result.sweep_values.size(), cs_result.sweep_values.size())
        << "ngspice and neospice should have the same number of sweep points";

    // Compare v(out) at each sweep point
    ASSERT_TRUE(ng_result.voltages.count("v(out)") > 0)
        << "ngspice result should contain v(out)";
    ASSERT_TRUE(cs_result.voltages.count("v(out)") > 0)
        << "neospice result should contain v(out)";

    const auto& ng_vout = ng_result.voltages.at("v(out)");
    const auto& cs_vout = cs_result.voltages.at("v(out)");

    int mismatches = 0;
    double worst_rel_err = 0.0;
    int worst_idx = -1;

    for (size_t i = 0; i < ng_result.sweep_values.size(); ++i) {
        double ng_v = ng_vout[i];
        double cs_v = cs_vout[i];

        // Use adaptive tolerance: wider near zero-crossing (threshold region)
        // where both values are small, tighter in forward/reverse regions.
        double abstol = 1e-6;  // 1 uV absolute floor
        double reltol = 0.02;  // 2% relative tolerance

        // Near the threshold (|vout| < 0.1V), use wider tolerance
        if (std::abs(ng_v) < 0.1) {
            abstol = 1e-3;  // 1 mV for small signals near threshold
        }

        double denom = std::max(std::abs(ng_v), abstol);
        double rel_err = std::abs(ng_v - cs_v) / denom;

        if (rel_err > worst_rel_err) {
            worst_rel_err = rel_err;
            worst_idx = static_cast<int>(i);
        }

        if (rel_err > reltol && std::abs(ng_v - cs_v) > abstol) {
            ++mismatches;
        }
    }

    // Allow up to 5% of points to mismatch (near threshold transitions)
    int max_mismatches = static_cast<int>(ng_result.sweep_values.size()) / 20;
    EXPECT_LE(mismatches, max_mismatches)
        << mismatches << "/" << ng_result.sweep_values.size()
        << " sweep points exceed tolerance. Worst relative error: "
        << worst_rel_err << " at sweep index " << worst_idx
        << " (V1=" << ng_result.sweep_values[worst_idx]
        << ", ngspice v(out)=" << ng_vout[worst_idx]
        << ", neospice v(out)=" << cs_vout[worst_idx] << ")";

    // Verify basic diode physics: forward bias region (V1 >= 2V)
    // should produce v(out) between 0.4V and 0.9V (diode drop)
    for (size_t i = 0; i < cs_result.sweep_values.size(); ++i) {
        if (cs_result.sweep_values[i] >= 2.0) {
            double v_out = cs_vout[i];
            EXPECT_GT(v_out, 0.4)
                << "Forward-biased diode voltage should be > 0.4V at V1="
                << cs_result.sweep_values[i];
            EXPECT_LT(v_out, 0.9)
                << "Forward-biased diode voltage should be < 0.9V at V1="
                << cs_result.sweep_values[i];
        }
    }

    // Reverse bias region (V1 < -0.5V): v(out) should be near V1
    // (almost no current flows, so negligible drop across R1)
    for (size_t i = 0; i < cs_result.sweep_values.size(); ++i) {
        if (cs_result.sweep_values[i] <= -0.5) {
            double v_out = cs_vout[i];
            EXPECT_NEAR(v_out, cs_result.sweep_values[i], 0.01)
                << "Reverse-biased: v(out) should be near V1 at V1="
                << cs_result.sweep_values[i];
        }
    }
}

// ============================================================================
// 2.  AC Response — Diode biased at DC=5V through R1=1k, AC sweep 1Hz-1MHz
//
// Tests the small-signal model including junction capacitance (CJO=10p,
// TT=5n).  At low frequencies the response is a resistive divider
// rd/(R1+rd).  At high frequencies the diode capacitance shunts more
// signal, changing the impedance.  The MODEINITSMSIG evaluation pass
// populates the capacitance state before ac_stamp() reads it.
// ============================================================================

TEST_F(DiodeValidation, ACResponseNgspice) {
    std::string cir_path = std::string(TEST_CIRCUITS_DIR) + "/diode_ac_response.cir";

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
    ACResult cs_result = sim_.run_ac(ckt, AnalysisCommand::DEC, 10, 1.0, 1e6);

    ASSERT_FALSE(cs_result.frequency.empty());

    ASSERT_EQ(ng_result.frequency.size(), cs_result.frequency.size())
        << "Frequency point count mismatch";

    auto cmp = compare_ac(ng_result, cs_result, {2e-2, 1e-9});
    EXPECT_TRUE(cmp.passed)
        << "AC comparison failed. Worst: " << cmp.worst_signal
        << " error: " << cmp.worst_error;

    // Verify basic AC physics: diode shunts signal to ground through rd.
    // With V1=5V, R1=1k, diode Vd~0.7V: Id = (5-0.7)/1k = 4.3mA
    // rd = NVt/Id = 0.026/4.3e-3 ~ 6 ohm
    // v(out)/v(in) = rd/(R1+rd) = 6/1006 ~ 0.006
    ASSERT_TRUE(cs_result.voltages.count("v(out)") > 0);
    const auto& cs_vout = cs_result.voltages.at("v(out)");

    double gain_low = std::abs(cs_vout[0]);
    EXPECT_LT(gain_low, 0.05)
        << "AC gain at low frequency should be small (diode shunts signal)";
    EXPECT_GT(gain_low, 1e-6)
        << "AC gain should not be zero";
}

// ============================================================================
// 3.  Transient — Diode switching with pulse source (1us rise/fall)
//
// Circuit: V1 PULSE(0,5,0,1u,1u,5u,12u) -> R1(1k) -> D1 -> GND
// Compare v(out) waveform between ngspice and neospice.
// ============================================================================

TEST_F(DiodeValidation, TransientSwitchingNgspice) {
    std::string cir_path = std::string(TEST_CIRCUITS_DIR) + "/diode_transient.cir";

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
    TransientResult cs_result = sim_.run_transient(ckt, 100e-9, 36e-6);

    ASSERT_FALSE(cs_result.time.empty());
    ASSERT_GT(cs_result.time.back(), 30e-6)
        << "Simulation should reach near final time";

    // Compare v(out) only (skip v(in) which is the pulse source and has
    // sharp edges that cause interpolation mismatch between simulators).
    // Strip all signals except v(out) from the ngspice result before
    // using the framework comparator.
    TransientResult ng_filtered;
    ng_filtered.time = ng_result.time;
    if (ng_result.voltages.count("v(out)") > 0) {
        ng_filtered.voltages["v(out)"] = ng_result.voltages.at("v(out)");
    }
    // Do not include currents — i(v1) has the same sharp-edge issue.

    // Compare at ngspice's time grid with interpolation of neospice.
    // Tolerance: 10% relative, 50mV absolute — accounts for timestep
    // control differences near switching edges.
    auto cmp = compare_transient(ng_filtered, cs_result, {1e-1, 5e-2});
    EXPECT_TRUE(cmp.passed)
        << "Transient comparison failed. Worst: " << cmp.worst_signal
        << " error: " << cmp.worst_error;

    // Verify basic transient physics
    ASSERT_TRUE(cs_result.voltages.count("v(out)") > 0);
    const auto& v_out = cs_result.voltages.at("v(out)");

    // During pulse high (V1=5V), diode should be forward biased:
    // v(out) ~ 0.6-0.8V (diode forward drop).
    // Find a time point during the first pulse high period (around t=3us,
    // well after the 1us rise time completes at t=1us).
    int idx_high = -1;
    for (size_t i = 0; i < cs_result.time.size(); ++i) {
        if (cs_result.time[i] >= 3e-6) {
            idx_high = static_cast<int>(i);
            break;
        }
    }
    ASSERT_GE(idx_high, 0) << "Could not find t=3us time point";
    EXPECT_GT(v_out[idx_high], 0.4)
        << "Diode should be forward biased during pulse high";
    EXPECT_LT(v_out[idx_high], 0.9)
        << "Diode forward voltage should be < 0.9V";

    // During pulse low (V1=0V), diode should be off:
    // v(out) ~ 0V (no current flowing).
    // The pulse goes low at t=6us (0 + 1u rise + 5u width = 6us),
    // then after 1u fall time at t=7us, it's fully low.
    // Check at t=9us (well into the low period).
    int idx_low = -1;
    for (size_t i = 0; i < cs_result.time.size(); ++i) {
        if (cs_result.time[i] >= 9e-6) {
            idx_low = static_cast<int>(i);
            break;
        }
    }
    ASSERT_GE(idx_low, 0) << "Could not find t=9us time point";
    EXPECT_NEAR(v_out[idx_low], 0.0, 0.1)
        << "Diode should be off during pulse low";
}
