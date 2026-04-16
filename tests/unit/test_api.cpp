#include <gtest/gtest.h>
#include "api/cudaspice.hpp"

using namespace cudaspice;

TEST(SimulatorAPI, ParseAndRunDC) {
    Simulator sim;
    std::string netlist = R"(
Voltage Divider
V1 in 0 10
R1 in out 1k
R2 out 0 1k
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(result.dc.has_value());
    EXPECT_NEAR(result.dc->node_voltages["v(out)"], 5.0, 1e-6);
}

TEST(SimulatorAPI, RunTransient) {
    Simulator sim;
    std::string netlist = R"(
RC
V1 in 0 5
R1 in out 1k
C1 out 0 1u
.tran 10u 5m
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(result.transient.has_value());
    EXPECT_GT(result.transient->time.size(), 10u);
}

TEST(SimulatorAPI, RunAC) {
    Simulator sim;
    std::string netlist = R"(
RC Lowpass
V1 in 0 DC 0 AC 1
R1 in out 1k
C1 out 0 1n
.ac dec 10 100 10meg
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(result.ac.has_value());
    EXPECT_GT(result.ac->frequency.size(), 10u);
}
