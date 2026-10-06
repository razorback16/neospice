#include "api/neospice.hpp"
#include "core/neo_solver.hpp"
#include "framework/ngspice_lib.hpp"
#include <gtest/gtest.h>
#include <cmath>
#include <numbers>
using namespace neospice;

TEST(Gradient, DividerDifferentialAndBranchOutputs) {
    Simulator sim;
    auto ckt=sim.parse("Divider\nV1 in 0 10\nR1 in out 1k\nR2 out 0 2k\n.op\n.end\n");
    auto g=sim.sensitivity(ckt,{"v(out)","v(in,out)","i(V1)"},{"r1","r2","v1"});
    ASSERT_TRUE(g.status.converged);
    EXPECT_EQ(g.parameters,(std::vector<std::string>{"r1:resistance","r2:resistance","v1:dc"}));
    EXPECT_EQ(g.adjoint_solves,3);
    EXPECT_NEAR(g.values[0],20.0/3,1e-12);
    EXPECT_NEAR(g.jacobian[0][0],-20.0/9000,1e-14);
    EXPECT_NEAR(g.jacobian[0][1],10.0/9000,1e-14);
    EXPECT_NEAR(g.jacobian[0][2],2.0/3,1e-14);
    EXPECT_NEAR(g.jacobian[1][0],20.0/9000,1e-14);
    EXPECT_NEAR(g.jacobian[2][0],10.0/9e6,1e-16);
    EXPECT_NEAR(g.jacobian[2][2],-1.0/3000,1e-16);
}

TEST(Gradient, NonlinearDiodeMatchesIndependentDifferences) {
    const auto netlist=[](double resistance,double supply) {
        return "Diode\nV1 in 0 "+std::to_string(supply)+"\nR1 in out "+std::to_string(resistance)+"\nD1 out 0 dm\n.model dm D(IS=1e-14 RS=10)\n.op\n.end\n";
    };
    Simulator sim;
    auto ckt=sim.parse(netlist(1000,1));
    ckt.options.reltol=1e-8;ckt.options.vntol=1e-10;
    auto g=sim.sensitivity(ckt,{"v(out)"},{"r1","v1"});
    auto value=[&](double r,double v) { auto c=sim.parse(netlist(r,v));c.options.reltol=1e-8;c.options.vntol=1e-10;return sim.run_dc(c).voltage("out"); };
    EXPECT_NEAR(g.jacobian[0][0],(value(1000.1,1)-value(999.9,1))/0.2,1e-9);
    EXPECT_NEAR(g.jacobian[0][1],(value(1000,1.0001)-value(1000,0.9999))/0.0002,1e-6);
    EXPECT_EQ(g.adjoint_solves,1);
}

TEST(Gradient, NonsymmetricControlledSource) {
    Simulator sim;
    auto c=sim.parse("VCVS\nV1 in 0 2\nR1 in mid 1k\nR2 mid 0 1k\nE1 out 0 mid 0 3\nR3 out 0 1k\n.op\n.end\n");
    auto g=sim.sensitivity(c,{"v(out)"},{"v1","r1"});
    EXPECT_NEAR(g.jacobian[0][0],1.5,1e-12);
    EXPECT_NEAR(g.jacobian[0][1],-0.0015,1e-12);
}

TEST(Gradient, CurrentSourceAndTemperatureAdjustedResistance) {
    Simulator sim;
    auto c=sim.parse("Temp\nI1 0 out 1m\nR1 out 0 1k TC1=0.01\n.op\n.end\n");
    c.options.temp=c.options.tnom+10;
    auto g=sim.sensitivity(c,{"v(out)"},{"r1","i1"});
    EXPECT_NEAR(g.jacobian[0][0],0.0011,1e-14);
    EXPECT_NEAR(g.jacobian[0][1],1100,1e-9);
    ASSERT_TRUE(c.set_param("r1",2000));
    EXPECT_NEAR(sim.run_dc(c).voltage("out"),2.2,1e-10);
}

