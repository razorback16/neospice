#include <gtest/gtest.h>
#include "devices/bsim4v7/bsim4v7_eval.hpp"
#include <cmath>

using namespace neospice;

// At Vgs=0 (deep subthreshold) with a nominal BSIM4v7 NMOS parameter set,
// gm must be > 0.  The FD path returns ~0 here because Ids ~ 1e-17 and
// h_fd = 1e-4 produces catastrophic cancellation.
TEST(BSIM4v7Subthreshold, GmNonZeroAtZeroBias) {
    BSIM4v7Params p{};
    p.VTH0  = 0.4;
    p.U0    = 0.04;
    p.TOXE  = 2e-9;
    p.W     = 1e-6;
    p.L     = 100e-9;
    p.nf    = 1.0;
    p.K1    = 0.5;
    p.NFACTOR = 1.0;
    p.NDEP  = 1.7e17;  // cm^-3
    p.XJ    = 1.5e-7;
    p.A0    = 1.0;
    p.AGS   = 0.0;
    p.B0    = 0.0;
    p.B1    = 0.0;
    p.KETA  = 0.0;
    p.RDSW  = 150.0;
    p.RDSWMIN = 0.0;
    p.PRWG  = 0.0;
    p.PRWB  = 0.0;
    p.UA    = 1e-9;
    p.UB    = 1e-19;
    p.VSAT  = 1e5;
    p.DELTA = 0.01;
    p.PCLM  = 1.3;
    p.ETA0  = 0.0;
    p.DSUB  = 0.0;

    auto r = bsim4v7_evaluate(0.0, 0.1, 0.0, p, 300.0);

    EXPECT_GT(r.Ids, 0.0);
    EXPECT_GT(r.gm, 0.0);
    EXPECT_GT(r.gds, 0.0);

    // Sanity: gm/Ids should be ~1/(n·Vt), i.e. in the range [5, 100] V^-1.
    double gm_over_Ids = r.gm / r.Ids;
    EXPECT_GT(gm_over_Ids, 5.0);
    EXPECT_LT(gm_over_Ids, 100.0);
}

// Above threshold (Vgs=1.0), the analytical branch must NOT kick in —
// strong-inversion gm/Ids ratio is much smaller (~ 1/Vgst ~ 1.5 V^-1).
TEST(BSIM4v7Subthreshold, AboveThresholdUnchanged) {
    BSIM4v7Params p{};
    p.VTH0 = 0.4; p.U0 = 0.04; p.TOXE = 2e-9;
    p.W = 1e-6; p.L = 100e-9; p.nf = 1.0;
    p.K1 = 0.5; p.NFACTOR = 1.0; p.NDEP = 1.7e17; p.XJ = 1.5e-7;
    p.A0 = 1.0; p.RDSW = 150.0; p.UA = 1e-9; p.UB = 1e-19;
    p.VSAT = 1e5; p.DELTA = 0.01; p.PCLM = 1.3;

    auto r = bsim4v7_evaluate(1.0, 1.0, 0.0, p, 300.0);
    EXPECT_GT(r.Ids, 1e-6);
    EXPECT_GT(r.gm, 1e-6);
    EXPECT_LT(r.gm / r.Ids, 10.0);
}
