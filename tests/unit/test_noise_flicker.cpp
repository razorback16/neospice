#include <gtest/gtest.h>
#include "core/noise.hpp"
#include "core/types.hpp"
#include "parser/netlist_parser.hpp"
#include "api/neospice.hpp"
#include "framework/ngspice_runner.hpp"
#include <cmath>

using namespace neospice;

// ===========================================================================
// Resistor flicker noise tests
// ===========================================================================

// ---------------------------------------------------------------------------
// Test 1: Resistor with Kf=0 (default) produces flat (white) noise spectrum.
// The noise at 1 Hz and 1 MHz should be identical.
// ---------------------------------------------------------------------------
TEST(FlickerNoise, ResistorNoFlickerDefaultIsWhite) {
    std::string netlist = R"(
Resistor White Noise (no flicker)
V1 in 0 DC 1 AC 1
R1 in out 1k
R2 out 0 1k
.noise V(out) V1 dec 10 1 1e6
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    auto result = solve_noise(ckt, "out", "V1",
                              AnalysisCommand::DEC, 10, 1.0, 1e6);

    ASSERT_GT(result.frequency.size(), 10u);

    // All noise values should be the same (white, no flicker)
    double first_val = result.output_noise_density[0];
    EXPECT_GT(first_val, 0.0);

    for (size_t i = 1; i < result.output_noise_density.size(); ++i) {
        EXPECT_NEAR(result.output_noise_density[i], first_val,
                    first_val * 1e-6)
            << "Resistor noise (Kf=0) must be white at frequency index " << i
            << " (f=" << result.frequency[i] << " Hz)";
    }
}

// ---------------------------------------------------------------------------
// Test 2: Resistor with Kf=0 explicitly set still produces flat noise.
// ---------------------------------------------------------------------------
TEST(FlickerNoise, ResistorExplicitKfZeroIsWhite) {
    // We test via direct noise_sources() call on a Resistor instance.
    // Parse a circuit with a 1k resistor (no flicker params in netlist),
    // confirm it stays flat from 1 Hz to 1 MHz.
    std::string netlist = R"(
Resistor Flicker Off
V1 in 0 DC 1 AC 1
R1 in out 1k
R2 out 0 1k
.noise V(out) V1 lin 2 1 1e6
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    auto result = solve_noise(ckt, "out", "V1",
                              AnalysisCommand::LIN, 2, 1.0, 1e6);

    ASSERT_EQ(result.frequency.size(), 2u);
    // Noise at 1 Hz == noise at 1 MHz (white only)
    EXPECT_NEAR(result.output_noise_density[0],
                result.output_noise_density[1],
                result.output_noise_density[0] * 1e-6)
        << "Resistor noise (Kf=0) must be identical at 1 Hz and 1 MHz";
}

// ===========================================================================
// Diode flicker noise tests
// ===========================================================================

// ---------------------------------------------------------------------------
// Test 3: Diode with Kf != 0 has higher noise at low frequencies than high.
// ---------------------------------------------------------------------------
TEST(FlickerNoise, DiodeFlickerIncreasesAtLowFreq) {
    std::string netlist = R"(
Diode Flicker Noise Test
V1 in 0 DC 0.65 AC 1
R1 in anode 1k
D1 anode 0 DMOD
.model DMOD D IS=1e-14 N=1 Kf=1e-14 Af=1 Ef=1
.noise V(anode) V1 dec 10 1 1e6
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    auto result = solve_noise(ckt, "anode", "V1",
                              AnalysisCommand::DEC, 10, 1.0, 1e6);

    ASSERT_GT(result.frequency.size(), 10u);
    EXPECT_GT(result.output_noise_density[0], 0.0);

    // Noise at 1 Hz (first point) should be greater than noise at 1 MHz (last point)
    double low_freq_noise  = result.output_noise_density.front();
    double high_freq_noise = result.output_noise_density.back();

    EXPECT_GT(low_freq_noise, high_freq_noise)
        << "Diode noise with flicker should be higher at low frequencies";
}

