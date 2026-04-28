// Tests for temperature-aware noise analysis (Task 9.5.2).
//
// Verifies that noise calculations use ckt.options.temp (set from
// .options temp=XX in the netlist) instead of the hardcoded T_NOMINAL.

#include <gtest/gtest.h>
#include "core/noise.hpp"
#include "core/types.hpp"
#include "parser/netlist_parser.hpp"
#include "api/neospice.hpp"
#include "framework/ngspice_runner.hpp"
#include "framework/comparator.hpp"
#include <cmath>

using namespace neospice;

// ---------------------------------------------------------------------------
// Helper: compute expected resistor-divider output noise at a given temperature
// R1=R2=R -> R_parallel = R/2
// ---------------------------------------------------------------------------
static double resistor_divider_noise(double R, double T) {
    return 4.0 * BOLTZMANN * T * (R / 2.0);
}

// ---------------------------------------------------------------------------
// Test 1: Resistor noise at hot temperature (T=100°C = 373.15 K) is higher
//         than at nominal temperature (T_NOMINAL = 300.15 K).
//
// Ratio should be 373.15 / 300.15 ≈ 1.243
// ---------------------------------------------------------------------------
TEST(NoiseTempAware, ResistorNoiseScalesWithTemp) {
    const double T_hot  = 373.15;  // 100°C
    const double T_nom  = T_NOMINAL;  // 27°C = 300.15 K
    const double R      = 1000.0;  // 1 kΩ

    // --- nominal temperature (default) ---
    std::string netlist_nom = R"(
Resistor noise nominal temp
V1 in 0 DC 1 AC 1
R1 in out 1k
R2 out 0 1k
.noise V(out) V1 lin 1 1000 1000
.end
)";
    NetlistParser parser;
    auto ckt_nom = parser.parse(netlist_nom);
    ASSERT_DOUBLE_EQ(ckt_nom.options.temp, T_NOMINAL);
    auto result_nom = solve_noise(ckt_nom, "out", "V1",
                                  AnalysisCommand::LIN, 1, 1000.0, 1000.0);

    // --- hot temperature via .options temp=100 ---
    std::string netlist_hot = R"(
Resistor noise hot temp
V1 in 0 DC 1 AC 1
R1 in out 1k
R2 out 0 1k
.options temp=100
.noise V(out) V1 lin 1 1000 1000
.end
)";
    auto ckt_hot = parser.parse(netlist_hot);
    ASSERT_NEAR(ckt_hot.options.temp, T_hot, 0.01)
        << ".options temp=100 should set T=373.15 K";
    auto result_hot = solve_noise(ckt_hot, "out", "V1",
                                  AnalysisCommand::LIN, 1, 1000.0, 1000.0);

    ASSERT_EQ(result_nom.frequency.size(), 1u);
    ASSERT_EQ(result_hot.frequency.size(), 1u);

    double noise_nom = result_nom.output_noise_density[0];
    double noise_hot = result_hot.output_noise_density[0];

    EXPECT_GT(noise_hot, noise_nom)
        << "Hot-temperature noise should exceed nominal-temperature noise";

    // Ratio should equal T_hot / T_nom within 0.1%
    double expected_ratio = T_hot / T_nom;
    double actual_ratio   = noise_hot / noise_nom;
    EXPECT_NEAR(actual_ratio, expected_ratio, expected_ratio * 1e-3)
        << "Noise ratio should equal T_hot/T_nom = " << expected_ratio;
}

// ---------------------------------------------------------------------------
// Test 2: Noise at T=100°C is ~24.3% higher than at nominal (absolute check)
// ---------------------------------------------------------------------------
TEST(NoiseTempAware, ResistorNoiseMagnitudeHot) {
    const double T_hot = 373.15;  // 100°C
    const double R     = 1000.0;

    std::string netlist = R"(
Resistor hot noise absolute check
V1 in 0 DC 1 AC 1
R1 in out 1k
R2 out 0 1k
.options temp=100
.noise V(out) V1 lin 1 1000 1000
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    auto result = solve_noise(ckt, "out", "V1",
                              AnalysisCommand::LIN, 1, 1000.0, 1000.0);

    ASSERT_EQ(result.frequency.size(), 1u);

    double expected = resistor_divider_noise(R, T_hot);
    EXPECT_NEAR(result.output_noise_density[0], expected, expected * 1e-3)
        << "Absolute output noise at T=100C should match 4kT*R_par";
}

