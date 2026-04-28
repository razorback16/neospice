#include <gtest/gtest.h>
#include "framework/ngspice_runner.hpp"
#include <cstdlib>

using namespace neospice;

TEST(NgspiceRunner, RunDC) {
    NgspiceRunner ngspice;
    auto result = ngspice.run_dc(std::string(TEST_CIRCUITS_DIR) + "/resistor_divider.cir");
    EXPECT_FALSE(result.node_voltages.empty());
}