TEST(Gradient, ACFilterComplexDerivatives) {
    Simulator sim;
    auto c=sim.parse("RC\nV1 in 0 DC 0 AC 2\nR1 in out 1k\nC1 out 0 1u\n.op\n.end\n");
    auto g=sim.sensitivity_ac(c,{"v(out)","v(in,out)"},{"r1","c1","v1","v1:ac_phase","v1:dc"},{10,100,1000});
    EXPECT_EQ(g.adjoint_solves,6);
    for (std::size_t i=0;i<g.frequency.size();++i) {
        std::complex<double> jw(0,2*std::numbers::pi*g.frequency[i]);
        auto denominator=1.0+jw*1e-3;
        EXPECT_NEAR(std::abs(g.values[i][0]-2.0/denominator),0,1e-12);
        EXPECT_NEAR(std::abs(g.jacobian[i][0][0]+2.0*jw*1e-6/(denominator*denominator)),0,1e-12);
        EXPECT_NEAR(std::abs(g.jacobian[i][0][1]+2.0*jw*1000.0/(denominator*denominator)),0,1e-6);
        EXPECT_NEAR(std::abs(g.jacobian[i][0][2]-1.0/denominator),0,1e-12);
        EXPECT_NEAR(std::abs(g.jacobian[i][0][3]-std::complex<double>(0,std::numbers::pi/180)*g.values[i][0]),0,1e-12);
        EXPECT_EQ(g.jacobian[i][0][4],std::complex<double>(0));
        EXPECT_NEAR(std::abs(g.jacobian[i][1][1]+g.jacobian[i][0][1]),0,1e-8);
    }
}

TEST(Gradient, ACInductorAndCurrentExcitation) {
    Simulator sim;
    auto c=sim.parse("RL\nI1 0 out DC 0 AC 1\nR1 out 0 100\nL1 out 0 1m\n.op\n.end\n");
    double frequency=1000;
    auto g=sim.sensitivity_ac(c,{"v(out)","i(l1)"},{"l1","i1"},{frequency});
    std::complex<double> jw(0,2*std::numbers::pi*frequency);
    auto denominator=100.0+jw*1e-3;
    EXPECT_NEAR(std::abs(g.jacobian[0][0][0]-10000.0*jw/(denominator*denominator)),0,1e-8);
    EXPECT_NEAR(std::abs(g.jacobian[0][1][0]+100.0*jw/(denominator*denominator)),0,1e-10);
    EXPECT_NEAR(std::abs(g.jacobian[0][0][1]-100.0*jw*1e-3/denominator),0,1e-12);
}

TEST(Gradient, ExplicitFailuresAndUnchangedParameters) {
    Simulator sim;
    auto c=sim.parse("Diode\nV1 in 0 1\nR1 in out 1k\nD1 out 0 dm\n.model dm D\n.op\n.end\n");
    EXPECT_THROW(sim.sensitivity(c,{"v(out)"},{"d1"}),std::invalid_argument);
    EXPECT_THROW(sim.sensitivity(c,{"i(r1)"},{"r1"}),std::invalid_argument);
    EXPECT_THROW(sim.sensitivity(c,{"v(absent)"},{"r1"}),std::invalid_argument);
    EXPECT_THROW(sim.sensitivity(c,{"v(out)"},{"r1","R1:resistance"}),std::invalid_argument);
    EXPECT_THROW(sim.sensitivity_ac(c,{"v(out)"},{"r1"},{100}),std::invalid_argument);
    auto before=sim.run_dc(c).voltage("out");
    sim.sensitivity(c,{"v(out)"},{"r1"});
    EXPECT_NEAR(sim.run_dc(c).voltage("out"),before,1e-12);
    auto linear=sim.parse("R\nV1 out 0 1\nR1 out 0 1k\n.op\n.end\n");
    EXPECT_THROW(sim.sensitivity_ac(linear,{"v(out)"},{"r1"},{0}),std::invalid_argument);
    EXPECT_THROW(sim.sensitivity(linear,{},{}),std::invalid_argument);
}

TEST(Gradient, GroundAndZeroDCReactiveDerivatives) {
    Simulator sim;
    auto c=sim.parse("RLC\nV1 in 0 1\nR1 in out 1k\nC1 out 0 1u\nL1 out 0 1m\n.op\n.end\n");
    auto g=sim.sensitivity(c,{"v(0)","i(l1)"},{"c1","l1"});
    EXPECT_EQ(g.values[0],0);
    for (auto& row:g.jacobian) for (double d:row) EXPECT_EQ(d,0);
}

