// =============================================================================
// Task 7.6: PDK-Style Netlist Integration Tests
//
// Comprehensive tests verifying subcircuit expansion, parameter expressions,
// .lib corner selection, .include, and nested subcircuits — all exercised
// through full DC / transient / AC simulation workflows.
// =============================================================================
#include <gtest/gtest.h>
#include "parser/netlist_parser.hpp"
#include "parser/subcircuit.hpp"
#include "parser/subcircuit_expand.hpp"
#include "core/dc.hpp"
#include "core/transient.hpp"
#include "core/ac.hpp"
#include "api/neospice.hpp"
#include "devices/resistor.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <complex>

using namespace neospice;
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Helper: wrap inline netlist with title line and .end terminator
// ---------------------------------------------------------------------------
static std::string wrap(const std::string& body) {
    return "PDK Integration Test\n" + body + "\n.end\n";
}

// =========================================================================
// Test 1: Resistor Divider Subcircuit — DC
// =========================================================================
TEST(PDKIntegration, ResistorDividerSubcircuit_DC) {
    // A parametric resistor divider subcircuit, instantiated with overrides.
    // R1 from in to out, R2 from out to ground => V(out) = VIN * R2/(R1+R2)
    // V(outp) = 3.0 * 10k/(20k+10k) = 1.0 V
    std::string netlist = wrap(R"(
.subckt rdiv in out r1=10k r2=10k
R1 in out {r1}
R2 out 0 {r2}
.ends rdiv

VIN inp 0 3.0
X1 inp outp rdiv r1=20k r2=10k
.op
)");

    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    DCResult result = solve_dc(ckt);

    EXPECT_NEAR(result.voltage("inp"), 3.0, 1e-6);
    // Divider: 3.0 * 10k/(20k+10k) = 1.0 V
    EXPECT_NEAR(result.voltage("outp"), 1.0, 1e-3);
}

// =========================================================================
// Test 2: MOSFET Wrapper Subcircuit — DC (PDK-style MOSFET cell)
// =========================================================================
TEST(PDKIntegration, MOSFETWrapper_DC) {
    // A PDK-style MOSFET wrapper with series gate resistance (Rg).
    // The wrapper encapsulates a BSIM4v7 MOSFET with parasitic gate R.
    std::string netlist = wrap(R"(
.subckt nmos_wrapper d g s b w=1u l=100n
Rg g gi 50
M1 d gi s b NMOD W={w} L={l}
.ends nmos_wrapper

.model NMOD NMOS LEVEL=14 VTH0=0.4 U0=0.04 TOXE=2e-9

VDD drain 0 1.8
VGS gate 0 1.0
X1 drain gate 0 0 nmos_wrapper w=2u l=100n
.op
)");

    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    DCResult result = solve_dc(ckt);

    // Gate is 1.0V, VTH0=0.4 => MOSFET should be ON (Vgs > Vth)
    EXPECT_NEAR(result.voltage("drain"), 1.8, 1e-6);
    EXPECT_NEAR(result.voltage("gate"), 1.0, 1e-6);

    // The MOSFET conducts => there should be drain current flowing
    // V1 provides current into drain node via VDD.
    double i_vdd = std::abs(result.current("vdd"));
    EXPECT_GT(i_vdd, 1e-6) << "MOSFET should be conducting";
}

// =========================================================================
// Test 3: CMOS Inverter Subcircuit — DC Transfer Curve
// =========================================================================
TEST(PDKIntegration, CMOSInverterSubcircuit_DCSweep) {
    // CMOS inverter as subcircuit, sweep input to verify transfer curve.
    // At VIN=0: output should be near VDD (PMOS on, NMOS off)
    // At VIN=VDD: output should be near 0 (PMOS off, NMOS on)
    std::string netlist = wrap(R"(
.subckt inv in out vdd vss wp=2u wn=1u l=100n
Mp out in vdd vdd PMOD W={wp} L={l}
Mn out in vss vss NMOD W={wn} L={l}
.ends inv

.model NMOD NMOS LEVEL=14 VTH0=0.4 U0=0.04 TOXE=2e-9
.model PMOD PMOS LEVEL=14 VTH0=-0.4 U0=0.02 TOXE=2e-9

VDD vdd 0 1.8
VIN in 0 0.0
X1 in out vdd 0 inv wp=4u wn=2u l=100n
CL out 0 10f
.dc VIN 0 1.8 0.3
)");

    NetlistParser parser;
    auto ckt = parser.parse(netlist);

    std::vector<DCSweepParam> params;
    DCSweepParam p;
    p.source_name = "VIN";
    p.start = 0.0;
    p.stop  = 1.8;
    p.step  = 0.3;
    params.push_back(p);

    DCSweepResult result = solve_dc_sweep(ckt, params);

    ASSERT_GE(result.sweep_values.size(), 6u);

    // At VIN=0: output should be close to VDD (1.8V)
    // First sweep point is VIN=0.0
    double v_out_at_0 = result.voltage("out")[0];
    EXPECT_GT(v_out_at_0, 1.4) << "At VIN=0, PMOS on => Vout near VDD";

    // At VIN=1.8: output should be close to 0
    double v_out_at_vdd = result.voltage("out").back();
    EXPECT_LT(v_out_at_vdd, 0.4) << "At VIN=VDD, NMOS on => Vout near 0";

    // Verify monotonic decreasing (inverter characteristic)
    for (size_t i = 1; i < result.voltage("out").size(); ++i) {
        EXPECT_LE(result.voltage("out")[i],
                  result.voltage("out")[i - 1] + 0.01)
            << "Inverter output should be monotonically non-increasing";
    }
}

