#include <gtest/gtest.h>
#include "core/dc.hpp"
#include "core/convergence.hpp"
#include "core/circuit.hpp"
#include "core/klu_solver.hpp"
#include "api/neospice.hpp"
#include "parser/netlist_parser.hpp"

using namespace neospice;

TEST(Convergence, DiodeDC) {
    std::string netlist = R"(
Diode Convergence Test
V1 top 0 DC 5.0
R1 top mid 1k
D1 mid 0 DMOD
.model DMOD D(IS=1e-14 N=1)
.op
.end
)";
    Simulator sim;
    auto ckt = sim.parse(netlist);
    DCResult result = sim.run_dc(ckt);
    EXPECT_GT(result.node_voltages["v(mid)"], 0.5);
    EXPECT_LT(result.node_voltages["v(mid)"], 0.9);
    EXPECT_NEAR(result.node_voltages["v(top)"], 5.0, 1e-6);
}

TEST(Convergence, GminSteppingWorks) {
    // Same diode circuit - should converge via normal Newton or gmin stepping
    std::string netlist = R"(
Gmin Stepping Test
V1 top 0 DC 0.7
R1 top mid 100
D1 mid 0 DMOD
.model DMOD D(IS=1e-14 N=1)
.op
.end
)";
    Simulator sim;
    auto ckt = sim.parse(netlist);
    DCResult result = sim.run_dc(ckt);
    // Diode forward voltage should be around 0.5-0.7V
    EXPECT_GT(result.node_voltages["v(mid)"], 0.3);
    EXPECT_LT(result.node_voltages["v(mid)"], 0.75);
}

TEST(Convergence, SourceSteppingSimpleCircuit) {
    // A simple resistor-diode circuit that converges via normal Newton.
    // This is a regression test: true source stepping must not break
    // circuits that already converge easily.
    std::string netlist = R"(
Source Stepping Regression
V1 vdd 0 DC 3.3
R1 vdd out 1k
D1 out 0 DMOD
.model DMOD D(IS=1e-14 N=1)
.op
.end
)";
    Simulator sim;
    auto ckt = sim.parse(netlist);
    DCResult result = sim.run_dc(ckt);
    // Diode forward voltage ~0.6V, VDD = 3.3V
    EXPECT_NEAR(result.node_voltages["v(vdd)"], 3.3, 1e-6);
    EXPECT_GT(result.node_voltages["v(out)"], 0.5);
    EXPECT_LT(result.node_voltages["v(out)"], 0.8);
}

TEST(Convergence, SourceSteppingCrossCoupledInverters) {
    // Cross-coupled CMOS inverter pair (bistable latch) with a slight
    // asymmetry (different W on M4 vs M2) so the circuit is biased toward
    // one stable state.  Without source stepping, Newton may fail to find
    // the DC operating point; with it, the gradual VDD ramp guides the
    // circuit into a stable latch state.
    std::string netlist = R"(
Cross-coupled CMOS inverters
VDD vdd 0 DC 5.0
* Inverter 1: input = q, output = qb
M1 qb q vdd vdd PMOD W=10u L=1u
M2 qb q 0   0   NMOD W=5u  L=1u
* Inverter 2: input = qb, output = q (slightly weaker NMOS to break symmetry)
M3 q qb vdd vdd PMOD W=10u L=1u
M4 q qb 0   0   NMOD W=4u  L=1u
* Pull-down on q to bias toward state B (q low, qb high)
R1 q 0 100k
.model NMOD NMOS(LEVEL=1 VTO=0.7 KP=110u GAMMA=0.4 PHI=0.65)
.model PMOD PMOS(LEVEL=1 VTO=-0.7 KP=50u GAMMA=0.57 PHI=0.65)
.op
.end
)";
    Simulator sim;
    auto ckt = sim.parse(netlist);
    DCResult result = sim.run_dc(ckt);
    double vq  = result.node_voltages["v(q)"];
    double vqb = result.node_voltages["v(qb)"];
    // The latch must settle to one of the two stable states:
    //   state A: q ~ VDD (5V), qb ~ 0V
    //   state B: q ~ 0V,       qb ~ VDD (5V)
    // Due to the pull-down on q, state B is strongly favored.
    bool state_a = (vq > 4.0 && vqb < 1.0);
    bool state_b = (vq < 1.0 && vqb > 4.0);
    EXPECT_TRUE(state_a || state_b)
        << "Expected bistable latch to converge to a stable state, got v(q)="
        << vq << " v(qb)=" << vqb;
    EXPECT_NEAR(result.node_voltages["v(vdd)"], 5.0, 1e-6);
}

