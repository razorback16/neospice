#include <gtest/gtest.h>
#include "core/dc.hpp"
#include "devices/device.hpp"
#include "devices/vsource.hpp"
#include "devices/resistor.hpp"
#include "devices/diode.hpp"

using namespace neospice;
using std::make_unique;

TEST(Convergence, DiodeDC) {
    Circuit ckt;
    auto n_top = ckt.node("top");
    auto n_mid = ckt.node("mid");
    DiodeModel dm;
    dm.Is = 1e-14;
    dm.N = 1.0;
    ckt.add_device(make_unique<VSource>("V1", n_top, GROUND_INTERNAL, 5.0));
    ckt.add_device(make_unique<Resistor>("R1", n_top, n_mid, 1000.0));
    ckt.add_device(make_unique<Diode>("D1", n_mid, GROUND_INTERNAL, dm));
    ckt.finalize();
    DCResult result = solve_dc(ckt);
    EXPECT_GT(result.node_voltages["v(mid)"], 0.5);
    EXPECT_LT(result.node_voltages["v(mid)"], 0.9);
    EXPECT_NEAR(result.node_voltages["v(top)"], 5.0, 1e-6);
}

TEST(Convergence, GminSteppingWorks) {
    // Same diode circuit - should converge via normal Newton or gmin stepping
    Circuit ckt;
    auto n_top = ckt.node("top");
    auto n_mid = ckt.node("mid");
    DiodeModel dm;
    dm.Is = 1e-14;
    dm.N = 1.0;
    ckt.add_device(make_unique<VSource>("V1", n_top, GROUND_INTERNAL, 0.7));
    ckt.add_device(make_unique<Resistor>("R1", n_top, n_mid, 100.0));
    ckt.add_device(make_unique<Diode>("D1", n_mid, GROUND_INTERNAL, dm));
    ckt.finalize();
    DCResult result = solve_dc(ckt);
    // Diode forward voltage should be around 0.5-0.7V
    EXPECT_GT(result.node_voltages["v(mid)"], 0.3);
    EXPECT_LT(result.node_voltages["v(mid)"], 0.75);
}
