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

using namespace neospice;

class VBICValidation : public ::testing::Test {
protected:
    void SetUp() override {
        ngspice_ = std::make_unique<NgspiceRunner>(NGSPICE_BINARY);
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

    auto cmp = compare_dc(ng_result, cs_result, {1e-3, 1e-9});
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
    ASSERT_TRUE(cs_result.dc_sweep.has_value());

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

    // Compare sweep values — check both have same number of points.
    ASSERT_EQ(ng_result.sweep_values.size(), cs_result.dc_sweep->sweep_values.size());

    // Compare current waveforms across the sweep.  At low VBE (sub-threshold),
    // currents are on the order of pA and the difference between simulators
    // is dominated by gmin and solver tolerance — not a model error.  Use
    // an absolute floor of 1e-9 A (1 nA) so that the comparison focuses on
    // the physically meaningful region (VBE > ~0.5V where Ic >> 1 nA).
    // Relative tolerance of 1% catches real model discrepancies.
    for (const auto& [name, ng_vec] : ng_result.currents) {
        auto it = cs_result.dc_sweep->currents.find(name);
        if (it == cs_result.dc_sweep->currents.end()) continue;
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
    ASSERT_TRUE(cs_result.ac.has_value());

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

    auto cmp = compare_ac(ng_result, *cs_result.ac, {1e-2, 1e-9});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}

// ============================================================================
// 5.  Switching Transient
// ============================================================================

TEST_F(VBICValidation, SwitchingTransient) {
    // VBIC NPN switching transient: pulse input drives base through Rb,
    // collector loaded with Rc.  Compare waveform at key timepoints.
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/vbic_transient.cir";

    auto ng_result = ngspice_->run_transient(path);
    auto ckt = sim_.load(path);
    auto cs_result = sim_.run(ckt);
    ASSERT_TRUE(cs_result.transient.has_value());

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

    auto cmp = compare_transient(*cs_result.transient, ng_result, {3e-1, 5e-2});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}