// =========================================================================
// Test 4: Three-Stage Inverter Chain — Transient
// =========================================================================
TEST(PDKIntegration, ThreeStageInverterChain_Transient) {
    // Chain of 3 inverters using X instances with larger FETs for reliable switching.
    // Uses same MOSFET sizes as the known-working cmos_inverter.cir test circuit.
    // Odd number of inversions => output inverts input.
    std::string netlist = wrap(R"(
.subckt inv in out vdd vss wp=2u wn=1u
Mp out in vdd vdd PMOD W={wp} L=100n
Mn out in vss vss NMOD W={wn} L=100n
.ends inv

.model NMOD NMOS LEVEL=14 VTH0=0.4 U0=0.04 TOXE=2e-9
.model PMOD PMOS LEVEL=14 VTH0=-0.4 U0=0.02 TOXE=2e-9

VDD vdd 0 1.8
VIN in 0 PULSE(0 1.8 0 100p 100p 5n 10n)
X1 in n1 vdd 0 inv wp=2u wn=1u
C1 n1 0 10f
X2 n1 n2 vdd 0 inv wp=2u wn=1u
C2 n2 0 10f
X3 n2 out vdd 0 inv wp=2u wn=1u
CL out 0 10f
.tran 10p 20n
)");

    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    auto result = solve_transient(ckt, 10e-12, 20e-9);

    ASSERT_FALSE(result.time.empty());
    ASSERT_FALSE(result.voltage("out").empty());
    ASSERT_FALSE(result.voltage("in").empty());

    // The output waveform should swing between the rails during the simulation.
    // With 3 inversions, the output inverts the input.
    double v_min = *std::min_element(result.voltage("out").begin(),
                                      result.voltage("out").end());
    double v_max = *std::max_element(result.voltage("out").begin(),
                                      result.voltage("out").end());

    // The chain should produce output that reaches near both rails
    EXPECT_GT(v_max, 1.0) << "3-stage chain output should reach near VDD";
    EXPECT_LT(v_min, 0.8) << "3-stage chain output should reach near VSS";

    // Verify all 3 stages produced intermediate waveforms
    ASSERT_FALSE(result.voltage("n1").empty());
    ASSERT_FALSE(result.voltage("n2").empty());

    // Verify time vector spans full simulation
    EXPECT_NEAR(result.time.front(), 0.0, 1e-12);
    EXPECT_NEAR(result.time.back(), 20e-9, 1e-9);
}

// =========================================================================
// Test 5: NMOS Common-Source Amp in Subcircuit — AC Analysis
// =========================================================================
TEST(PDKIntegration, NMOSAmplifierSubcircuit_AC) {
    // A common-source amplifier wrapped in a subcircuit.
    // Uses the same circuit topology as tests/circuits/nmos_cs_amp_ac.cir
    // but wrapped in a subcircuit. Verify nonzero AC response.
    std::string netlist = wrap(R"(
.subckt cs_amp in out vdd vss rd=5k
Rd vdd out {rd}
M1 out in vss vss NMOD W=10u L=100n
.ends cs_amp

.model NMOD NMOS LEVEL=14 VTH0=0.4 U0=0.04 TOXE=2e-9

VDD vdd 0 1.8
VG gate 0 DC 0.9 AC 1
X1 gate drain vdd 0 cs_amp rd=5k
CL drain 0 100f
.ac dec 10 1k 100g
)");

    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    auto result = solve_ac(ckt, AnalysisCommand::DEC, 10, 1e3, 100e9);

    ASSERT_FALSE(result.frequency.empty());
    ASSERT_FALSE(result.voltage("drain").empty());

    // The AC response at the drain should be nonzero (MOSFET amplifies)
    double mag_low = std::abs(result.voltage("drain").front());
    EXPECT_GT(mag_low, 0.01)
        << "Common-source amp should have nonzero AC response at low freq";

    // Verify frequency vector is populated correctly
    EXPECT_NEAR(result.frequency.front(), 1e3, 1.0);
    EXPECT_GE(result.frequency.size(), 70u) << "10 pts/decade over 7 decades";
}

