#include <gtest/gtest.h>
#include "devices/switch.hpp"
#include "devices/vsource.hpp"
#include "devices/resistor.hpp"
#include "core/matrix.hpp"
#include "core/circuit.hpp"
#include "core/types.hpp"
#include "api/neospice.hpp"
#include <cmath>
#include <stdexcept>

using namespace neospice;

// ===========================================================================
// SwitchModel tests
// ===========================================================================

TEST(SwitchModel, DefaultsSW) {
    SwitchModel m;
    m.name = "SMOD";
    m.is_voltage_controlled = true;
    EXPECT_DOUBLE_EQ(m.Vt,   0.0);
    EXPECT_DOUBLE_EQ(m.Vh,   0.0);
    EXPECT_DOUBLE_EQ(m.Ron,  1.0);
    EXPECT_DOUBLE_EQ(m.Roff, 1e6);
}

TEST(SwitchModel, DefaultsCSW) {
    SwitchModel m;
    m.name = "WMOD";
    m.is_voltage_controlled = false;
    EXPECT_FALSE(m.is_voltage_controlled);
    EXPECT_DOUBLE_EQ(m.Vt,   0.0);
    EXPECT_DOUBLE_EQ(m.Vh,   0.0);
    EXPECT_DOUBLE_EQ(m.Ron,  1.0);
    EXPECT_DOUBLE_EQ(m.Roff, 1e6);
}

// ===========================================================================
// switch_smooth_step function tests
// ===========================================================================

TEST(SwitchSmoothStep, BelowMinus1ReturnZero) {
    EXPECT_DOUBLE_EQ(switch_smooth_step(-2.0), 0.0);
    EXPECT_DOUBLE_EQ(switch_smooth_step(-1.0), 0.0);
}

TEST(SwitchSmoothStep, AbovePlus1ReturnOne) {
    EXPECT_DOUBLE_EQ(switch_smooth_step(1.0), 1.0);
    EXPECT_DOUBLE_EQ(switch_smooth_step(2.0), 1.0);
}

TEST(SwitchSmoothStep, AtZeroReturnHalf) {
    EXPECT_DOUBLE_EQ(switch_smooth_step(0.0), 0.5);
}

TEST(SwitchSmoothStep, MonotonicallyIncreasing) {
    double prev = switch_smooth_step(-1.0);
    for (int i = -10; i <= 10; ++i) {
        double x = i * 0.1;
        double cur = switch_smooth_step(x);
        EXPECT_GE(cur, prev);
        prev = cur;
    }
}

// ===========================================================================
// VSwitch unit tests
// ===========================================================================

TEST(VSwitch, Construction) {
    SwitchModel m;
    m.name = "S1"; m.Vt = 1.0; m.Vh = 0.1; m.Ron = 10.0; m.Roff = 1e6;
    VSwitch sw("S1", 0, GROUND_INTERNAL, 2, GROUND_INTERNAL, m);
    EXPECT_EQ(sw.name(), "S1");
    EXPECT_DOUBLE_EQ(sw.model().Vt, 1.0);
    EXPECT_DOUBLE_EQ(sw.model().Vh, 0.1);
    EXPECT_DOUBLE_EQ(sw.model().Ron, 10.0);
    EXPECT_DOUBLE_EQ(sw.model().Roff, 1e6);
}

TEST(VSwitch, StampPatternPartialGround) {
    SwitchModel m;
    m.Ron = 1.0; m.Roff = 1e6;
    // np=0, nn=GROUND; only (0,0) is stamped
    VSwitch sw("S1", 0, GROUND_INTERNAL, 2, GROUND_INTERNAL, m);
    SparsityBuilder builder(3);
    sw.stamp_pattern(builder);
    auto pattern = builder.build();
    EXPECT_EQ(pattern.nnz(), 1);  // only (0,0)
}

TEST(VSwitch, StampPatternAllTerminals) {
    SwitchModel m;
    m.Ron = 1.0; m.Roff = 1e6;
    // np=0, nn=1
    VSwitch sw("S1", 0, 1, 2, GROUND_INTERNAL, m);
    SparsityBuilder builder(3);
    sw.stamp_pattern(builder);
    auto pattern = builder.build();
    EXPECT_EQ(pattern.nnz(), 4);  // (0,0), (0,1), (1,0), (1,1)
}

