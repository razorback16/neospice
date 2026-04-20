#include <gtest/gtest.h>

#include "devices/bsim4v7/bsim4v7_shim.hpp"

#include <array>
#include <cmath>

using namespace neospice::bsim4v7;

namespace {

// Helper: build a Shim::Ckt pointing at three 2-slot state vectors
// (slot 0 = charge q, slot 1 = current i for one integration variable at
// qcap=0).  Caller seeds state0[0]/state1[0]/state2[0] with charge
// histories, we check what NIintegrate writes into state0[1].
struct StateFixture {
    std::array<double, 2> s0{};
    std::array<double, 2> s1{};
    std::array<double, 2> s2{};
    Shim::Ckt ckt;

    StateFixture() {
        ckt.CKTstate0 = s0.data();
        ckt.CKTstate1 = s1.data();
        ckt.CKTstate2 = s2.data();
    }
};

} // namespace

TEST(BSIM4v7NIintegrate, BackwardEulerOrder1) {
    // BE / Gear-1 with step h: ag[0] = 1/h, ag[1] = -1/h
    // state0[1] (= numerical current) should become ag[0]*q_new + ag[1]*q_old
    // geq = ag[0] * cap
    // ceq = state0[1] - geq * q_new
    StateFixture fx;
    const double h     = 1e-9;
    const double q_new = 1.5e-15;
    const double q_old = 1.0e-15;
    const double cap   = 1e-15;   // 1 fF

    fx.s0[0] = q_new;
    fx.s1[0] = q_old;
    fx.s2[0] = 0.0;              // unused at order 1

    fx.ckt.CKTorder = 1;
    fx.ckt.CKTag[0] =  1.0 / h;
    fx.ckt.CKTag[1] = -1.0 / h;

    double geq = -12345.0, ceq = -12345.0;
    int rc = Shim::NIintegrate(&fx.ckt, &geq, &ceq, cap, 0);
    EXPECT_EQ(rc, Shim::OK);

    const double expected_current = fx.ckt.CKTag[0] * q_new
                                  + fx.ckt.CKTag[1] * q_old;
    EXPECT_DOUBLE_EQ(fx.s0[1], expected_current);

    const double expected_geq = fx.ckt.CKTag[0] * cap;
    EXPECT_DOUBLE_EQ(geq, expected_geq);

    const double expected_ceq = expected_current - expected_geq * q_new;
    EXPECT_DOUBLE_EQ(ceq, expected_ceq);
}

TEST(BSIM4v7NIintegrate, Gear2SumsThreeHistories) {
    // Gear-2: CKTorder = 2, CKTintegrateMethod = 1 (Gear).
    // Synthetic coefficients (not a real Gear-2 formula — just distinct
    // non-zero values so we can verify the sum loop picks up each term).
    StateFixture fx;
    fx.s0[0] = 3.0;
    fx.s1[0] = 2.0;
    fx.s2[0] = 1.0;

    fx.ckt.CKTorder = 2;
    fx.ckt.CKTintegrateMethod = 1;  // Gear
    fx.ckt.CKTag[0] = 7.0;
    fx.ckt.CKTag[1] = 11.0;
    fx.ckt.CKTag[2] = 13.0;

    const double cap = 2.5e-15;
    double geq = 0.0, ceq = 0.0;
    int rc = Shim::NIintegrate(&fx.ckt, &geq, &ceq, cap, 0);
    EXPECT_EQ(rc, Shim::OK);

    const double expected_current =
        fx.ckt.CKTag[0] * fx.s0[0] +
        fx.ckt.CKTag[1] * fx.s1[0] +
        fx.ckt.CKTag[2] * fx.s2[0];
    EXPECT_DOUBLE_EQ(fx.s0[1], expected_current);
    EXPECT_DOUBLE_EQ(geq, fx.ckt.CKTag[0] * cap);
    EXPECT_DOUBLE_EQ(ceq, expected_current - geq * fx.s0[0]);
}

TEST(BSIM4v7NIintegrate, TrapezoidalOrder2) {
    // Trapezoidal order 2: deriv = -i_{n-1} + ag[0]*q_n + ag[1]*q_{n-1}
    StateFixture fx;
    const double h = 1e-9;
    fx.s0[0] = 3.0e-15;  // q_n
    fx.s1[0] = 2.0e-15;  // q_{n-1}
    fx.s1[1] = 0.5e-6;   // i_{n-1} (previous derivative)

    fx.ckt.CKTorder = 2;
    fx.ckt.CKTintegrateMethod = 0;  // Trapezoidal
    fx.ckt.CKTag[0] =  2.0 / h;
    fx.ckt.CKTag[1] = -2.0 / h;

    const double cap = 1e-15;
    double geq = 0.0, ceq = 0.0;
    int rc = Shim::NIintegrate(&fx.ckt, &geq, &ceq, cap, 0);
    EXPECT_EQ(rc, Shim::OK);

    const double expected_current =
        -fx.s1[1] + fx.ckt.CKTag[0] * fx.s0[0] + fx.ckt.CKTag[1] * fx.s1[0];
    EXPECT_DOUBLE_EQ(fx.s0[1], expected_current);
    EXPECT_DOUBLE_EQ(geq, fx.ckt.CKTag[0] * cap);
    EXPECT_DOUBLE_EQ(ceq, expected_current - geq * fx.s0[0]);
}

TEST(BSIM4v7NIintegrate, CapZeroIsBSIM4CallPattern) {
    // BSIM4 b4ld.c always passes cap=0.0 and consumes ceq as the pure
    // numerical current-from-charge (it multiplies the analytic
    // capacitance in externally).  Verify geq==0 in that case and
    // ceq equals the Gear sum.
    StateFixture fx;
    fx.s0[0] = 4.2e-16;
    fx.s1[0] = 4.0e-16;
    fx.ckt.CKTorder = 1;
    fx.ckt.CKTag[0] =  1e9;
    fx.ckt.CKTag[1] = -1e9;

    double geq = 1.0, ceq = 0.0;
    int rc = Shim::NIintegrate(&fx.ckt, &geq, &ceq, /*cap=*/0.0, 0);
    EXPECT_EQ(rc, Shim::OK);

    EXPECT_DOUBLE_EQ(geq, 0.0);
    const double expected = fx.ckt.CKTag[0] * fx.s0[0]
                          + fx.ckt.CKTag[1] * fx.s1[0];
    EXPECT_DOUBLE_EQ(ceq, expected);
    EXPECT_DOUBLE_EQ(fx.s0[1], expected);
}
