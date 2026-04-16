#include <gtest/gtest.h>
#include "core/types.hpp"

TEST(Smoke, BuildWorks) {
    EXPECT_TRUE(true);
}

TEST(Smoke, ThermalVoltage) {
    double vt = cudaspice::thermal_voltage();
    EXPECT_NEAR(vt, 0.02586, 0.001);
}
