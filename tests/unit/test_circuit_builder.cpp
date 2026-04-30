#include <gtest/gtest.h>
#include "api/neospice.hpp"

using namespace neospice;

TEST(CircuitBuilderMigrated, ResistorDividerDC) {
    Simulator sim;
    auto ckt = sim.parse(R"(
Resistor Divider
V1 in 0 DC 10
R1 in out 1k
R2 out 0 1k
.op
.end
)");

    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    EXPECT_NEAR(std::get<DCResult>(result.analysis).voltage("out"), 5.0, 1e-6);
}

TEST(CircuitBuilderMigrated, RCTransient) {
    Simulator sim;
    auto ckt = sim.parse(R"(
RC Transient
V1 in 0 DC 5
R1 in out 1k
C1 out 0 1u
.tran 10u 5m
.end
)");

    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<TransientResult>(result.analysis));
    auto& tran = std::get<TransientResult>(result.analysis);
    EXPECT_GT(tran.time.size(), 10u);
    EXPECT_NEAR(tran.voltage("out").back(), 5.0, 0.01);
}

TEST(CircuitBuilderMigrated, ACLowpass) {
    Simulator sim;
    auto ckt = sim.parse(R"(
AC Lowpass
V1 in 0 DC 0 AC 1
R1 in out 1k
C1 out 0 1n
.ac dec 10 100 10meg
.end
)");

    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<ACResult>(result.analysis));
    auto& ac = std::get<ACResult>(result.analysis);
    EXPECT_GT(ac.frequency.size(), 10u);
    auto gain = ac.magnitude_db("out");
    EXPECT_NEAR(gain.front(), 0.0, 1.0);
    EXPECT_LT(gain.back(), -20.0);
}

TEST(CircuitBuilderMigrated, PulseSource) {
    Simulator sim;
    auto ckt = sim.parse(R"(
Pulse Source
V1 in 0 PULSE(0 5 0 1n 1n 1u 2u)
R1 in 0 1k
.tran 10n 10u
.end
)");

    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<TransientResult>(result.analysis));
}

TEST(CircuitBuilderMigrated, DiodeWithModel) {
    Simulator sim;
    auto ckt = sim.parse(R"(
Diode With Model
V1 in 0 DC 0.7
D1 in 0 DMOD
.model DMOD D(IS=1e-14 N=1)
.op
.end
)");

    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
}

TEST(CircuitBuilderMigrated, RawLine) {
    Simulator sim;
    auto ckt = sim.parse(R"(
Raw Line
V1 in 0 10
R1 in out 1k
R2 out 0 1k
.op
.end
)");

    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    EXPECT_NEAR(std::get<DCResult>(result.analysis).voltage("out"), 5.0, 1e-6);
}

TEST(CircuitBuilderMigrated, ISourceAndInductor) {
    Simulator sim;
    auto ckt = sim.parse(R"(
ISource And Inductor
I1 0 in DC 1m
R1 in 0 1k
L1 in out 1m
R2 out 0 1k
.op
.end
)");

    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    auto& dc = std::get<DCResult>(result.analysis);
    EXPECT_NEAR(dc.voltage("in"), dc.voltage("out"), 1e-6);
}

TEST(CircuitBuilderMigrated, SinSource) {
    Simulator sim;
    auto ckt = sim.parse(R"(
Sin Source
V1 in 0 SIN(0 1 1k)
R1 in 0 1k
.tran 1u 2m
.end
)");

    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<TransientResult>(result.analysis));
    auto& tran = std::get<TransientResult>(result.analysis);
    EXPECT_GT(tran.time.size(), 10u);
}
