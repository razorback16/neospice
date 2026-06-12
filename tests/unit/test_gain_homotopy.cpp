// [3B] Variable-gain homotopy (semiconductor device-gain continuation) tests.
//
// Covers:
//   1. Device scaling: with options.device_gain_fact == 0.0 a MOSFET / BJT
//      contributes ~no nonlinear current (device effectively open -> output
//      node sits at the passive-network solution); with == 1.0 it conducts and
//      pulls the node far away.  This exercises the per-device lambda scaling in
//      mos*_load.cpp / bjt_load.cpp directly via newton_solve.
//   2. variable_gain_homotopy() converges and lands on the same operating point
//      as the normal DC solve, and the DC cascade can report GAIN_HOMOTOPY.
#include <gtest/gtest.h>
#include "core/dc.hpp"
#include "core/convergence.hpp"
#include "core/circuit.hpp"
#include "core/neo_solver.hpp"
#include "core/newton.hpp"
#include "api/neospice.hpp"

using namespace neospice;

namespace {

// Solve a parsed circuit with a fixed device_gain_fact using a single direct
// Newton solve (MODEDCOP|MODEINITJCT) and return the converged node solution.
NewtonResult solve_with_lambda(Circuit& ckt, std::vector<double>& solution,
                               double lambda) {
    auto solver = std::make_unique<NeoSolver>();
    solver->symbolic(ckt.pattern());
    ckt.integrator_ctx.options = &ckt.options;
    ckt.integrator_ctx.mode = 0x10 | 0x200;  // MODEDCOP | MODEINITJCT
    ckt.options.device_gain_fact = lambda;
    solution.assign(ckt.num_vars(), 0.0);
    NewtonResult r;
    try {
        r = newton_solve(ckt, *solver, solution, ckt.options);
    } catch (const std::runtime_error&) {
        r.converged = false;
    }
    ckt.options.device_gain_fact = 1.0;
    return r;
}

} // namespace

// ---------------------------------------------------------------------------
// 1. Device scaling unit tests
// ---------------------------------------------------------------------------

TEST(GainHomotopy, MosfetScalingZeroVsFull) {
    // NMOS common-source with the drain pulled to 5V through 10k and the gate
    // biased well above threshold.  At lambda=1 the channel conducts and the
    // drain is pulled low; at lambda=0 the channel current is scaled to ~0 so
    // the drain floats up to the 5V rail (the passive-network solution).
    std::string netlist = R"(
MOSFET device-gain scaling
VDD vdd 0 DC 5.0
VG  g   0 DC 3.0
RD  vdd d 10k
M1  d g 0 0 NMOD W=20u L=1u
.model NMOD NMOS(LEVEL=1 VTO=0.7 KP=110u GAMMA=0.4 PHI=0.65 LAMBDA=0.01)
.op
.end
)";
    Simulator sim;

    // lambda = 1.0 : transistor conducts, drain pulled below the rail.
    auto ckt_full = sim.parse(netlist);
    std::vector<double> sol_full;
    auto r_full = solve_with_lambda(ckt_full, sol_full, 1.0);
    ASSERT_TRUE(r_full.converged);
    NodeId d_full = ckt_full.node("d");
    double vd_full = sol_full[static_cast<int32_t>(d_full)];

    // lambda = 0.0 : channel/junction nonlinearity scaled to ~0, drain ~= rail.
    auto ckt_zero = sim.parse(netlist);
    std::vector<double> sol_zero;
    auto r_zero = solve_with_lambda(ckt_zero, sol_zero, 0.0);
    ASSERT_TRUE(r_zero.converged);
    NodeId d_zero = ckt_zero.node("d");
    double vd_zero = sol_zero[static_cast<int32_t>(d_zero)];

    // At lambda=0 essentially no current flows through RD -> drain ~ 5V.
    EXPECT_NEAR(vd_zero, 5.0, 1e-3)
        << "device should be ~open at lambda=0, drain at rail";
    // At lambda=1 the device conducts and pulls the drain well below the rail.
    EXPECT_LT(vd_full, 4.0)
        << "device should conduct at lambda=1, drain pulled down (vd=" << vd_full << ")";
}

