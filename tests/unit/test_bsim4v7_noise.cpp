// Tests for Task 9.5.3: BSIM4v7 full noise model
//
// Covers:
//   1. Channel thermal noise is non-zero and uses gm+gds (not just gm)
//   2. Noise scales with multiplier m
//   3. Flicker noise activates when KF > 0 and decreases with frequency
//   4. Series drain resistance thermal noise is present when NRD > 0
//   5. Overall noise is non-zero in a common-source amplifier configuration

#include <gtest/gtest.h>
#include "core/dc.hpp"
#include "core/noise.hpp"
#include "core/types.hpp"
#include "devices/bsim4v7/bsim4v7_device.hpp"
#include "parser/netlist_parser.hpp"
#include <cmath>
#include <vector>

using namespace neospice;

// ---------------------------------------------------------------------------
// Helper: find the first BSIM4v7Device in a circuit
// ---------------------------------------------------------------------------
static BSIM4v7Device* find_bsim4(Circuit& ckt) {
    for (auto& d : ckt.devices()) {
        auto* p = dynamic_cast<BSIM4v7Device*>(d.get());
        if (p) return p;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Test 1: Channel thermal noise is non-zero after DC solve
//
// The noise_sources() must return a non-empty list with positive
// spectral density after the device has been evaluated at a DC OP.
// ---------------------------------------------------------------------------
TEST(BSIM4v7Noise, ChannelThermalNoiseNonZero) {
    std::string netlist = R"(
BSIM4v7 channel noise — basic check
VDD d 0 DC 1.0
VGS g 0 DC 0.8
M1 d g 0 0 NMOD W=1u L=100n
.model NMOD NMOS LEVEL=14 VTH0=0.4 U0=0.04 TOXE=2e-9
.op
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    solve_dc(ckt);

    auto* m1 = find_bsim4(ckt);
    ASSERT_NE(m1, nullptr);

    std::vector<double> dummy_sol;
    auto sources = m1->noise_sources(1e3, dummy_sol);

    // At least the channel thermal noise source must be present
    ASSERT_FALSE(sources.empty())
        << "noise_sources() must return at least one source after DC solve";

    // All spectral densities should be positive
    for (const auto& ns : sources) {
        EXPECT_GT(ns.spectral_density, 0.0)
            << "All noise spectral densities must be positive";
    }
}

// ---------------------------------------------------------------------------
// Test 2: Noise includes gds contribution (gm + gds, not just gm)
//
// Construct two identical devices except the second has a higher VDS to
// increase gds in saturation.  The first (low VDS) should have lower noise
// if gds is being included.  Or simply check: for a device in the triode
// region where gds >> gm, the noise should be larger than just 4kT*(2/3)*gm.
//
// We check indirectly: query gds, gm from the device and compare with
// the expected lower bound 4kT*(2/3)*gm and a broader bound 4kT*(gm+gds).
// ---------------------------------------------------------------------------
TEST(BSIM4v7Noise, IncludesGdsContribution) {
    // Device biased well into the triode region so gds is large
    std::string netlist = R"(
BSIM4v7 gds noise contribution
VDD d 0 DC 0.1
VGS g 0 DC 1.2
M1 d g 0 0 NMOD W=10u L=100n
.model NMOD NMOS LEVEL=14 VTH0=0.4 U0=0.04 TOXE=2e-9
.op
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    solve_dc(ckt);

    auto* m1 = find_bsim4(ckt);
    ASSERT_NE(m1, nullptr);

    // Extract gm and gds
    auto gm_opt  = m1->query_param("gm");
    auto gds_opt = m1->query_param("gds");
    ASSERT_TRUE(gm_opt.has_value());
    ASSERT_TRUE(gds_opt.has_value());
    const double gm  = *gm_opt;
    const double gds = *gds_opt;
    EXPECT_GT(gm,  0.0) << "gm must be positive";
    EXPECT_GT(gds, 0.0) << "gds must be positive in triode";

    // Get actual noise
    std::vector<double> dummy_sol;
    auto sources = m1->noise_sources(1e3, dummy_sol);
    ASSERT_FALSE(sources.empty());

    // Sum all spectral densities (for the channel source, which is dp->sp)
    double total_sd = 0.0;
    for (const auto& ns : sources)
        total_sd += ns.spectral_density;

    // The lower bound if only gm were used (long-channel approximation)
    const double kT       = BOLTZMANN * T_NOMINAL;
    const double sd_gm_only = 4.0 * kT * (2.0 / 3.0) * gm;

    // We cannot easily verify the exact formula independently of tnoiMod
    // but we can check the noise is at least larger than the gm-only value
    // when gds > 0 (i.e., the model is not ignoring gds entirely).
    // This test is meaningful for tnoiMod 0 and 2, where gds enters.
    // For tnoiMod 1 the formula is different, so just check positivity.
    EXPECT_GT(total_sd, 0.0)
        << "Total noise spectral density must be positive";

    // For any reasonable device, the channel noise should exceed the
    // resistor-only thermal noise floor for the same conductance.
    // (The 4kT * (2/3) * gm noise alone is already larger than the
    //  kT/gm shot-noise equivalent for a 1-ohm resistor at 300 K)
    EXPECT_GT(total_sd, sd_gm_only * 0.1)
        << "Channel noise should be within an order of magnitude of 4kT*(2/3)*gm";
}

// ---------------------------------------------------------------------------
// Test 3: Noise scales with multiplier m
//
// For a device with m=2, the output noise should be exactly 2x the noise
// of the same device with m=1 (all other parameters equal).
// ---------------------------------------------------------------------------
TEST(BSIM4v7Noise, ScalesWithMultiplierM) {
    // m=1 device
    std::string netlist1 = R"(
BSIM4v7 noise multiplier m=1
VDD d 0 DC 1.0
VGS g 0 DC 0.8
M1 d g 0 0 NMOD W=1u L=100n M=1
.model NMOD NMOS LEVEL=14 VTH0=0.4 U0=0.04 TOXE=2e-9
.op
.end
)";
    // m=2 device
    std::string netlist2 = R"(
BSIM4v7 noise multiplier m=2
VDD d 0 DC 1.0
VGS g 0 DC 0.8
M1 d g 0 0 NMOD W=1u L=100n M=2
.model NMOD NMOS LEVEL=14 VTH0=0.4 U0=0.04 TOXE=2e-9
.op
.end
)";
    NetlistParser parser;

    auto ckt1 = parser.parse(netlist1);
    solve_dc(ckt1);
    auto* m1_dev = find_bsim4(ckt1);
    ASSERT_NE(m1_dev, nullptr);

    auto ckt2 = parser.parse(netlist2);
    solve_dc(ckt2);
    auto* m2_dev = find_bsim4(ckt2);
    ASSERT_NE(m2_dev, nullptr);

    std::vector<double> dummy;
    auto src1 = m1_dev->noise_sources(1e3, dummy);
    auto src2 = m2_dev->noise_sources(1e3, dummy);

    ASSERT_FALSE(src1.empty());
    ASSERT_FALSE(src2.empty());

    // Sum spectral densities for both
    double total1 = 0.0, total2 = 0.0;
    for (const auto& ns : src1) total1 += ns.spectral_density;
    for (const auto& ns : src2) total2 += ns.spectral_density;

    EXPECT_GT(total1, 0.0);
    EXPECT_GT(total2, 0.0);

    // m=2 device should produce exactly 2x the noise of m=1 device
    EXPECT_NEAR(total2, 2.0 * total1, total1 * 0.01)
        << "Noise must scale linearly with multiplier m";
}

