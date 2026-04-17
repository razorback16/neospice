#include <gtest/gtest.h>
#include "devices/bsim4v7/bsim4v7_shim.hpp"

using namespace neospice::bsim4v7;

TEST(BSIM4v7ShimCkt, HasLoadPathFields) {
    Shim::Ckt ckt;
    // State ring
    EXPECT_EQ(nullptr, ckt.CKTstate0);
    EXPECT_EQ(nullptr, ckt.CKTstate1);
    EXPECT_EQ(nullptr, ckt.CKTstate2);
    // Integrator coeffs (8 slots)
    EXPECT_EQ(0.0, ckt.CKTag[0]);
    EXPECT_EQ(0.0, ckt.CKTag[7]);
    // RHS pointers (bound by the device adapter per evaluate)
    EXPECT_EQ(nullptr, ckt.CKTrhs);
    EXPECT_EQ(nullptr, ckt.CKTrhsOld);
    // Matrix binding (null until adapter binds)
    EXPECT_EQ(nullptr, ckt.mat);
    // Order (Gear order; 1 = BE, 2 = trap/Gear2)
    EXPECT_EQ(1, ckt.CKTorder);
    // MODE flag values mirror UCB/ngspice cktdefs.h bit layout exactly
    EXPECT_EQ(0x1,    Shim::MODETRAN);
    EXPECT_EQ(0x2,    Shim::MODEAC);
    EXPECT_EQ(0x70,   Shim::MODEDC);
    EXPECT_EQ(0x10,   Shim::MODEDCOP);
    EXPECT_EQ(0x20,   Shim::MODETRANOP);
    EXPECT_EQ(0x40,   Shim::MODEDCTRANCURVE);
    EXPECT_EQ(0x100,  Shim::MODEINITFLOAT);
    EXPECT_EQ(0x200,  Shim::MODEINITJCT);
    EXPECT_EQ(0x400,  Shim::MODEINITFIX);
    EXPECT_EQ(0x800,  Shim::MODEINITSMSIG);
    EXPECT_EQ(0x1000, Shim::MODEINITTRAN);
    EXPECT_EQ(0x2000, Shim::MODEINITPRED);
    EXPECT_EQ(0x10000,Shim::MODEUIC);
    EXPECT_NE(0,      Shim::MODEBYPASS);
}
