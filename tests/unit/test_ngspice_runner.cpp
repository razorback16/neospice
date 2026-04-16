#include <gtest/gtest.h>
#include "framework/ngspice_runner.hpp"
#include <cstdlib>

using namespace cudaspice;

TEST(NgspiceRunner, DISABLED_RunDC) {
    NgspiceRunner ngspice(NGSPICE_BINARY);
    auto result = ngspice.run_dc(std::string(TEST_CIRCUITS_DIR) + "/resistor_divider.cir");
    EXPECT_FALSE(result.node_voltages.empty());
}
