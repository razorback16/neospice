#include <gtest/gtest.h>
#include "devices/bsim4v7/bsim4v7_eval.hpp"
#include <cmath>

using namespace neospice;

// With BSIM4 defaults DSUB=0.56, ETA0=0.08 and Leff=100nm, theta0vb0 is
// small enough that the DIBL Vth-shift at Vds=1.8V cannot exceed ~0.2V.
// The device must remain OFF at Vgs=0, Vds=1.8V (Ids < 1 nA).
TEST(BSIM4v7DIBLClamp, NmosOffAtVgsZeroHighVds) {
    BSIM4v7Params p{};
    p.VTH0  = 0.4;
    p.U0    = 0.04;
    p.TOXE  = 2e-9;
    p.W     = 1e-6;
    p.L     = 100e-9;
    p.nf    = 1.0;
    p.K1    = 0.5;
    p.NFACTOR = 1.0;
    p.NDEP  = 1.7e17;
    p.XJ    = 1.5e-7;
    p.A0    = 1.0;
    p.PCLM  = 1.3;
    p.ETA0  = 0.08;
    p.DSUB  = 0.56;

    auto r = bsim4v7_evaluate(0.0, 1.8, 0.0, p, 300.0);

    // Before the fix: Vth_eff ~ -0.75V, Ids > 1e-4 A (NMOS strongly on).
    // After the fix: Vth_eff > 0.15V, subthreshold leakage only.
    EXPECT_LT(r.Ids, 1e-9) << "NMOS should be OFF at Vgs=0, Vds=1.8V";
}

// At Vds=0, DIBL shift must vanish regardless of DSUB, ETA0.
TEST(BSIM4v7DIBLClamp, NoDIBLShiftAtZeroVds) {
    BSIM4v7Params p{};
    p.VTH0 = 0.4; p.U0 = 0.04; p.TOXE = 2e-9;
    p.W = 1e-6; p.L = 100e-9; p.nf = 1.0;
    p.K1 = 0.5; p.NFACTOR = 1.0; p.NDEP = 1.7e17; p.XJ = 1.5e-7;
    p.ETA0 = 0.08; p.DSUB = 0.56;

    // At Vds ≈ 0 and Vgs = VTH0, Ids should be a small strong-inversion
    // linear-region current (~nA-to-µA), not the runaway subthreshold
    // current that would indicate Vth was shifted by DIBL.
    auto r = bsim4v7_evaluate(0.4, 1e-4, 0.0, p, 300.0);
    EXPECT_LT(r.Ids, 1e-5);
}