// =========================================================================
// Test 6: .lib Corner Selection + Subcircuit Integration
// =========================================================================
class PDKLibTest : public ::testing::Test {
protected:
    fs::path tmp_dir_;

    void SetUp() override {
        tmp_dir_ = fs::temp_directory_path() / ("neospice_pdk_lib_test_" +
                   std::to_string(reinterpret_cast<uintptr_t>(this)));
        fs::create_directories(tmp_dir_);
    }

    void TearDown() override {
        fs::remove_all(tmp_dir_);
    }

    fs::path write_file(const std::string& name, const std::string& content) {
        fs::path p = tmp_dir_ / name;
        fs::create_directories(p.parent_path());
        std::ofstream ofs(p);
        ofs << content;
        return p;
    }
};

TEST_F(PDKLibTest, CornerSelectionTTvsFF) {
    // Library with two corners: TT (VTH0=0.4) and FF (VTH0=0.3).
    // FF corner has lower threshold => more current at same bias.
    write_file("models.lib",
        ".lib tt\n"
        ".model NMOD NMOS LEVEL=14 VTH0=0.4 U0=0.04 TOXE=2e-9\n"
        ".endl tt\n"
        "\n"
        ".lib ff\n"
        ".model NMOD NMOS LEVEL=14 VTH0=0.3 U0=0.04 TOXE=2e-9\n"
        ".endl ff\n");

    auto run_corner = [&](const std::string& corner) -> double {
        fs::path main_path = write_file("main_" + corner + ".sp",
            "Corner test\n"
            ".lib models.lib " + corner + "\n"
            "VDD drain 0 1.8\n"
            "VGS gate 0 0.6\n"
            "M1 drain gate 0 0 NMOD W=1u L=100n\n"
            ".op\n"
            ".end\n");

        NetlistParser parser;
        auto ckt = parser.parse_file(main_path.string());
        DCResult result = solve_dc(ckt);
        return std::abs(result.current("vdd"));
    };

    double i_tt = run_corner("tt");
    double i_ff = run_corner("ff");

    // FF corner has lower Vth => higher current at same Vgs
    EXPECT_GT(i_ff, i_tt)
        << "FF corner (VTH0=0.3) should conduct more than TT (VTH0=0.4) at Vgs=0.6V";
    // Both should have nonzero current (Vgs=0.6 > both thresholds)
    EXPECT_GT(i_tt, 1e-9) << "TT corner should conduct (Vgs > Vth)";
    EXPECT_GT(i_ff, 1e-9) << "FF corner should conduct (Vgs > Vth)";
}

// =========================================================================
// Test 7: .include + Subcircuit Integration
// =========================================================================
TEST_F(PDKLibTest, IncludeWithSubcircuit) {
    // Included file defines a subcircuit with a resistor divider.
    // Main netlist uses X instance referencing the included subcircuit.
    // Topology: R1 from in to out, R2 from out to ground.
    write_file("cells.sp",
        ".subckt rdiv in out\n"
        "R1 in out 2k\n"
        "R2 out 0 2k\n"
        ".ends rdiv\n");

    fs::path main_path = write_file("main_inc.sp",
        "Include subcircuit test\n"
        ".include cells.sp\n"
        "VIN inp 0 6.0\n"
        "X1 inp outp rdiv\n"
        ".op\n"
        ".end\n");

    NetlistParser parser;
    auto ckt = parser.parse_file(main_path.string());
    DCResult result = solve_dc(ckt);

    // Divider: 6.0 * 2k/(2k+2k) = 3.0V
    EXPECT_NEAR(result.voltage("outp"), 3.0, 1e-3);
}

// =========================================================================
// Test 8: .include + .lib Combined (PDK-style flow)
// =========================================================================
TEST_F(PDKLibTest, IncludeAndLibCombined) {
    // Realistic PDK flow: main file .lib's model cards and .include's cell defs.
    write_file("models.lib",
        ".lib tt\n"
        ".model NMOD NMOS LEVEL=14 VTH0=0.4 U0=0.04 TOXE=2e-9\n"
        ".endl tt\n");

    write_file("cells.sp",
        ".subckt nmos_cell d g s b w=1u l=100n\n"
        "M1 d g s b NMOD W={w} L={l}\n"
        ".ends nmos_cell\n");

    fs::path main_path = write_file("main_pdk.sp",
        "PDK-style combined test\n"
        ".lib models.lib tt\n"
        ".include cells.sp\n"
        "VDD drain 0 1.8\n"
        "VGS gate 0 1.0\n"
        "X1 drain gate 0 0 nmos_cell w=2u l=100n\n"
        ".op\n"
        ".end\n");

    NetlistParser parser;
    auto ckt = parser.parse_file(main_path.string());
    DCResult result = solve_dc(ckt);

    // MOSFET should be conducting
    double i_vdd = std::abs(result.current("vdd"));
    EXPECT_GT(i_vdd, 1e-6) << "MOSFET in subcircuit should conduct current";
    EXPECT_NEAR(result.voltage("drain"), 1.8, 1e-6);
}

