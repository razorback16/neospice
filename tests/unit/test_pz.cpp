#include <gtest/gtest.h>
#include "api/neospice.hpp"
#include <cmath>

using namespace neospice;

TEST(PoleZero, RCLowpassPole) {
    // Simple RC lowpass: pole at s = -1/(RC) = -1/(1k * 1u) = -1000 rad/s
    Simulator sim;
    auto ckt = sim.parse(R"(
RC lowpass PZ test
V1 in 0 AC 1
R1 in out 1k
C1 out 0 1u
.pz in 0 out 0 vol pz
.end
)");
    auto result = sim.run(ckt);
    ASSERT_TRUE(result.pz.has_value());
    ASSERT_GE(result.pz->poles.size(), 1u);
    // Expect a pole near s = -1000
    bool found_pole = false;
    for (auto& p : result.pz->poles) {
        if (std::abs(p.real() + 1000.0) < 100.0 && std::abs(p.imag()) < 100.0) {
            found_pole = true;
            break;
        }
    }
    EXPECT_TRUE(found_pole) << "Expected pole near s=-1000 rad/s";
}

TEST(PoleZero, PZParser) {
    Simulator sim;
    auto ckt = sim.parse(R"(
PZ parser test
V1 in 0 1
R1 in out 1k
.pz in 0 out 0 vol pol
.end
)");
    ASSERT_EQ(ckt.analyses.size(), 1u);
    EXPECT_EQ(ckt.analyses[0].type, AnalysisCommand::PZ);
    EXPECT_EQ(ckt.analyses[0].pz_in_pos, "in");
    EXPECT_EQ(ckt.analyses[0].pz_in_neg, "0");
    EXPECT_EQ(ckt.analyses[0].pz_out_pos, "out");
    EXPECT_EQ(ckt.analyses[0].pz_out_neg, "0");
    EXPECT_EQ(ckt.analyses[0].pz_transfer, PZTransferType::VOLTAGE);
    EXPECT_EQ(ckt.analyses[0].pz_type, PZType::POLES);
}

TEST(PoleZero, PZParserCurrent) {
    Simulator sim;
    auto ckt = sim.parse(R"(
PZ parser current test
V1 in 0 1
R1 in out 1k
.pz in 0 out 0 cur zer
.end
)");
    ASSERT_EQ(ckt.analyses.size(), 1u);
    EXPECT_EQ(ckt.analyses[0].pz_transfer, PZTransferType::CURRENT);
    EXPECT_EQ(ckt.analyses[0].pz_type, PZType::ZEROS);
}

TEST(PoleZero, PZParserBoth) {
    Simulator sim;
    auto ckt = sim.parse(R"(
PZ parser both test
V1 in 0 1
R1 in out 1k
.pz in 0 out 0 vol pz
.end
)");
    ASSERT_EQ(ckt.analyses.size(), 1u);
    EXPECT_EQ(ckt.analyses[0].pz_type, PZType::BOTH);
}

TEST(PoleZero, PZParserError) {
    Simulator sim;
    // Too few arguments
    EXPECT_THROW(sim.parse(R"(
PZ error test
V1 in 0 1
R1 in out 1k
.pz in 0 out 0 vol
.end
)"), ParseError);
}

TEST(PoleZero, PolesOnlyMode) {
    // When requesting only poles, zeros vector should be empty
    Simulator sim;
    auto ckt = sim.parse(R"(
RC lowpass poles only
V1 in 0 AC 1
R1 in out 1k
C1 out 0 1u
.pz in 0 out 0 vol pol
.end
)");
    auto result = sim.run(ckt);
    ASSERT_TRUE(result.pz.has_value());
    EXPECT_GE(result.pz->poles.size(), 1u);
    EXPECT_TRUE(result.pz->zeros.empty());
    EXPECT_EQ(result.pz->type, PZType::POLES);
}
