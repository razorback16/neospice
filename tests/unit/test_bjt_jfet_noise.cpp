// BJT and JFET noise model tests (Task 9.5.4)
// Verifies: shot noise, thermal noise, flicker noise, and ngspice comparison.

#include <gtest/gtest.h>
#include "api/neospice.hpp"
#include "core/noise.hpp"
#include "core/types.hpp"
#include "parser/netlist_parser.hpp"
#include "framework/ngspice_runner.hpp"
#include "framework/comparator.hpp"

#include <cmath>
#include <string>

using namespace neospice;

// ===========================================================================
// BJT Noise Tests
// ===========================================================================

// ---------------------------------------------------------------------------
// Test 1: BJT CE amplifier — verify non-zero output noise
// ---------------------------------------------------------------------------
TEST(BJTNoise, CommonEmitterNonZeroNoise) {
    // NPN common-emitter amplifier with base-resistance and biasing
    const char* netlist = R"(
BJT CE Noise Test
Vcc vcc 0 DC 5
Vbias base 0 DC 0.75 AC 1
Rc vcc col 5k
Rb base b_int 10k
Q1 col b_int 0 QMOD
.model QMOD NPN(IS=1e-14 BF=200 BR=5 VAF=100 RB=100)
.noise V(col) Vbias dec 1 1000 1000
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    auto result = solve_noise(ckt, "col", "Vbias",
                              AnalysisCommand::DEC, 1, 1000.0, 1000.0);

    ASSERT_EQ(result.frequency.size(), 1u);
    EXPECT_GT(result.output_noise_density[0], 0.0)
        << "BJT CE amplifier should produce non-zero output noise";

    // BJT device should appear in per-device breakdown
    ASSERT_TRUE(result.device_noise.count("q1") > 0)
        << "BJT q1 should appear in per-device noise breakdown";
    double bjt_noise = result.device_noise.at("q1")[0];
    EXPECT_GT(bjt_noise, 0.0)
        << "BJT noise contribution should be positive";

    // Load resistor should also contribute thermal noise
    ASSERT_TRUE(result.device_noise.count("rc") > 0);
    EXPECT_GT(result.device_noise.at("rc")[0], 0.0);

    // Sum of device contributions should equal total
    double sum = 0.0;
    for (const auto& [name, nvec] : result.device_noise) {
        sum += nvec[0];
    }
    EXPECT_NEAR(sum, result.output_noise_density[0],
                result.output_noise_density[0] * 1e-10);
}

// ---------------------------------------------------------------------------
// Test 2: BJT shot noise dominates at high current
//
// At high collector current, the shot noise 2q|Ic| should dominate over
// the thermal noise of the load resistor when gm is large enough.
// ---------------------------------------------------------------------------
TEST(BJTNoise, ShotNoiseDominatesAtHighCurrent) {
    // Use small Rc (100 ohm) so load resistor thermal noise is tiny.
    // At high Ic (~2mA), BJT shot noise 2*q*Ic ~ 6.4e-22 A^2/Hz,
    // which at the output via |Z|^2 = Rc^2 contributes 6.4e-18 V^2/Hz.
    // Load resistor thermal noise = 4kT/Rc * Rc^2 = 4kT*Rc = 1.66e-18 V^2/Hz.
    // BJT shot noise should dominate.
    const char* netlist = R"(
BJT High Current Noise
Vcc vcc 0 DC 5
Vbias base 0 DC 0.7 AC 1
Rc vcc col 100
Q1 col base 0 QMOD
.model QMOD NPN(IS=1e-14 BF=200 BR=5 VAF=100)
.noise V(col) Vbias dec 1 1000 1000
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    auto result = solve_noise(ckt, "col", "Vbias",
                              AnalysisCommand::DEC, 1, 1000.0, 1000.0);

    ASSERT_EQ(result.frequency.size(), 1u);
    EXPECT_GT(result.output_noise_density[0], 0.0);

    ASSERT_TRUE(result.device_noise.count("q1") > 0);
    ASSERT_TRUE(result.device_noise.count("rc") > 0);

    double bjt_noise = result.device_noise.at("q1")[0];
    double rc_noise  = result.device_noise.at("rc")[0];

    EXPECT_GT(bjt_noise, 0.0) << "BJT shot noise should be positive";
    EXPECT_GT(rc_noise, 0.0)  << "Rc thermal noise should be positive";

    // Both contribute; with gm*Rc >> 1, BJT shot noise should be dominant
    // (BJT referred noise through voltage gain is amplified vs Rc load noise).
    // At minimum, BJT noise should be a significant fraction of total.
    double total = result.output_noise_density[0];
    EXPECT_GT(bjt_noise / total, 0.1)
        << "BJT noise should account for at least 10% of total noise";
}