// =========================================================================
// Test 9: Nested Subcircuits — Buffer from Two Inverters
// =========================================================================
TEST(PDKIntegration, NestedSubcircuit_Buffer_DC) {
    // Higher-level 'buf' subcircuit instantiates two 'inv' subcircuits.
    // Buffer: input -> inv1 -> inv2 -> output (non-inverting)
    std::string netlist = wrap(R"(
.subckt inv in out vdd vss
Mp out in vdd vdd PMOD W=2u L=100n
Mn out in vss vss NMOD W=1u L=100n
.ends inv

.subckt buf in out vdd vss
X1 in mid vdd vss inv
X2 mid out vdd vss inv
.ends buf

.model NMOD NMOS LEVEL=14 VTH0=0.4 U0=0.04 TOXE=2e-9
.model PMOD PMOS LEVEL=14 VTH0=-0.4 U0=0.02 TOXE=2e-9

VDD vdd 0 1.8
VIN in 0 0.0
Xbuf in out vdd 0 buf
CL out 0 10f
.op
)");

    NetlistParser parser;
    auto ckt = parser.parse(netlist);

    // Verify hierarchical expansion created the expected devices.
    // buf expands to: xbuf.x1.mp, xbuf.x1.mn, xbuf.x2.mp, xbuf.x2.mn
    std::set<std::string> names;
    for (const auto& dev : ckt.devices()) {
        names.insert(dev->name());
    }
    EXPECT_NE(names.find("xbuf.x1.mp"), names.end())
        << "Expected nested device xbuf.x1.mp";
    EXPECT_NE(names.find("xbuf.x2.mn"), names.end())
        << "Expected nested device xbuf.x2.mn";

    DCResult result = solve_dc(ckt);

    // Buffer: input=0 => inv1=high => inv2=low ... wait, no:
    // input=0 => inv1 output=high (~1.8V) => inv2 output=low (~0V)
    // So the buffer output follows the input (both low) only when input
    // passes through the threshold. For input=0:
    // inv1: PMOS on => mid ~ VDD, inv2: NMOS on => out ~ 0
    // Wait: input=0 => PMOS on, NMOS off => mid=VDD(1.8)
    //        mid=1.8 => PMOS off, NMOS on => out=0
    // That means buf(0) = 0. Correct — buffer preserves logic level.
    EXPECT_LT(result.voltage("out"), 0.3)
        << "Buffer output should follow input = 0V => output near 0";
}

TEST(PDKIntegration, NestedSubcircuit_Buffer_HighInput_DC) {
    // Same buffer, but with input = VDD. Output should be near VDD.
    std::string netlist = wrap(R"(
.subckt inv in out vdd vss
Mp out in vdd vdd PMOD W=2u L=100n
Mn out in vss vss NMOD W=1u L=100n
.ends inv

.subckt buf in out vdd vss
X1 in mid vdd vss inv
X2 mid out vdd vss inv
.ends buf

.model NMOD NMOS LEVEL=14 VTH0=0.4 U0=0.04 TOXE=2e-9
.model PMOD PMOS LEVEL=14 VTH0=-0.4 U0=0.02 TOXE=2e-9

VDD vdd 0 1.8
VIN in 0 1.8
Xbuf in out vdd 0 buf
CL out 0 10f
.op
)");

    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    DCResult result = solve_dc(ckt);

    // input=1.8 => inv1: NMOS on => mid~0 => inv2: PMOS on => out~1.8
    EXPECT_GT(result.voltage("out"), 1.4)
        << "Buffer output should follow input = 1.8V => output near VDD";
}

// =========================================================================
// Test 10: Parameter Expression Chain
// =========================================================================
TEST(PDKIntegration, ParameterExpressionChain) {
    // Verify chained parameter expressions resolve correctly.
    // Top-level .param chain: tech_l -> min_w -> inv_wp.
    // Parameters are passed as explicit overrides to the X instance,
    // using the resolved global param values.
    std::string netlist = wrap(R"(
.param tech_l=100n
.param min_w={2*tech_l}
.param inv_wp={2*min_w}

.subckt inv in out vdd vss wp=2u wn=1u l=100n
Mp out in vdd vdd PMOD W={wp} L={l}
Mn out in vss vss NMOD W={wn} L={l}
.ends inv

.model NMOD NMOS LEVEL=14 VTH0=0.4 U0=0.04 TOXE=2e-9
.model PMOD PMOS LEVEL=14 VTH0=-0.4 U0=0.02 TOXE=2e-9

VDD vdd 0 1.8
VIN in 0 0.0
X1 in out vdd 0 inv wp={inv_wp} wn={min_w} l={tech_l}
CL out 0 10f
.op
)");

    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    DCResult result = solve_dc(ckt);

    // If parameters resolved correctly:
    // tech_l = 100n, min_w = 200n, inv_wp = 400n
    // PMOS W=400n, L=100n, NMOS W=200n, L=100n
    // VIN=0 => PMOS on => output ~ VDD
    EXPECT_GT(result.voltage("out"), 1.4)
        << "Parameter chain should resolve => inverter output high for input=0";
}

