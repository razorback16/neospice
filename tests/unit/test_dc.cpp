#include <gtest/gtest.h>
#include "core/dc.hpp"
#include "devices/device.hpp"
#include "devices/vsource.hpp"
#include "devices/isource.hpp"
#include "devices/resistor.hpp"

using namespace neospice;
using std::make_unique;

TEST(DC, ResistorDivider) {
    Circuit ckt;
    int32_t n_top = static_cast<int32_t>(ckt.node("top"));
    int32_t n_mid = static_cast<int32_t>(ckt.node("mid"));
    ckt.add_device(make_unique<VSource>("V1", n_top, GROUND_INTERNAL, 10.0));
    ckt.add_device(make_unique<Resistor>("R1", n_top, n_mid, 1000.0));
    ckt.add_device(make_unique<Resistor>("R2", n_mid, GROUND_INTERNAL, 1000.0));
    ckt.finalize();
    DCResult result = solve_dc(ckt);
    EXPECT_NEAR(result.voltage("top"), 10.0, 1e-6);
    EXPECT_NEAR(result.voltage("mid"), 5.0, 1e-6);
}

TEST(DC, CurrentSourceWithResistor) {
    Circuit ckt;
    int32_t n1 = static_cast<int32_t>(ckt.node("n1"));
    // ISource convention: current flows from np to nn through source
    // np=GROUND, nn=n1 means current enters n1
    ckt.add_device(make_unique<ISource>("I1", GROUND_INTERNAL, n1, 0.001));
    ckt.add_device(make_unique<Resistor>("R1", n1, GROUND_INTERNAL, 2000.0));
    ckt.finalize();
    DCResult result = solve_dc(ckt);
    EXPECT_NEAR(result.voltage("n1"), 2.0, 1e-6);
}

TEST(DC, BranchCurrentReported) {
    Circuit ckt;
    int32_t n_top = static_cast<int32_t>(ckt.node("top"));
    int32_t n_mid = static_cast<int32_t>(ckt.node("mid"));
    ckt.add_device(make_unique<VSource>("V1", n_top, GROUND_INTERNAL, 10.0));
    ckt.add_device(make_unique<Resistor>("R1", n_top, n_mid, 1000.0));
    ckt.add_device(make_unique<Resistor>("R2", n_mid, GROUND_INTERNAL, 1000.0));
    ckt.finalize();
    DCResult result = solve_dc(ckt);
    // Branch current through V1 should be reported
    ASSERT_TRUE(result.branch_currents.count("i(v1)") > 0);
    // Current = 10V / 2000 ohms = 5mA
    EXPECT_NEAR(std::abs(result.current("v1")), 5e-3, 1e-9);
}