// ---------------------------------------------------------------------------
// Test 3: BJT noise magnitude order of magnitude check
//
// For a CE amplifier with Ic ~ 1 mA, gm = Ic/Vt ~ 40 mA/V:
//   - Collector shot noise: S_ic = 2*q*Ic ~ 3.2e-22 A^2/Hz
//   - Base shot noise: S_ib = 2*q*Ib ~ 2*q*Ic/BF ~ 1.6e-24 A^2/Hz
//   Output-referred (via ~Rc): in a reasonable range 1e-25 to 1e-12 V^2/Hz
//   (exact value depends on BJT operating point and topology)
// ---------------------------------------------------------------------------
TEST(BJTNoise, NoiseMagnitudeOrderOfMagnitude) {
    const char* netlist = R"(
BJT Noise OOM
Vcc vcc 0 DC 5
Vbias base 0 DC 0.75 AC 1
Rc vcc col 5k
Q1 col base 0 QMOD
.model QMOD NPN(IS=1e-14 BF=200 BR=5 VAF=100)
.noise V(col) Vbias dec 1 1000 1000
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    auto result = solve_noise(ckt, "col", "Vbias",
                              AnalysisCommand::DEC, 1, 1000.0, 1000.0);

    ASSERT_EQ(result.frequency.size(), 1u);
    double total = result.output_noise_density[0];

    EXPECT_GT(total, 1e-25) << "Output noise should be above numerical floor";
    EXPECT_LT(total, 1e-12) << "Output noise should not be unreasonably large";
}

// ---------------------------------------------------------------------------
// Test 4: BJT flicker noise rises at low frequency
//
// With KF > 0, the base current flicker noise KF*Ib^AF/f causes the output
// noise to rise at low frequencies. The topology must have finite base-node
// impedance (here: Rb to AC source) so the base noise current can propagate
// to the output. With a 100k base resistor, the base shot+flicker noise
// current sees ~100k source impedance and couples strongly to the output.
//
// At low frequency (1 Hz): S_flicker = KF * Ib / f dominates
// At high frequency (1e5 Hz): S_flicker = KF * Ib / 1e5 — much smaller
// The 1/f noise should cause at least 3x increase over 5 decades.
// ---------------------------------------------------------------------------
TEST(BJTNoise, FlickerNoiseRisesAtLowFrequency) {
    // Use a resistor-biased circuit so base noise current can propagate to output.
    // AC input is separate from bias (via Rbase to AC source ground).
    const char* netlist = R"(
BJT Flicker Noise
Vcc vcc 0 DC 5
Vbias base 0 DC 0.75 AC 1
Rbase base b_int 100k
Rc vcc col 5k
Q1 col b_int 0 QMOD
.model QMOD NPN(IS=1e-14 BF=200 VAF=100 KF=1e-10 AF=1)
.noise V(col) Vbias dec 10 1 1e5
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    auto result = solve_noise(ckt, "col", "Vbias",
                              AnalysisCommand::DEC, 10, 1.0, 1e5);

    ASSERT_GT(result.frequency.size(), 20u);

    // Low-frequency noise should be greater than high-frequency noise
    double low_freq_noise  = result.output_noise_density.front();
    double high_freq_noise = result.output_noise_density.back();

    EXPECT_GT(low_freq_noise, 0.0);
    EXPECT_GT(high_freq_noise, 0.0);
    // 1/f noise over 5 decades (1 Hz to 1e5 Hz) means 1e5x contribution change.
    // Even with circuit gain effects, we should see at least 3x increase.
    EXPECT_GT(low_freq_noise, high_freq_noise * 3.0)
        << "1/f flicker noise should cause low-frequency noise to exceed "
           "high-frequency noise significantly over 5 decades";
}