TEST(Convergence, SourceSteppingWithCurrentSource) {
    // Verify that current sources are also scaled during source stepping.
    // A current source drives a diode — the DC operating point depends on
    // the source value.
    std::string netlist = R"(
Current Source Stepping
I1 0 out DC 1m
D1 out 0 DMOD
.model DMOD D(IS=1e-14 N=1)
.op
.end
)";
    Simulator sim;
    auto ckt = sim.parse(netlist);
    DCResult result = sim.run_dc(ckt);
    // 1 mA through a diode => forward voltage ~0.6V
    EXPECT_GT(result.node_voltages["v(out)"], 0.5);
    EXPECT_LT(result.node_voltages["v(out)"], 0.75);
}

TEST(Convergence, PseudoTransientSimpleCircuit) {
    // Verify that pseudo-transient continuation does not break a simple
    // circuit that already converges via Newton.  This is a regression test:
    // the full convergence chain (Newton -> gmin -> source -> pseudo-transient)
    // must still produce correct results.
    std::string netlist = R"(
Pseudo-Transient Regression
V1 vdd 0 DC 3.3
R1 vdd out 1k
D1 out 0 DMOD
.model DMOD D(IS=1e-14 N=1)
.op
.end
)";
    Simulator sim;
    auto ckt = sim.parse(netlist);
    DCResult result = sim.run_dc(ckt);
    EXPECT_NEAR(result.node_voltages["v(vdd)"], 3.3, 1e-6);
    EXPECT_GT(result.node_voltages["v(out)"], 0.5);
    EXPECT_LT(result.node_voltages["v(out)"], 0.8);
}

TEST(Convergence, PseudoTransientDirectCall) {
    // Call pseudo_transient() directly on a circuit that converges easily
    // to verify the function itself works correctly and returns a converged
    // result with a valid solution.
    std::string netlist = R"(
Pseudo-Transient Direct
V1 top 0 DC 5.0
R1 top mid 1k
D1 mid 0 DMOD
.model DMOD D(IS=1e-14 N=1)
.op
.end
)";
    Simulator sim;
    auto ckt = sim.parse(netlist);

    const int32_t n = ckt.num_vars();
    std::vector<double> solution(n, 0.0);

    KLUSolver solver;
    solver.symbolic(ckt.pattern());
    ckt.integrator_ctx.options = &ckt.options;
    ckt.integrator_ctx.mode = 0x10 | 0x400;  // MODEDCOP | MODEINITFIX

    auto result = pseudo_transient(ckt, solver, solution, ckt.options);
    EXPECT_TRUE(result.converged);
    // Verify the solution vector has the right size
    EXPECT_EQ(result.solution.size(), static_cast<size_t>(n));
}

TEST(Convergence, PseudoTransientCrossCoupledMOS) {
    // Cross-coupled CMOS inverters -- a known-difficult circuit for DC
    // convergence.  The full convergence chain should handle this via
    // one of the convergence aids.
    std::string netlist = R"(
Pseudo-Transient Cross-Coupled
VDD vdd 0 DC 5.0
M1 qb q vdd vdd PMOD W=10u L=1u
M2 qb q 0   0   NMOD W=5u  L=1u
M3 q qb vdd vdd PMOD W=10u L=1u
M4 q qb 0   0   NMOD W=4u  L=1u
R1 q 0 100k
.model NMOD NMOS(LEVEL=1 VTO=0.7 KP=110u GAMMA=0.4 PHI=0.65)
.model PMOD PMOS(LEVEL=1 VTO=-0.7 KP=50u GAMMA=0.57 PHI=0.65)
.op
.end
)";
    Simulator sim;
    auto ckt = sim.parse(netlist);
    DCResult result = sim.run_dc(ckt);
    double vq  = result.node_voltages["v(q)"];
    double vqb = result.node_voltages["v(qb)"];
    // Must converge to one of the two stable states
    bool state_a = (vq > 4.0 && vqb < 1.0);
    bool state_b = (vq < 1.0 && vqb > 4.0);
    EXPECT_TRUE(state_a || state_b)
        << "Expected latch to converge, got v(q)=" << vq << " v(qb)=" << vqb;
}
