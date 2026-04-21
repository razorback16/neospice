#include <gtest/gtest.h>
#include "parser/netlist_parser.hpp"
#include "api/neospice.hpp"

using namespace neospice;

TEST(Parser, ResistorDivider) {
    std::string netlist = R"(
Resistor Divider
V1 in 0 DC 10
R1 in mid 1k
R2 mid 0 1k
.op
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    EXPECT_EQ(ckt.num_nodes(), 2); // in, mid
    EXPECT_EQ(ckt.devices().size(), 3u); // V1, R1, R2
    EXPECT_EQ(ckt.analyses.size(), 1u);
    EXPECT_EQ(ckt.analyses[0].type, AnalysisCommand::OP);
}

TEST(Parser, TransientAnalysis) {
    std::string netlist = R"(
RC Circuit
V1 in 0 PULSE(0 5 0 1n 1n 10u 20u)
R1 in out 1k
C1 out 0 1u
.tran 0.1u 50u
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    EXPECT_EQ(ckt.devices().size(), 3u);
    ASSERT_EQ(ckt.analyses.size(), 1u);
    EXPECT_EQ(ckt.analyses[0].type, AnalysisCommand::TRAN);
    EXPECT_NEAR(ckt.analyses[0].tran_tstep, 0.1e-6, 1e-12);
    EXPECT_NEAR(ckt.analyses[0].tran_tstop, 50e-6, 1e-12);
}

TEST(Parser, ACAnalysis) {
    std::string netlist = R"(
AC Test
V1 in 0 DC 0 AC 1
R1 in out 1k
C1 out 0 1n
.ac dec 10 1 1g
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    ASSERT_EQ(ckt.analyses.size(), 1u);
    EXPECT_EQ(ckt.analyses[0].type, AnalysisCommand::AC);
    EXPECT_EQ(ckt.analyses[0].ac_npoints, 10);
}

TEST(Parser, DiodeWithModel) {
    std::string netlist = R"(
Diode Test
V1 in 0 5
R1 in out 1k
D1 out 0 MYDIODE
.model MYDIODE D(Is=1e-14 N=1.0 Cj0=1p Vj=0.7)
.op
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    EXPECT_EQ(ckt.devices().size(), 3u);
}

TEST(Parser, BElementVoltageMode) {
    // B element (behavioral voltage source) should parse without throwing.
    std::string netlist = R"(
B Element Test
V1 in 0 DC 2.0
R1 out 0 1k
B1 out 0 V={V(in)*2}
.op
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    // V1 + R1 + B1 = 3 devices
    EXPECT_EQ(ckt.devices().size(), 3u);
}

TEST(Parser, BElementCurrentMode) {
    // B element (behavioral current source) should parse without throwing.
    std::string netlist = R"(
B Element Current Test
V1 in 0 DC 1.0
R1 out 0 1k
B1 out 0 I={V(in)*1m}
.op
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    EXPECT_EQ(ckt.devices().size(), 3u);
}

TEST(Parser, VCVSElement) {
    // E element should parse without throwing
    std::string netlist = R"(
VCVS Test
V1 in 0 DC 2.0
E1 out 0 in 0 3.0
R1 out 0 1k
.op
.end
)";
    NetlistParser parser;
    // Should not throw
    EXPECT_NO_THROW(parser.parse(netlist));
}

TEST(Parser, Options) {
    std::string netlist = R"(
Options Test
V1 in 0 5
R1 in 0 1k
.options reltol=1e-4 abstol=1e-15
.op
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    EXPECT_NEAR(ckt.options.reltol, 1e-4, 1e-10);
    EXPECT_NEAR(ckt.options.abstol, 1e-15, 1e-20);
}

TEST(Parser, InductorElement) {
    std::string netlist = R"(
Inductor Test
V1 in 0 5
L1 in out 10m
R1 out 0 100
.op
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    EXPECT_EQ(ckt.devices().size(), 3u);
}

// ============================================================
// .func directive support
// ============================================================

TEST(Parser, FuncInParam) {
    // .func used in .param expression
    std::string netlist = R"(
Func in Param
.func square(x) {x*x}
.param myval=square(3)
V1 out 0 DC 1
R1 out 0 1k
.op
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    EXPECT_EQ(ckt.devices().size(), 2u);
}

TEST(Parser, FuncMultipleDefinitions) {
    // Multiple .func definitions
    std::string netlist = R"(
Multiple Funcs
.func square(x) {x*x}
.func double(x) {2*x}
.param a=square(3)
.param b=double(a)
V1 out 0 DC 1
R1 out 0 1k
.op
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    EXPECT_EQ(ckt.devices().size(), 2u);
}

TEST(Parser, StepParamParsing) {
    Simulator sim;
    auto ckt = sim.parse(R"(
Test step
R1 1 0 1k
V1 1 0 1
.step param rval 100 10k 100
.op
.end
)");
    ASSERT_EQ(ckt.step_commands.size(), 1u);
    EXPECT_EQ(ckt.step_commands[0].kind, StepCommand::PARAM);
    EXPECT_EQ(ckt.step_commands[0].name, "rval");
    EXPECT_DOUBLE_EQ(ckt.step_commands[0].start, 100.0);
    EXPECT_DOUBLE_EQ(ckt.step_commands[0].stop, 10e3);
    EXPECT_DOUBLE_EQ(ckt.step_commands[0].step, 100.0);
}

TEST(Parser, StepSourceParsing) {
    Simulator sim;
    auto ckt = sim.parse(R"(
Test step
R1 1 0 1k
V1 1 0 1
.step V1 0 5 0.1
.op
.end
)");
    ASSERT_EQ(ckt.step_commands.size(), 1u);
    EXPECT_EQ(ckt.step_commands[0].kind, StepCommand::SOURCE);
    EXPECT_EQ(ckt.step_commands[0].name, "v1");
}

TEST(Parser, StepTempParsing) {
    Simulator sim;
    auto ckt = sim.parse(R"(
Test step
R1 1 0 1k
V1 1 0 1
.step temp -40 125 5
.op
.end
)");
    ASSERT_EQ(ckt.step_commands.size(), 1u);
    EXPECT_EQ(ckt.step_commands[0].kind, StepCommand::TEMP);
}
