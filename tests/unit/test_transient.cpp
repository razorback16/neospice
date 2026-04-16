#include <gtest/gtest.h>
#include "core/transient.hpp"
#include "parser/netlist_parser.hpp"
#include <cmath>

using namespace cudaspice;

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

    EXPECT_NEAR(result.voltages["v(in)"].front(), 5.0, 0.01);

    // Find index closest to t=1ms
    int idx_1ms = 0;
    for (size_t i = 0; i < result.time.size(); ++i) {
        if (result.time[i] >= 1e-3) { idx_1ms = i; break; }
    }
    double expected_1tau = 5.0 * (1.0 - std::exp(-1.0));
    EXPECT_NEAR(result.voltages["v(out)"][idx_1ms], expected_1tau, 0.1);

    // At t=5ms (5tau): should be close to 5V
    EXPECT_NEAR(result.voltages["v(out)"].back(), 5.0, 0.05);
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
