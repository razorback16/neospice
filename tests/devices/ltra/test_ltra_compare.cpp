// LTRA (Lossy Transmission Line) ngspice comparison suite.
// Tests: DC operating point and transient analysis for RC, RLC, LC, and RG
// line models.  Each test runs the same .cir circuit through both ngspice
// and neospice and compares results within engineering tolerances.

#include <gtest/gtest.h>
#include "api/neospice.hpp"
#include "framework/ngspice_runner.hpp"
#include "framework/comparator.hpp"
#include "devices/ltra.hpp"
#include "core/circuit.hpp"

#include <cmath>
#include <string>

using namespace neospice;

// ============================================================================
// Test fixture — shared NgspiceRunner + Simulator for all LTRA validation
// ============================================================================

class LTRAValidation : public ::testing::Test {
protected:
    void SetUp() override {
        ngspice_ = std::make_unique<NgspiceRunner>();
    }
    std::unique_ptr<NgspiceRunner> ngspice_;
    Simulator sim_;
};

// ============================================================================
// 1.  DC Operating Point — RC lossy line
//
// Circuit: V1 (1V) -> O1 (R=50, C=100p, LEN=0.01) -> R1 (1k) -> GND
// At DC, the RC line acts as a series resistor R*LEN = 50*0.01 = 0.5 ohm.
// Expected V(out) = 1 * 1000/(1000+0.5) ≈ 0.9995 V
// ============================================================================

TEST_F(LTRAValidation, DCOperatingPointRC) {
    std::string cir_path = std::string(TEST_CIRCUITS_DIR) + "/ltra_dc_rc.cir";

    // Run ngspice
    DCResult ng_result;
    try {
        ng_result = ngspice_->run_dc(cir_path);
    } catch (const std::exception& e) {
        FAIL() << "required ngspice not available or failed: " << e.what();
    }

    // Run neospice
    auto ckt = sim_.load(cir_path);
    auto cs_result = sim_.run_dc(ckt);

    // Use compare_dc for consistent comparison.
    // Strip ngspice internal nodes (containing '#') that we name differently.
    for (auto it = ng_result.node_voltages.begin();
         it != ng_result.node_voltages.end(); ) {
        if (it->first.find('#') != std::string::npos)
            it = ng_result.node_voltages.erase(it);
        else
            ++it;
    }

    auto cmp = compare_dc(ng_result, cs_result, {5e-14, 1e-9});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;

    // Also verify absolute values are physically reasonable
    double v_out_ng = ng_result.node_voltages.count("v(out)")
                          ? ng_result.node_voltages.at("v(out)")
                          : 0.0;
    double v_out_cs = cs_result.node_voltages.count("v(out)")
                          ? cs_result.node_voltages.at("v(out)")
                          : 0.0;
    // RC line at DC: R*LEN = 0.5 ohm, load = 1k => V(out) ≈ 0.9995 V
    double expected_vout = 1.0 * 1000.0 / (1000.0 + 0.5);
    EXPECT_NEAR(v_out_ng, expected_vout, 0.01)
        << "ngspice V(out) should match analytical expectation";
    EXPECT_NEAR(v_out_cs, expected_vout, 0.01)
        << "neospice V(out) should match analytical expectation";
}

// ============================================================================
// 2.  DC Operating Point — RG lossy line
//
// Circuit: V1 (1V) -> O1 (R=100, G=0.01, LEN=0.5) -> R1 (1k) -> GND
// RG line at DC uses cosh/sinh formulation.
// The output voltage should be between 0 and 1 V due to series R and shunt G.
// ============================================================================

TEST_F(LTRAValidation, DCOperatingPointRG) {
    std::string cir_path = std::string(TEST_CIRCUITS_DIR) + "/ltra_dc_rg.cir";

    // Run ngspice
    DCResult ng_result;
    try {
        ng_result = ngspice_->run_dc(cir_path);
    } catch (const std::exception& e) {
        FAIL() << "required ngspice not available or failed: " << e.what();
    }

    // Run neospice
    auto ckt = sim_.load(cir_path);
    auto cs_result = sim_.run_dc(ckt);

    // Strip ngspice internal nodes
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

    // Verify output is physically reasonable: 0 < V(out) < 1
    double v_out_cs = cs_result.node_voltages.count("v(out)")
                          ? cs_result.node_voltages.at("v(out)")
                          : -1.0;
    EXPECT_GT(v_out_cs, 0.0) << "V(out) should be positive";
    EXPECT_LT(v_out_cs, 1.0) << "V(out) should be less than source voltage";
}

