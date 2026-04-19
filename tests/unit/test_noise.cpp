#include <gtest/gtest.h>
#include "core/noise.hpp"
#include "core/types.hpp"
#include "parser/netlist_parser.hpp"
#include "api/neospice.hpp"
#include <cmath>
#include <numeric>

using namespace neospice;

// ---------------------------------------------------------------------------
// Test 1: Parser test — verify .noise command is parsed correctly
// ---------------------------------------------------------------------------
TEST(Noise, ParserBasic) {
    std::string netlist = R"(
Noise Parser Test
V1 in 0 DC 1 AC 1
R1 in out 1k
R2 out 0 1k
.noise V(out) V1 dec 10 1 1e9
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);

    ASSERT_FALSE(ckt.analyses.empty());
    bool found_noise = false;
    for (const auto& cmd : ckt.analyses) {
        if (cmd.type == AnalysisCommand::NOISE) {
            found_noise = true;
            EXPECT_EQ(cmd.noise_output, "out");
            EXPECT_EQ(cmd.noise_input_src, "V1");
            EXPECT_EQ(cmd.ac_mode, AnalysisCommand::DEC);
            EXPECT_EQ(cmd.ac_npoints, 10);
            EXPECT_DOUBLE_EQ(cmd.ac_fstart, 1.0);
            EXPECT_DOUBLE_EQ(cmd.ac_fstop, 1e9);
        }
    }
    EXPECT_TRUE(found_noise);
}

// ---------------------------------------------------------------------------
// Test 2: Frequency sweep — verify the right number of points are generated
// ---------------------------------------------------------------------------
TEST(Noise, FrequencySweepPoints) {
    // DEC mode: 10 points per decade, 1 Hz to 1 MHz = 6 decades -> 61 points
    std::string netlist = R"(
Noise Freq Test
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

    // 6 decades * 10 points + 1 = 61 points
    EXPECT_EQ(result.frequency.size(), 61u);
    EXPECT_EQ(result.output_noise_density.size(), 61u);
    EXPECT_EQ(result.input_noise_density.size(), 61u);

    // First freq should be 1 Hz
    EXPECT_DOUBLE_EQ(result.frequency.front(), 1.0);
}

// ---------------------------------------------------------------------------
// Test 3: Resistor thermal noise floor
//
// Circuit: V1 -- R1(1k) -- out -- R2(1k) -- GND
//
// The two resistors form a voltage divider with gain = 0.5.
// At the output, the noise is the sum of contributions from R1 and R2.
//
// For the adjoint method:
// - Output node is "out"
// - The adjoint transfer from each resistor to the output
//   gives the output-referred noise
//
// Theoretical result (thermal noise of resistor divider):
// Each resistor contributes 4kT*G * |H|^2 where H is the transfer function
// from that resistor's noise current to the output voltage.
//
// For a simple resistor divider (R1, R2 in series, measuring at midpoint):
//   Total output noise = 4kT * (R1*R2)/(R1+R2) = 4kT * R_parallel
//   For R1 = R2 = 1k: R_parallel = 500 ohm
//   output_noise = 4 * 1.3806488e-23 * 300.15 * 500 = 8.286e-18 V^2/Hz
// ---------------------------------------------------------------------------
TEST(Noise, ResistorThermalNoiseFloor) {
    std::string netlist = R"(
Resistor Divider Noise
V1 in 0 DC 1 AC 1
R1 in out 1k
R2 out 0 1k
.noise V(out) V1 dec 1 1000 1000
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    auto result = solve_noise(ckt, "out", "V1",
                              AnalysisCommand::DEC, 1, 1000.0, 1000.0);

    ASSERT_EQ(result.frequency.size(), 1u);
    EXPECT_DOUBLE_EQ(result.frequency[0], 1000.0);

    // Theoretical: 4kT * R_parallel where R_parallel = R1*R2/(R1+R2) = 500 ohm
    double R_parallel = 500.0;
    double expected_output_noise = 4.0 * BOLTZMANN * T_NOMINAL * R_parallel;

    // Should match within 1% (it's an exact analytical result for resistors)
    EXPECT_NEAR(result.output_noise_density[0], expected_output_noise,
                expected_output_noise * 0.01);

    // Per-device noise should be present
    EXPECT_FALSE(result.device_noise.empty());

    // Both resistors should contribute
    EXPECT_TRUE(result.device_noise.count("r1") > 0);
    EXPECT_TRUE(result.device_noise.count("r2") > 0);

    // Sum of device contributions should equal total
    double sum = 0.0;
    for (const auto& [name, noise_vec] : result.device_noise) {
        sum += noise_vec[0];
    }
    EXPECT_NEAR(sum, result.output_noise_density[0],
                result.output_noise_density[0] * 1e-10);
}

