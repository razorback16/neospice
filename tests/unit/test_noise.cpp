#include <gtest/gtest.h>
#include "core/noise.hpp"
#include "core/types.hpp"
#include "parser/netlist_parser.hpp"
#include "api/neospice.hpp"
#include <cmath>

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