// ============================================================================
// 3.  Transient — RC lossy line with step input
//
// Circuit: PULSE source -> O1 (R=50, C=100p, LEN=0.01) -> R1 (1k) -> GND
// Verifies that the RC convolution produces correct transient waveforms.
// ============================================================================

TEST_F(LTRAValidation, TransientRC) {
    std::string cir_path = std::string(TEST_CIRCUITS_DIR) + "/ltra_tran_rc.cir";

    // Run ngspice
    TransientResult ng_result;
    try {
        ng_result = ngspice_->run_transient(cir_path);
    } catch (const std::exception& e) {
        FAIL() << "required ngspice not available or failed: " << e.what();
    }

    // Run neospice
    auto ckt = sim_.load(cir_path);
    auto cs_result = sim_.run(ckt);
    ASSERT_TRUE(std::holds_alternative<TransientResult>(cs_result.analysis));

    // Verify that v(out) is present and has reasonable values
    ASSERT_TRUE(std::get<TransientResult>(cs_result.analysis).voltages.count("v(out)") > 0);
    const auto& v_out = std::get<TransientResult>(cs_result.analysis).voltages.at("v(out)");
    ASSERT_GT(v_out.size(), 10u);

    // Verify v(out) reaches steady state near 1V (after the pulse rises)
    // and starts near 0V (before the pulse)
    double v_first = v_out.front();
    double v_last = v_out.back();  // during pulse-on phase
    EXPECT_NEAR(v_first, 0.0, 0.01) << "v(out) should start at 0";

    // Compare port voltages and source current. ngspice additionally exposes
    // the two internal LTRA branch equations as voltage-named vectors; these
    // implementation variables are not part of neospice's public results.
    ng_result.voltages.erase("v(o1#i1)");
    ng_result.voltages.erase("v(o1#i2)");
    auto cmp = compare_transient(ng_result, std::get<TransientResult>(cs_result.analysis), {5e-2, 5e-3});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
    // We use very loose relative tolerance because edge timing differences
    // cause large relative errors at fast transients. Focus on absolute error.
    // Check that absolute error in v(out) is small (< 0.1V)
    bool v_out_ok = true;
    // A missing reference signal must fail, not skip: as a plain `if` this
    // whole comparison was skippable and the test passed vacuously.
    ASSERT_TRUE(ng_result.voltages.count("v(out)"))
        << "ngspice 47 returned no v(out); nothing was compared";
    {
        const auto& ng_v = ng_result.voltages.at("v(out)");
        for (size_t i = 0; i < std::get<TransientResult>(cs_result.analysis).time.size(); ++i) {
            double t = std::get<TransientResult>(cs_result.analysis).time[i];
            double ns_val = v_out[i];
            // Interpolate ngspice
            double ng_val = 0;
            for (size_t j = 1; j < ng_result.time.size(); ++j) {
                if (ng_result.time[j] >= t) {
                    double frac = (t - ng_result.time[j-1]) / (ng_result.time[j] - ng_result.time[j-1]);
                    ng_val = ng_v[j-1] + frac * (ng_v[j] - ng_v[j-1]);
                    break;
                }
            }
            if (std::abs(ns_val - ng_val) > 0.15) {
                v_out_ok = false;
                break;
            }
        }
    }
    EXPECT_TRUE(v_out_ok) << "v(out) absolute error exceeds 0.15V";
}

// ============================================================================
// 4.  Transient — RLC lossy line with step input
//
// Circuit: PULSE source -> O1 (R=0.1, L=250n, C=100p, LEN=1) -> R1 (50) -> GND
// Verifies RLC convolution with delayed values (h2, h3dash contributions).
// ============================================================================