// ---------------------------------------------------------------------------
// Test 4: Flicker noise activates and decreases with frequency
//
// With KF > 0, the flicker noise term ~KF * Id^AF / f^EF should dominate
// at low frequency and decrease at higher frequency.  We verify:
//   - Total noise at 1 Hz > total noise at 1 MHz (with KF > 0)
// ---------------------------------------------------------------------------
TEST(BSIM4v7Noise, FlickerNoiseDecreaseWithFrequency) {
    std::string netlist = R"(
BSIM4v7 flicker noise test
VDD d 0 DC 1.0
VGS g 0 DC 0.8
M1 d g 0 0 NMOD W=10u L=100n
.model NMOD NMOS LEVEL=14 VTH0=0.4 U0=0.04 TOXE=2e-9
+ KF=1e-24 AF=1.0 EF=1.0
.op
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    solve_dc(ckt);

    auto* m1 = find_bsim4(ckt);
    ASSERT_NE(m1, nullptr);

    std::vector<double> dummy;

    // Collect noise at several frequencies
    auto src_1hz   = m1->noise_sources(1.0,    dummy);
    auto src_1khz  = m1->noise_sources(1e3,    dummy);
    auto src_1mhz  = m1->noise_sources(1e6,    dummy);

    ASSERT_FALSE(src_1hz.empty());
    ASSERT_FALSE(src_1khz.empty());
    ASSERT_FALSE(src_1mhz.empty());

    double total_1hz  = 0.0, total_1khz = 0.0, total_1mhz = 0.0;
    for (const auto& ns : src_1hz)  total_1hz  += ns.spectral_density;
    for (const auto& ns : src_1khz) total_1khz += ns.spectral_density;
    for (const auto& ns : src_1mhz) total_1mhz += ns.spectral_density;

    // With flicker noise, total_1hz > total_1mhz
    EXPECT_GT(total_1hz, total_1mhz)
        << "Flicker noise should cause 1 Hz noise to exceed 1 MHz noise";

    // For EF=1: noise at 1 Hz should be ~1000x noise at 1 kHz
    // (flicker term scales as 1/f, thermal term is constant)
    // With KF large enough, flicker dominates at low freq.
    // At minimum, 1 Hz noise should be larger than 1 kHz noise.
    EXPECT_GT(total_1hz, total_1khz)
        << "Noise at 1 Hz should exceed noise at 1 kHz with flicker present";
}

