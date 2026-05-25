#include <gtest/gtest.h>
#include "core/dc.hpp"
#include "core/circuit.hpp"
#include "api/neospice.hpp"
#include "parser/netlist_parser.hpp"

using namespace neospice;

TEST(SrcFact, SourceSteppingStillWorks) {
    std::string netlist = R"(
CKTsrcFact Cross-Coupled
VDD vdd 0 DC 5.0
M1 qb q vdd vdd PMOD W=10u L=1u
M2 qb q 0   0   NMOD W=5u  L=1u
M3 q qb vdd vdd PMOD W=10u L=1u
M4 q qb 0   0   NMOD W=4u  L=1u
R1 q 0 100k
.model NMOD NMOS(LEVEL=1 VTO=0.7 KP=110u GAMMA=0.4 PHI=0.65)
.model PMOD PMOS(LEVEL=1 VTO=-0.7 KP=50u GAMMA=0.57 PHI=0.65)
.op
.end
)";
    Simulator sim;
    auto ckt = sim.parse(netlist);
    DCResult result = sim.run_dc(ckt);
    double vq  = result.voltage("q");
    double vqb = result.voltage("qb");
    bool state_a = (vq > 4.0 && vqb < 1.0);
    bool state_b = (vq < 1.0 && vqb > 4.0);
    EXPECT_TRUE(state_a || state_b)
        << "Expected latch to converge, got v(q)=" << vq << " v(qb)=" << vqb;
}

TEST(SrcFact, SimpleCircuitRegression) {
    std::string netlist = R"(
SrcFact Regression
V1 vdd 0 DC 3.3
R1 vdd out 1k
D1 out 0 DMOD
.model DMOD D(IS=1e-14 N=1)
.op
.end
)";
    Simulator sim;
    auto ckt = sim.parse(netlist);
    DCResult result = sim.run_dc(ckt);
    EXPECT_NEAR(result.voltage("vdd"), 3.3, 1e-6);
    EXPECT_GT(result.voltage("out"), 0.5);
}