TEST(GainHomotopy, BjtScalingZeroVsFull) {
    // NPN common-emitter: base driven through 100k from 5V, collector pulled to
    // 5V through 1k.  At lambda=1 the BJT sinks collector current and pulls the
    // collector node low; at lambda=0 the transistor currents are scaled to ~0
    // so the collector floats up to the 5V rail.
    std::string netlist = R"(
BJT device-gain scaling
VCC vcc 0 DC 5.0
RB  vcc b 100k
RC  vcc c 1k
Q1  c b 0 QMOD
.model QMOD NPN(IS=1e-15 BF=100 VAF=100)
.op
.end
)";
    Simulator sim;

    auto ckt_full = sim.parse(netlist);
    std::vector<double> sol_full;
    auto r_full = solve_with_lambda(ckt_full, sol_full, 1.0);
    ASSERT_TRUE(r_full.converged);
    double vc_full = sol_full[static_cast<int32_t>(ckt_full.node("c"))];

    auto ckt_zero = sim.parse(netlist);
    std::vector<double> sol_zero;
    auto r_zero = solve_with_lambda(ckt_zero, sol_zero, 0.0);
    ASSERT_TRUE(r_zero.converged);
    double vc_zero = sol_zero[static_cast<int32_t>(ckt_zero.node("c"))];

    // lambda=0 -> ~no collector current -> collector ~ 5V rail.
    EXPECT_NEAR(vc_zero, 5.0, 1e-3)
        << "BJT should be ~open at lambda=0, collector at rail";
    // lambda=1 -> transistor sinks current -> collector pulled well below rail.
    EXPECT_LT(vc_full, 4.0)
        << "BJT should conduct at lambda=1, collector pulled down (vc=" << vc_full << ")";
}

// ---------------------------------------------------------------------------
// 2. variable_gain_homotopy() end-to-end
// ---------------------------------------------------------------------------

TEST(GainHomotopy, DirectCallConvergesToSameOperatingPoint) {
    // Call variable_gain_homotopy() directly on a transistor circuit and verify
    // it lands on the same operating point that the normal DC solve produces.
    std::string netlist = R"(
Gain-homotopy direct call
VDD vdd 0 DC 5.0
VG  g   0 DC 2.5
RD  vdd d 4.7k
M1  d g 0 0 NMOD W=20u L=1u
.model NMOD NMOS(LEVEL=1 VTO=0.7 KP=110u GAMMA=0.4 PHI=0.65 LAMBDA=0.01)
.op
.end
)";
    Simulator sim;

    // Reference operating point from the standard DC solve.
    auto ckt_ref = sim.parse(netlist);
    DCResult ref = sim.run_dc(ckt_ref);
    double vd_ref = ref.voltage("d");

    // Homotopy from scratch.
    auto ckt = sim.parse(netlist);
    auto solver = std::make_unique<NeoSolver>();
    solver->symbolic(ckt.pattern());
    ckt.integrator_ctx.options = &ckt.options;
    ckt.integrator_ctx.mode = 0x10 | 0x200;  // MODEDCOP | MODEINITJCT
    std::vector<double> solution(ckt.num_vars(), 0.0);

    auto result = variable_gain_homotopy(ckt, *solver, solution, ckt.options);
    EXPECT_TRUE(result.converged);
    // device_gain_fact must be restored to its full value on exit.
    EXPECT_EQ(ckt.options.device_gain_fact, 1.0);

    double vd_homotopy = solution[static_cast<int32_t>(ckt.node("d"))];
    EXPECT_NEAR(vd_homotopy, vd_ref, 1e-3)
        << "homotopy operating point must match direct DC solve";
}

TEST(GainHomotopy, RegressionDefaultPathUnchanged) {
    // A circuit that converges directly must NOT be reported as using the
    // homotopy (the late fallback never runs for easy circuits, so the default
    // device_gain_fact=1.0 path is bit-identical).
    std::string netlist = R"(
Easy MOSFET DC
VDD vdd 0 DC 5.0
VG  g   0 DC 2.0
RD  vdd d 1k
M1  d g 0 0 NMOD W=10u L=1u
.model NMOD NMOS(LEVEL=1 VTO=0.7 KP=110u GAMMA=0.4 PHI=0.65)
.op
.end
)";
    Simulator sim;
    auto ckt = sim.parse(netlist);
    DCResult result = sim.run_dc(ckt);
    EXPECT_NE(result.status.convergence_method, ConvergenceMethod::GAIN_HOMOTOPY)
        << "easy circuit must not enter the homotopy fallback";
}