TEST(VSwitch, EvaluateOffState) {
    SwitchModel m;
    m.Vt = 1.0; m.Vh = 0.1; m.Ron = 1.0; m.Roff = 1e6;

    // np=0, nn=1; control: nc+=2, nc-=ground
    VSwitch sw("S1", 0, 1, 2, GROUND_INTERNAL, m);
    SparsityBuilder builder(3);
    sw.stamp_pattern(builder);
    builder.add(2, 2);  // make 3x3
    auto pattern = builder.build();

    NumericMatrix mat(pattern);
    sw.assign_offsets(pattern);

    // Control voltage = 0.0 < Vt - Vh = 0.9  → OFF state
    std::vector<double> voltages = {0.0, 0.0, 0.0};
    std::vector<double> rhs(3, 0.0);
    sw.evaluate(voltages, mat, rhs);

    double Goff = 1.0 / 1e6;
    EXPECT_NEAR(mat.value(pattern.offset(0, 0)),  Goff, 1e-15);
    EXPECT_NEAR(mat.value(pattern.offset(0, 1)), -Goff, 1e-15);
    EXPECT_NEAR(mat.value(pattern.offset(1, 0)), -Goff, 1e-15);
    EXPECT_NEAR(mat.value(pattern.offset(1, 1)),  Goff, 1e-15);
}

TEST(VSwitch, EvaluateOnState) {
    SwitchModel m;
    m.Vt = 1.0; m.Vh = 0.1; m.Ron = 1.0; m.Roff = 1e6;

    VSwitch sw("S1", 0, 1, 2, GROUND_INTERNAL, m);
    SparsityBuilder builder(3);
    sw.stamp_pattern(builder);
    builder.add(2, 2);
    auto pattern = builder.build();

    NumericMatrix mat(pattern);
    sw.assign_offsets(pattern);

    // Control voltage = 5.0 >> Vt + Vh = 1.1 → ON state
    std::vector<double> voltages = {0.0, 0.0, 5.0};
    std::vector<double> rhs(3, 0.0);
    sw.evaluate(voltages, mat, rhs);

    double Gon = 1.0 / 1.0;
    EXPECT_NEAR(mat.value(pattern.offset(0, 0)),  Gon, 1e-9);
    EXPECT_NEAR(mat.value(pattern.offset(0, 1)), -Gon, 1e-9);
    EXPECT_NEAR(mat.value(pattern.offset(1, 0)), -Gon, 1e-9);
    EXPECT_NEAR(mat.value(pattern.offset(1, 1)),  Gon, 1e-9);
}

TEST(VSwitch, EvaluateTransitionAtThreshold) {
    SwitchModel m;
    m.Vt = 1.0; m.Vh = 0.5; m.Ron = 1.0; m.Roff = 1e6;

    VSwitch sw("S1", 0, 1, 2, GROUND_INTERNAL, m);
    SparsityBuilder builder(3);
    sw.stamp_pattern(builder);
    builder.add(2, 2);
    auto pattern = builder.build();

    NumericMatrix mat(pattern);
    sw.assign_offsets(pattern);

    // Control voltage = Vt = 1.0 → exactly at threshold → f=0.5
    std::vector<double> voltages = {0.0, 0.0, 1.0};
    std::vector<double> rhs(3, 0.0);
    sw.evaluate(voltages, mat, rhs);

    double Gon  = 1.0 / 1.0;
    double Goff = 1.0 / 1e6;
    double G_mid = Goff + 0.5 * (Gon - Goff);
    EXPECT_NEAR(mat.value(pattern.offset(0, 0)),  G_mid, 1e-9);
    EXPECT_NEAR(mat.value(pattern.offset(0, 1)), -G_mid, 1e-9);
}

// ===========================================================================
// CSwitch unit tests
// ===========================================================================

