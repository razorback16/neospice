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
    ASSERT_TRUE(std::holds_alternative<PZResult>(result.analysis));
    ASSERT_GE(std::get<PZResult>(result.analysis).poles.size(), 1u);
    // Expect a pole near s = -1000
    bool found_pole = false;
    for (auto& p : std::get<PZResult>(result.analysis).poles) {
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
    ASSERT_TRUE(std::holds_alternative<PZCmd>(ckt.analyses[0]));
    auto& pz = std::get<PZCmd>(ckt.analyses[0]);
    EXPECT_EQ(pz.in_pos, "in");
    EXPECT_EQ(pz.in_neg, "0");
    EXPECT_EQ(pz.out_pos, "out");
    EXPECT_EQ(pz.out_neg, "0");
    EXPECT_EQ(pz.transfer, PZTransferType::VOLTAGE);
    EXPECT_EQ(pz.type, PZType::POLES);
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
    ASSERT_TRUE(std::holds_alternative<PZCmd>(ckt.analyses[0]));
    auto& pz = std::get<PZCmd>(ckt.analyses[0]);
    EXPECT_EQ(pz.transfer, PZTransferType::CURRENT);
    EXPECT_EQ(pz.type, PZType::ZEROS);
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
    ASSERT_TRUE(std::holds_alternative<PZCmd>(ckt.analyses[0]));
    EXPECT_EQ(std::get<PZCmd>(ckt.analyses[0]).type, PZType::BOTH);
}

TEST(PoleZero, PZParserError) {
    Simulator sim;
    // Too few arguments
    EXPECT_NO_THROW(sim.parse(R"(
PZ error test
V1 in 0 1
R1 in out 1k
.pz in 0 out 0 vol
.end
)"));
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
    ASSERT_TRUE(std::holds_alternative<PZResult>(result.analysis));
    EXPECT_GE(std::get<PZResult>(result.analysis).poles.size(), 1u);
    EXPECT_TRUE(std::get<PZResult>(result.analysis).zeros.empty());
    EXPECT_EQ(std::get<PZResult>(result.analysis).type, PZType::POLES);
}