// ---------------------------------------------------------------------------
// Test 3: Below-nominal temperature reduces noise (T=-50°C = 223.15 K)
// ---------------------------------------------------------------------------
TEST(NoiseTempAware, ResistorNoiseColdTemperature) {
    const double T_cold = 223.15;  // -50°C

    std::string netlist = R"(
Resistor cold noise
V1 in 0 DC 1 AC 1
R1 in out 1k
R2 out 0 1k
.options temp=-50
.noise V(out) V1 lin 1 1000 1000
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    ASSERT_NEAR(ckt.options.temp, T_cold, 0.01)
        << ".options temp=-50 should set T=223.15 K";

    auto result = solve_noise(ckt, "out", "V1",
                              AnalysisCommand::LIN, 1, 1000.0, 1000.0);
    ASSERT_EQ(result.frequency.size(), 1u);

    double expected = resistor_divider_noise(1000.0, T_cold);
    EXPECT_NEAR(result.output_noise_density[0], expected, expected * 1e-3)
        << "Cold-temperature noise should match 4kT_cold*R_par";

    // Cold noise < nominal noise
    double expected_nom = resistor_divider_noise(1000.0, T_NOMINAL);
    EXPECT_LT(result.output_noise_density[0], expected_nom)
        << "Cold-temperature noise should be less than nominal";
}

// ---------------------------------------------------------------------------
// Test 4: Default temperature produces T_NOMINAL noise (regression guard)
// ---------------------------------------------------------------------------
TEST(NoiseTempAware, DefaultTempProducesNominalNoise) {
    std::string netlist = R"(
Resistor noise default temp
V1 in 0 DC 1 AC 1
R1 in out 1k
R2 out 0 1k
.noise V(out) V1 lin 1 1000 1000
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    auto result = solve_noise(ckt, "out", "V1",
                              AnalysisCommand::LIN, 1, 1000.0, 1000.0);

    ASSERT_EQ(result.frequency.size(), 1u);

    double expected = resistor_divider_noise(1000.0, T_NOMINAL);
    EXPECT_NEAR(result.output_noise_density[0], expected, expected * 1e-3)
        << "Default temperature should produce T_NOMINAL noise";
}

// ---------------------------------------------------------------------------
// Test 5: BSIM4v7 channel noise scales with temperature
//
// At higher temperature, 4kT*(2/3)*gm increases proportionally (assuming gm
// is roughly the same at both temperatures for a simple gate-voltage sweep).
// We just verify that the MOSFET noise at hot temperature is larger than at
// nominal temperature.
// ---------------------------------------------------------------------------
TEST(NoiseTempAware, MOSFETNoiseSacalesWithTemp) {
    auto make_mosfet_netlist = [](const char* temp_option) -> std::string {
        std::string s = R"(
NMOS Noise Temp Test
VDD vdd 0 DC 1.8
VG gate 0 DC 0.9 AC 1
RD vdd drain 5k
M1 drain gate 0 0 NMOD W=10u L=100n
.model NMOD NMOS LEVEL=14 VTH0=0.4 U0=0.04 TOXE=2e-9
)";
        s += temp_option;
        s += "\n.noise V(drain) VG lin 1 1000 1000\n.end\n";
        return s;
    };

    NetlistParser parser;

    auto ckt_nom = parser.parse(make_mosfet_netlist(""));
    auto result_nom = solve_noise(ckt_nom, "drain", "VG",
                                  AnalysisCommand::LIN, 1, 1000.0, 1000.0);

    auto ckt_hot = parser.parse(make_mosfet_netlist(".options temp=100"));
    auto result_hot = solve_noise(ckt_hot, "drain", "VG",
                                  AnalysisCommand::LIN, 1, 1000.0, 1000.0);

    ASSERT_EQ(result_nom.frequency.size(), 1u);
    ASSERT_EQ(result_hot.frequency.size(), 1u);

    ASSERT_TRUE(result_nom.device_noise.count("m1") > 0);
    ASSERT_TRUE(result_hot.device_noise.count("m1") > 0);

    double mosfet_nom = result_nom.device_noise.at("m1")[0];
    double mosfet_hot = result_hot.device_noise.at("m1")[0];

    EXPECT_GT(mosfet_nom, 0.0);
    EXPECT_GT(mosfet_hot, 0.0);
    EXPECT_GT(mosfet_hot, mosfet_nom)
        << "MOSFET channel thermal noise should increase with temperature";
}