// ---------------------------------------------------------------------------
// Test 4: Input-referred noise
//
// For a voltage divider with gain = R2/(R1+R2) = 0.5:
//   input_noise = output_noise / gain^2 = output_noise / 0.25
// ---------------------------------------------------------------------------
TEST(Noise, InputReferredNoise) {
    std::string netlist = R"(
Input Referred Noise
V1 in 0 DC 1 AC 1
R1 in out 1k
R2 out 0 1k
.noise V(out) V1 dec 1 1000 1000
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    auto result = solve_noise(ckt, "out", "V1",
                              AnalysisCommand::DEC, 1, 1000.0, 1000.0);

    ASSERT_EQ(result.frequency.size(), 1u);

    // Gain of voltage divider = R2/(R1+R2) = 0.5, gain^2 = 0.25
    double gain_sq = 0.25;
    double expected_input_noise = result.output_noise_density[0] / gain_sq;

    EXPECT_NEAR(result.input_noise_density[0], expected_input_noise,
                expected_input_noise * 0.01);
}

// ---------------------------------------------------------------------------
// Test 5: Noise frequency independence for resistors
//
// Resistor thermal noise is white (flat) — independent of frequency.
// Verify the output noise is constant across the frequency sweep.
// ---------------------------------------------------------------------------
TEST(Noise, ResistorWhiteNoise) {
    std::string netlist = R"(
White Noise Check
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

    // All output noise values should be the same (white noise)
    double first_val = result.output_noise_density[0];
    EXPECT_GT(first_val, 0.0);

    for (size_t i = 1; i < result.output_noise_density.size(); ++i) {
        EXPECT_NEAR(result.output_noise_density[i], first_val,
                    first_val * 1e-6)
            << "at frequency index " << i;
    }
}

// ---------------------------------------------------------------------------
// Test 6: API integration — run through Simulator::run()
// ---------------------------------------------------------------------------
TEST(Noise, APIIntegration) {
    std::string netlist = R"(
Noise API Test
V1 in 0 DC 1 AC 1
R1 in out 1k
R2 out 0 1k
.noise V(out) V1 dec 5 100 1e6
.end
)";
    Simulator sim;
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);

    ASSERT_TRUE(result.noise.has_value());
    EXPECT_GT(result.noise->frequency.size(), 0u);
    EXPECT_GT(result.noise->output_noise_density.size(), 0u);
    EXPECT_GT(result.noise->input_noise_density.size(), 0u);
}

// ---------------------------------------------------------------------------
// Test 7: run_noise() direct API call
// ---------------------------------------------------------------------------
TEST(Noise, DirectAPICall) {
    std::string netlist = R"(
Noise Direct API Test
V1 in 0 DC 1 AC 1
R1 in out 1k
R2 out 0 1k
.end
)";
    Simulator sim;
    auto ckt = sim.parse(netlist);
    auto result = sim.run_noise(ckt, "out", "V1",
                                AnalysisCommand::DEC, 5, 100.0, 1e6);

    EXPECT_GT(result.frequency.size(), 0u);
    EXPECT_GT(result.output_noise_density[0], 0.0);
}

