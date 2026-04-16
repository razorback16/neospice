#include <gtest/gtest.h>
#include "devices/bsim4v7/bsim4v7_eval.hpp"
using namespace neospice;

// In saturation (Vds > Vdsat), gds should be Ids / Va where Va is
// finite (~5-20V for a short-channel NMOS). Without VACLM/VADIBL
// our gds is near zero and the Ids/gds ratio blows up.
TEST(BSIM4v7VACLM, SaturationGdsReasonable) {
    BSIM4v7Params p{};
    p.VTH0 = 0.4; p.U0 = 0.04; p.TOXE = 2e-9;
    p.W = 1e-6; p.L = 100e-9; p.PCLM = 1.3;
    p.PDIBLC1 = 0.39; p.PDIBLC2 = 0.0086; p.DROUT = 0.56;
    p.VSAT = 1e5; p.DELTA = 0.01;
    // Keep subthreshold analytical branch inactive at this operating point.
    p.K1 = 0.5; p.NFACTOR = 1.0; p.NDEP = 1.7e17; p.XJ = 1.5e-7;
    p.A0 = 1.0; p.RDSW = 150.0;
    p.UA = 1e-9; p.UB = 1e-19;

    auto r = bsim4v7_evaluate(1.0, 1.8, 0.0, p, 300.0);

    EXPECT_GT(r.Ids, 1e-5);
    EXPECT_GT(r.gds, 0.0);
    // Va = Ids/gds should be in (1, 50) V for a 100nm device.
    double Va = r.Ids / r.gds;
    EXPECT_GT(Va, 1.0);
    EXPECT_LT(Va, 50.0);
}
