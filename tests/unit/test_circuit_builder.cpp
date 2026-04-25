#include <gtest/gtest.h>
#include "api/circuit_builder.hpp"
#include "api/neospice.hpp"

using namespace neospice;

TEST(CircuitBuilder, ResistorDividerDC) {
    auto ckt = CircuitBuilder()
        .title("Resistor Divider")
        .vsource("V1", "in", "0", {.dc = 10.0})
        .resistor("R1", "in", "out", 1e3)
        .resistor("R2", "out", "0", 1e3)
        .raw_line(".op")
        .build();

    Simulator sim;
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    EXPECT_NEAR(std::get<DCResult>(result.analysis).voltage("out"), 5.0, 1e-6);
}

TEST(CircuitBuilder, RCTransient) {
    auto ckt = CircuitBuilder()
        .vsource("V1", "in", "0", {.dc = 5.0})
        .resistor("R1", "in", "out", 1e3)
        .capacitor("C1", "out", "0", 1e-6)
        .raw_line(".tran 10u 5m")
        .build();

    Simulator sim;
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<TransientResult>(result.analysis));
    auto& tran = std::get<TransientResult>(result.analysis);
    EXPECT_GT(tran.time.size(), 10u);
    EXPECT_NEAR(tran.voltage("out").back(), 5.0, 0.01);
}

TEST(CircuitBuilder, ACLowpass) {
    auto ckt = CircuitBuilder()
        .vsource("V1", "in", "0", {.dc = 0.0, .ac_mag = 1.0})
        .resistor("R1", "in", "out", 1e3)
        .capacitor("C1", "out", "0", 1e-9)
        .raw_line(".ac dec 10 100 10meg")
        .build();

    Simulator sim;
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<ACResult>(result.analysis));
    auto& ac = std::get<ACResult>(result.analysis);
    EXPECT_GT(ac.frequency.size(), 10u);
    auto gain = ac.magnitude_db("out");
    EXPECT_NEAR(gain.front(), 0.0, 1.0);
    EXPECT_LT(gain.back(), -20.0);
}

TEST(CircuitBuilder, PulseSource) {
    auto ckt = CircuitBuilder()
        .vsource_pulse("V1", "in", "0", {.v1 = 0, .v2 = 5, .td = 0,
                                          .tr = 1e-9, .tf = 1e-9,
                                          .pw = 1e-6, .per = 2e-6})
        .resistor("R1", "in", "0", 1e3)
        .raw_line(".tran 10n 10u")
        .build();

    Simulator sim;
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<TransientResult>(result.analysis));
}

TEST(CircuitBuilder, DiodeWithModel) {
    auto ckt = CircuitBuilder()
        .vsource("V1", "in", "0", {.dc = 0.7})
        .diode("D1", "in", "0", "DMOD")
        .model("DMOD", "D", {{"IS", 1e-14}, {"N", 1.0}})
        .raw_line(".op")
        .build();

    Simulator sim;
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
}

TEST(CircuitBuilder, RawLine) {
    auto ckt = CircuitBuilder()
        .raw_line("V1 in 0 10")
        .raw_line("R1 in out 1k")
        .raw_line("R2 out 0 1k")
        .raw_line(".op")
        .build();

    Simulator sim;
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    EXPECT_NEAR(std::get<DCResult>(result.analysis).voltage("out"), 5.0, 1e-6);
}

TEST(CircuitBuilder, ISourceAndInductor) {
    auto ckt = CircuitBuilder()
        .isource("I1", "0", "in", {.dc = 1e-3})
        .resistor("R1", "in", "0", 1e3)
        .inductor("L1", "in", "out", 1e-3)
        .resistor("R2", "out", "0", 1e3)
        .raw_line(".op")
        .build();

    Simulator sim;
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    auto& dc = std::get<DCResult>(result.analysis);
    EXPECT_NEAR(dc.voltage("in"), dc.voltage("out"), 1e-6);
}

TEST(CircuitBuilder, SinSource) {
    auto ckt = CircuitBuilder()
        .vsource_sin("V1", "in", "0", {.vo = 0, .va = 1, .freq = 1e3})
        .resistor("R1", "in", "0", 1e3)
        .raw_line(".tran 1u 2m")
        .build();

    Simulator sim;
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<TransientResult>(result.analysis));
    auto& tran = std::get<TransientResult>(result.analysis);
    EXPECT_GT(tran.time.size(), 10u);
}