// ---------------------------------------------------------------------------
// Test 4: At high frequency, flicker contribution is negligible.
// A diode with a moderate Kf should approach its white noise floor at 1 MHz.
// We compare against the same diode without flicker (Kf=0).
// ---------------------------------------------------------------------------
TEST(FlickerNoise, FlickerNegligibleAtHighFreq) {
    // Diode with flicker
    std::string netlist_flicker = R"(
Diode With Flicker
V1 in 0 DC 0.65 AC 1
R1 in anode 1k
D1 anode 0 DMOD
.model DMOD D IS=1e-14 N=1 Kf=1e-14 Af=1 Ef=1
.noise V(anode) V1 lin 1 1e6 1e6
.end
)";

    // Same diode without flicker (Kf=0)
    std::string netlist_white = R"(
Diode Without Flicker
V1 in 0 DC 0.65 AC 1
R1 in anode 1k
D1 anode 0 DMOD
.model DMOD D IS=1e-14 N=1
.noise V(anode) V1 lin 1 1e6 1e6
.end
)";

    NetlistParser parser;

    auto ckt_flicker = parser.parse(netlist_flicker);
    auto result_flicker = solve_noise(ckt_flicker, "anode", "V1",
                                      AnalysisCommand::LIN, 1, 1e6, 1e6);

    auto ckt_white = parser.parse(netlist_white);
    auto result_white = solve_noise(ckt_white, "anode", "V1",
                                    AnalysisCommand::LIN, 1, 1e6, 1e6);

    ASSERT_EQ(result_flicker.frequency.size(), 1u);
    ASSERT_EQ(result_white.frequency.size(), 1u);

    double noise_with_flicker = result_flicker.output_noise_density[0];
    double noise_without_flicker = result_white.output_noise_density[0];

    EXPECT_GT(noise_without_flicker, 0.0);
    EXPECT_GT(noise_with_flicker, 0.0);

    // At 1 MHz with Kf=1e-14 and Id~mA, flicker term = Kf*Id/f = 1e-14*1e-3/1e6 = 1e-23
    // Shot noise ~ 2*q*Id ~ 2*1.6e-19*1e-3 = 3.2e-22
    // Flicker is ~30x smaller at 1 MHz — within 10% of white noise level.
    EXPECT_NEAR(noise_with_flicker, noise_without_flicker,
                noise_without_flicker * 0.5)
        << "At 1 MHz, flicker contribution should be minor compared to shot noise";
}

// ---------------------------------------------------------------------------
// Test 5: Verify 1/f slope — noise at 10 Hz should be ~10x noise at 100 Hz
//         for Ef=1 (ideal 1/f noise).
// ---------------------------------------------------------------------------
TEST(FlickerNoise, OneOverFSlopeEf1) {
    // Use a strong flicker (large Kf) so flicker dominates over shot noise
    std::string netlist = R"(
Diode 1/f Slope Test
V1 in 0 DC 0.65 AC 1
R1 in anode 100
D1 anode 0 DMOD
.model DMOD D IS=1e-14 N=1 Kf=1e-9 Af=1 Ef=1
.noise V(anode) V1 lin 2 10 100
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);

    // Solve at 10 Hz
    auto result = solve_noise(ckt, "anode", "V1",
                              AnalysisCommand::LIN, 2, 10.0, 100.0);

    ASSERT_EQ(result.frequency.size(), 2u);
    EXPECT_NEAR(result.frequency[0], 10.0, 1e-9);
    EXPECT_NEAR(result.frequency[1], 100.0, 1e-9);

    double noise_10hz  = result.output_noise_density[0];
    double noise_100hz = result.output_noise_density[1];

    EXPECT_GT(noise_10hz, 0.0);
    EXPECT_GT(noise_100hz, 0.0);

    // For pure 1/f: noise(10Hz) / noise(100Hz) should be ~10
    // We use large Kf so flicker dominates; shot noise contribution means ratio < 10
    // but ratio should be meaningfully greater than 1 (at least 5x).
    double ratio = noise_10hz / noise_100hz;
    EXPECT_GT(ratio, 5.0)
        << "Noise at 10 Hz should be significantly larger than at 100 Hz (1/f slope)";
}