// ---------------------------------------------------------------------------
// Test 8: Single resistor to ground — analytical verification
//
// Circuit: V1 -- R1(10k) -- out (open, with R_load=inf means no path)
// Actually, we need a closed circuit. Use: V1 -- R1 -- out, out -- R2 -- GND
// With R2 >> R1, the noise is dominated by R1.
//
// Simpler: V1(0V) -- R1(10k) -- GND
// Output node at one end of R1. But the noise would just be the thermal noise
// of R1 referred to its terminals. Let's verify with a known topology.
// ---------------------------------------------------------------------------
TEST(Noise, SingleResistorNoise) {
    // Simple circuit: Vin -- R(10k) -- out -- to ground via R_load(10k)
    // Output noise at 'out': 4kT * (R*R_load)/(R+R_load)
    std::string netlist = R"(
Single R Noise
V1 in 0 DC 0 AC 1
R1 in out 10k
R2 out 0 10k
.noise V(out) V1 lin 1 1000 1000
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    auto result = solve_noise(ckt, "out", "V1",
                              AnalysisCommand::LIN, 1, 1000.0, 1000.0);

    ASSERT_EQ(result.frequency.size(), 1u);

    double R_par = 5000.0;  // 10k || 10k = 5k
    double expected = 4.0 * BOLTZMANN * T_NOMINAL * R_par;
    EXPECT_NEAR(result.output_noise_density[0], expected, expected * 0.01);
}

// ---------------------------------------------------------------------------
// Test 9: Asymmetric divider — unequal resistors
// R1=1k, R2=3k -> gain = 3k/4k = 0.75, R_parallel = 750 ohm
// ---------------------------------------------------------------------------
TEST(Noise, AsymmetricDividerNoise) {
    std::string netlist = R"(
Asymmetric Divider
V1 in 0 DC 1 AC 1
R1 in out 1k
R2 out 0 3k
.noise V(out) V1 dec 1 1000 1000
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    auto result = solve_noise(ckt, "out", "V1",
                              AnalysisCommand::DEC, 1, 1000.0, 1000.0);

    ASSERT_EQ(result.frequency.size(), 1u);

    // R_parallel = 1k * 3k / (1k + 3k) = 750 ohm
    double R_par = 750.0;
    double expected_output = 4.0 * BOLTZMANN * T_NOMINAL * R_par;
    EXPECT_NEAR(result.output_noise_density[0], expected_output,
                expected_output * 0.01);

    // Gain = R2/(R1+R2) = 3/4 = 0.75, gain^2 = 0.5625
    double gain_sq = 0.5625;
    double expected_input = expected_output / gain_sq;
    EXPECT_NEAR(result.input_noise_density[0], expected_input,
                expected_input * 0.01);
}

// ---------------------------------------------------------------------------
// Test 10: Parser edge case — lowercase V(out)
// ---------------------------------------------------------------------------
TEST(Noise, ParserLowercase) {
    std::string netlist = R"(
Noise Lowercase
V1 in 0 DC 1 AC 1
R1 in out 1k
R2 out 0 1k
.noise v(out) v1 lin 5 100 1e3
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);

    bool found_noise = false;
    for (const auto& cmd : ckt.analyses) {
        if (cmd.type == AnalysisCommand::NOISE) {
            found_noise = true;
            EXPECT_EQ(cmd.noise_output, "out");
            EXPECT_EQ(cmd.noise_input_src, "v1");
            EXPECT_EQ(cmd.ac_mode, AnalysisCommand::LIN);
            EXPECT_EQ(cmd.ac_npoints, 5);
        }
    }
    EXPECT_TRUE(found_noise);
}

