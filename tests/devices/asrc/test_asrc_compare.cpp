// ASRC (B element behavioral source) ngspice comparison suite.
// Tests: voltage-mode doubler, current-mode VCCS, nonlinear squaring,
//        trig function, multi-variable expression, AC gain.
// Each test runs the same .cir circuit through both ngspice and neospice
// and compares results within engineering tolerances.

#include <gtest/gtest.h>
#include "api/neospice.hpp"
#include "framework/ngspice_runner.hpp"
#include "framework/comparator.hpp"

#include <cmath>
#include <complex>
#include <string>

using namespace neospice;

// ============================================================================
// Test fixture — shared NgspiceRunner + Simulator for all ASRC validation
// ============================================================================

class ASRCValidation : public ::testing::Test {
protected:
    void SetUp() override {
        ngspice_ = std::make_unique<NgspiceRunner>();
    }
    std::unique_ptr<NgspiceRunner> ngspice_;
    Simulator sim_;
};

// ============================================================================
// 1.  Voltage Mode Doubler
//
// Circuit: V1 (1.5V) -> B1 out 0 V={V(in)*2} -> R1 (1k) to GND
// Expected: V(out) = 3.0 V
// ============================================================================

TEST_F(ASRCValidation, VoltageDoublerDC) {
    std::string cir_path = std::string(TEST_CIRCUITS_DIR) + "/asrc_voltage_doubler.cir";

    // Run ngspice
    DCResult ng_result;
    try {
        ng_result = ngspice_->run_dc(cir_path);
    } catch (const std::exception& e) {
        GTEST_SKIP() << "ngspice not available or failed: " << e.what();
    }

    // Run neospice
    auto ckt = sim_.load(cir_path);
    auto cs_result = sim_.run_dc(ckt);

    auto cmp = compare_dc(ng_result, cs_result, {1e-6, 1e-12});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;

    // Verify absolute value
    double v_out = cs_result.node_voltages.count("v(out)")
                       ? cs_result.node_voltages.at("v(out)") : 0.0;
    EXPECT_NEAR(v_out, 3.0, 1e-6)
        << "V(out) should be V(in)*2 = 1.5*2 = 3.0";
}

// ============================================================================
// 2.  Current Mode VCCS
//
// Circuit: V1 (5V at 'in') -> B1 0 out I={V(in)*1e-3} -> R1 (1k) to GND
// Current = V(in)*1e-3 = 5mA flowing from 0 to out => V(out) = I*R = 5V
// ============================================================================

TEST_F(ASRCValidation, CurrentModeVCCS) {
    std::string cir_path = std::string(TEST_CIRCUITS_DIR) + "/asrc_vccs.cir";

    // Run ngspice
    DCResult ng_result;
    try {
        ng_result = ngspice_->run_dc(cir_path);
    } catch (const std::exception& e) {
        GTEST_SKIP() << "ngspice not available or failed: " << e.what();
    }

    // Run neospice
    auto ckt = sim_.load(cir_path);
    auto cs_result = sim_.run_dc(ckt);

    auto cmp = compare_dc(ng_result, cs_result, {1e-6, 1e-12});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;

    // Verify absolute value
    double v_out = cs_result.node_voltages.count("v(out)")
                       ? cs_result.node_voltages.at("v(out)") : 0.0;
    EXPECT_NEAR(v_out, 5.0, 1e-6)
        << "V(out) should be V(in)*1e-3*R = 5*1e-3*1000 = 5.0";
}

// ============================================================================
// 3.  Nonlinear Squaring Function
//
// Circuit: V1 (3V) -> B1 out 0 V={V(in)*V(in)} -> R1 (1k) to GND
// Expected: V(out) = 9.0 V
// ============================================================================

