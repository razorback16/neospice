#include <gtest/gtest.h>
#include "core/dc.hpp"
#include "api/neospice.hpp"
#include "parser/netlist_parser.hpp"

using namespace neospice;

TEST(Convergence, DiodeDC) {
    std::string netlist = R"(
Diode Convergence Test
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
    EXPECT_GT(result.node_voltages["v(mid)"], 0.5);
    EXPECT_LT(result.node_voltages["v(mid)"], 0.9);
    EXPECT_NEAR(result.node_voltages["v(top)"], 5.0, 1e-6);
}

TEST(Convergence, GminSteppingWorks) {
    // Same diode circuit - should converge via normal Newton or gmin stepping
    std::string netlist = R"(
Gmin Stepping Test
V1 top 0 DC 0.7
R1 top mid 100
D1 mid 0 DMOD
.model DMOD D(IS=1e-14 N=1)
.op
.end
)";
    Simulator sim;
    auto ckt = sim.parse(netlist);
    DCResult result = sim.run_dc(ckt);
    // Diode forward voltage should be around 0.5-0.7V
    EXPECT_GT(result.node_voltages["v(mid)"], 0.3);
    EXPECT_LT(result.node_voltages["v(mid)"], 0.75);
}