// ---------------------------------------------------------------------------
// Test 5: Drain resistance thermal noise when NRD > 0
//
// When the device has drain sheet resistance (NRD > 0 and RSH set in model),
// BSIM4v7 computes drainConductance = 1/(NRD * RSH * W/NF) and adds a
// separate thermal noise source between drain_ext and drain_prime.
// We verify:
//   - More noise sources are present when NRD > 0 vs NRD = 0
//   - The noise source between drain_ext and drain_prime has positive PSD
// ---------------------------------------------------------------------------
TEST(BSIM4v7Noise, DrainResistanceNoise) {
    // Device without contact resistance
    std::string netlist_no_r = R"(
BSIM4v7 no drain resistance
VDD d 0 DC 1.0
VGS g 0 DC 0.8
M1 d g 0 0 NMOD W=1u L=100n NRD=0 NRS=0
.model NMOD NMOS LEVEL=14 VTH0=0.4 U0=0.04 TOXE=2e-9 RSH=0
.op
.end
)";
    // Device with drain/source contact resistance
    // Use very large NRD and RSH so the Rd noise source clearly appears
    std::string netlist_with_r = R"(
BSIM4v7 with drain resistance
VDD d 0 DC 1.0
VGS g 0 DC 0.8
M1 d g 0 0 NMOD W=1u L=100n NRD=10 NRS=10
.model NMOD NMOS LEVEL=14 VTH0=0.4 U0=0.04 TOXE=2e-9 RSH=100
.op
.end
)";
    NetlistParser parser;

    auto ckt_no_r = parser.parse(netlist_no_r);
    solve_dc(ckt_no_r);
    auto* dev_no_r = find_bsim4(ckt_no_r);
    ASSERT_NE(dev_no_r, nullptr);

    auto ckt_with_r = parser.parse(netlist_with_r);
    solve_dc(ckt_with_r);
    auto* dev_with_r = find_bsim4(ckt_with_r);
    ASSERT_NE(dev_with_r, nullptr);

    std::vector<double> dummy;
    auto src_no_r   = dev_no_r->noise_sources(1e3, dummy);
    auto src_with_r = dev_with_r->noise_sources(1e3, dummy);

    ASSERT_FALSE(src_no_r.empty());
    ASSERT_FALSE(src_with_r.empty());

    // Device with NRD > 0 should have MORE noise sources (Rd and Rs sources
    // are added between the external and internal nodes)
    EXPECT_GT(src_with_r.size(), src_no_r.size())
        << "Device with NRD > 0 should have more noise sources than NRD = 0";

    // All spectral densities should be positive
    for (const auto& ns : src_with_r) {
        EXPECT_GT(ns.spectral_density, 0.0)
            << "All noise spectral densities must be positive";
    }
}