// =========================================================================
// Test 11: Multiple X Instances with Different Parameter Overrides
// =========================================================================
TEST(PDKIntegration, MultipleInstancesDifferentParams_DC) {
    // Two resistor blocks with different R values form a divider.
    // This tests that per-instance parameter overrides are kept separate.
    std::string netlist = wrap(R"(
.subckt rblock a b r=1k
R1 a b {r}
.ends rblock

VIN inp 0 12.0
X1 inp mid rblock r=3k
X2 mid 0 rblock r=1k
.op
)");

    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    DCResult result = solve_dc(ckt);

    // Voltage divider: V(mid) = 12 * 1k / (3k + 1k) = 3.0V
    EXPECT_NEAR(result.voltage("mid"), 3.0, 1e-3);
}

// =========================================================================
// Test 12: RC Filter Subcircuit — Transient
// =========================================================================
TEST(PDKIntegration, RCFilterSubcircuit_Transient) {
    // RC lowpass filter as subcircuit. Verify step response time constant.
    std::string netlist = wrap(R"(
.subckt rc_lpf in out r=1k c=1u
R1 in out {r}
C1 out 0 {c}
.ends rc_lpf

VIN inp 0 5.0
X1 inp outp rc_lpf r=1k c=1u
.ic v(outp)=0
.tran 10u 5m
)");

    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    auto result = solve_transient(ckt, 10e-6, 5e-3);

    ASSERT_FALSE(result.time.empty());
    ASSERT_FALSE(result.voltage("outp").empty());

    // tau = R*C = 1k * 1u = 1ms
    // At t=1ms, V(out) ~ 5*(1-e^-1) ~ 3.16V
    // Find nearest sample to 1ms (adaptive steps may not land exactly on grid)
    int idx_1ms = 0;
    double min_dist = std::numeric_limits<double>::max();
    for (size_t i = 0; i < result.time.size(); ++i) {
        double dist = std::abs(result.time[i] - 1e-3);
        if (dist < min_dist) { min_dist = dist; idx_1ms = static_cast<int>(i); }
    }
    double expected_1tau = 5.0 * (1.0 - std::exp(-1.0));
    EXPECT_NEAR(result.voltage("outp")[idx_1ms], expected_1tau, 0.15);

    // At end (5ms = 5*tau), should be close to 5V
    EXPECT_NEAR(result.voltage("outp").back(), 5.0, 0.1);
}

// =========================================================================
// Test 13: RC Filter Subcircuit — AC
// =========================================================================
TEST(PDKIntegration, RCFilterSubcircuit_AC) {
    // RC lowpass filter as subcircuit. Verify -3dB point.
    std::string netlist = wrap(R"(
.subckt rc_lpf in out r=1k c=1n
R1 in out {r}
C1 out 0 {c}
.ends rc_lpf

VIN inp 0 DC 0 AC 1
X1 inp outp rc_lpf r=1k c=1n
.ac dec 10 100 10meg
)");

    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    auto result = solve_ac(ckt, AnalysisCommand::DEC, 10, 100.0, 10e6);

    ASSERT_FALSE(result.frequency.empty());
    ASSERT_FALSE(result.voltage("outp").empty());

    // fc = 1/(2*pi*R*C) = 1/(2*pi*1e3*1e-9) ~ 159.15 kHz
    double fc = 1.0 / (2.0 * M_PI * 1e3 * 1e-9);

    // Find frequency closest to fc
    int idx_fc = 0;
    double min_diff = 1e20;
    for (size_t i = 0; i < result.frequency.size(); ++i) {
        double diff = std::abs(result.frequency[i] - fc);
        if (diff < min_diff) { min_diff = diff; idx_fc = static_cast<int>(i); }
    }

    double mag_at_fc = std::abs(result.voltage("outp")[idx_fc]);
    // At -3dB point, magnitude should be 1/sqrt(2) ~ 0.707
    EXPECT_NEAR(mag_at_fc, 1.0 / std::sqrt(2.0), 0.05);

    // At low frequency: magnitude ~ 1
    EXPECT_NEAR(std::abs(result.voltage("outp").front()), 1.0, 0.01);
}

