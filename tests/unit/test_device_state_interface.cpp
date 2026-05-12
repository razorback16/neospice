#include <gtest/gtest.h>
#include "devices/device.hpp"
#include "devices/capacitor.hpp"
#include "devices/resistor.hpp"

using namespace neospice;

TEST(DeviceInterface, DefaultStateCountIsZero) {
    // Use Resistor as a simple stateless device (old Diode was also stateless)
    Resistor r("R1", 1, 0, 1000.0);
    EXPECT_EQ(0, r.state_vars());
    // Capacitor now has 2 state vars (charge, current) for state-vector integration
    Capacitor c("C1", 1, 0, 1e-9);
    EXPECT_EQ(2, c.state_vars());
}

TEST(DeviceInterface, SetStatePtrsDefaultIsNoop) {
    // Use Resistor as a simple stateless device
    Resistor r("R1", 1, 0, 1000.0);
    // Must not crash / throw for a stateless device when given nullptrs
    r.set_state_ptrs(nullptr, nullptr, nullptr, 0);
}
