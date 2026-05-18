#include <gtest/gtest.h>
#include "core/transient.hpp"
#include "parser/netlist_parser.hpp"
#include <cmath>

using namespace neospice;

TEST(Transient, RCStepResponse) {
    // RC circuit: V1=5V DC, R=1k, C=1uF -> tau = 1ms
    // .ic forces v(out)=0 so we see the charging curve
    std::string netlist = R"(
RC Step Response
V1 in 0 5
R1 in out 1k
C1 out 0 1u
.ic v(out)=0
.tran 10u 5m
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    auto result = solve_transient(ckt, 10e-6, 5e-3);

    EXPECT_NEAR(result.voltage("in").front(), 5.0, 0.01);

    // Find index nearest to t=1ms
    int idx_1ms = 0;
    double best_1ms = 1e30;
    for (size_t i = 0; i < result.time.size(); ++i) {
        double d = std::abs(result.time[i] - 1e-3);
        if (d < best_1ms) { best_1ms = d; idx_1ms = static_cast<int>(i); }
    }
    double expected_1tau = 5.0 * (1.0 - std::exp(-1.0));
    EXPECT_NEAR(result.voltage("out")[idx_1ms], expected_1tau, 0.1);

    // At t=5ms (5tau): should be close to 5V
    EXPECT_NEAR(result.voltage("out").back(), 5.0, 0.05);
}

TEST(Transient, PulseSourceCompletes) {
    // PULSE source should use HARD breakpoints — verify simulation completes
    std::string netlist = R"(
PULSE breakpoint test
V1 in 0 PULSE(0 5 0 1n 1n 5u 10u)
R1 in out 1k
C1 out 0 100p
.tran 100n 50u
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    auto result = solve_transient(ckt, 100e-9, 50e-6);
    EXPECT_GT(result.time.size(), 10u);
    EXPECT_NEAR(result.time.back(), 50e-6, 1e-9);
}

TEST(Transient, SinSourceCompletes) {
    // SIN source should use SOFT breakpoints — verify simulation completes
    std::string netlist = R"(
SIN breakpoint test
V1 in 0 SIN(0 5 1MEG)
R1 in out 1k
C1 out 0 100p
.tran 100n 10u
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    auto result = solve_transient(ckt, 100e-9, 10e-6);
    EXPECT_GT(result.time.size(), 10u);
    EXPECT_NEAR(result.time.back(), 10e-6, 1e-9);
}

TEST(Transient, CustomRestartStepScale) {
    // Verify custom restart_step_scale is parsed and simulation completes
    std::string netlist = R"(
Custom restart_step_scale
V1 in 0 PULSE(0 5 0 1n 1n 5u 10u)
R1 in out 1k
C1 out 0 100p
.options restart_step_scale=0.05
.tran 100n 50u
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    EXPECT_DOUBLE_EQ(ckt.options.restart_step_scale, 0.05);
    auto result = solve_transient(ckt, 100e-9, 50e-6);
    EXPECT_GT(result.time.size(), 10u);
    EXPECT_NEAR(result.time.back(), 50e-6, 1e-9);
}

TEST(Transient, ResultHasTimeVector) {
    std::string netlist = R"(
Simple
V1 in 0 5
R1 in 0 1k
.tran 1u 10u
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    auto result = solve_transient(ckt, 1e-6, 10e-6);

    EXPECT_GE(result.time.size(), 10u);
    EXPECT_NEAR(result.time.front(), 0.0, 1e-12);
    EXPECT_NEAR(result.time.back(), 10e-6, 1e-6);
}