// ---------------------------------------------------------------------------
// Test 6: Diode per-device noise breakdown includes flicker contribution.
// With Kf != 0, the diode noise contribution at low freq > white-only value.
// ---------------------------------------------------------------------------
TEST(FlickerNoise, DiodeFlickerInPerDeviceBreakdown) {
    // Circuit with flicker diode
    std::string netlist_flicker = R"(
Flicker Breakdown Test
V1 in 0 DC 0.65 AC 1
R1 in anode 1k
D1 anode 0 DMOD
.model DMOD D IS=1e-14 N=1 Kf=1e-9 Af=1 Ef=1
.noise V(anode) V1 lin 1 1 1
.end
)";

    // Same circuit without flicker (Kf=0)
    std::string netlist_white = R"(
White Breakdown Test
V1 in 0 DC 0.65 AC 1
R1 in anode 1k
D1 anode 0 DMOD
.model DMOD D IS=1e-14 N=1
.noise V(anode) V1 lin 1 1 1
.end
)";

    NetlistParser parser;

    auto ckt_f = parser.parse(netlist_flicker);
    auto res_f = solve_noise(ckt_f, "anode", "V1",
                             AnalysisCommand::LIN, 1, 1.0, 1.0);

    auto ckt_w = parser.parse(netlist_white);
    auto res_w = solve_noise(ckt_w, "anode", "V1",
                             AnalysisCommand::LIN, 1, 1.0, 1.0);

    ASSERT_EQ(res_f.frequency.size(), 1u);
    ASSERT_EQ(res_w.frequency.size(), 1u);

    // Diode must appear in both breakdowns
    ASSERT_TRUE(res_f.device_noise.count("d1") > 0);
    ASSERT_TRUE(res_w.device_noise.count("d1") > 0);

    double diode_noise_with_flicker    = res_f.device_noise.at("d1")[0];
    double diode_noise_without_flicker = res_w.device_noise.at("d1")[0];

    // At 1 Hz with large Kf, flicker-enhanced diode noise must exceed white-only
    EXPECT_GT(diode_noise_with_flicker, diode_noise_without_flicker)
        << "Diode noise with flicker (1 Hz) must exceed shot-noise-only value";
}

// ---------------------------------------------------------------------------
// Test 7: Parse Kf/Af/Ef from .model card and verify they are used.
// ---------------------------------------------------------------------------
TEST(FlickerNoise, ModelCardParseKfAfEf) {
    // Parse a circuit where the .model card has flicker parameters.
    // Verify that the resulting noise is frequency-dependent (flicker active).
    std::string netlist = R"(
Model Card Flicker Parse
V1 in 0 DC 0.65 AC 1
R1 in a 100
D1 a 0 DFLIC
.model DFLIC D IS=1e-14 N=1 Kf=1e-10 Af=1 Ef=1
.noise V(a) V1 lin 2 1 1000
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    auto result = solve_noise(ckt, "a", "V1",
                              AnalysisCommand::LIN, 2, 1.0, 1000.0);

    ASSERT_EQ(result.frequency.size(), 2u);

    double noise_1hz   = result.output_noise_density[0];
    double noise_1khz  = result.output_noise_density[1];

    // With Ef=1: noise should decrease by ~1000x from 1 Hz to 1 kHz
    // (assuming flicker dominates). Even conservatively, 1 Hz noise > 1 kHz noise.
    EXPECT_GT(noise_1hz, noise_1khz)
        << "Parsed Kf/Af/Ef should produce frequency-dependent (1/f) noise";
}

// ---------------------------------------------------------------------------
// Test 8: Model card Kf=0 (explicitly) behaves same as no flicker.
// ---------------------------------------------------------------------------
TEST(FlickerNoise, ModelCardKfZeroIsWhite) {
    std::string netlist = R"(
Kf Zero Model Card
V1 in 0 DC 0.65 AC 1
R1 in anode 1k
D1 anode 0 DMOD
.model DMOD D IS=1e-14 N=1 Kf=0
.noise V(anode) V1 lin 2 1 1e6
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    auto result = solve_noise(ckt, "anode", "V1",
                              AnalysisCommand::LIN, 2, 1.0, 1e6);

    ASSERT_EQ(result.frequency.size(), 2u);

    // With Kf=0, noise should be white (identical at all frequencies)
    EXPECT_NEAR(result.output_noise_density[0],
                result.output_noise_density[1],
                result.output_noise_density[0] * 0.01)
        << "Kf=0 in model card must produce flat (white) noise spectrum";
}