TEST_F(ASRCValidation, NonlinearSquare) {
    std::string cir_path = std::string(TEST_CIRCUITS_DIR) + "/asrc_square.cir";

    // Run ngspice
    DCResult ng_result;
    try {
        ng_result = ngspice_->run_dc(cir_path);
    } catch (const std::exception& e) {
        GTEST_SKIP() << "ngspice not available or failed: " << e.what();
    }

    // Run neospice
    auto ckt = sim_.load(cir_path);
    auto cs_result = sim_.run_dc(ckt);

    auto cmp = compare_dc(ng_result, cs_result, {1e-6, 1e-12});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;

    // Verify absolute value
    double v_out = cs_result.node_voltages.count("v(out)")
                       ? cs_result.node_voltages.at("v(out)") : 0.0;
    EXPECT_NEAR(v_out, 9.0, 1e-6)
        << "V(out) should be V(in)^2 = 3^2 = 9.0";
}

// ============================================================================
// 4.  Trig Function — sin()
//
// Circuit: V1 (pi/6 V) -> B1 out 0 V={sin(V(in))} -> R1 (1k) to GND
// Expected: V(out) = sin(pi/6) = 0.5 V
// ============================================================================

TEST_F(ASRCValidation, TrigSin) {
    std::string cir_path = std::string(TEST_CIRCUITS_DIR) + "/asrc_trig.cir";

    // Run ngspice
    DCResult ng_result;
    try {
        ng_result = ngspice_->run_dc(cir_path);
    } catch (const std::exception& e) {
        GTEST_SKIP() << "ngspice not available or failed: " << e.what();
    }

    // Run neospice
    auto ckt = sim_.load(cir_path);
    auto cs_result = sim_.run_dc(ckt);

    auto cmp = compare_dc(ng_result, cs_result, {1e-6, 1e-12});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;

    // Verify absolute value
    double v_out = cs_result.node_voltages.count("v(out)")
                       ? cs_result.node_voltages.at("v(out)") : 0.0;
    EXPECT_NEAR(v_out, 0.5, 1e-6)
        << "V(out) should be sin(pi/6) = 0.5";
}

// ============================================================================
// 5.  Multi-Variable Expression
//
// Circuit: V1=2V (a), V2=3V (b) -> B1 out 0 V={V(a)*V(a) + V(b)*2}
// Expected: V(out) = 4 + 6 = 10.0 V
// ============================================================================

TEST_F(ASRCValidation, MultiVariable) {
    std::string cir_path = std::string(TEST_CIRCUITS_DIR) + "/asrc_multi_var.cir";

    // Run ngspice
    DCResult ng_result;
    try {
        ng_result = ngspice_->run_dc(cir_path);
    } catch (const std::exception& e) {
        GTEST_SKIP() << "ngspice not available or failed: " << e.what();
    }

    // Run neospice
    auto ckt = sim_.load(cir_path);
    auto cs_result = sim_.run_dc(ckt);

    auto cmp = compare_dc(ng_result, cs_result, {1e-6, 1e-12});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;

    // Verify absolute value
    double v_out = cs_result.node_voltages.count("v(out)")
                       ? cs_result.node_voltages.at("v(out)") : 0.0;
    EXPECT_NEAR(v_out, 10.0, 1e-6)
        << "V(out) should be V(a)^2 + V(b)*2 = 4 + 6 = 10.0";
}

// ============================================================================
// 6a. Temperature Coefficient — Voltage Mode
//
// Circuit: V1=1V -> B1 out 0 V={V(in)} tc1=0.001 tc2=0.00001
// TEMP=100 => difference = (100+273.15) - 300.15 = 73.0 K
// factor = 1 + 0.001*73.0 + 0.00001*73.0^2 = 1 + 0.073 + 0.05329 = 1.12629
// Expected: V(out) = 1.0 * 1.12629 ≈ 1.12629 V (but ngspice is the reference)
// ============================================================================

