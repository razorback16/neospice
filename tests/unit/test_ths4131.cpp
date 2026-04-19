// THS4131 fully-differential op-amp macro model validation.
// Tests: DC operating point, AC frequency response, ngspice comparison.
// The THS4131 subcircuit uses BJTs, resistors, capacitors, inductors,
// voltage/current sources, VCVS (E), and VCCS (G) — exercises the full
// element set.

#include <gtest/gtest.h>
#include "api/neospice.hpp"
#include "framework/ngspice_runner.hpp"
#include "framework/comparator.hpp"

#include <cmath>
#include <complex>
#include <string>

using namespace neospice;

class THS4131Test : public ::testing::Test {
protected:
    void SetUp() override {
        cir_path_ = std::string(TEST_CIRCUITS_DIR) + "/ths4131_diff_amp.cir";
        ngspice_ = std::make_unique<NgspiceRunner>(NGSPICE_BINARY);
    }
    std::string cir_path_;
    std::unique_ptr<NgspiceRunner> ngspice_;
    Simulator sim_;
};

// DC operating point: unity-gain diff amp with 100mV input.
// Differential output ~100mV, CM near VOCM=0V.
TEST_F(THS4131Test, DCOperatingPoint) {
    auto ckt = sim_.load(cir_path_);
    auto dc = sim_.run_dc(ckt);

    ASSERT_TRUE(dc.node_voltages.count("v(out_p)") > 0);
    ASSERT_TRUE(dc.node_voltages.count("v(out_n)") > 0);

    double v_out_p = dc.node_voltages.at("v(out_p)");
    double v_out_n = dc.node_voltages.at("v(out_n)");
    double v_diff = v_out_p - v_out_n;
    double v_cm   = (v_out_p + v_out_n) / 2.0;

    EXPECT_NEAR(std::abs(v_diff), 0.1, 0.002)
        << "Differential output should be ~100mV, got " << v_diff << "V";

    EXPECT_NEAR(v_cm, 0.0, 0.01)
        << "CM output should be near VOCM=0V, got " << v_cm << "V";
}

// AC response: verify bandwidth is reasonable for THS4131 (GBW ~150MHz).
// At unity gain, the -3dB point should be in the tens-of-MHz range.
TEST_F(THS4131Test, ACBandwidth) {
    auto ckt = sim_.load(cir_path_);
    auto ac = sim_.run_ac(ckt, AnalysisCommand::DEC, 10, 1.0, 100e6);

    ASSERT_FALSE(ac.frequency.empty());
    ASSERT_TRUE(ac.voltages.count("v(out_p)") > 0);
    ASSERT_TRUE(ac.voltages.count("v(out_n)") > 0);

    const auto& v_out_p = ac.voltages.at("v(out_p)");
    const auto& v_out_n = ac.voltages.at("v(out_n)");

    // Compute differential gain at each frequency
    std::vector<double> gain_db(ac.frequency.size());
    for (size_t i = 0; i < ac.frequency.size(); ++i) {
        double diff_mag = std::abs(v_out_p[i] - v_out_n[i]);
        gain_db[i] = 20.0 * std::log10(std::max(diff_mag, 1e-20));
    }

    // Low-frequency gain should be ~0 dB (unity gain)
    EXPECT_NEAR(gain_db[0], 0.0, 0.1)
        << "Low-frequency differential gain should be ~0 dB (unity)";

    // At 100MHz, gain should have rolled off significantly
    EXPECT_LT(gain_db.back(), -3.0)
        << "Gain at 100MHz should be below -3dB for THS4131 at unity gain";
}

// Compare DC operating point with ngspice.
// After the VCCS polarity fix, neospice and ngspice agree to within
// ~2µV on all node voltages.
TEST_F(THS4131Test, NgspiceDCComparison) {
    DCResult ng_result;
    try {
        ng_result = ngspice_->run_dc(cir_path_);
    } catch (const std::exception& e) {
        GTEST_SKIP() << "ngspice not available or failed: " << e.what();
    }

    auto ckt = sim_.load(cir_path_);
    auto cs_result = sim_.run_dc(ckt);

    auto get = [](const DCResult& r, const char* k) -> double {
        auto it = r.node_voltages.find(k);
        return (it != r.node_voltages.end()) ? it->second : 0.0;
    };

    double ng_out_p = get(ng_result, "v(out_p)");
    double ng_out_n = get(ng_result, "v(out_n)");
    double cs_out_p = get(cs_result, "v(out_p)");
    double cs_out_n = get(cs_result, "v(out_n)");

    // Individual node voltages
    EXPECT_NEAR(cs_out_p, ng_out_p, 0.5e-3)
        << "V(out_p) mismatch: neospice=" << cs_out_p
        << " ngspice=" << ng_out_p;
    EXPECT_NEAR(cs_out_n, ng_out_n, 0.5e-3)
        << "V(out_n) mismatch: neospice=" << cs_out_n
        << " ngspice=" << ng_out_n;

    // Differential output
    double ng_diff = ng_out_p - ng_out_n;
    double cs_diff = cs_out_p - cs_out_n;
    EXPECT_NEAR(cs_diff, ng_diff, 0.5e-3)
        << "Differential output mismatch: neospice=" << cs_diff
        << " ngspice=" << ng_diff;

    // Common-mode level
    double ng_cm = (ng_out_p + ng_out_n) / 2.0;
    double cs_cm = (cs_out_p + cs_out_n) / 2.0;
    EXPECT_NEAR(cs_cm, ng_cm, 0.5e-3)
        << "CM output mismatch: neospice=" << cs_cm
        << " ngspice=" << ng_cm;
}

// Compare AC response with ngspice
TEST_F(THS4131Test, NgspiceACComparison) {
    ACResult ng_result;
    try {
        ng_result = ngspice_->run_ac(cir_path_);
    } catch (const std::exception& e) {
        GTEST_SKIP() << "ngspice not available or failed: " << e.what();
    }

    if (ng_result.frequency.empty()) {
        GTEST_SKIP() << "ngspice returned empty AC result";
    }

    auto ckt = sim_.load(cir_path_);
    auto cs_result = sim_.run_ac(ckt, AnalysisCommand::DEC, 10, 1.0, 100e6);

    ASSERT_FALSE(cs_result.frequency.empty());
    ASSERT_EQ(ng_result.frequency.size(), cs_result.frequency.size());

    // Compare voltage signals only — branch currents (i(v.*)) are internal
    // to the subcircuit and may differ between simulators.
    ACResult ng_filtered;
    ng_filtered.frequency = ng_result.frequency;
    for (const auto& [name, data] : ng_result.voltages) {
        ng_filtered.voltages[name] = data;
    }
    auto cmp = compare_ac(ng_filtered, cs_result, {1e-2, 1e-6});
    EXPECT_TRUE(cmp.passed)
        << "AC comparison failed. Worst: " << cmp.worst_signal
        << " error: " << cmp.worst_error;
}
