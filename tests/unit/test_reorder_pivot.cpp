#include <gtest/gtest.h>
#include "core/dc.hpp"
#include "core/circuit.hpp"
#include "api/neospice.hpp"
#include "parser/netlist_parser.hpp"

using namespace neospice;

TEST(ReorderPivot, LargeImpedanceRatioConverges) {
    std::string netlist = R"(
Large Impedance Ratio
V1 vdd 0 DC 5.0
R_sense vdd mid 0.001
R_load mid out 100
R_feedback out 0 100MEG
D1 out 0 DMOD
.model DMOD D(IS=1e-14 N=1)
.op
.end
)";
    Simulator sim;
    auto ckt = sim.parse(netlist);
    DCResult result = sim.run_dc(ckt);
    EXPECT_NEAR(result.voltage("vdd"), 5.0, 1e-6);
}

TEST(ReorderPivot, RegressionSimpleDiode) {
    std::string netlist = R"(
Reorder Regression
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