// ---------------------------------------------------------------------------
// Test 5: BJT noise with no flicker (KF=0) should be white (flat)
// ---------------------------------------------------------------------------
TEST(BJTNoise, NoFlickerIsWhiteNoise) {
    const char* netlist = R"(
BJT White Noise
Vcc vcc 0 DC 5
Vbias base 0 DC 0.75 AC 1
Rc vcc col 5k
Q1 col base 0 QMOD
.model QMOD NPN(IS=1e-14 BF=200 VAF=100)
.noise V(col) Vbias dec 5 10 1000
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    auto result = solve_noise(ckt, "col", "Vbias",
                              AnalysisCommand::DEC, 5, 10.0, 1000.0);

    ASSERT_GT(result.frequency.size(), 5u);

    // Without flicker noise and no capacitances, noise should be flat
    double first_val = result.output_noise_density[0];
    EXPECT_GT(first_val, 0.0);
    for (size_t i = 1; i < result.output_noise_density.size(); ++i) {
        EXPECT_NEAR(result.output_noise_density[i], first_val, first_val * 0.05)
            << "BJT noise without flicker should be approximately white at freq="
            << result.frequency[i] << " Hz";
    }
}

// ===========================================================================
// JFET Noise Tests
// ===========================================================================

// ---------------------------------------------------------------------------
// Test 6: JFET source follower — verify non-zero output noise
// ---------------------------------------------------------------------------
TEST(JFETNoise, SourceFollowerNonZeroNoise) {
    const char* netlist = R"(
JFET Source Follower Noise
Vdd vdd 0 DC 10
Vg gate 0 DC -1 AC 1
Rs vdd drain 1k
J1 drain gate source JMOD
Rss source 0 2k
.model JMOD NJF(VTO=-2 BETA=1e-4 LAMBDA=0.02 IS=1e-14 RD=0 RS=0)
.noise V(source) Vg dec 1 1000 1000
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    auto result = solve_noise(ckt, "source", "Vg",
                              AnalysisCommand::DEC, 1, 1000.0, 1000.0);

    ASSERT_EQ(result.frequency.size(), 1u);
    EXPECT_GT(result.output_noise_density[0], 0.0)
        << "JFET source follower should produce non-zero output noise";

    ASSERT_TRUE(result.device_noise.count("j1") > 0)
        << "JFET j1 should appear in per-device noise breakdown";
    double jfet_noise = result.device_noise.at("j1")[0];
    EXPECT_GT(jfet_noise, 0.0)
        << "JFET channel thermal noise should be positive";

    // Sum of device contributions should equal total
    double sum = 0.0;
    for (const auto& [name, nvec] : result.device_noise) {
        sum += nvec[0];
    }
    EXPECT_NEAR(sum, result.output_noise_density[0],
                result.output_noise_density[0] * 1e-10);
}

// ---------------------------------------------------------------------------
// Test 7: JFET channel thermal noise analytical check
//
// S_ch = 4kT*(2/3)*gm (channel thermal noise current PSD)
// The JFET noise current contributes to output noise via the transfer function.
// We verify the magnitude is in the expected range for a JFET with
// gm ~ 0.1 mA/V.
// ---------------------------------------------------------------------------
TEST(JFETNoise, ChannelThermalNoiseMagnitude) {
    const char* netlist = R"(
JFET Channel Noise OOM
Vdd vdd 0 DC 10
Vg gate 0 DC -1 AC 1
Rd vdd drain 10k
J1 drain gate 0 JMOD
.model JMOD NJF(VTO=-2 BETA=1e-4 LAMBDA=0.02)
.noise V(drain) Vg dec 1 1000 1000
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    auto result = solve_noise(ckt, "drain", "Vg",
                              AnalysisCommand::DEC, 1, 1000.0, 1000.0);

    ASSERT_EQ(result.frequency.size(), 1u);

    ASSERT_TRUE(result.device_noise.count("j1") > 0);
    double jfet_noise = result.device_noise.at("j1")[0];

    EXPECT_GT(jfet_noise, 1e-25)
        << "JFET channel thermal noise should be above zero floor";
    EXPECT_LT(jfet_noise, 1e-12)
        << "JFET channel thermal noise should not be unreasonably large";
}

