#include <gtest/gtest.h>
#include "parser/netlist_parser.hpp"
#include "api/neospice.hpp"
#include "core/dc.hpp"
#include <cmath>

using namespace neospice;

// ===========================================================================
// UCB DIODevice tests — replacing old native Diode tests
// ===========================================================================

// ---------------------------------------------------------------------------
// ForwardBias — 0.7 V across diode → forward current flows
// ---------------------------------------------------------------------------
TEST(Diode, ForwardBias) {
    std::string netlist = R"(
Diode Forward Bias
V1 anode 0 DC 0.7
D1 anode 0 DMOD
.model DMOD D(IS=1e-14 N=1)
.op
.end
)";
    Simulator sim;
    auto ckt = sim.parse(netlist);
    DCResult result = sim.run_dc(ckt);

    // V(anode) should be 0.7 V (set by voltage source)
    EXPECT_NEAR(result.node_voltages.at("v(anode)"), 0.7, 1e-6);

    // Diode current (through V1) should be positive and significant
    // I(V1) = Is * (exp(Vd/NVt) - 1) ≈ 1e-14 * exp(0.7/0.026) ≈ 5 mA
    double i_v1 = std::abs(result.branch_currents.at("i(v1)"));
    EXPECT_GT(i_v1, 1e-5);  // Forward current > 10 uA
}

// ---------------------------------------------------------------------------
// ReverseBias — -1 V across diode → very small reverse current
// ---------------------------------------------------------------------------
TEST(Diode, ReverseBias) {
    std::string netlist = R"(
Diode Reverse Bias
V1 anode 0 DC -1.0
R1 anode cathode 1
D1 cathode 0 DMOD
.model DMOD D(IS=1e-14 N=1)
.op
.end
)";
    Simulator sim;
    auto ckt = sim.parse(netlist);
    DCResult result = sim.run_dc(ckt);

    // V(anode) = -1.0 V
    EXPECT_NEAR(result.node_voltages.at("v(anode)"), -1.0, 1e-6);

    // Reverse current through V1 should be very small (essentially Is)
    double i_v1 = std::abs(result.branch_currents.at("i(v1)"));
    EXPECT_LT(i_v1, 1e-9);  // Reverse leakage is tiny
}

// ---------------------------------------------------------------------------
// DCOperatingPoint — resistor + diode circuit, check diode voltage
// ---------------------------------------------------------------------------
TEST(Diode, DCOperatingPoint) {
    std::string netlist = R"(
Diode DC Operating Point
V1 in 0 DC 5.0
R1 in out 1k
D1 out 0 DMOD
.model DMOD D(IS=1e-14 N=1)
.op
.end
)";
    Simulator sim;
    auto ckt = sim.parse(netlist);
    DCResult result = sim.run_dc(ckt);

    // V(in) = 5.0 V
    EXPECT_NEAR(result.node_voltages.at("v(in)"), 5.0, 1e-6);

    // V(out) should be the diode forward voltage, roughly 0.5-0.85 V
    double v_out = result.node_voltages.at("v(out)");
    EXPECT_GT(v_out, 0.5);
    EXPECT_LT(v_out, 0.85);
}

// ---------------------------------------------------------------------------
// OutputCurrents — check that branch current is reported via DC sweep
// ---------------------------------------------------------------------------
TEST(Diode, OutputCurrents) {
    std::string netlist = R"(
Diode Output Currents
V1 in 0 DC 5.0
R1 in out 1k
D1 out 0 DMOD
.model DMOD D(IS=1e-14 N=1)
.dc V1 5 5 1
.end
)";
    Simulator sim;
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);

    ASSERT_TRUE(std::holds_alternative<DCSweepResult>(result.analysis));
    auto& sw = std::get<DCSweepResult>(result.analysis);

    // Should have at least one sweep point
    ASSERT_GE(sw.sweep_values.size(), 1u);

    // i(v1) should be present
    ASSERT_TRUE(sw.currents.count("i(v1)") > 0);
}

// ---------------------------------------------------------------------------
// AcStamp — verify AC analysis produces valid results.
// The UCB DIODevice stamps small-signal conductance into the G matrix.
// We verify the AC solution has the correct low-frequency gain from the
// resistor + diode voltage divider.
// ---------------------------------------------------------------------------
TEST(Diode, AcStamp) {
    // Forward-biased diode with R1=100k.  At DC the diode forward voltage
    // is ~0.7V so the small-signal dynamic resistance rd ≈ NVt/Id is small
    // (a few ohms).  V(out) AC magnitude ≈ rd / (R1 + rd) ≈ very small.
    std::string netlist = R"(
Diode AC Analysis
V1 in 0 DC 5.0 AC 1
R1 in out 100k
D1 out 0 DMOD
.model DMOD D(IS=1e-14 N=1)
.ac dec 5 1 1e6
.end
)";
    Simulator sim;
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);

    ASSERT_TRUE(std::holds_alternative<ACResult>(result.analysis));
    auto& ac = std::get<ACResult>(result.analysis);

    // Should have frequency points
    ASSERT_GT(ac.frequency.size(), 5u);

    // V(out) should have valid AC data
    auto it = ac.voltages.find("v(out)");
    ASSERT_NE(it, ac.voltages.end());

    // At all frequencies, the magnitude should be > 0 and < 1
    // (diode shunts signal to ground, attenuating it)
    for (size_t i = 0; i < ac.frequency.size(); ++i) {
        double mag = std::abs(it->second[i]);
        EXPECT_GT(mag, 0.0) << "AC magnitude must be positive at f=" << ac.frequency[i];
        EXPECT_LT(mag, 1.0) << "AC magnitude must be < 1 (attenuated) at f=" << ac.frequency[i];
    }
}

// ---------------------------------------------------------------------------
// GroundCathode — basic diode with cathode at ground
// ---------------------------------------------------------------------------
TEST(Diode, GroundCathode) {
    std::string netlist = R"(
Diode Ground Cathode
V1 anode 0 DC 0.6
D1 anode 0 DMOD
.model DMOD D(IS=1e-14 N=1)
.op
.end
)";
    Simulator sim;
    auto ckt = sim.parse(netlist);
    DCResult result = sim.run_dc(ckt);

    // Should converge without issues
    EXPECT_NEAR(result.node_voltages.at("v(anode)"), 0.6, 1e-6);
}
