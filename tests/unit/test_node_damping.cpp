#include <gtest/gtest.h>
#include "core/dc.hpp"
#include "core/circuit.hpp"
#include "core/neo_solver.hpp"
#include "core/newton.hpp"
#include "api/neospice.hpp"
#include "parser/netlist_parser.hpp"

using namespace neospice;

TEST(NodeDamping, HighGainComparatorConverges) {
    // CMOS inverter with large gain used as a comparator.
    // VDD=5V, input biased slightly below mid-supply so output is driven
    // toward VDD.  This circuit has a very high small-signal gain and
    // can produce large Newton voltage swings (>10V) during DC solve,
    // exactly the scenario that node damping is designed to handle.
    // The key requirement: the simulation must converge to a valid
    // operating point without failure.
    std::string netlist = R"(
High-Gain CMOS Comparator
VDD vdd 0 DC 5.0
VIN inp 0 DC 2.3
* CMOS inverter stage 1 (high gain)
MN1 out1 inp 0   0   NMOD W=10u L=1u
MP1 out1 inp vdd vdd PMOD W=20u L=1u
* CMOS inverter stage 2 (output buffer)
MN2 out  out1 0   0   NMOD W=10u L=1u
MP2 out  out1 vdd vdd PMOD W=20u L=1u
.model NMOD NMOS(LEVEL=1 VTO=0.7 KP=110u GAMMA=0.4 PHI=0.65)
.model PMOD PMOS(LEVEL=1 VTO=-0.7 KP=50u GAMMA=0.57 PHI=0.65)
.op
.end
)";
    Simulator sim;
    auto ckt = sim.parse(netlist);
    ckt.options.node_damping = true;
    DCResult result = sim.run_dc(ckt);
    double vout = result.voltage("out");
    // With VIN=2.3V (below NMOS threshold of mid-supply), inverter output
    // should be near VDD (high) or near 0V (low) — a valid rail state.
    EXPECT_TRUE(vout > 3.5 || vout < 1.5)
        << "Expected output near a rail (0 or VDD), got v(out)=" << vout;
}

TEST(NodeDamping, SimpleDiodeStillConverges) {
    std::string netlist = R"(
Node Damping Regression
V1 top 0 DC 5.0
R1 top mid 1k
D1 mid 0 DMOD
.model DMOD D(IS=1e-14 N=1)
.op
.end
)";
    Simulator sim;
    auto ckt = sim.parse(netlist);
    DCResult result = sim.run_dc(ckt);
    EXPECT_GT(result.voltage("mid"), 0.5);
    EXPECT_LT(result.voltage("mid"), 0.9);
}