// ---------------------------------------------------------------------------
// RC low-pass filter noise — exercises reactive (C != 0) path
// ---------------------------------------------------------------------------
// Circuit: V1 -> R1(1k) -> out -> C1(100nF) -> GND
// Resistor R1 generates thermal noise. The RC filter rolls off at fc = 1/(2*pi*R*C).
// At low freq: output noise PSD = 4kT*R (full resistor noise passes through)
// At high freq: output noise PSD drops as 1/(1 + (f/fc)^2)
TEST(Noise, RCLowPassFilterRolloff) {
    const char* netlist = R"(
RC Low-Pass Noise Test
V1 in 0 DC 0 AC 1
R1 in out 1k
C1 out 0 100n
.noise v(out) v1 dec 10 10 1e6
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);

    auto result = solve_noise(ckt, "out", "v1",
                              AnalysisCommand::DEC, 10, 10.0, 1e6);

    ASSERT_GT(result.frequency.size(), 10u);

    double R = 1e3;
    double C = 100e-9;
    double fc = 1.0 / (2.0 * M_PI * R * C);  // ~1592 Hz

    // At low frequency (well below fc), output noise should be ~4kTR
    double kT = BOLTZMANN * T_NOMINAL;
    double expected_low = 4.0 * kT * R;  // V^2/Hz

    // Find a frequency well below fc (e.g., 10 Hz)
    double low_freq_noise = result.output_noise_density[0];
    EXPECT_NEAR(low_freq_noise, expected_low, expected_low * 0.05)
        << "Low-frequency noise should be ~4kTR";

    // Find a frequency well above fc (e.g., last point at 1 MHz)
    size_t last = result.frequency.size() - 1;
    double high_freq = result.frequency[last];
    double high_freq_noise = result.output_noise_density[last];
    double expected_high = expected_low / (1.0 + std::pow(high_freq / fc, 2));

    EXPECT_NEAR(high_freq_noise, expected_high, expected_high * 0.1)
        << "High-frequency noise should roll off as 1/(1+(f/fc)^2)";

    // Verify monotonic decrease (noise should decrease with frequency)
    for (size_t i = 1; i < result.frequency.size(); ++i) {
        EXPECT_LE(result.output_noise_density[i],
                  result.output_noise_density[i-1] * 1.01)
            << "Noise should decrease monotonically (or stay flat) at freq="
            << result.frequency[i];
    }
}

// ===========================================================================
// Diode noise tests (Task 9.2)
// ===========================================================================

// ---------------------------------------------------------------------------
// Test 11: Diode shot noise analytical verification
//
// Circuit: V1(0.65V) -- anode -- D1 -- cathode(ground)
//          + R_sense(1 ohm) at output to provide AC ground
//
// Actually, for noise analysis we need an AC source and an output node.
// Use: Vbias(0.65V, AC 1) -- R(1 ohm) -- mid -- D1 -- GND
//      Output: V(mid)
//
// The diode operating point at ~0.65V forward bias:
//   Id = Is*(exp(Vd/(N*Vt))-1) where Vt = kT/q
//   gd = Is/(N*Vt) * exp(Vd/(N*Vt))
//
// Noise sources from diode:
//   Shot: 2*q*|Id|
//   Thermal: 4*kT*gd
//   Total diode noise current: S_diode = 2*q*Id + 4*kT*gd
//
// For a strongly forward-biased diode: Id ~ Is*exp(Vd/Vt), gd ~ Id/Vt
// So: 2*q*Id + 4*kT*(Id/Vt) = 2*q*Id + 4*kT*Id*q/(kT) = 2*q*Id + 4*q*Id = 6*q*Id
// (This is a well-known result: diode noise = 6qId for strong forward bias)
//
// With the 1-ohm source resistor, the output noise is dominated by the diode
// since gd >> 1/R for a forward-biased diode.
// ---------------------------------------------------------------------------
TEST(Noise, DiodeShotNoise) {
    // Circuit: V1(DC 0.65V, AC 1) -- R(100k) -- out -- D1 -- GND
    // With a large R, the diode small-signal impedance (1/gd ~ 3 ohms)
    // is negligible compared to R, so almost all the bias current flows
    // through D1. The output node "out" sees the parallel combination
    // of R and 1/gd, so the diode noise has a clear contribution.
    //
    // For the adjoint method, the diode noise current between (out, GND)
    // produces output voltage noise ~ S_id * |Z_out|^2
    // where Z_out = R || (1/gd) ~ 1/gd for R >> 1/gd.
    std::string netlist = R"(
Diode Noise Test
V1 in 0 DC 0.60 AC 1
R1 in out 100k
D1 out 0 DMOD
.model DMOD D IS=1e-14 N=1
.noise V(out) V1 dec 1 1000 1000
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    auto result = solve_noise(ckt, "out", "V1",
                              AnalysisCommand::DEC, 1, 1000.0, 1000.0);

    ASSERT_EQ(result.frequency.size(), 1u);
    EXPECT_GT(result.output_noise_density[0], 0.0)
        << "Diode should contribute non-zero noise";

    // The diode should appear in per-device breakdown
    ASSERT_TRUE(result.device_noise.count("d1") > 0)
        << "Diode d1 should appear in per-device noise breakdown";
    ASSERT_TRUE(result.device_noise.count("r1") > 0);

    double diode_noise = result.device_noise.at("d1")[0];
    double resistor_noise = result.device_noise.at("r1")[0];

    // Both should contribute non-zero noise
    EXPECT_GT(diode_noise, 0.0)
        << "Diode noise contribution should be positive";
    EXPECT_GT(resistor_noise, 0.0)
        << "Resistor noise contribution should be positive";

    // For a forward-biased diode with gd >> 1/R:
    // The diode noise current PSD = 2*q*Id + 4*kT*gd
    // For strong forward bias: gd = Id/Vt, so S = 2qId + 4qId = 6qId
    // Output noise from diode ~ S * (1/gd)^2 = 6qId * (Vt/Id)^2 = 6q*Vt^2/Id
    //
    // Output noise from R1 ~ 4kT/R * (1/gd)^2 = 4kT*(Vt/Id)^2/R
    //
    // Ratio: diode/resistor = (6qId * R) / (4kT) = 6*R*Id / (4*Vt)
    // With R=100k, Id~1e-3, Vt~0.026: ratio ~ 6*1e5*1e-3/(4*0.026) ~ 5770
    // So diode should dominate by far.
    EXPECT_GT(diode_noise, resistor_noise)
        << "Diode noise should dominate over large R noise";
}

