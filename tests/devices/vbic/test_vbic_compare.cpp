// VBIC (Vertical Bipolar InterCompany) validation suite.
// Compares neospice VBIC device output against ngspice for:
//   1. NPN DC operating point
//   2. Gummel plot (DC sweep of VBE)
//   3. PNP DC operating point
//   4. AC small-signal (CE amplifier)
//   5. Switching transient

#include <gtest/gtest.h>
#include "api/neospice.hpp"
#include "framework/ngspice_runner.hpp"
#include "framework/comparator.hpp"
#include <algorithm>
#include <cmath>
#include <complex>

using namespace neospice;

class VBICValidation : public ::testing::Test {
protected:
    void SetUp() override {
        ngspice_ = std::make_unique<NgspiceRunner>();
    }
    std::unique_ptr<NgspiceRunner> ngspice_;
    Simulator sim_;
};

// ============================================================================
// 1.  NPN DC Operating Point
// ============================================================================

TEST_F(VBICValidation, NpnDcOperatingPoint) {
    // Common-emitter NPN with VBIC LEVEL=4 model.
    //   Vcc = 5V, Rc = 2k, Rb = 100k
    //   Bias: Ib ~ (5 - Vbe) / 100k, Ic ~ IS * exp(Vbe/(NF*VT))
    //   Expected: transistor in active region, V(col) between 0 and Vcc.
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/vbic_npn_dc.cir";

    auto ng_result = ngspice_->run_dc(path);
    auto ckt = sim_.load(path);
    auto cs_result = sim_.run_dc(ckt);

    // Strip internal nodes from ngspice result (names containing '#')
    for (auto it = ng_result.node_voltages.begin();
         it != ng_result.node_voltages.end(); ) {
        if (it->first.find('#') != std::string::npos)
            it = ng_result.node_voltages.erase(it);
        else
            ++it;
    }

    auto cmp = compare_dc(ng_result, cs_result, {1e-9, 1e-9});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}

// ============================================================================
// 2.  Gummel Plot — DC Sweep of VBE
// ============================================================================

TEST_F(VBICValidation, GummelPlot) {
    // Sweep VBE from 0.3V to 0.9V with Vce=2V fixed.
    // Collector current should follow Ic ~ IS * exp(Vbe/(NF*VT)) in
    // the low-injection region.
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/vbic_gummel.cir";

    auto ng_result = ngspice_->run_dc_sweep(path);
    auto ckt = sim_.load(path);
    auto cs_result = sim_.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCSweepResult>(cs_result.analysis));

    // Strip internal ngspice nodes (names containing '#')
    for (auto it = ng_result.voltages.begin();
         it != ng_result.voltages.end(); ) {
        if (it->first.find('#') != std::string::npos)
            it = ng_result.voltages.erase(it);
        else
            ++it;
    }
    for (auto it = ng_result.currents.begin();
         it != ng_result.currents.end(); ) {
        if (it->first.find('#') != std::string::npos)
            it = ng_result.currents.erase(it);
        else
            ++it;
    }

    const auto sweep_error = validate_dc_sweep_data(
        ng_result, std::get<DCSweepResult>(cs_result.analysis));
    ASSERT_TRUE(sweep_error.empty()) << sweep_error;

    // Compare sweep values — check both have same number of points.
    ASSERT_EQ(ng_result.sweep_values.size(), std::get<DCSweepResult>(cs_result.analysis).sweep_values.size());

    // Compare current waveforms across the sweep.  At low VBE (sub-threshold),
    // currents are on the order of pA and the difference between simulators
    // is dominated by gmin and solver tolerance — not a model error.  Use
    // an absolute floor of 1e-9 A (1 nA) so that the comparison focuses on
    // the physically meaningful region (VBE > ~0.5V where Ic >> 1 nA).
    // Relative tolerance of 1% catches real model discrepancies.
    for (const auto& [name, ng_vec] : ng_result.currents) {
        auto it = std::get<DCSweepResult>(cs_result.analysis).currents.find(name);
        ASSERT_NE(it, std::get<DCSweepResult>(cs_result.analysis).currents.end()) << name;
        const auto& cs_vec = it->second;
        ASSERT_EQ(ng_vec.size(), cs_vec.size());

        double worst_err = 0.0;
        for (size_t i = 0; i < ng_vec.size(); ++i) {
            double denom = std::max(std::abs(ng_vec[i]), 1e-9);
            double err = std::abs(ng_vec[i] - cs_vec[i]) / denom;
            worst_err = std::max(worst_err, err);
        }
        EXPECT_LT(worst_err, 1e-2)
            << "Gummel current '" << name << "' worst relative error: " << worst_err;
    }
}