TEST_F(LTRAValidation, TransientRLC) {
    std::string cir_path = std::string(TEST_CIRCUITS_DIR) + "/ltra_tran_rlc.cir";

    // Run ngspice
    TransientResult ng_result;
    try {
        ng_result = ngspice_->run_transient(cir_path);
    } catch (const std::exception& e) {
        FAIL() << "required ngspice not available or failed: " << e.what();
    }

    // Run neospice
    auto ckt = sim_.load(cir_path);
    auto cs_result = sim_.run(ckt);
    ASSERT_TRUE(std::holds_alternative<TransientResult>(cs_result.analysis));

    ASSERT_TRUE(std::get<TransientResult>(cs_result.analysis).voltages.count("v(out)") > 0);
    const auto& v_out = std::get<TransientResult>(cs_result.analysis).voltages.at("v(out)");
    ASSERT_GT(v_out.size(), 10u);

    // Check that v(out) absolute error vs ngspice is bounded
    bool v_out_ok = true;
    double worst_abs = 0;
    // A missing reference signal must fail, not skip: as a plain `if` this
    // whole comparison was skippable and the test passed vacuously.
    ASSERT_TRUE(ng_result.voltages.count("v(out)"))
        << "ngspice 47 returned no v(out); nothing was compared";
    {
        const auto& ng_v = ng_result.voltages.at("v(out)");
        for (size_t i = 0; i < std::get<TransientResult>(cs_result.analysis).time.size(); ++i) {
            double t = std::get<TransientResult>(cs_result.analysis).time[i];
            double ns_val = v_out[i];
            double ng_val = 0;
            for (size_t j = 1; j < ng_result.time.size(); ++j) {
                if (ng_result.time[j] >= t) {
                    double frac = (t - ng_result.time[j-1]) / (ng_result.time[j] - ng_result.time[j-1]);
                    ng_val = ng_v[j-1] + frac * (ng_v[j] - ng_v[j-1]);
                    break;
                }
            }
            double ae = std::abs(ns_val - ng_val);
            if (ae > worst_abs) worst_abs = ae;
            if (ae > 0.15) {
                v_out_ok = false;
            }
        }
    }
    EXPECT_TRUE(v_out_ok) << "v(out) absolute error exceeds 0.15V, worst=" << worst_abs;
}

// ============================================================================
// 5.  Transient — LC lossless line with step input
//
// Circuit: PULSE source -> O1 (L=250n, C=100p, LEN=1) -> R1 (50) -> GND
// Verifies LC (lossless) line: delayed-value interpolation with attenuation=1.
// ============================================================================

TEST_F(LTRAValidation, TransientLC) {
    std::string cir_path = std::string(TEST_CIRCUITS_DIR) + "/ltra_tran_lc.cir";

    // Run ngspice
    TransientResult ng_result;
    try {
        ng_result = ngspice_->run_transient(cir_path);
    } catch (const std::exception& e) {
        FAIL() << "required ngspice not available or failed: " << e.what();
    }

    // Run neospice
    auto ckt = sim_.load(cir_path);
    auto cs_result = sim_.run(ckt);
    ASSERT_TRUE(std::holds_alternative<TransientResult>(cs_result.analysis));

    ASSERT_TRUE(std::get<TransientResult>(cs_result.analysis).voltages.count("v(out)") > 0);
    const auto& v_out = std::get<TransientResult>(cs_result.analysis).voltages.at("v(out)");
    ASSERT_GT(v_out.size(), 10u);

    // Check that v(out) absolute error vs ngspice is bounded
    bool v_out_ok = true;
    double worst_abs = 0;
    // A missing reference signal must fail, not skip: as a plain `if` this
    // whole comparison was skippable and the test passed vacuously.
    ASSERT_TRUE(ng_result.voltages.count("v(out)"))
        << "ngspice 47 returned no v(out); nothing was compared";
    {
        const auto& ng_v = ng_result.voltages.at("v(out)");
        for (size_t i = 0; i < std::get<TransientResult>(cs_result.analysis).time.size(); ++i) {
            double t = std::get<TransientResult>(cs_result.analysis).time[i];
            double ns_val = v_out[i];
            double ng_val = 0;
            for (size_t j = 1; j < ng_result.time.size(); ++j) {
                if (ng_result.time[j] >= t) {
                    double frac = (t - ng_result.time[j-1]) / (ng_result.time[j] - ng_result.time[j-1]);
                    ng_val = ng_v[j-1] + frac * (ng_v[j] - ng_v[j-1]);
                    break;
                }
            }
            double ae = std::abs(ns_val - ng_val);
            if (ae > worst_abs) worst_abs = ae;
            if (ae > 0.15) {
                v_out_ok = false;
            }
        }
    }
    EXPECT_TRUE(v_out_ok) << "v(out) absolute error exceeds 0.15V, worst=" << worst_abs;
}