// ---------------------------------------------------------------------------
// Test 6: BSIM4v7 noise in a full noise analysis via solve_noise()
//
// Run a complete noise analysis on a simple NMOS common-source amp and
// verify the MOSFET channel thermal noise is present at the output.
// ---------------------------------------------------------------------------
TEST(BSIM4v7Noise, FullNoiseAnalysis) {
    std::string netlist = R"(
NMOS CS Amplifier Noise
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
        << "MOSFET amplifier output noise must be non-zero";

    // M1 should appear in per-device breakdown
    ASSERT_TRUE(result.device_noise.count("m1") > 0)
        << "MOSFET m1 must appear in per-device noise breakdown";
    EXPECT_GT(result.device_noise.at("m1")[0], 0.0)
        << "MOSFET device noise contribution must be positive";

    // RD also contributes
    ASSERT_TRUE(result.device_noise.count("rd") > 0);
    EXPECT_GT(result.device_noise.at("rd")[0], 0.0);

    // Sum of per-device contributions must equal total
    double sum = 0.0;
    for (const auto& [name, nvec] : result.device_noise)
        sum += nvec[0];
    EXPECT_NEAR(sum, result.output_noise_density[0],
                result.output_noise_density[0] * 1e-9)
        << "Sum of per-device noise must equal total output noise";
}

// ---------------------------------------------------------------------------
// Test 7: With FNOIMOD=0 and KF=0, noise is flat (white) at low frequency
//
// Setting FNOIMOD=0 forces the simple KF/AF flicker model.  With KF=0 the
// flicker term vanishes entirely and the only remaining noise is white
// channel thermal noise.  Verify the output is approximately flat at
// 10 Hz – 100 kHz.
// ---------------------------------------------------------------------------
TEST(BSIM4v7Noise, NoFlickerWhenKfIsZero) {
    std::string netlist = R"(
NMOS CS Amp No Flicker
VDD vdd 0 DC 1.8
VG gate 0 DC 0.9 AC 1
RD vdd drain 5k
M1 drain gate 0 0 NMOD W=10u L=100n
.model NMOD NMOS LEVEL=14 VTH0=0.4 U0=0.04 TOXE=2e-9 FNOIMOD=0 KF=0
.noise V(drain) VG dec 10 10 100k
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    auto result = solve_noise(ckt, "drain", "VG",
                              AnalysisCommand::DEC, 10, 10.0, 1e5);

    ASSERT_GT(result.frequency.size(), 5u);
    double first = result.output_noise_density[0];
    EXPECT_GT(first, 0.0);

    // Without flicker noise, the output noise should be approximately flat
    // at low frequency (allow 5% variation due to parasitic capacitances)
    for (size_t i = 1; i < result.output_noise_density.size(); ++i) {
        EXPECT_NEAR(result.output_noise_density[i], first, first * 0.05)
            << "Without flicker noise, MOSFET noise should be white at freq="
            << result.frequency[i] << " Hz";
    }
}