TEST(CSwitch, ConstructorThrowsOnNullVSource) {
    SwitchModel m;
    m.Ron = 1.0; m.Roff = 1e6;
    EXPECT_THROW(CSwitch("W1", 0, 1, nullptr, m), std::invalid_argument);
}

TEST(CSwitch, Construction) {
    SwitchModel m;
    m.name = "WMOD"; m.is_voltage_controlled = false;
    m.Vt = 0.01; m.Vh = 0.005; m.Ron = 1.0; m.Roff = 1e6;
    VSource vsense("Vsense", 2, GROUND_INTERNAL, 0.0);
    vsense.set_branch_index(2);
    CSwitch csw("W1", 0, 1, &vsense, m);
    EXPECT_EQ(csw.name(), "W1");
    EXPECT_DOUBLE_EQ(csw.model().Vt, 0.01);
    EXPECT_DOUBLE_EQ(csw.model().Vh, 0.005);
}

TEST(CSwitch, EvaluateOffState) {
    SwitchModel m;
    m.Vt = 0.01; m.Vh = 0.005; m.Ron = 1.0; m.Roff = 1e6;

    VSource vsense("Vsense", 2, GROUND_INTERNAL, 0.0);
    vsense.set_branch_index(2);
    CSwitch csw("W1", 0, 1, &vsense, m);

    SparsityBuilder builder(3);
    csw.stamp_pattern(builder);
    builder.add(2, 2);
    auto pattern = builder.build();

    NumericMatrix mat(pattern);
    csw.assign_offsets(pattern);

    // Branch current at index 2 = 0.0 < It - Ih = 0.005 → OFF
    std::vector<double> voltages = {0.0, 0.0, 0.0};
    std::vector<double> rhs(3, 0.0);
    csw.evaluate(voltages, mat, rhs);

    double Goff = 1.0 / 1e6;
    EXPECT_NEAR(mat.value(pattern.offset(0, 0)),  Goff, 1e-15);
    EXPECT_NEAR(mat.value(pattern.offset(0, 1)), -Goff, 1e-15);
}

TEST(CSwitch, EvaluateOnState) {
    SwitchModel m;
    m.Vt = 0.01; m.Vh = 0.005; m.Ron = 1.0; m.Roff = 1e6;

    VSource vsense("Vsense", 2, GROUND_INTERNAL, 0.0);
    vsense.set_branch_index(2);
    CSwitch csw("W1", 0, 1, &vsense, m);

    SparsityBuilder builder(3);
    csw.stamp_pattern(builder);
    builder.add(2, 2);
    auto pattern = builder.build();

    NumericMatrix mat(pattern);
    csw.assign_offsets(pattern);

    // Branch current at index 2 = 1.0 >> It + Ih = 0.015 → ON
    std::vector<double> voltages = {0.0, 0.0, 1.0};
    std::vector<double> rhs(3, 0.0);
    csw.evaluate(voltages, mat, rhs);

    double Gon = 1.0 / 1.0;
    EXPECT_NEAR(mat.value(pattern.offset(0, 0)),  Gon, 1e-9);
    EXPECT_NEAR(mat.value(pattern.offset(0, 1)), -Gon, 1e-9);
}

// ===========================================================================
// Integration tests: VSwitch (S element) via Simulator
// ===========================================================================

TEST(VSwitchIntegration, SwitchON) {
    // S1 is ON (Vctrl=5V >> Vt+Vh=1.1V)
    // Circuit: Vin=5V → R1=1k → out → S1(Ron=1) → GND
    // V(out) = 5 * Ron / (R1 + Ron) = 5 * 1 / 1001 ≈ 4.995e-3 V
    Simulator sim;
    std::string netlist = R"(
VSwitch ON test
Vin in 0 DC 5.0
Vctrl ctrl 0 DC 5.0
R1 in out 1k
S1 out 0 ctrl 0 SMOD
.model SMOD SW(Vt=1.0 Vh=0.1 Ron=1 Roff=1Meg)
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(result.dc.has_value());
    // V(out) ≈ 5 * 1/(1000+1) = 4.995e-3 V
    double expected = 5.0 * 1.0 / (1000.0 + 1.0);
    EXPECT_NEAR(result.dc->node_voltages["v(out)"], expected, 1e-3);
}