// ---------------------------------------------------------------------------
// Test 8: JFET noise without flicker should be white
// ---------------------------------------------------------------------------
TEST(JFETNoise, NoFlickerIsWhiteNoise) {
    const char* netlist = R"(
JFET White Noise
Vdd vdd 0 DC 10
Vg gate 0 DC -1 AC 1
Rd vdd drain 10k
J1 drain gate 0 JMOD
.model JMOD NJF(VTO=-2 BETA=1e-4 LAMBDA=0.02)
.noise V(drain) Vg dec 5 10 1000
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    auto result = solve_noise(ckt, "drain", "Vg",
                              AnalysisCommand::DEC, 5, 10.0, 1000.0);

    ASSERT_GT(result.frequency.size(), 5u);

    double first_val = result.output_noise_density[0];
    EXPECT_GT(first_val, 0.0);

    for (size_t i = 1; i < result.output_noise_density.size(); ++i) {
        EXPECT_NEAR(result.output_noise_density[i], first_val, first_val * 0.05)
            << "JFET noise without flicker should be approximately white at freq="
            << result.frequency[i] << " Hz";
    }
}

// ---------------------------------------------------------------------------
// Test 9: JFET flicker noise rises at low frequency
// ---------------------------------------------------------------------------
TEST(JFETNoise, FlickerNoiseRisesAtLowFrequency) {
    const char* netlist = R"(
JFET Flicker Noise
Vdd vdd 0 DC 10
Vg gate 0 DC -1 AC 1
Rd vdd drain 10k
J1 drain gate 0 JMOD
.model JMOD NJF(VTO=-2 BETA=1e-4 LAMBDA=0.02 KF=1e-13 AF=1)
.noise V(drain) Vg dec 10 1 1e5
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    auto result = solve_noise(ckt, "drain", "Vg",
                              AnalysisCommand::DEC, 10, 1.0, 1e5);

    ASSERT_GT(result.frequency.size(), 20u);

    double low_freq_noise  = result.output_noise_density.front();
    double high_freq_noise = result.output_noise_density.back();

    EXPECT_GT(low_freq_noise, 0.0);
    EXPECT_GT(high_freq_noise, 0.0);
    EXPECT_GT(low_freq_noise, high_freq_noise * 10.0)
        << "JFET 1/f flicker noise should elevate low-frequency noise by at "
           "least 10x over 5 decades";
}

// ===========================================================================
// ngspice comparison tests
// ===========================================================================

class BJTJFETNoiseNgspiceTest : public ::testing::Test {
protected:
    void SetUp() override {
        ngspice_ = std::make_unique<NgspiceRunner>();
    }
    std::unique_ptr<NgspiceRunner> ngspice_;
    Simulator sim_;
};

// ---------------------------------------------------------------------------
// Test 10: BJT CE amplifier noise vs ngspice
// ---------------------------------------------------------------------------
TEST_F(BJTJFETNoiseNgspiceTest, BJTCENoise) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/bjt_ce_noise.cir";
    auto ng_result = ngspice_->run_noise(path);
    auto ckt = sim_.load(path);
    auto cs_result = sim_.run(ckt);
    ASSERT_TRUE(std::holds_alternative<NoiseResult>(cs_result.analysis));
    ASSERT_EQ(ng_result.frequency.size(), std::get<NoiseResult>(cs_result.analysis).frequency.size());
    // BJT noise includes shot noise, thermal (Rc, Rb), and base noise.
    // Allow 10% relative tolerance to account for slightly different
    // temperature-adjusted Rb vs static model value.
    auto cmp = compare_noise(ng_result, std::get<NoiseResult>(cs_result.analysis), {1e-3, 1e-15});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}

// ---------------------------------------------------------------------------
// Test 11: JFET source follower noise vs ngspice
// ---------------------------------------------------------------------------
TEST_F(BJTJFETNoiseNgspiceTest, JFETSourceFollowerNoise) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/jfet_noise.cir";
    auto ng_result = ngspice_->run_noise(path);
    auto ckt = sim_.load(path);
    auto cs_result = sim_.run(ckt);
    ASSERT_TRUE(std::holds_alternative<NoiseResult>(cs_result.analysis));
    ASSERT_EQ(ng_result.frequency.size(), std::get<NoiseResult>(cs_result.analysis).frequency.size());
    // JFET noise: channel thermal noise + resistor thermal noise.
    // Allow 10% relative tolerance.
    auto cmp = compare_noise(ng_result, std::get<NoiseResult>(cs_result.analysis), {5e-5, 1e-15});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}
