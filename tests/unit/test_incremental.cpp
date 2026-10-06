#include "api/neospice.hpp"
#include <gtest/gtest.h>
#include <cmath>
#include <numbers>
using namespace neospice;
namespace {
const std::string divider="Divider\nV1 in 0 10\nR1 in out 1k\nR2 out 0 1k\n.op\n.end\n";
}
TEST(Incremental, ReusesSymbolicAndMatchesFreshDividers) {
    Simulator sim;auto c=sim.parse(divider);
    EXPECT_EQ(c.reuse_statistics().dc_symbolic_analyses,0);
    for (double r : {1000,2000,20,100000,500}) {
        c.update_param("r2",r);
        auto incremental=sim.re_solve(c);
        auto fresh=sim.parse(divider);fresh.update_param("r2",r);
        auto reference=sim.run_dc(fresh);
        EXPECT_NEAR(incremental.voltage("out"),reference.voltage("out"),1e-12);
        EXPECT_NEAR(incremental.current("v1"),reference.current("v1"),1e-14);
    }
    EXPECT_EQ(c.reuse_statistics().dc_runs,5);
    EXPECT_EQ(c.reuse_statistics().dc_symbolic_analyses,1);
}
TEST(Incremental, NonlinearAndTemperatureChangesMatchFresh) {
    const std::string text="Diode\nV1 in 0 1\nR1 in out 1k TC1=0.001\nD1 out 0 dm\n.model dm D(IS=1e-14 RS=10)\n.op\n.end\n";
    Simulator sim;auto c=sim.parse(text);
    for(double r : {1000,1500,400}) {
        c.update_param("r1",r);c.options.temp=300.15+r/100;
        auto result=sim.re_solve(c);
        auto fresh=sim.parse(text);fresh.update_param("r1",r);fresh.options.temp=c.options.temp;
        EXPECT_NEAR(result.voltage("out"),sim.run_dc(fresh).voltage("out"),1e-10);
    }
    EXPECT_EQ(c.reuse_statistics().dc_symbolic_analyses,1);
}
TEST(Incremental, ACSweepReusesPatternAcrossValueAndGridChanges) {
    const std::string text="RC\nV1 in 0 DC 0 AC 1\nR1 in out 1k\nC1 out 0 1u\n.op\n.end\n";
    Simulator sim;auto c=sim.parse(text);
    for(double cap : {1e-6,1e-5,5e-8}) {
        c.update_param("c1",cap);
        auto ac=sim.re_solve_ac(c,ACMode::DEC,8,10,1e5);
        auto fresh=sim.parse(text);fresh.update_param("c1",cap);
        auto expected=sim.run_ac(fresh,ACMode::DEC,8,10,1e5);
        ASSERT_EQ(ac.frequency,expected.frequency);
        for(std::size_t i=0;i<ac.frequency.size();++i)
            EXPECT_NEAR(std::abs(ac.voltage("out")[i]-expected.voltage("out")[i]),0,1e-12);
    }
    auto changed=sim.re_solve_ac(c,ACMode::LIN,3,100,300);
    ASSERT_EQ(changed.frequency.size(),3);
    EXPECT_EQ(c.reuse_statistics().ac_symbolic_analyses,1);
    EXPECT_EQ(c.reuse_statistics().dc_symbolic_analyses,1);
    EXPECT_EQ(c.reuse_statistics().ac_runs,4);
}
TEST(Incremental, MovesAndExplicitCacheReset) {
    Simulator sim;auto original=sim.parse(divider);sim.re_solve(original);
    auto moved=std::move(original);moved.update_param("r2",2000);
    EXPECT_NEAR(sim.re_solve(moved).voltage("out"),20.0/3,1e-12);
    EXPECT_EQ(moved.reuse_statistics().dc_symbolic_analyses,1);
    moved.clear_reuse_cache();
    EXPECT_EQ(moved.reuse_statistics().dc_runs,0);
    EXPECT_NEAR(sim.re_solve(moved).voltage("out"),20.0/3,1e-12);
    EXPECT_EQ(moved.reuse_statistics().dc_symbolic_analyses,1);
    EXPECT_THROW(moved.R("new",moved.find_node("out"),GND,1000),std::logic_error);
}
TEST(Incremental, InvalidUpdatesAreAtomic) {
    Simulator sim;auto c=sim.parse(divider);auto before=sim.re_solve(c).voltage("out");
    EXPECT_THROW(c.update_param("r2",0),std::invalid_argument);
    EXPECT_THROW(c.update_param("r2",NAN),std::invalid_argument);
    EXPECT_THROW(c.update_param("missing",1),std::invalid_argument);
    EXPECT_DOUBLE_EQ(sim.re_solve(c).voltage("out"),before);
    EXPECT_EQ(c.reuse_statistics().dc_symbolic_analyses,1);
}
TEST(Incremental, FailedSolveCanRecoverAfterRepair) {
    Simulator sim;
    auto c=sim.parse("Failure\nV1 out 0 1\nV2 out 0 2\nR1 out 0 1k\n.op\n.end\n");
    c.options.no_throw=true;
    EXPECT_FALSE(sim.re_solve(c).status.converged);
    EXPECT_EQ(c.operating_point(),nullptr);
    // This topology remains singular even with equal sources; a valid new
    // circuit must start with its own cache, never the failed circuit's state.
    c=sim.parse(divider);
    EXPECT_TRUE(sim.re_solve(c).status.converged);
    EXPECT_EQ(c.reuse_statistics().dc_runs,1);
}
TEST(Incremental, IndependentCircuitsAndSaveFilters) {
    Simulator sim;auto a=sim.parse(divider);auto b=sim.parse(divider);
    a.update_param("r2",2000);a.save_signals={"v(out)"};
    auto x=sim.re_solve(a);auto y=sim.re_solve(b);
    EXPECT_EQ(x.node_voltages.size(),1);
    EXPECT_NEAR(x.voltage("out"),20.0/3,1e-12);
    EXPECT_NEAR(y.voltage("out"),5,1e-12);
    EXPECT_EQ(a.reuse_statistics().dc_runs,1);EXPECT_EQ(b.reuse_statistics().dc_runs,1);
}
TEST(Incremental, InterleavedAnalysesDoNotReuseOldValues) {
    Simulator sim;
    auto c=sim.parse("RC\nV1 in 0 DC 1 AC 1\nR1 in out 1k\nR2 out 0 1k\nC1 out 0 1u\n.op\n.end\n");
    sim.re_solve(c);sim.run_transient(c,1e-5,1e-4);
    c.update_param("r2",2000);
    EXPECT_NEAR(sim.re_solve(c).voltage("out"),2.0/3,1e-12);
    auto g=sim.sensitivity(c,{"v(out)"},{"r1"});
    EXPECT_TRUE(g.status.converged);
    c.update_param("v1",3);
    EXPECT_NEAR(sim.re_solve(c).voltage("out"),2,1e-12);
    EXPECT_EQ(c.reuse_statistics().dc_symbolic_analyses,1);
}