TEST(VSwitchIntegration, SwitchOFF) {
    // S1 is OFF (Vctrl=0V << Vt-Vh=0.9V)
    // Circuit: Vin=5V → R1=1k → out → S1(Roff=1Meg) → GND
    // V(out) = 5 * 1e6 / (1000 + 1e6) ≈ 4.995 V
    Simulator sim;
    std::string netlist = R"(
VSwitch OFF test
Vin in 0 DC 5.0
Vctrl ctrl 0 DC 0.0
R1 in out 1k
S1 out 0 ctrl 0 SMOD
.model SMOD SW(Vt=1.0 Vh=0.1 Ron=1 Roff=1Meg)
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(result.dc.has_value());
    // V(out) ≈ 5 * 1e6/(1000+1e6) ≈ 4.995 V
    double expected = 5.0 * 1e6 / (1000.0 + 1e6);
    EXPECT_NEAR(result.dc->node_voltages["v(out)"], expected, 0.01);
}

TEST(VSwitchIntegration, SwitchAtThreshold) {
    // S1 at threshold (Vctrl=Vt=1.0V, Vh=0.5V, x=0 → f=0.5)
    // G = Goff + 0.5*(Gon - Goff)
    Simulator sim;
    std::string netlist = R"(
VSwitch threshold test
Vin in 0 DC 5.0
Vctrl ctrl 0 DC 1.0
R1 in out 1k
S1 out 0 ctrl 0 SMOD
.model SMOD SW(Vt=1.0 Vh=0.5 Ron=1 Roff=1Meg)
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(result.dc.has_value());
    double Gon  = 1.0 / 1.0;
    double Goff = 1.0 / 1e6;
    double G_mid = Goff + 0.5 * (Gon - Goff);
    double R_mid = 1.0 / G_mid;
    double expected = 5.0 * R_mid / (1000.0 + R_mid);
    EXPECT_NEAR(result.dc->node_voltages["v(out)"], expected, 1e-3);
}

TEST(VSwitchIntegration, ACStamp) {
    // Switch at OFF state — appears as fixed Roff=1Meg in AC
    // V(out)/Vin = Roff / (R1 + Roff) should be flat across frequency
    Simulator sim;
    std::string netlist = R"(
VSwitch AC test
Vin in 0 DC 0 AC 1
Vctrl ctrl 0 DC 0.0
R1 in out 1k
S1 out 0 ctrl 0 SMOD
.model SMOD SW(Vt=1.0 Vh=0.1 Ron=1 Roff=1Meg)
.ac dec 5 1 1Meg
.end
)";
    auto ckt = sim.parse(netlist);
    auto ac_result = sim.run_ac(ckt, AnalysisCommand::DEC, 5, 1.0, 1e6);
    ASSERT_FALSE(ac_result.frequency.empty());

    auto it = ac_result.voltages.find("v(out)");
    ASSERT_NE(it, ac_result.voltages.end());

    double Roff = 1e6;
    double R1 = 1e3;
    double expected_mag = Roff / (R1 + Roff);

    for (std::size_t i = 0; i < it->second.size(); ++i) {
        EXPECT_NEAR(std::abs(it->second[i]), expected_mag, 1e-3)
            << "at frequency " << ac_result.frequency[i] << " Hz";
    }
}

// ===========================================================================
// Integration tests: CSwitch (W element) via Simulator
// ===========================================================================

TEST(CSwitchIntegration, SwitchON) {
    // Vsense carries I = 5mA (Vin=5V, R=1k)
    // It=0.001A, Ih=0.0001A → threshold range [0.9mA, 1.1mA]
    // I=5mA >> 1.1mA → ON  (Ron=1)
    // V(out) = 5 * Ron / (R1 + Ron) where R1 connects in to out
    Simulator sim;
    std::string netlist = R"(
CSwitch ON test
Vin in 0 DC 5.0
Vsense in mid DC 0
R1 mid 0 1k
R2 in out 1k
W1 out 0 Vsense WMOD
.model WMOD CSW(It=0.001 Ih=0.0001 Ron=1 Roff=1Meg)
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(result.dc.has_value());
    // I through Vsense ≈ 5V/1k = 5mA >> threshold → ON
    // V(out) = 5 * 1 / (1000 + 1) ≈ 4.995e-3 V
    double expected = 5.0 * 1.0 / (1000.0 + 1.0);
    EXPECT_NEAR(result.dc->node_voltages["v(out)"], expected, 1e-3);
}