// =========================================================================
// Test 14: Subcircuit with Internal MOSFET Model — Self-contained PDK Cell
// =========================================================================
TEST(PDKIntegration, SelfContainedPDKCell_DC) {
    // Subcircuit body contains its own .model — simulates a self-contained
    // PDK cell that bundles the device model.
    std::string netlist = wrap(R"(
.subckt nfet d g s b w=1u l=100n
.model NMOD_CELL NMOS LEVEL=14 VTH0=0.4 U0=0.04 TOXE=2e-9
M1 d g s b NMOD_CELL W={w} L={l}
.ends nfet

VDD drain 0 1.8
VGS gate 0 0.8
X1 drain gate 0 0 nfet w=5u l=100n
.op
)");

    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    DCResult result = solve_dc(ckt);

    // MOSFET should be on (Vgs=0.8 > Vth=0.4)
    double i_vdd = std::abs(result.current("vdd"));
    EXPECT_GT(i_vdd, 1e-6) << "Self-contained MOSFET cell should conduct";
}

// =========================================================================
// Test 15: Deeply Nested Subcircuits — 3 Levels Deep
// =========================================================================
TEST(PDKIntegration, DeeplyNestedSubcircuits_DC) {
    // Level 1: rblock (single R)
    // Level 2: rdiv (rblock from in->out for top, rblock from out->0 for bottom)
    // Level 3: vdiv wraps rdiv
    // Verify DC operates correctly through 3 levels of nesting.
    std::string netlist = wrap(R"(
.subckt rblock a b r=1k
R1 a b {r}
.ends rblock

.subckt rdiv in out r1=1k r2=1k
X1 in out rblock r={r1}
X2 out 0 rblock r={r2}
.ends rdiv

.subckt vdiv in out r1=2k r2=2k
Xdiv in out rdiv r1={r1} r2={r2}
.ends vdiv

VIN inp 0 10.0
Xvd inp outp vdiv r1=3k r2=1k
.op
)");

    NetlistParser parser;
    auto ckt = parser.parse(netlist);

    // Verify deep hierarchical naming
    std::set<std::string> names;
    for (const auto& dev : ckt.devices()) {
        names.insert(dev->name());
    }
    // xvd -> xdiv -> x1 -> r1 should give xvd.xdiv.x1.r1
    EXPECT_NE(names.find("xvd.xdiv.x1.r1"), names.end())
        << "Expected 3-level nested device xvd.xdiv.x1.r1";
    EXPECT_NE(names.find("xvd.xdiv.x2.r1"), names.end())
        << "Expected 3-level nested device xvd.xdiv.x2.r1";

    DCResult result = solve_dc(ckt);

    // Divider: V(outp) = 10 * 1k / (3k + 1k) = 2.5V
    EXPECT_NEAR(result.voltage("outp"), 2.5, 1e-2);
}

// =========================================================================
// Test 16: Nested Subcircuit Definition Within Body
// =========================================================================
TEST(PDKIntegration, NestedSubcircuitDefinitionInBody_DC) {
    // The 'outer' subcircuit contains a nested definition of 'inner'.
    // This tests that subcircuit definitions within subcircuit bodies
    // are properly parsed and expanded.
    std::string netlist = wrap(R"(
.subckt outer a b
.subckt inner x y
R1 x y 500
.ends inner
Xin a mid inner
R2 mid b 500
.ends outer

VIN inp 0 10
Xout inp outp outer
Rload outp 0 1Meg
.op
)");

    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    DCResult result = solve_dc(ckt);

    // inner R1=500, outer R2=500 in series: total 1k
    // Divider with Rload=1Meg: outp ~ 10 * 1Meg/(1k+1Meg) ~ 9.99V
    // But R2 connects mid to b (=outp). Rload from outp to 0.
    // Current path: inp -> inner.R1(500) -> mid -> R2(500) -> outp -> Rload(1M) -> 0
    // V(outp) = 10 * 1e6 / (500+500+1e6) ~ 9.99V
    EXPECT_NEAR(result.voltage("outp"), 10.0 * 1e6 / (1000 + 1e6), 1e-2);
}

// =========================================================================
// Test 17: CMOS Inverter with Parasitics Subcircuit — Transient
// =========================================================================
TEST(PDKIntegration, CMOSInverterWithParasitics_Transient) {
    // PDK cell with parasitic gate resistance on both NMOS and PMOS.
    // Verify transient simulation completes successfully with internal nodes
    // and produces a reasonable waveform.
    std::string netlist = wrap(R"(
.subckt inv_para in out vdd vss
Rgp in gp 20
Rgn in gn 20
Mp out gp vdd vdd PMOD W=4u L=100n
Mn out gn vss vss NMOD W=2u L=100n
.ends inv_para

.model NMOD NMOS LEVEL=14 VTH0=0.4 U0=0.04 TOXE=2e-9
.model PMOD PMOS LEVEL=14 VTH0=-0.4 U0=0.02 TOXE=2e-9

VDD vdd 0 1.8
VIN in 0 PULSE(0 1.8 1n 100p 100p 5n 12n)
X1 in out vdd 0 inv_para
CL out 0 50f
.tran 10p 20n
)");

    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    auto result = solve_transient(ckt, 10e-12, 20e-9);

    ASSERT_FALSE(result.time.empty());
    ASSERT_FALSE(result.voltage("out").empty());

    // The output should swing between rail voltages during the simulation.
    // Find min and max of the output waveform.
    double v_min = *std::min_element(result.voltage("out").begin(),
                                      result.voltage("out").end());
    double v_max = *std::max_element(result.voltage("out").begin(),
                                      result.voltage("out").end());

    // Output should reach near VDD at some point (when input is low)
    EXPECT_GT(v_max, 1.2) << "Inverter output should reach near VDD";
    // Output should reach near 0 at some point (when input is high)
    EXPECT_LT(v_min, 0.6) << "Inverter output should reach near VSS";
    // The swing should be significant (at least 1V rail-to-rail)
    EXPECT_GT(v_max - v_min, 1.0) << "Inverter should have significant swing";
}