// ---------------------------------------------------------------------------
// Test 6: set_sim_temp() / sim_temp() accessors on Device base class
// ---------------------------------------------------------------------------
TEST(NoiseTempAware, DeviceSimTempAccessors) {
    // Resistor is a concrete Device — test the accessor directly
    // We exercise through the Circuit/noise path: after solve_noise,
    // the resistor's sim_temp_ should equal ckt.options.temp.
    // (We can't easily inspect sim_temp_ directly; the temperature ratio
    // test above implicitly confirms the setter is used.)

    // Additional guard: verify default is T_NOMINAL
    std::string netlist = R"(
Device accessor test
V1 in 0 DC 1 AC 1
R1 in out 1k
R2 out 0 1k
.noise V(out) V1 lin 1 1000 1000
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);

    // Before noise solve, sim_temp should be T_NOMINAL (default)
    for (const auto& dev : ckt.devices()) {
        EXPECT_DOUBLE_EQ(dev->sim_temp(), T_NOMINAL)
            << "Device " << dev->name()
            << " should default to T_NOMINAL before noise solve";
    }

    // After noise solve, sim_temp should still be T_NOMINAL (unchanged)
    auto result = solve_noise(ckt, "out", "V1",
                              AnalysisCommand::LIN, 1, 1000.0, 1000.0);
    for (const auto& dev : ckt.devices()) {
        EXPECT_DOUBLE_EQ(dev->sim_temp(), T_NOMINAL)
            << "Device " << dev->name()
            << " should still be T_NOMINAL after default-temp noise solve";
    }

    // Set custom temperature and run — sim_temp should be updated
    ckt.options.temp = 400.0;
    auto result2 = solve_noise(ckt, "out", "V1",
                               AnalysisCommand::LIN, 1, 1000.0, 1000.0);
    for (const auto& dev : ckt.devices()) {
        EXPECT_DOUBLE_EQ(dev->sim_temp(), 400.0)
            << "Device " << dev->name()
            << " should have sim_temp=400 after solve with custom temp";
    }
    // And noise should scale correspondingly
    EXPECT_NEAR(result2.output_noise_density[0] / result.output_noise_density[0],
                400.0 / T_NOMINAL, 1e-3)
        << "Noise ratio should equal T2/T1";
}

// ---------------------------------------------------------------------------
// Test 7: ngspice comparison for resistor noise at T=100°C
//
// Uses tests/circuits/resistor_noise_hot.cir which has .options temp=100.
// neospice output noise (V^2/Hz) is converted to V/sqrt(Hz) for comparison.
// ---------------------------------------------------------------------------
class NoiseTempNgspiceTest : public ::testing::Test {
protected:
    void SetUp() override {
        ngspice_ = std::make_unique<NgspiceRunner>();
    }
    std::unique_ptr<NgspiceRunner> ngspice_;
    Simulator sim_;
};

TEST_F(NoiseTempNgspiceTest, ResistorNoiseHotVsNgspice) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/resistor_noise_hot.cir";
    auto ng_result = ngspice_->run_noise(path);
    auto ckt = sim_.load(path);
    auto cs_result = sim_.run(ckt);
    ASSERT_TRUE(std::holds_alternative<NoiseResult>(cs_result.analysis));
    ASSERT_EQ(ng_result.frequency.size(), std::get<NoiseResult>(cs_result.analysis).frequency.size());
    // Same tolerance as the default-temperature resistor divider noise test
    auto cmp = compare_noise(ng_result, std::get<NoiseResult>(cs_result.analysis), {1e-3, 1e-15});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}