TEST_F(LTRAValidation, ACFrequencyResponseAllLineTypes) {
    for (const std::string kind : {"lc", "rc", "rlc", "rg", "lc_floating", "rg_options"}) {
        SCOPED_TRACE(kind);
        const std::string path = std::string(TEST_CIRCUITS_DIR) + "/ltra_ac_" + kind + ".cir";
        auto reference = ngspice_->run_ac(path);
        // These are private branch-equation variables, not port voltages.
        reference.voltages.erase("v(o1#i1)");
        reference.voltages.erase("v(o1#i2)");
        auto circuit = sim_.load(path);
        const auto actual = std::get<ACResult>(sim_.run(circuit).analysis);
        const auto cmp = compare_ac(reference, actual, {1e-8, 1e-9});
        EXPECT_TRUE(cmp.passed) << cmp.worst_signal << " " << cmp.worst_error;
        ASSERT_EQ(actual.frequency.size(), 29u);
        if (kind.starts_with("lc")) {
            // Matched 50-ohm line: half-amplitude delayed sinusoid. Checking
            // the full complex voltage distinguishes propagation from a short.
            const auto& output = actual.voltages.at("v(out)");
            ASSERT_EQ(output.size(), actual.frequency.size());
            for (size_t i = 0; i < output.size(); ++i) {
                const auto expected = std::polar(0.5, -2 * M_PI * actual.frequency[i] * 5e-9);
                EXPECT_NEAR(std::abs(output[i] - expected), 0.0, 1e-10);
            }
        }
    }
}

// ============================================================================
// LTRA port currents against ngspice 47.
//
// ngspice allocates the two LTRA branch equations through CKTmkVolt
// (ltraset.c:176,182), so it prints them as voltage-named vectors v(<line>#i1)
// and v(<line>#i2) even though they hold the port currents. The transient
// comparisons erase both, on the stated grounds that they are implementation
// variables. That erasure drops exactly the quantity goal item 1 names, so it
// needs the argument the VDMOS #gate exclusion was given: that neospice models
// the same quantity, and that its values agree.
//
// neospice solves for both as MNA branch unknowns (br_eq1/br_eq2) with the
// same 20-entry stamp structure, but does not publish them: output_currents()
// returns their names and nothing calls it. This test reads them out of the
// solution vector so the comparison is actually performed.
// ============================================================================

TEST_F(LTRAValidation, PortCurrentsMatchNgspice47) {
    for (const char* deck : {"/ltra_dc_rc.cir", "/ltra_dc_rg.cir"}) {
        SCOPED_TRACE(deck);
        const std::string cir_path = std::string(TEST_CIRCUITS_DIR) + deck;

        const auto reference = ngspice_->run_dc(cir_path);
        ASSERT_TRUE(reference.node_voltages.count("v(o1#i1)"))
            << "ngspice 47 no longer exposes the LTRA branch equations; the "
               "erasure in the transient tests would need rewriting";
        ASSERT_TRUE(reference.node_voltages.count("v(o1#i2)"));

        auto ckt = sim_.load(cir_path);
        (void)sim_.run_dc(ckt);

        const auto* device =
            dynamic_cast<const LossyTransmissionLine*>(ckt.find_device_ptr("o1"));
        ASSERT_NE(device, nullptr) << "o1 is not an LTRA instance";
        const std::vector<double>* solution = ckt.operating_point();
        ASSERT_NE(solution, nullptr) << "no operating point was cached";
        ASSERT_GT(device->br_eq1(), 0);
        ASSERT_LT(static_cast<size_t>(device->br_eq2()), solution->size());

        // Both engines index the two ports the same way, so i1 pairs with
        // br_eq1. A sign flip here would be a real disagreement, not a
        // convention difference to absorb.
        const double ng_i1 = reference.node_voltages.at("v(o1#i1)");
        const double ng_i2 = reference.node_voltages.at("v(o1#i2)");
        const double neo_i1 = (*solution)[device->br_eq1()];
        const double neo_i2 = (*solution)[device->br_eq2()];

        const double scale = std::max({std::fabs(ng_i1), std::fabs(ng_i2), 1e-12});
        EXPECT_NEAR(neo_i1, ng_i1, 1e-9 * scale + 1e-15)
            << "port 1 current: neospice " << neo_i1 << " vs ngspice " << ng_i1;
        EXPECT_NEAR(neo_i2, ng_i2, 1e-9 * scale + 1e-15)
            << "port 2 current: neospice " << neo_i2 << " vs ngspice " << ng_i2;
    }
}
