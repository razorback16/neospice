#include <gtest/gtest.h>
#include "devices/device.hpp"
#include "devices/diode.hpp"
#include "devices/capacitor.hpp"

using namespace neospice;

TEST(DeviceInterface, DefaultStateCountIsZero) {
    DiodeModel m;
    Diode d("D1", 1, 0, m);
    EXPECT_EQ(0, d.state_vars());
    Capacitor c("C1", 1, 0, 1e-9);
    EXPECT_EQ(0, c.state_vars());
}

TEST(DeviceInterface, SetStatePtrsDefaultIsNoop) {
    DiodeModel m;
    Diode d("D1", 1, 0, m);
    // Must not crash / throw for a stateless device when given nullptrs
    d.set_state_ptrs(nullptr, nullptr, nullptr, 0);
}
