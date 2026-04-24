#include <gtest/gtest.h>
#include <algorithm>
#include "api/neospice.hpp"

using namespace neospice;

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

TEST(TransientResultAPI, DiffVoltage) {
    neospice::Simulator sim;
    std::string netlist = R"(
Diff test
V1 a 0 5
V2 b 0 3
R1 a 0 1k
R2 b 0 1k
.tran 1u 10u
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(result.transient.has_value());
    auto& tran = *result.transient;

    // diff() returns v(a) - v(b) at every time point
    auto d = tran.diff("a", "b");
    ASSERT_EQ(d.size(), tran.time.size());
    for (auto v : d)
        EXPECT_NEAR(v, 2.0, 1e-6);

    // signal_names() lists all available signals
    auto names = tran.signal_names();
    EXPECT_TRUE(std::find(names.begin(), names.end(), "v(a)") != names.end());
    EXPECT_TRUE(std::find(names.begin(), names.end(), "v(b)") != names.end());
}