// =========================================================================
// Test 18: Parameter Override Propagation Through Nesting
// =========================================================================
TEST(PDKIntegration, ParameterOverridePropagation) {
    // Verify that parameter overrides at the top-level X instance propagate
    // correctly through multiple levels of subcircuit nesting.
    // Topology: inp -> Xtop(4k) -> outp -> Xbot(1k) -> 0  (proper divider)
    std::string netlist = wrap(R"(
.subckt rblock a b r=1k
R1 a b {r}
.ends rblock

.subckt rdiv in out rtop=1k rbot=1k
Xtop in out rblock r={rtop}
Xbot out 0 rblock r={rbot}
.ends rdiv

VIN inp 0 10.0
Xdiv inp outp rdiv rtop=4k rbot=1k
.op
)");

    NetlistParser parser;
    auto ckt = parser.parse(netlist);

    // Verify the expanded resistors have correct values
    for (const auto& dev : ckt.devices()) {
        auto* r = dynamic_cast<const Resistor*>(dev.get());
        if (r && r->name() == "xdiv.xtop.r1") {
            EXPECT_NEAR(r->resistance(), 4000.0, 1e-6)
                << "Top resistor should be 4k from override";
        }
        if (r && r->name() == "xdiv.xbot.r1") {
            EXPECT_NEAR(r->resistance(), 1000.0, 1e-6)
                << "Bottom resistor should be 1k from override";
        }
    }

    DCResult result = solve_dc(ckt);
    // V(outp) = 10 * 1k / (4k + 1k) = 2.0V
    EXPECT_NEAR(result.voltage("outp"), 2.0, 1e-2);
}

// =========================================================================
// Test 19: CMOS Inverter Chain with Parametric Sizing — DC
// =========================================================================
TEST(PDKIntegration, ParametricInverterChain_DC) {
    // Two inverters with different W/L ratios, verifying that per-instance
    // parameter overrides work for MOSFET subcircuits.
    std::string netlist = wrap(R"(
.subckt inv in out vdd vss wp=2u wn=1u l=100n
Mp out in vdd vdd PMOD W={wp} L={l}
Mn out in vss vss NMOD W={wn} L={l}
.ends inv

.model NMOD NMOS LEVEL=14 VTH0=0.4 U0=0.04 TOXE=2e-9
.model PMOD PMOS LEVEL=14 VTH0=-0.4 U0=0.02 TOXE=2e-9

VDD vdd 0 1.8
VIN in 0 0.0
X1 in mid vdd 0 inv wp=4u wn=2u l=100n
X2 mid out vdd 0 inv wp=8u wn=4u l=100n
CL out 0 10f
.op
)");

    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    DCResult result = solve_dc(ckt);

    // input=0 => X1: PMOS on => mid ~ VDD
    EXPECT_GT(result.voltage("mid"), 1.4);
    // mid~VDD => X2: NMOS on => out ~ 0
    EXPECT_LT(result.voltage("out"), 0.3);
}

// =========================================================================
// Test 20: Subcircuit with .param Inside Body
// =========================================================================
TEST(PDKIntegration, SubcircuitWithParamInBody_DC) {
    // Subcircuit body defines internal .param that derives from defaults.
    std::string netlist = wrap(R"(
.subckt rdiv in out r=1k
.param r2val={r*2}
R1 in mid {r}
R2 mid out {r2val}
.ends rdiv

VIN inp 0 9.0
X1 inp outp rdiv r=1k
Rload outp 0 1Meg
.op
)");

    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    DCResult result = solve_dc(ckt);

    // R1=1k, R2=2k. Divider: V(outp) = 9 * Rload/(1k+2k+Rload)
    // With Rload=1Meg >> 3k: V(outp) ~ 9 * (2k || 1Meg) / (1k + 2k || 1Meg)
    // Actually: R1 in->mid (1k), R2 mid->outp (2k), Rload outp->0 (1Meg)
    // Current from inp through R1 and R2: I = 9/(1k+2k+1Meg) ~ 9/1.003Meg
    // V(outp) = I * Rload = 9 * 1e6 / (3000 + 1e6) ~ 8.973V
    // OR simplified: if Rload is large, mid divides as R2/(R1+R2) referenced from inp
    // V(mid->outp) drop = I * R2 = 9 * 2000 / (3000+1e6), V(outp) = V(inp) - I*(R1+R2)
    // Better: V(outp) = 9 * 1e6 / (1e6 + 3000) ~ 8.973V
    EXPECT_NEAR(result.voltage("outp"), 9.0 * 1e6 / (3000 + 1e6), 1e-2);
}