// ============================================================================
// 3.  PNP DC Operating Point
// ============================================================================

TEST_F(VBICValidation, PnpDcOperatingPoint) {
    // PNP common-emitter: emitter at Vee=5V, collector pulled to GND
    // through Rc=2k, base biased through Rb=100k from GND.
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/vbic_pnp_dc.cir";

    auto ng_result = ngspice_->run_dc(path);
    auto ckt = sim_.load(path);
    auto cs_result = sim_.run_dc(ckt);

    // Strip internal nodes from ngspice result
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
}

// ============================================================================
// 4.  AC Small-Signal — CE Amplifier Gain
// ============================================================================

TEST_F(VBICValidation, AcSmallSignal) {
    // Common-emitter amplifier with VBIC transistor.
    //   AC sweep from 100 Hz to 1 GHz.
    //   Expect gain > 1 at midband, phase inversion.
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/vbic_ac.cir";

    auto ng_result = ngspice_->run_ac(path);
    auto ckt = sim_.load(path);
    auto cs_result = sim_.run(ckt);
    ASSERT_TRUE(std::holds_alternative<ACResult>(cs_result.analysis));

    // Strip internal ngspice nodes
    for (auto it = ng_result.voltages.begin();
         it != ng_result.voltages.end(); ) {
        if (it->first.find('#') != std::string::npos)
            it = ng_result.voltages.erase(it);
        else
            ++it;
    }
    for (auto it = ng_result.currents.begin();
         it != ng_result.currents.end(); ) {
        if (it->first.find('#') != std::string::npos)
            it = ng_result.currents.erase(it);
        else
            ++it;
    }

    auto cmp = compare_ac(ng_result, std::get<ACResult>(cs_result.analysis), {5e-7, 1e-9});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}

// ============================================================================
// 5.  Switching Transient
// ============================================================================

TEST_F(VBICValidation, SwitchingTransient) {
    // VBIC NPN switching transient: pulse input drives base through Rb,
    // collector loaded with Rc. Compare every required waveform on the
    // combined adaptive grids using the standard comparator tolerance.
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/vbic_transient.cir";

    auto ng_result = ngspice_->run_transient(path);
    auto ckt = sim_.load(path);
    // Use the same netlist options as the ngspice reference.
    auto cs_result = sim_.run(ckt);
    ASSERT_TRUE(std::holds_alternative<TransientResult>(cs_result.analysis));

    // Strip internal ngspice nodes
    for (auto it = ng_result.voltages.begin();
         it != ng_result.voltages.end(); ) {
        if (it->first.find('#') != std::string::npos)
            it = ng_result.voltages.erase(it);
        else
            ++it;
    }
    for (auto it = ng_result.currents.begin();
         it != ng_result.currents.end(); ) {
        if (it->first.find('#') != std::string::npos)
            it = ng_result.currents.erase(it);
        else
            ++it;
    }

    auto cmp = compare_transient(ng_result, std::get<TransientResult>(cs_result.analysis));
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}

