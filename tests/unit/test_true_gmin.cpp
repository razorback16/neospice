#include <gtest/gtest.h>
#include "core/dc.hpp"
#include "core/convergence.hpp"
#include "core/circuit.hpp"
#include "core/neo_solver.hpp"
#include "api/neospice.hpp"
#include "parser/netlist_parser.hpp"

using namespace neospice;

TEST(TrueGmin, DirectCallConverges) {
    std::string netlist = R"(
True Gmin Direct
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

    auto solver = std::make_unique<NeoSolver>();
    solver->symbolic(ckt.pattern());
    ckt.integrator_ctx.options = &ckt.options;

    auto result = true_gmin_stepping(ckt, *solver, solution, ckt.options,
                                     0x10 | 0x200,
                                     0x10 | 0x100);
    EXPECT_TRUE(result.converged);
}

TEST(TrueGmin, InConvergenceCascade) {
    std::string netlist = R"(
True Gmin Cascade Regression
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
    EXPECT_NEAR(result.voltage("vdd"), 3.3, 1e-6);
    EXPECT_GT(result.voltage("out"), 0.5);
    EXPECT_LT(result.voltage("out"), 0.8);
}