TEST(Gradient, RealTransposeWithEquilibration) {
    SparsityBuilder builder(2);
    for(int i=0;i<2;++i)for(int j=0;j<2;++j)builder.add(i,j);
    auto pattern=builder.build();NumericMatrix matrix(pattern);
    matrix.add(pattern.offset(0,0),2e-6);matrix.add(pattern.offset(0,1),3e-6);
    matrix.add(pattern.offset(1,0),4e6);matrix.add(pattern.offset(1,1),9e6);
    NeoSolver solver;solver.set_equilibrate(true);solver.symbolic(pattern);
    ASSERT_FALSE(solver.numeric(pattern,matrix));
    std::vector<double> rhs{6,12};
    solver.solve_transposed(rhs);
    EXPECT_NEAR(rhs[0],1e6,1e-8);EXPECT_NEAR(rhs[1],1e-6,1e-18);
}

TEST(Gradient, FailureStatusDoesNotContainUsableDerivatives) {
    Simulator sim;
    auto c=sim.parse("Inconsistent\nV1 out 0 1\nV2 out 0 2\nR1 out 0 1k\n.op\n.end\n");
    c.options.no_throw=true;
    auto dc=sim.sensitivity(c,{"v(out)"},{"r1"});
    EXPECT_FALSE(dc.status.converged);EXPECT_TRUE(dc.jacobian.empty());EXPECT_TRUE(dc.values.empty());
    auto ac=sim.sensitivity_ac(c,{"v(out)"},{"r1"},{100});
    EXPECT_FALSE(ac.status.converged);EXPECT_TRUE(ac.jacobian.empty());
}

TEST(Gradient, ComplexSingularityIsReportedBeforeSolve) {
    SparsityBuilder builder(2);builder.add(0,0);builder.add(1,1);
    auto pattern=builder.build();NeoSolver solver;solver.symbolic(pattern);
    EXPECT_THROW(solver.numeric_complex(pattern,{0,0,0,0}),std::runtime_error);
    std::vector<double> rhs(4,1);
    EXPECT_THROW(solver.solve_complex(rhs),std::logic_error);
}

TEST(Gradient, ACResistorOverrideAndCoupledInductorRejection) {
    Simulator sim;
    auto c=sim.parse("RAC\nV1 in 0 DC 1 AC 1\nR1 in out 1k RAC=2k\nR2 out 0 1k\n.op\n.end\n");
    auto g=sim.sensitivity_ac(c,{"v(out)"},{"r1"},{100});
    EXPECT_NEAR(g.values[0][0].real(),1.0/3,1e-12);
    EXPECT_EQ(g.jacobian[0][0][0],std::complex<double>{});
    auto coupled=sim.parse("Coupled\nV1 in 0 DC 0 AC 1\nR1 in a 1\nL1 a 0 1m\nL2 b 0 2m\nR2 b 0 1\nK1 L1 L2 0.5\n.op\n.end\n");
    EXPECT_THROW(sim.sensitivity_ac(coupled,{"v(b)"},{"l1"},{100}),std::invalid_argument);
}

TEST(Gradient, MatchesNgspice47Sensitivity) {
    const auto path=std::string(TEST_CIRCUITS_DIR)+"/sens_divider.cir";
    NgspiceLib reference;reference.load_circuit(path);reference.run();
    ASSERT_NE(reference.cur_plot(),nullptr);
    const std::string plot=reference.cur_plot();
    ASSERT_TRUE(plot.starts_with("sens"));
    Simulator sim;auto c=sim.load(path);
    auto gradient=sim.sensitivity(c,{"v(out)"},{"r1","r2","v1"});
    const std::vector<std::string> names={"r1","r2","v1"};
    for (std::size_t i=0;i<names.size();++i) {
        auto* vector=reference.get_vec_info(plot+"."+names[i]);
        ASSERT_NE(vector,nullptr);ASSERT_EQ(vector->v_length,1);
        ASSERT_NE(vector->v_realdata,nullptr);
        EXPECT_NEAR(gradient.jacobian[0][i],vector->v_realdata[0],1e-6);
    }
}
