// Task 7: first exercise of the translated UCB BSIM4v7 load path end-to-end.
//
// Builds a trivial NMOS op-point circuit mirroring tests/goldens/probe.cir,
// constructs a BSIM4v7Device directly (parser support lands in T8), runs
// newton_solve, and compares the drain current against an ngspice OP
// golden.
//
// Circuit:
//   VDD d 0 0.1
//   VGS g 0 0.8
//   VBS b 0 0
//   M1 d g 0 b NMOD W=1u L=100n
//   .model NMOD NMOS LEVEL=14 VTH0=0.4 U0=0.04 TOXE=2e-9

#include <gtest/gtest.h>

#include "core/circuit.hpp"
#include "core/neo_solver.hpp"
#include "core/newton.hpp"
#include "devices/bsim4v7/bsim4v7_def.hpp"
#include "devices/bsim4v7/bsim4v7_device.hpp"
#include "devices/vsource.hpp"

using namespace neospice;
// NMOS is a macro (from bsim4v7_def.hpp) — no `using` declaration
// possible; it's referenced directly below.

namespace {

// ---------------------------------------------------------------------------
// Model-card helper: minimal NMOS .model LEVEL=14 matching the one in
// tests/goldens/probe.cir.  Sets the "*Given" flags so BSIM4v7temp honours
// each parameter; UCB param.c already mirrors the front-end's "Given"
// convention.
// ---------------------------------------------------------------------------
static void fill_nmod_card(BSIM4v7ModelCard& card) {
    auto& m = card.ucb;
    m.BSIM4v7modName      = "NMOD";
    m.BSIM4v7type         = NMOS;       m.BSIM4v7typeGiven = 1;
    m.BSIM4v7vth0         = 0.4;        m.BSIM4v7vth0Given = 1;
    m.BSIM4v7u0           = 0.04;       m.BSIM4v7u0Given   = 1;
    m.BSIM4v7toxe         = 2e-9;       m.BSIM4v7toxeGiven = 1;
    m.BSIM4v7nextModel    = nullptr;
    m.BSIM4v7instances    = nullptr;    // populated by BSIM4v7Device::make
}

} // namespace

// ---------------------------------------------------------------------------
TEST(BSIM4v7UCBLoad, NmosDcOpMatchesNgspice) {
    Circuit ckt;

    // Node allocation (neospice convention: GROUND_INTERNAL = -1, real
    // nodes are consecutive non-negative integers starting at 0).
    int32_t nd = static_cast<int32_t>(ckt.node("d"));
    int32_t ng = static_cast<int32_t>(ckt.node("g"));
    int32_t nb = static_cast<int32_t>(ckt.node("b"));
    auto ns = GROUND_INTERNAL;

    // Sources.
    ckt.add_device(std::make_unique<VSource>("VDD", nd, GROUND_INTERNAL, 0.1));
    ckt.add_device(std::make_unique<VSource>("VGS", ng, GROUND_INTERNAL, 0.8));
    ckt.add_device(std::make_unique<VSource>("VBS", nb, GROUND_INTERNAL, 0.0));

    // Model card (shared; lives as long as the device).
    BSIM4v7ModelCard card;
    fill_nmod_card(card);

    // MOSFET.  W=1u, L=100n, NF=1.
    BSIM4v7Device::Geom g;
    g.W = 1e-6;
    g.L = 1e-7;
    g.NF = 1.0;
    auto m1 = BSIM4v7Device::make("M1", nd, ng, ns, nb, g, card);
    // Grab the VSource pointer before moving the device (we'll need
    // branch_index() below).  The VDD device is at devices_[0].
    auto* vdd = dynamic_cast<VSource*>(ckt.devices()[0].get());
    ASSERT_NE(vdd, nullptr);
    ckt.add_device(std::move(m1));

    // Finalise + symbolic factor.
    ckt.finalize();

    auto solver = std::make_unique<NeoSolver>();
    solver->symbolic(ckt.pattern());

    // Newton initial guess: zero + VDD pin.  (solve_dc would do this too
    // but we want to exercise newton_solve directly to keep the test
    // independent of DC fallback logic.)
    //
    // Set the integrator mode to MODEDCOP | MODEINITJCT so BSIM4v7load takes
    // the junction-initialisation branch.  Plain DC op uses 0x10 (MODEDCOP),
    // not the full 0x70 mask.
    constexpr int MODEDCOP_BIT    = 0x10;
    constexpr int MODEINITJCT_BIT = 0x200;
    ckt.integrator_ctx.mode  = MODEDCOP_BIT | MODEINITJCT_BIT;
    ckt.integrator_ctx.order = 1;

    std::vector<double> solution(ckt.num_vars(), 0.0);
    auto result = newton_solve(ckt, *solver, solution, ckt.options);
    ASSERT_TRUE(result.converged) << "Newton failed to converge";
    // newton_solve modifies solution in-place; no copy needed

    // Drain current through M1 = -solution[VDD.branch_index()] because
    // VSource's MNA row carries "current flowing through the source from
    // the positive terminal to the negative terminal" — with VDD wired
    // d→0 and the NMOS sinking current d→s, that branch current is
    // negative when Id > 0.  See src/devices/vsource.cpp:92 and
    // tests/unit/test_dc.cpp:48 for the existing |.| convention.
    const double id_sim = -solution[vdd->branch_index()];

    // Golden from ngspice op() on tests/goldens/probe.cir, date 2026-04-16.
    // Captured with: ngspice -b tests/goldens/probe.cir → @m1[id] = 5.754970e-05.
    // ngspice version: ngspice-42.
    constexpr double id_golden = 5.754970e-05;
    constexpr double rel_tol   = 1e-3;
    constexpr double abs_floor = 1e-15;
    const double tol = std::max(std::abs(id_golden) * rel_tol, abs_floor);
    EXPECT_NEAR(id_sim, id_golden, tol)
        << "Drain current mismatch.  Sim=" << id_sim
        << " Golden=" << id_golden;
}

// ---------------------------------------------------------------------------
// Phase-2 test: BSIM4v7setup allocates internal nodes when rgateMod != 0
// (see bsim4v7_setup.cpp:2325 et al.).  declare_internal_nodes now
// delegates those allocations to Circuit::node() via the Shim::Ckt
// node_alloc callback.  Verify the device successfully runs setup and
// produces a journal (no throw).
// ---------------------------------------------------------------------------
TEST(BSIM4v7UCBLoad, DeclareInternalNodesSucceedsWithRgateMod) {
    Circuit ckt;
    int32_t nd = static_cast<int32_t>(ckt.node("d"));
    int32_t ng = static_cast<int32_t>(ckt.node("g"));
    auto ns = GROUND_INTERNAL;
    int32_t nb = static_cast<int32_t>(ckt.node("b"));

    BSIM4v7ModelCard card;
    fill_nmod_card(card);
    // Enable rgateMod so BSIM4v7setup allocates a "gate" internal node.
    card.ucb.BSIM4v7rgateMod      = 1;
    card.ucb.BSIM4v7rgateModGiven = 1;

    BSIM4v7Device::Geom g;
    g.W = 1e-6;
    g.L = 1e-7;
    g.NF = 1.0;
    auto dev = BSIM4v7Device::make("M1", nd, ng, ns, nb, g, card);
    ckt.add_device(std::move(dev));

    // finalize calls declare_internal_nodes then stamp_pattern — no throw.
    EXPECT_NO_THROW(ckt.finalize());
    // rgateMod=1 adds at least 1 internal node, so num_nodes > 3 (d,g,b).
    EXPECT_GT(ckt.num_nodes(), 3);
}
