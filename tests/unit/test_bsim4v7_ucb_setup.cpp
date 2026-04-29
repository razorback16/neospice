// T12: Golden preprocessing test for BSIM4v7 UCB Z-port Phase 1a.
//
// Exercises the full preprocessing chain on a known NMOS model card and
// asserts that selected scalar fields on the instance (and on the allocated
// size-dependent parameter struct pParam) match ngspice-derived goldens to
// tight tolerance. See tests/goldens/bsim4v7_nmos_setup.json for the
// authoritative values and how they were obtained.

#include <gtest/gtest.h>
#include "devices/bsim4v7/bsim4v7_def.hpp"
#include "devices/bsim4v7/bsim4v7_shim.hpp"
#include "core/matrix.hpp"
#include <cmath>
#include <cstdlib>

using namespace neospice::bsim4v7;

namespace {

struct ModelFixture {
    BSIM4v7Model    model{};
    BSIM4v7Instance inst{};
    Shim::Ckt       ckt{};
    // Matrix size need only cover the node IDs the setup will allocate;
    // BSIM4 adds up to ~10 internal nodes depending on options. 64 is
    // a comfortable headroom for this minimal instance.
    neospice::SparsityBuilder builder{64};
    Shim::Matrix    matrix;
    int             states = 0;

    ModelFixture() : matrix(builder) {
        // --- Model card: NMOD NMOS LEVEL=14 VTH0=0.4 U0=0.04 TOXE=2e-9 ---
        // Match BSIM4v7Device::make() — stamp the real kernel version so
        // BSIM4v7checkModel doesn't fire its "wrong version number" warning.
        model.BSIM4v7version = "4.7.0";
        model.BSIM4v7versionGiven = 1;
        model.BSIM4v7modName = "NMOD";
        model.BSIM4v7type        = NMOS;
        model.BSIM4v7typeGiven   = 1;
        model.BSIM4v7mobMod      = 0;
        model.BSIM4v7capMod      = 2;
        model.BSIM4v7rdsMod      = 0;
        model.BSIM4v7vth0        = 0.4;   model.BSIM4v7vth0Given = 1;
        model.BSIM4v7u0          = 0.04;  model.BSIM4v7u0Given   = 1;
        model.BSIM4v7toxe        = 2e-9;  model.BSIM4v7toxeGiven = 1;

        // --- Instance: M1 W=1u L=100n, NF=1 ---
        inst.BSIM4v7name     = "M1";
        inst.BSIM4v7w  = 1e-6;  inst.BSIM4v7wGiven  = 1;
        inst.BSIM4v7l  = 1e-7;  inst.BSIM4v7lGiven  = 1;
        inst.BSIM4v7nf = 1.0;   inst.BSIM4v7nfGiven = 1;

        // Minimal topology: D=1, G=2, S=0 (ground), B=3.
        inst.BSIM4v7dNode    = 1; inst.BSIM4v7gNodeExt = 2;
        inst.BSIM4v7sNode    = 0; inst.BSIM4v7bNode    = 3;

        inst.BSIM4v7modPtr       = &model;
        inst.BSIM4v7nextInstance = nullptr;
        model.BSIM4v7instances   = &inst;
        model.BSIM4v7nextModel   = nullptr;

        // --- Circuit state: T=300.15 K (27 C), matches ngspice default ---
        ckt.CKTtemp    = 300.15;
        ckt.CKTnomTemp = 300.15;
    }