TEST(Incremental, NonlinearFailureRepairsSameCircuit) {
    Simulator sim;
    auto c=sim.parse("Fold\nV1 in 0 1\nB1 out 0 I={v(out)^2+v(out)+10000*(v(in)-1)}\n.end\n");
    c.options.no_throw=true;
    EXPECT_TRUE(sim.re_solve(c).status.converged);
    c.update_param("v1",1.0001);
    EXPECT_FALSE(sim.re_solve(c).status.converged);
    EXPECT_EQ(c.operating_point(),nullptr);
    c.update_param("v1",1);
    auto repaired=sim.re_solve(c);
    EXPECT_TRUE(repaired.status.converged);
    EXPECT_NEAR(repaired.voltage("out"),0,1e-10);
    EXPECT_EQ(c.reuse_statistics().dc_symbolic_analyses,2);
}
TEST(Incremental, SingularACCacheIsDiscardedAndRecovers) {
    Simulator sim;
    auto c=sim.parse("Resonance\nI1 0 out DC 0 AC 1\nL1 out 0 1\nC1 out 0 1\n.op\n.end\n");
    c.options.no_throw=true;
    double f=1/(2*std::numbers::pi);
    auto failed=sim.re_solve_ac(c,ACMode::LIN,1,f,f);
    EXPECT_FALSE(failed.status.converged);
    EXPECT_TRUE(failed.voltages.empty());
    c.update_param("c1",0.5);
    auto repaired=sim.re_solve_ac(c,ACMode::LIN,1,f,f);
    EXPECT_TRUE(repaired.status.converged);
    EXPECT_NEAR(std::abs(repaired.voltage("out")[0]-std::complex<double>(0,2)),0,1e-12);
    EXPECT_EQ(c.reuse_statistics().ac_symbolic_analyses,2);
}
