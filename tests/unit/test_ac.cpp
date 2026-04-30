#include <gtest/gtest.h>
#include "core/ac.hpp"
#include "parser/netlist_parser.hpp"
#include <cmath>

using namespace neospice;

TEST(AC, RCLowpass) {
    // RC lowpass: R=1k, C=1nF -> fc = 1/(2*pi*RC) ~ 159kHz
    std::string netlist = R"(
RC Lowpass
V1 in 0 DC 0 AC 1
R1 in out 1k
C1 out 0 1n
.ac dec 10 100 10meg
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    auto result = solve_ac(ckt, AnalysisCommand::DEC, 10, 100.0, 10e6);

    EXPECT_FALSE(result.frequency.empty());
    EXPECT_FALSE(result.voltage("out").empty());

    // Find frequency closest to fc ~ 159kHz
    double fc = 1.0 / (2.0 * M_PI * 1e3 * 1e-9);
    int idx_fc = 0;
    double min_diff = 1e20;
    for (size_t i = 0; i < result.frequency.size(); ++i) {
        double diff = std::abs(result.frequency[i] - fc);
        if (diff < min_diff) { min_diff = diff; idx_fc = static_cast<int>(i); }
    }

    double mag_at_fc = std::abs(result.voltage("out")[idx_fc]);
    EXPECT_NEAR(mag_at_fc, 1.0 / std::sqrt(2.0), 0.05);

    // At low frequency: magnitude ~ 1
    EXPECT_NEAR(std::abs(result.voltage("out").front()), 1.0, 0.01);
}

TEST(AC, ResultHasFrequencyVector) {
    std::string netlist = R"(
Simple
V1 in 0 DC 0 AC 1
R1 in 0 1k
.ac dec 5 1 1meg
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    auto result = solve_ac(ckt, AnalysisCommand::DEC, 5, 1.0, 1e6);

    // DEC mode: 5 points per decade, 6 decades (1 to 1MHz) -> 30 points + 1
    EXPECT_GE(result.frequency.size(), 30u);
}