// =========================================================================
// Test 21: Large Subcircuit Instance Count (Stress Test)
// =========================================================================
TEST(PDKIntegration, ManyInstances_DC) {
    // 10 series resistor blocks forming a divider chain.
    // Each block is 1k => total 10k from inp to ground.
    std::string body = R"(
.subckt rblock a b r=1k
R1 a b {r}
.ends rblock

VIN inp 0 10.0
)";
    // Create 10 instances in series: inp -> n1 -> n2 -> ... -> n9 -> 0
    for (int i = 1; i <= 10; ++i) {
        std::string from = (i == 1) ? "inp" : ("n" + std::to_string(i - 1));
        std::string to   = (i == 10) ? "0" : ("n" + std::to_string(i));
        body += "X" + std::to_string(i) + " " + from + " " + to + " rblock r=1k\n";
    }
    body += ".op\n";

    std::string netlist = wrap(body);
    NetlistParser parser;
    auto ckt = parser.parse(netlist);

    // Should have VIN + 10 resistors = 11 devices
    EXPECT_EQ(ckt.devices().size(), 11u);

    DCResult result = solve_dc(ckt);

    // Linear voltage drop: V(n5) = 10 * 5/10 = 5.0V
    EXPECT_NEAR(result.voltage("n5"), 5.0, 1e-3);
    // V(n1) = 10 * 9/10 = 9.0V
    EXPECT_NEAR(result.voltage("n1"), 9.0, 1e-3);
}

// =========================================================================
// Test 22: .include with .lib and Subcircuit — Full PDK Flow
// =========================================================================
TEST_F(PDKLibTest, FullPDKFlowIncludeLibSubcircuit) {
    // Most realistic PDK flow:
    // 1. .lib selects model corner from a library file
    // 2. .include brings in cell definitions
    // 3. Top-level uses X instances of the cells
    // 4. Run DC and verify

    write_file("pdk/models.lib",
        ".lib tt\n"
        ".model NMOD NMOS LEVEL=14 VTH0=0.4 U0=0.04 TOXE=2e-9\n"
        ".model PMOD PMOS LEVEL=14 VTH0=-0.4 U0=0.02 TOXE=2e-9\n"
        ".endl tt\n");

    write_file("pdk/cells/inv.sp",
        ".subckt inv in out vdd vss wp=2u wn=1u l=100n\n"
        "Mp out in vdd vdd PMOD W={wp} L={l}\n"
        "Mn out in vss vss NMOD W={wn} L={l}\n"
        ".ends inv\n");

    write_file("pdk/cells/buf.sp",
        ".subckt buf in out vdd vss\n"
        "X1 in mid vdd vss inv\n"
        "X2 mid out vdd vss inv\n"
        ".ends buf\n");

    fs::path main_path = write_file("design.sp",
        "Full PDK flow test\n"
        ".lib pdk/models.lib tt\n"
        ".include pdk/cells/inv.sp\n"
        ".include pdk/cells/buf.sp\n"
        "VDD vdd 0 1.8\n"
        "VIN in 0 1.8\n"
        "Xbuf in out vdd 0 buf\n"
        "CL out 0 10f\n"
        ".op\n"
        ".end\n");

    NetlistParser parser;
    auto ckt = parser.parse_file(main_path.string());
    DCResult result = solve_dc(ckt);

    // input=1.8V (high) => buf output = high
    // inv1: in=1.8 => NMOS on => mid ~ 0
    // inv2: mid=0  => PMOS on => out ~ 1.8
    EXPECT_GT(result.voltage("out"), 1.4)
        << "Full PDK flow: buffer with high input should output high";
}

// =========================================================================
// Test 23: Simulator API with Subcircuit Netlist
// =========================================================================
TEST(PDKIntegration, SimulatorAPISubcircuit) {
    // Test using the high-level Simulator API with a subcircuit netlist.
    // Divider: R1 from in to out, R2 from out to ground.
    std::string netlist_text = wrap(R"(
.subckt rdiv in out r1=1k r2=1k
R1 in out {r1}
R2 out 0 {r2}
.ends rdiv

VIN inp 0 8.0
X1 inp outp rdiv r1=3k r2=1k
.op
)");

    Simulator sim;
    auto ckt = sim.parse(netlist_text);
    auto result = sim.run(ckt);

    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    // V(outp) = 8 * 1k/(3k+1k) = 2.0V
    EXPECT_NEAR(std::get<DCResult>(result.analysis).voltage("outp"), 2.0, 1e-2);
}