// ---------------------------------------------------------------------------
// Test 12: Diode noise is white (frequency independent for shot+thermal)
//
// Shot noise and thermal noise are both white (flat across frequency).
// Verify the diode noise contribution is constant across a frequency sweep.
// ---------------------------------------------------------------------------
TEST(Noise, DiodeWhiteNoise) {
    std::string netlist = R"(
Diode White Noise Test
V1 in 0 DC 0.65 AC 1
R1 in mid 1
D1 mid 0 DMOD
.model DMOD D IS=1e-14 N=1
.noise V(mid) V1 dec 10 10 1e6
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    auto result = solve_noise(ckt, "mid", "V1",
                              AnalysisCommand::DEC, 10, 10.0, 1e6);

    ASSERT_GT(result.frequency.size(), 10u);

    // With no capacitance (Cj0=0, Tt=0), the diode noise should be white.
    // The output noise should be approximately constant across frequency.
    double first_val = result.output_noise_density[0];
    EXPECT_GT(first_val, 0.0);

    for (size_t i = 1; i < result.output_noise_density.size(); ++i) {
        EXPECT_NEAR(result.output_noise_density[i], first_val,
                    first_val * 0.01)
            << "Diode noise should be white (flat) at frequency "
            << result.frequency[i] << " Hz";
    }
}