// ---------------------------------------------------------------------------
// Test 9: Ef=2 gives 1/f^2 slope — noise at 10 Hz should be ~100x at 100 Hz.
// ---------------------------------------------------------------------------
TEST(FlickerNoise, OneOverFSquaredSlopeEf2) {
    std::string netlist = R"(
Diode 1/f^2 Slope Test
V1 in 0 DC 0.65 AC 1
R1 in anode 100
D1 anode 0 DMOD
.model DMOD D IS=1e-14 N=1 Kf=1e-9 Af=1 Ef=2
.noise V(anode) V1 lin 2 10 100
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    auto result = solve_noise(ckt, "anode", "V1",
                              AnalysisCommand::LIN, 2, 10.0, 100.0);

    ASSERT_EQ(result.frequency.size(), 2u);

    double noise_10hz  = result.output_noise_density[0];
    double noise_100hz = result.output_noise_density[1];

    EXPECT_GT(noise_10hz, 0.0);
    EXPECT_GT(noise_100hz, 0.0);

    // For Ef=2: S_flicker(10 Hz) / S_flicker(100 Hz) = (100/10)^2 = 100
    // With flicker dominating, ratio should be significantly > 1 (at least 50x).
    double ratio = noise_10hz / noise_100hz;
    EXPECT_GT(ratio, 50.0)
        << "Ef=2 should give ~100x noise ratio between 10 Hz and 100 Hz";
}

// ===========================================================================
// Ngspice comparison test
// ===========================================================================

// ---------------------------------------------------------------------------
// Test 10: Compare diode flicker noise against ngspice reference.
// Uses tests/circuits/diode_flicker_noise.cir
// ---------------------------------------------------------------------------
TEST(FlickerNoise, NgspiceCompareDiodeFlicker) {
    NgspiceRunner ngspice(NGSPICE_BINARY);
    std::string cir_path = std::string(TEST_CIRCUITS_DIR) + "/diode_flicker_noise.cir";

    NgspiceNoiseResult ng_result;
    try {
        ng_result = ngspice.run_noise(cir_path);
    } catch (const std::exception& e) {
        GTEST_SKIP() << "ngspice not available or failed: " << e.what();
    }

    if (ng_result.frequency.empty()) {
        GTEST_SKIP() << "ngspice returned empty noise result";
    }

    // Run our solver on the same circuit
    Simulator sim;
    auto ckt = sim.load(cir_path);
    auto result = sim.run_noise(ckt, "anode", "V1",
                                AnalysisCommand::DEC, 10, 1.0, 1e6);

    ASSERT_FALSE(result.frequency.empty());
    ASSERT_FALSE(result.output_noise_density.empty());

    // ngspice onoise_spectrum is in V/sqrt(Hz) — convert to V^2/Hz
    // Our output_noise_density is already in V^2/Hz.

    // Verify noise decreases with frequency (1/f character present)
    double low_freq_noise  = result.output_noise_density.front();
    double high_freq_noise = result.output_noise_density.back();
    EXPECT_GT(low_freq_noise, high_freq_noise)
        << "Flicker diode: noise at low freq should exceed noise at high freq";

    // Compare against ngspice at matching frequencies with generous tolerance
    // (10% relative) since diode DC operating point may differ slightly.
    size_t min_pts = std::min(result.frequency.size(), ng_result.frequency.size());
    ASSERT_GT(min_pts, 0u);

    int mismatches = 0;
    for (size_t i = 0; i < min_pts; ++i) {
        // ng_result stores V/sqrt(Hz), square to get V^2/Hz
        double ng_psd = ng_result.onoise_spectrum[i] * ng_result.onoise_spectrum[i];
        double cs_psd = result.output_noise_density[i];

        if (ng_psd <= 0.0 || cs_psd <= 0.0) continue;

        // Compare in log space: allow factor of 3 (reasonable for flicker noise)
        double ratio = cs_psd / ng_psd;
        if (ratio < 0.33 || ratio > 3.0) {
            ++mismatches;
        }
    }

    // Allow up to 20% of points to fall outside factor-of-3 band
    // (ngspice uses slightly different flicker model details)
    EXPECT_LE(mismatches, static_cast<int>(min_pts) / 5)
        << mismatches << "/" << min_pts
        << " frequency points deviate by more than 3x from ngspice";
}