TEST_F(ASRCValidation, TempCoVoltageMode) {
    std::string cir_path = std::string(TEST_CIRCUITS_DIR) + "/asrc_tempco.cir";

    // Run ngspice
    DCResult ng_result;
    try {
        ng_result = ngspice_->run_dc(cir_path);
    } catch (const std::exception& e) {
        GTEST_SKIP() << "ngspice not available or failed: " << e.what();
    }

    // Run neospice
    auto ckt = sim_.load(cir_path);
    auto cs_result = sim_.run_dc(ckt);

    auto cmp = compare_dc(ng_result, cs_result, {1e-6, 1e-12});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;

    // Verify that temperature scaling is applied (output > 1.0)
    double v_out = cs_result.node_voltages.count("v(out)")
                       ? cs_result.node_voltages.at("v(out)") : 0.0;
    EXPECT_GT(v_out, 1.05)
        << "V(out) should be > 1.0 due to tc1/tc2 at TEMP=100";
}

// ============================================================================
// 6b. Temperature Coefficient — Current Mode
//
// Circuit: V1=5V -> B1 0 out I={V(in)*1e-3} tc1=0.001 tc2=0.00001 -> R1(1k)
// TEMP=100 => factor ≈ 1.12629
// Expected: V(out) = 5*1e-3*1000*factor ≈ 5.63 V (but ngspice is the reference)
// ============================================================================

TEST_F(ASRCValidation, TempCoCurrentMode) {
    std::string cir_path = std::string(TEST_CIRCUITS_DIR) + "/asrc_tempco_current.cir";

    // Run ngspice
    DCResult ng_result;
    try {
        ng_result = ngspice_->run_dc(cir_path);
    } catch (const std::exception& e) {
        GTEST_SKIP() << "ngspice not available or failed: " << e.what();
    }

    // Run neospice
    auto ckt = sim_.load(cir_path);
    auto cs_result = sim_.run_dc(ckt);

    auto cmp = compare_dc(ng_result, cs_result, {1e-6, 1e-12});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;

    // Verify that temperature scaling is applied (output > 5.0)
    double v_out = cs_result.node_voltages.count("v(out)")
                       ? cs_result.node_voltages.at("v(out)") : 0.0;
    EXPECT_GT(v_out, 5.3)
        << "V(out) should be > 5.0 due to tc1/tc2 at TEMP=100";
}

// ============================================================================
// 6.  AC Analysis — Behavioral Voltage Amplifier
//
// Circuit: V1 AC=1 -> B1 out 0 V={V(in)*5} -> R1 (1k) to GND
// Expected: flat gain of 5 (14 dB) at all frequencies.
// The B element should pass AC small-signal linearization correctly.
// ============================================================================

TEST_F(ASRCValidation, ACGain) {
    std::string cir_path = std::string(TEST_CIRCUITS_DIR) + "/asrc_ac_gain.cir";

    // Run ngspice AC
    ACResult ng_result;
    try {
        ng_result = ngspice_->run_ac(cir_path);
    } catch (const std::exception& e) {
        GTEST_SKIP() << "ngspice not available or failed: " << e.what();
    }

    // Run neospice
    auto ckt = sim_.load(cir_path);
    auto cs_result = sim_.run(ckt);
    ASSERT_TRUE(std::holds_alternative<ACResult>(cs_result.analysis)) << "neospice should produce AC result";

    auto cmp = compare_ac(ng_result, std::get<ACResult>(cs_result.analysis), {1e-10, 1e-15});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;

    // Verify gain at a mid-band frequency
    // V(out) should be 5*V(in) = 5+0j at all frequencies
    if (!std::get<ACResult>(cs_result.analysis).voltages.empty() &&
        std::get<ACResult>(cs_result.analysis).voltages.count("v(out)")) {
        const auto& vout = std::get<ACResult>(cs_result.analysis).voltages.at("v(out)");
        ASSERT_FALSE(vout.empty());
        // Check magnitude at first frequency point
        double mag = std::abs(vout[0]);
        EXPECT_NEAR(mag, 5.0, 1e-3)
            << "AC gain should be 5.0 (the multiplier in the B expression)";
    }
}