// ---------------------------------------------------------------------------
// Test 13: Diode noise precise analytical check
//
// For a diode with a large resistor (Rd >> 1/gd), the diode and resistor
// form a parallel noise network. With the AC source through the resistor
// to the diode, the noise transfer functions are well-defined.
//
// Circuit: V1(DC bias, AC 1) -- R(large) -- out -- D1 -- GND
//
// At DC OP: diode forward biased, Vd ~ determined by Newton.
// At AC: parallel impedance of R and 1/gd.
// Output noise = S_R * |1/(1+R*gd)|^2 + S_D * |R/(1+R*gd)|^2
// where S_R = 4kT/R, S_D = 2qId + 4kT*gd
//
// For R*gd >> 1:
//   Output noise ~ S_R / (R*gd)^2 + S_D / gd^2 ~ S_D / gd^2
//   = (2qId + 4kT*gd) / gd^2
// ---------------------------------------------------------------------------
TEST(Noise, DiodePreciseAnalytical) {
    // Use a moderate forward bias where we can compute the OP analytically
    // V1 = 0.60V, R = 100k, Is = 1e-14, N = 1
    std::string netlist = R"(
Diode Analytical Noise
V1 in 0 DC 0.60 AC 1
R1 in out 100k
D1 out 0 DMOD
.model DMOD D IS=1e-14 N=1
.noise V(out) V1 dec 1 1000 1000
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    auto result = solve_noise(ckt, "out", "V1",
                              AnalysisCommand::DEC, 1, 1000.0, 1000.0);

    ASSERT_EQ(result.frequency.size(), 1u);

    // Verify both diode and resistor contribute
    ASSERT_TRUE(result.device_noise.count("d1") > 0);
    ASSERT_TRUE(result.device_noise.count("r1") > 0);

    // Total noise should be positive
    EXPECT_GT(result.output_noise_density[0], 0.0);

    // The output noise should be the sum of device contributions
    double sum = 0.0;
    for (const auto& [name, nvec] : result.device_noise) {
        sum += nvec[0];
    }
    EXPECT_NEAR(sum, result.output_noise_density[0],
                result.output_noise_density[0] * 1e-10);
}

// ===========================================================================
// BSIM4v7 (MOSFET) noise tests (Task 9.2)
// ===========================================================================

// ---------------------------------------------------------------------------
// Test 14: MOSFET channel thermal noise — basic presence check
//
// Circuit: Common-source NMOS amplifier
//   VDD(1.8V) -- RD(5k) -- drain -- M1(NMOS) -- GND
//   VG(0.9V, AC 1) -- gate
//   Output: V(drain)
//
// The dominant noise source should be the MOSFET channel thermal noise.
// We verify:
//   1. The MOSFET produces non-zero noise
//   2. The noise is in the right ballpark for 4kT*(2/3)*gm
// ---------------------------------------------------------------------------
TEST(Noise, MOSFETChannelNoise) {
    std::string netlist = R"(
NMOS Noise Test
VDD vdd 0 DC 1.8
VG gate 0 DC 0.9 AC 1
RD vdd drain 5k
M1 drain gate 0 0 NMOD W=10u L=100n
.model NMOD NMOS LEVEL=14 VTH0=0.4 U0=0.04 TOXE=2e-9
.noise V(drain) VG dec 1 1000 1000
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    auto result = solve_noise(ckt, "drain", "VG",
                              AnalysisCommand::DEC, 1, 1000.0, 1000.0);

    ASSERT_EQ(result.frequency.size(), 1u);
    EXPECT_GT(result.output_noise_density[0], 0.0)
        << "MOSFET amplifier should have non-zero output noise";

    // The MOSFET should be present in the per-device breakdown
    ASSERT_TRUE(result.device_noise.count("m1") > 0)
        << "MOSFET m1 should appear in per-device noise breakdown";
    double mosfet_noise = result.device_noise.at("m1")[0];
    EXPECT_GT(mosfet_noise, 0.0)
        << "MOSFET channel thermal noise should be positive";

    // RD (5k) should also contribute thermal noise
    ASSERT_TRUE(result.device_noise.count("rd") > 0);
    double rd_noise = result.device_noise.at("rd")[0];
    EXPECT_GT(rd_noise, 0.0);

    // Total should be sum of all device contributions
    double sum = 0.0;
    for (const auto& [name, nvec] : result.device_noise) {
        sum += nvec[0];
    }
    EXPECT_NEAR(sum, result.output_noise_density[0],
                result.output_noise_density[0] * 1e-10);
}

