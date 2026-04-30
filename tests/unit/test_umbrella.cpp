#include <gtest/gtest.h>
#include "neospice/neospice.hpp"

using namespace neospice;

TEST(UmbrellaHeader, CompileCheck) {
    Circuit ckt;
    Simulator sim;
    EXPECT_FALSE(ckt.is_finalized());
}

TEST(UmbrellaHeader, AllTypesAccessible) {
    NodeId n = GND;
    DevId d{0};
    ModelId m{0};
    (void)n; (void)d; (void)m;

    ACMode mode = ACMode::DEC;
    ConvergenceMethod cm = ConvergenceMethod::DIRECT;
    (void)mode; (void)cm;
}