TEST_F(VBICValidation, ExcessPhaseAC) {
    const std::string path = std::string(TEST_CIRCUITS_DIR) + "/vbic_delay_ac.cir";
    auto expected = ngspice_->run_ac(path);
    auto circuit = sim_.load(path);
    const auto result = sim_.run(circuit);
    ASSERT_TRUE(std::holds_alternative<ACResult>(result.analysis));
    std::erase_if(expected.voltages, [](const auto& item) {
        return item.first.find('#') != std::string::npos;
    });
    const auto& actual = std::get<ACResult>(result.analysis);
    const auto comparison = compare_ac(expected, actual);
    EXPECT_TRUE(comparison.passed) << comparison.worst_signal << " " << comparison.worst_error;
    // For this zero-charge, zero-Early-effect transistor, the independent
    // small-signal delay transfer is H(s)=1/(1+s*TD+s*s*TD*TD/3).
    const auto& zero = actual.current("vc_zero");
    const auto& slow = actual.current("vc_slow");
    ASSERT_EQ(zero.size(), actual.frequency.size());
    ASSERT_EQ(slow.size(), actual.frequency.size());
    ASSERT_FALSE(actual.frequency.empty());
    for (size_t i = 0; i < actual.frequency.size(); ++i) {
        const std::complex<double> s(0.0, 2.0 * std::acos(-1.0) * actual.frequency[i]);
        const auto expected_ratio = 1.0 / (1.0 + s * 1e-6 + s * s * (1e-12 / 3.0));
        ASSERT_GT(std::abs(zero[i]), 1e-3);
        EXPECT_LT(std::abs(slow[i] / zero[i] - expected_ratio), 1e-7);
    }
}

TEST_F(VBICValidation, ExcessPhaseTransient) {
    const std::string path = std::string(TEST_CIRCUITS_DIR) + "/vbic_delay_transient.cir";
    auto expected = ngspice_->run_transient(path);
    auto circuit = sim_.load(path);
    const auto result = sim_.run(circuit);
    ASSERT_TRUE(std::holds_alternative<TransientResult>(result.analysis));
    std::erase_if(expected.voltages, [](const auto& item) {
        return item.first.find('#') != std::string::npos;
    });
    const auto comparison = compare_transient(expected, std::get<TransientResult>(result.analysis));
    EXPECT_TRUE(comparison.passed) << comparison.worst_signal << " " << comparison.worst_error;
}

TEST_F(VBICValidation, ExcessPhaseGear) {
    const std::string path = std::string(TEST_CIRCUITS_DIR) + "/vbic_delay_gear.cir";
    auto expected = ngspice_->run_transient(path);
    auto circuit = sim_.load(path);
    const auto result = sim_.run(circuit);
    ASSERT_TRUE(std::holds_alternative<TransientResult>(result.analysis));
    std::erase_if(expected.voltages, [](const auto& item) {
        return item.first.find('#') != std::string::npos;
    });
    const auto comparison = compare_transient(expected, std::get<TransientResult>(result.analysis));
    EXPECT_TRUE(comparison.passed) << comparison.worst_signal << " " << comparison.worst_error;
}

TEST_F(VBICValidation, ExcessPhaseUIC) {
    const std::string path = std::string(TEST_CIRCUITS_DIR) + "/vbic_delay_uic.cir";
    auto expected = ngspice_->run_transient(path);
    auto circuit = sim_.load(path);
    const auto result = sim_.run(circuit);
    ASSERT_TRUE(std::holds_alternative<TransientResult>(result.analysis));
    std::erase_if(expected.voltages, [](const auto& item) {
        return item.first.find('#') != std::string::npos;
    });
    const auto comparison = compare_transient(expected, std::get<TransientResult>(result.analysis));
    EXPECT_TRUE(comparison.passed) << comparison.worst_signal << " " << comparison.worst_error;
}

TEST_F(VBICValidation, ExcessPhaseNoise) {
    for (const std::string name : {"vbic_delay_noise.cir", "vbic_delay_noise_pnp.cir"}) {
        SCOPED_TRACE(name);
        const std::string path = std::string(TEST_CIRCUITS_DIR) + "/" + name;
        const auto expected = ngspice_->run_noise(path);
        auto circuit = sim_.load(path);
        const auto result = sim_.run(circuit);
        ASSERT_TRUE(std::holds_alternative<NoiseResult>(result.analysis));
        const auto comparison = compare_noise(expected, std::get<NoiseResult>(result.analysis));
        EXPECT_TRUE(comparison.passed) << comparison.worst_signal << " " << comparison.worst_error;
    }
}
