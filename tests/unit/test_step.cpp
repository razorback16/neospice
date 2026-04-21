#include <gtest/gtest.h>
#include "api/neospice.hpp"

using namespace neospice;

TEST(StepSweep, SourceSweep) {
    Simulator sim;
    auto ckt = sim.load(std::string(TEST_CIRCUITS_DIR) + "/step_resistor.cir");
    auto result = sim.run(ckt);
    ASSERT_TRUE(result.step != nullptr);
    const auto& sr = *result.step;
    EXPECT_EQ(sr.step_values.size(), 6u); // 0,1,2,3,4,5
    EXPECT_DOUBLE_EQ(sr.step_values[0], 0.0);
    EXPECT_DOUBLE_EQ(sr.step_values[5], 5.0);

    // At V1=5V, V(out) should be 2.5V (voltage divider)
    ASSERT_TRUE(sr.results[5].dc.has_value());
    double vout = sr.results[5].dc->voltage("out");
    EXPECT_NEAR(vout, 2.5, 1e-6);

    // At V1=0V, V(out) should be 0V
    ASSERT_TRUE(sr.results[0].dc.has_value());
    double vout0 = sr.results[0].dc->voltage("out");
    EXPECT_NEAR(vout0, 0.0, 1e-6);
}

TEST(StepSweep, TempSweep) {
    Simulator sim;
    auto ckt = sim.parse(R"(
Temp sweep
R1 in 0 1k TC1=0.01
V1 in 0 1
.step temp 27 127 50
.op
.end
)");
    auto result = sim.run(ckt);
    ASSERT_TRUE(result.step != nullptr);
    const auto& sr = *result.step;
    EXPECT_EQ(sr.step_values.size(), 3u); // 27, 77, 127
    // Higher temp => higher resistance => lower current
    ASSERT_TRUE(sr.results[0].dc.has_value());
    ASSERT_TRUE(sr.results[2].dc.has_value());
}