TEST(CSwitchIntegration, SwitchOFF) {
    // Vsense carries I ≈ 0 (no drive through sense path)
    // W1 will be in OFF state
    Simulator sim;
    std::string netlist = R"(
CSwitch OFF test
Vin in 0 DC 5.0
Vsense in mid DC 0
R1 mid 0 100Meg
R2 in out 1k
W1 out 0 Vsense WMOD
.model WMOD CSW(It=0.1 Ih=0.01 Ron=1 Roff=1Meg)
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(result.dc.has_value());
    // I through Vsense = 5V / 100Meg = 50nA << threshold → OFF
    // V(out) ≈ 5 * 1e6 / (1000 + 1e6) ≈ 4.995 V
    double expected = 5.0 * 1e6 / (1000.0 + 1e6);
    EXPECT_NEAR(result.dc->node_voltages["v(out)"], expected, 0.01);
}

// ===========================================================================
// Parser tests
// ===========================================================================

TEST(SwitchParser, ParseSElement) {
    // Verify S element parses without error
    Simulator sim;
    std::string netlist = R"(
S element parser test
Vin in 0 DC 5.0
Vctrl ctrl 0 DC 5.0
R1 in out 1k
S1 out 0 ctrl 0 SMOD
.model SMOD SW(Vt=0 Vh=0.1 Ron=1 Roff=1Meg)
.op
.end
)";
    EXPECT_NO_THROW(sim.parse(netlist));
}

TEST(SwitchParser, ParseWElement) {
    // Verify W element parses without error
    Simulator sim;
    std::string netlist = R"(
W element parser test
Vin in 0 DC 5.0
Vsense in mid DC 0
R1 mid 0 1k
R2 in out 1k
W1 out 0 Vsense WMOD
.model WMOD CSW(It=0 Ih=0.01 Ron=1 Roff=1Meg)
.op
.end
)";
    EXPECT_NO_THROW(sim.parse(netlist));
}

TEST(SwitchParser, ParseSWModelParameters) {
    // Verify SW model parameters are parsed correctly via a simulation
    // Use Vt=2.0 Vh=0.5 so we can distinguish from defaults
    // Vctrl=5.0V >> Vt+Vh=2.5V → switch is ON (Ron=2)
    Simulator sim;
    std::string netlist = R"(
SW model parameter test
Vin in 0 DC 5.0
Vctrl ctrl 0 DC 5.0
R1 in out 500
S1 out 0 ctrl 0 MYMOD
.model MYMOD SW(Vt=2.0 Vh=0.5 Ron=2 Roff=500k)
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(result.dc.has_value());
    // V(out) = 5 * Ron / (R1 + Ron) = 5 * 2 / (500 + 2) ≈ 0.0199 V
    double expected = 5.0 * 2.0 / (500.0 + 2.0);
    EXPECT_NEAR(result.dc->node_voltages["v(out)"], expected, 1e-3);
}

TEST(SwitchParser, ParseCSWModelParameters) {
    // Verify CSW model parameters (It/Ih vs Vt/Vh key names)
    Simulator sim;
    std::string netlist = R"(
CSW model parameter test
Vin in 0 DC 5.0
Vsense in mid DC 0
R1 mid 0 1k
R2 in out 1k
W1 out 0 Vsense WMOD
.model WMOD CSW(It=0.001 Ih=0.0001 Ron=1 Roff=1Meg)
.op
.end
)";
    EXPECT_NO_THROW(sim.parse(netlist));
}