    ~ModelFixture() {
        // BSIM4v7temp mallocs bsim4SizeDependParam nodes and threads them onto
        // model.pSizeDependParamKnot. BSIM4v7Model is a POD, so release here.
        auto *p = model.pSizeDependParamKnot;
        while (p) {
            auto *next = p->pNext;
            std::free(p);
            p = next;
        }
    }
};

// Goldens extracted from a patched ngspice (BSIM4.8.0 kernel, b4temp.c patched to
// emit pParam->BSIM4* and here->BSIM4* fields as T12_GOLDEN stderr lines
// after BSIM4v7temp completes). See tests/goldens/bsim4v7_nmos_setup.json.
//
// NOTE: leff/weff/cdep0/k1ox/phi live on pParam (bsim4SizeDependParam).
//       vth0/u0temp/vfb/vsattemp on the instance are copies of pParam
//       values with stress-effect deltas applied — they are equal to
//       pParam values here because no stress parameters are set.
//
// These are the preprocessing fields whose formulas are identical in
// BSIM4 4.7.0 (our port) and 4.8.0 (ngspice 42 / local tree) for this
// minimal model card, so the cross-version comparison is meaningful.

namespace golden {
    constexpr double leff     =  1.00000000000000009e-07;
    constexpr double weff     =  9.99999999999999955e-07;
    constexpr double vth0     =  4.00000000000000022e-01;
    constexpr double u0temp   =  4.00000000000000008e-02;
    constexpr double vfb      = -6.51465384484832377e-01;
    constexpr double vsattemp =  8.00000000000000000e+04;
    constexpr double k1ox     =  2.54357049852350026e-01;
    constexpr double cdep0    =  1.31088134194791168e-03;
    constexpr double phi      =  8.20995507725037266e-01;

    constexpr double rel_tol  = 1e-10;
}

static inline double atol_for(double g) {
    using std::abs;
    // Mix of relative and a small absolute floor for values close to zero.
    return std::max(abs(g) * golden::rel_tol, 1e-20);
}

TEST(BSIM4v7UCBSetup, NmosPreprocChainMatchesNgspice) {
    ModelFixture f;

    // UCB ordering: BSIM4v7setup populates default model params and allocates
    // per-instance pParam; BSIM4v7temp then fills it with temperature-adjusted
    // values and internally calls BSIM4v7checkModel.
    ASSERT_EQ(0, BSIM4v7setup(&f.matrix, &f.model, &f.ckt, &f.states));
    ASSERT_EQ(0, BSIM4v7temp (&f.model, &f.ckt));

    ASSERT_NE(f.inst.pParam, nullptr);
    const auto *p = f.inst.pParam;

    // Geometry: LINT=WINT=0 by default, so effective = drawn.
    EXPECT_NEAR(p->BSIM4v7leff, golden::leff, atol_for(golden::leff));
    EXPECT_NEAR(p->BSIM4v7weff, golden::weff, atol_for(golden::weff));

    // Ngspice-derived goldens @ T=300.15K.
    EXPECT_NEAR(p->BSIM4v7vth0,     golden::vth0,     atol_for(golden::vth0));
    EXPECT_NEAR(p->BSIM4v7u0temp,   golden::u0temp,   atol_for(golden::u0temp));
    EXPECT_NEAR(p->BSIM4v7vfb,      golden::vfb,      atol_for(golden::vfb));
    EXPECT_NEAR(p->BSIM4v7vsattemp, golden::vsattemp, atol_for(golden::vsattemp));
    EXPECT_NEAR(p->BSIM4v7k1ox,     golden::k1ox,     atol_for(golden::k1ox));
    EXPECT_NEAR(p->BSIM4v7cdep0,    golden::cdep0,    atol_for(golden::cdep0));
    EXPECT_NEAR(p->BSIM4v7phi,      golden::phi,      atol_for(golden::phi));

    // Instance-level mirrors (no stress → identical to pParam).
    EXPECT_NEAR(f.inst.BSIM4v7vth0,     golden::vth0,     atol_for(golden::vth0));
    EXPECT_NEAR(f.inst.BSIM4v7u0temp,   golden::u0temp,   atol_for(golden::u0temp));
    EXPECT_NEAR(f.inst.BSIM4v7vfb,      golden::vfb,      atol_for(golden::vfb));
    EXPECT_NEAR(f.inst.BSIM4v7vsattemp, golden::vsattemp, atol_for(golden::vsattemp));
}

} // namespace
