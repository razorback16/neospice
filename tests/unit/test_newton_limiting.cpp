#include <gtest/gtest.h>
#include "devices/bsim4v7/bsim4v7.hpp"
#include "core/circuit.hpp"
#include <vector>

using namespace neospice;

// A single BSIM4v7 device with an old solution where Vgs = 0.4V (at Vth)
// and a proposed new solution with Vgs = 2.0V (a 1.6V jump). limit_voltages
// must clamp this to at most 0.5V per step (since |Vgs_old - Vth| = 0,
// so max(0.5, 0.5*0) = 0.5).
TEST(BSIM4v7Limit, LargeVgsStepClamped) {
    Circuit ckt;
    int gate   = ckt.node("g");
    int drain  = ckt.node("d");
    int source = ckt.node("s");
    int bulk   = ckt.node("b");
    ckt.finalize();

    BSIM4v7Params p{};
    p.VTH0 = 0.4;
    p.U0   = 0.04;
    p.TOXE = 2e-9;
    p.W    = 1e-6;
    p.L    = 100e-9;

    BSIM4v7 dev("m1", drain, gate, source, bulk, p);

    std::vector<double> old_sol(ckt.num_vars(), 0.0);
    old_sol[gate]   = 0.4;  // Vgs = 0.4 (at Vth)
    old_sol[drain]  = 0.1;
    old_sol[source] = 0.0;
    old_sol[bulk]   = 0.0;

    std::vector<double> new_sol = old_sol;
    new_sol[gate] = 2.0;  // proposed Vgs = 2.0 -> delta 1.6V

    dev.limit_voltages(old_sol, new_sol);

    double delta = new_sol[gate] - old_sol[gate];
    EXPECT_LT(delta, 0.55);  // clamped, not the raw 1.6V
    EXPECT_GT(delta, 0.0);    // not reversed
}

// When the step is small (<= 0.5V), no clamping should occur.
TEST(BSIM4v7Limit, SmallVgsStepUnchanged) {
    Circuit ckt;
    int gate   = ckt.node("g");
    int drain  = ckt.node("d");
    int source = ckt.node("s");
    int bulk   = ckt.node("b");
    ckt.finalize();

    BSIM4v7Params p{};
    p.VTH0 = 0.4; p.U0 = 0.04; p.TOXE = 2e-9;
    p.W = 1e-6; p.L = 100e-9;

    BSIM4v7 dev("m1", drain, gate, source, bulk, p);

    std::vector<double> old_sol(ckt.num_vars(), 0.0);
    old_sol[gate] = 0.6;
    std::vector<double> new_sol = old_sol;
    new_sol[gate] = 0.8;  // 0.2V delta

    dev.limit_voltages(old_sol, new_sol);

    EXPECT_NEAR(new_sol[gate], 0.8, 1e-12);  // unchanged
}