TEST(SwitchParser, ErrorUnknownModel) {
    Simulator sim;
    std::string netlist = R"(
S with unknown model
Vin in 0 DC 5.0
Vctrl ctrl 0 DC 5.0
R1 in out 1k
S1 out 0 ctrl 0 NOMODEL
.op
.end
)";
    EXPECT_THROW(sim.parse(netlist), ParseError);
}

TEST(SwitchParser, ErrorWElementUnknownVSource) {
    Simulator sim;
    std::string netlist = R"(
W with unknown VSource
Vin in 0 DC 5.0
R1 in out 1k
W1 out 0 Vdoesnotexist WMOD
.model WMOD CSW(It=0 Ih=0.01 Ron=1 Roff=1Meg)
.op
.end
)";
    EXPECT_THROW(sim.parse(netlist), ParseError);
}

TEST(SwitchParser, ErrorTooFewTokensSElement) {
    Simulator sim;
    std::string netlist = R"(
S too few tokens
Vin in 0 DC 5.0
S1 out 0 ctrl
.op
.end
)";
    EXPECT_THROW(sim.parse(netlist), ParseError);
}

TEST(SwitchParser, ErrorTooFewTokensWElement) {
    Simulator sim;
    std::string netlist = R"(
W too few tokens
Vin in 0 DC 5.0
Vsense in mid DC 0
R1 mid 0 1k
W1 out 0 Vsense
.op
.end
)";
    EXPECT_THROW(sim.parse(netlist), ParseError);
}

TEST(SwitchParser, ErrorWrongModelTypeForSElement) {
    // S element with a CSW model should throw
    Simulator sim;
    std::string netlist = R"(
S element with wrong model type
Vin in 0 DC 5.0
Vctrl ctrl 0 DC 5.0
R1 in out 1k
S1 out 0 ctrl 0 WRONGMOD
.model WRONGMOD CSW(It=0 Ih=0.01 Ron=1 Roff=1Meg)
.op
.end
)";
    EXPECT_THROW(sim.parse(netlist), ParseError);
}

TEST(SwitchParser, ErrorWrongModelTypeForWElement) {
    // W element with an SW model should throw
    Simulator sim;
    std::string netlist = R"(
W element with wrong model type
Vin in 0 DC 5.0
Vsense in mid DC 0
R1 mid 0 1k
R2 in out 1k
W1 out 0 Vsense WRONGMOD
.model WRONGMOD SW(Vt=0 Vh=0.1 Ron=1 Roff=1Meg)
.op
.end
)";
    EXPECT_THROW(sim.parse(netlist), ParseError);
}

// ===========================================================================
// ngspice comparison: relay circuit
// ===========================================================================

TEST(VSwitchNgspice, RelaySwitchON) {
    // Simple relay: S1 ON (Vctrl >> Vt+Vh)
    // V(out) ≈ Vin * Ron/(R1+Ron) = 5 * 1/1001 ≈ 4.995e-3 V
    Simulator sim;
    std::string netlist = R"(
VSwitch relay ON
Vin in 0 DC 5.0
Vctrl ctrl 0 DC 5.0
R1 in out 1k
S1 out 0 ctrl 0 SMOD
.model SMOD SW(Vt=1.0 Vh=0.1 Ron=1 Roff=1Meg)
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(result.dc.has_value());
    double expected = 5.0 * 1.0 / 1001.0;
    EXPECT_NEAR(result.dc->node_voltages["v(out)"], expected, expected * 0.01);
}

TEST(VSwitchNgspice, RelaySwitchOFF) {
    // Simple relay: S1 OFF (Vctrl << Vt-Vh)
    // V(out) ≈ Vin * Roff/(R1+Roff) = 5 * 1e6/1001000 ≈ 4.995 V
    Simulator sim;
    std::string netlist = R"(
VSwitch relay OFF
Vin in 0 DC 5.0
Vctrl ctrl 0 DC 0.0
R1 in out 1k
S1 out 0 ctrl 0 SMOD
.model SMOD SW(Vt=1.0 Vh=0.1 Ron=1 Roff=1Meg)
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(result.dc.has_value());
    double expected = 5.0 * 1e6 / (1000.0 + 1e6);
    EXPECT_NEAR(result.dc->node_voltages["v(out)"], expected, 0.001);
}