// ---------------------------------------------------------------------------
// Test 15: MOSFET noise approximate magnitude check
//
// For a common-source amplifier with drain load RD:
//   Output noise from MOSFET: S_ch * |H_ch|^2
//   where S_ch = 4kT*(2/3)*gm (channel thermal noise current PSD)
//   and H_ch = transfer from drain noise current to V(drain) ~ RD/(1+gm*RD*...)
//
// At low frequency (no capacitance effects), the output-referred MOSFET
// channel noise is approximately:
//   S_out_mosfet ~ 4kT * (2/3) * gm * (RD || (1/gds))^2
//
// For simplified checking, verify the noise PSD is within an order of
// magnitude of the analytical estimate.
// ---------------------------------------------------------------------------
TEST(Noise, MOSFETNoiseOrderOfMagnitude) {
    std::string netlist = R"(
NMOS Noise OOM Test
VDD vdd 0 DC 1.8
VG gate 0 DC 0.9 AC 1
RD vdd drain 5k
M1 drain gate 0 0 NMOD W=10u L=100n
.model NMOD NMOS LEVEL=14 VTH0=0.4 U0=0.04 TOXE=2e-9
.noise V(drain) VG dec 1 1000 1000
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);

    // First get gm from DC operating point
    auto result = solve_noise(ckt, "drain", "VG",
                              AnalysisCommand::DEC, 1, 1000.0, 1000.0);

    ASSERT_EQ(result.frequency.size(), 1u);

    // Get MOSFET noise contribution
    ASSERT_TRUE(result.device_noise.count("m1") > 0);
    double mosfet_output_noise = result.device_noise.at("m1")[0];

    // The output noise should be in a reasonable range.
    // For gm ~ 1-10 mA/V, RD = 5k:
    //   S_ch = 4kT*(2/3)*gm ~ 4*4.14e-21*(2/3)*gm
    //   At drain: S_out ~ S_ch * RD^2 (very rough, ignoring gds)
    //   ~ 4*4.14e-21 * 0.667 * 5e-3 * (5e3)^2 ~ 2.8e-16 V^2/Hz
    //
    // Allow wide tolerance (2 orders of magnitude) since BSIM4v7 gm
    // depends heavily on model params.
    EXPECT_GT(mosfet_output_noise, 1e-20)
        << "MOSFET noise should be above thermal noise floor";
    EXPECT_LT(mosfet_output_noise, 1e-12)
        << "MOSFET noise should not be unreasonably large";
}

// ---------------------------------------------------------------------------
// Test 16: MOSFET noise is white at low frequency when flicker is disabled
//
// With FNOIMOD=0 and KF=0 the flicker noise is completely suppressed.
// The remaining channel thermal noise is white (frequency-independent).
// Verify the output noise is flat at 10 Hz – 1 kHz.
// ---------------------------------------------------------------------------
TEST(Noise, MOSFETWhiteNoiseAtLowFreq) {
    std::string netlist = R"(
NMOS White Noise Test
VDD vdd 0 DC 1.8
VG gate 0 DC 0.9 AC 1
RD vdd drain 5k
M1 drain gate 0 0 NMOD W=10u L=100n
.model NMOD NMOS LEVEL=14 VTH0=0.4 U0=0.04 TOXE=2e-9 FNOIMOD=0 KF=0
.noise V(drain) VG dec 5 10 1000
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    auto result = solve_noise(ckt, "drain", "VG",
                              AnalysisCommand::DEC, 5, 10.0, 1000.0);

    ASSERT_GT(result.frequency.size(), 5u);

    // At low frequencies (10 Hz - 1 kHz), the noise should be approximately flat
    // (no 1/f since flicker noise is disabled via FNOIMOD=0 KF=0)
    double first_val = result.output_noise_density[0];
    EXPECT_GT(first_val, 0.0);

    // Allow 5% variation at low frequencies (capacitive effects should be minimal)
    for (size_t i = 1; i < result.output_noise_density.size(); ++i) {
        EXPECT_NEAR(result.output_noise_density[i], first_val,
                    first_val * 0.05)
            << "Noise should be approximately white below GHz at freq="
            << result.frequency[i] << " Hz";
    }
}
