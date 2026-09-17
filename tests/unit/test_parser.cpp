#include <gtest/gtest.h>
#include "parser/netlist_parser.hpp"
#include "api/neospice.hpp"
#include "devices/vcvs.hpp"
#include "devices/vccs.hpp"
#include "devices/vcvs_nonlinear.hpp"
#include "devices/vccs_nonlinear.hpp"
#include "devices/cccs_nonlinear.hpp"
#include "devices/ccvs_nonlinear.hpp"
#include "devices/switch.hpp"

using namespace neospice;

TEST(Parser, ModelExpressionsPreserveGroupingAndParameters) {
    const auto card = parse_model_card({".model", "QM",
        "NPN (BF={2 * (50 + 10)}, IS='10 * 1e-15') VAF={voltage}"},
        {{"voltage", 80.0}});
    EXPECT_DOUBLE_EQ(card.params.at("bf"), 120.0);
    EXPECT_DOUBLE_EQ(card.params.at("is"), 1e-14);
    EXPECT_DOUBLE_EQ(card.params.at("vaf"), 80.0);
    EXPECT_EQ(card.params.size(), 3u);
    EXPECT_TRUE(card.temperature_expressions.empty());
}

TEST(Parser, InvalidModelExpressionsCannotUseDefaults) {
    for (const std::string value : {"{missing}", "{1 / 0}", "{2*(3+4)", "'2*(3+4)"}) {
        SCOPED_TRACE(value);
        EXPECT_THROW(parse_model_card({".model", "QM", "NPN BF=" + value}), ParseError);
    }
}

TEST(Parser, ModelTemperatureExpressionRemainsDeferred) {
    const auto card = parse_model_card({".model", "POWER",
        "VDMOS(Vto={base - slope * (TEMPER - 25)} Kp={2*10m})"},
        {{"base", 0.65}, {"slope", 0.0016}});
    EXPECT_FALSE(card.params.contains("vto"));
    ASSERT_EQ(card.temperature_expressions.size(), 1u);
    EXPECT_EQ(card.temperature_expressions.front().first, "vto");
    EXPECT_DOUBLE_EQ(card.params.at("kp"), 0.02);
    const auto numeric_override = parse_model_card({".model", "POWER",
        "VDMOS(Vto={TEMPER/100} Vto=0.7)"});
    ASSERT_EQ(numeric_override.temperature_expressions.size(), 1u);
    EXPECT_EQ(numeric_override.temperature_expressions.front().first, "vto");
    EXPECT_DOUBLE_EQ(numeric_override.params.at("vto"), 0.7);
}

TEST(Parser, ModelTempAliasRequiresPspiceCompatibility) {
    const std::string deck = "Temperature alias\nVd d 0 2\nVg g 0 1\n"
        "M1 d g 0 POWER\n.model POWER VDMOS(Vto={TEMP/100})\n.end\n";
    NetlistParser parser;
    parser.set_force_pspice_compat(false);
    EXPECT_THROW(parser.parse(deck), ParseError);
    parser.set_force_pspice_compat(true);
    EXPECT_NO_THROW(parser.parse(deck));
}

TEST(Parser, UnsupportedModelTemperatureUpdatesFailExplicitly) {
    NetlistParser parser;
    for (const std::string deck : {
        "Diode topology update\nD1 d 0 DM\n.model DM D(RS={TEMPER})\n.end\n",
        "MOS topology update\nM1 d g 0 POWER\n.model POWER VDMOS(RD={TEMPER})\n.end\n",
        "Passive model update\nR1 d 0 RM 1k\n.model RM R(TC1={TEMPER})\n.end\n"}) {
        SCOPED_TRACE(deck);
        EXPECT_THROW(parser.parse(deck), ParseError);
    }
}

TEST(Parser, ModelTemperatureErrorIsReportedAndCircuitCanRecover) {
    Simulator sim;
    auto ckt = sim.parse("Temperature error recovery\n"
        "Vd d 0 2\nVg g 0 1\nM1 d g 0 FIRST\n"
        ".model FIRST VDMOS(Vto={TEMPER/100} Kp=.44)\n"
        "Vd2 d2 0 2\nVg2 g2 0 1\nM2 d2 g2 0 SECOND\n"
        ".model SECOND VDMOS(Vto=.5 Kp={1/(60-TEMPER)})\n.op\n.end\n");
    const auto original = sim.run_dc(ckt);
    ckt.options.temp = 333.15;
    EXPECT_THROW(sim.run_dc(ckt), ParseError);
    EXPECT_EQ(ckt.operating_point(), nullptr);
    ckt.options.temp = 300.15;
    const auto recovered = sim.run_dc(ckt);
    EXPECT_NEAR(recovered.current("vd"), original.current("vd"), 1e-12);
    EXPECT_NEAR(recovered.current("vd2"), original.current("vd2"), 1e-12);
}

TEST(Parser, ModelParametersBeforeInsideAndAfterParentheses) {
    for (const std::string body : {
             "NPN(LEVEL=4 IS=1e-14 TD=2n BF=120)",
             "NPN LEVEL=4 IS=1e-14 TD=2n BF=120",
             "NPN LEVEL=4(IS=1e-14 TD=2n) BF=120",
             "NPN LEVEL=4 (IS=1e-14 TD=2n) BF=120",
             "NPN (LEVEL=4) IS=1e-14 (TD=2n BF=120)",
             "NPN (LEVEL=4, IS=1e-14) TD=2n BF=120"}) {
        SCOPED_TRACE(body);
        const auto card = parse_model_card({".model", "QM", body});
        EXPECT_EQ(card.type, "npn");
        ASSERT_TRUE(card.params.contains("level"));
        ASSERT_TRUE(card.params.contains("is"));
        ASSERT_TRUE(card.params.contains("td"));
        ASSERT_TRUE(card.params.contains("bf"));
        EXPECT_DOUBLE_EQ(card.params.at("level"), 4.0);
        EXPECT_DOUBLE_EQ(card.params.at("is"), 1e-14);
        EXPECT_DOUBLE_EQ(card.params.at("td"), 2e-9);
        EXPECT_DOUBLE_EQ(card.params.at("bf"), 120.0);
    }
}

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
    EXPECT_TRUE(std::holds_alternative<OpCmd>(ckt.analyses[0]));
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
    ASSERT_TRUE(std::holds_alternative<TranCmd>(ckt.analyses[0]));
    auto& tran = std::get<TranCmd>(ckt.analyses[0]);
    EXPECT_NEAR(tran.tstep, 0.1e-6, 1e-12);
    EXPECT_NEAR(tran.tstop, 50e-6, 1e-12);
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
    ASSERT_TRUE(std::holds_alternative<ACCmd>(ckt.analyses[0]));
    EXPECT_EQ(std::get<ACCmd>(ckt.analyses[0]).npoints, 10);
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

TEST(Parser, EElementValueWithoutEquals) {
    std::string netlist = R"(
E VALUE without equals
V1 in 0 DC 1
E1 out 0 VALUE {V(in)*2}
R1 out 0 1k
.op
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    EXPECT_EQ(ckt.devices().size(), 4u);
}

TEST(Parser, GElementValueWithoutEquals) {
    std::string netlist = R"(
G VALUE without equals
V1 in 0 DC 1
G1 out 0 VALUE {V(in)*0.001}
R1 out 0 1k
.op
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    EXPECT_EQ(ckt.devices().size(), 4u);
}

TEST(Parser, AKOCrossScopeResolution) {
    std::string netlist = R"(
AKO cross-scope test
.subckt mybjt C B E
.model QON NPN(BF=100 IS=1e-14)
.model QP AKO:QON NPN(BF=200)
Q1 C B E QP
.ends
X1 col base 0 mybjt
VCC col 0 5
VBB base 0 0.7
.op
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    bool has_bjt = false;
    for (const auto& dev : ckt.devices()) {
        if (dev->name().find("q1") != std::string::npos)
            has_bjt = true;
    }
    EXPECT_TRUE(has_bjt);
}

// ============================================================
// Parenthesized control node pairs in E and G elements
// ============================================================

TEST(Parser, EElementParenthesizedControlNodes) {
    // E1 2 0 (3,4) 2.0 — linear VCVS with parenthesized control pair
    std::string netlist = R"(
VCVS paren control nodes
V1 3 0 DC 1
E1 2 0 (3,4) 2.0
R1 2 0 1k
.op
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    // V1, E1, R1 = 3 devices
    EXPECT_EQ(ckt.devices().size(), 3u);
    bool found_vcvs = false;
    for (const auto& dev : ckt.devices()) {
        if (dynamic_cast<const VCVS*>(dev.get()) != nullptr) {
            found_vcvs = true;
        }
    }
    EXPECT_TRUE(found_vcvs) << "Expected VCVS device to be parsed correctly";
}

TEST(Parser, GElementParenthesizedControlNodes) {
    // G1 2 0 (3,4) 1e-3 — linear VCCS with parenthesized control pair
    std::string netlist = R"(
VCCS paren control nodes
V1 3 0 DC 1
G1 2 0 (3,4) 1e-3
R1 2 0 1k
.op
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    // V1, G1, R1 = 3 devices
    EXPECT_EQ(ckt.devices().size(), 3u);
    bool found_vccs = false;
    for (const auto& dev : ckt.devices()) {
        if (dynamic_cast<const VCCS*>(dev.get()) != nullptr) {
            found_vccs = true;
        }
    }
    EXPECT_TRUE(found_vccs) << "Expected VCCS device to be parsed correctly";
}

TEST(Parser, SElementParenthesizedNodePairsTopLevel) {
    // S1 (n+,n-) (nc+,nc-) model — fully parenthesized form at top level.
    // Tokenizes to 4 whitespace tokens; must not be rejected by the size guard.
    std::string netlist = R"(
VSWITCH paren node pairs
V1 10 0 DC 10
V40 40 0 DC 8
S1 (10,11) (40,0) SwModel
RL 11 0 1k
.MODEL SwModel VSWITCH (Ron=1 Roff=1MEG Von=5 Voff=0)
.op
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    bool found_vswitch = false;
    for (const auto& dev : ckt.devices()) {
        if (dynamic_cast<const VSwitch*>(dev.get()) != nullptr) {
            found_vswitch = true;
        }
    }
    EXPECT_TRUE(found_vswitch) << "Expected VSwitch device to be parsed from fully-paren form";
}

TEST(Parser, SElementMixedFlatAndParenNodes) {
    // S1 n+ n- (nc+,nc-) model — flat output, parenthesized control pair.
    std::string netlist = R"(
VSWITCH mixed node forms
V1 10 0 DC 10
V40 40 0 DC 8
S1 10 11 (40,0) SwModel
RL 11 0 1k
.MODEL SwModel VSWITCH (Ron=1 Roff=1MEG Von=5 Voff=0)
.op
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    bool found_vswitch = false;
    for (const auto& dev : ckt.devices()) {
        if (dynamic_cast<const VSwitch*>(dev.get()) != nullptr) {
            found_vswitch = true;
        }
    }
    EXPECT_TRUE(found_vswitch) << "Expected VSwitch device from mixed flat/paren form";
}

TEST(Parser, EElementPolyParenthesizedNodes) {
    // EREF 98 0 POLY(2) (99,0) (50,0) 0 0.5 0.5 — POLY VCVS with parenthesized pairs
    std::string netlist = R"(
VCVS POLY paren control nodes
V1 99 0 DC 1
V2 50 0 DC 1
EREF 98 0 POLY(2) (99,0) (50,0) 0 0.5 0.5
R1 98 0 1k
.op
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    // V1, V2, EREF, R1 = 4 devices
    EXPECT_EQ(ckt.devices().size(), 4u);
    bool found_poly_vcvs = false;
    for (const auto& dev : ckt.devices()) {
        if (dynamic_cast<const NonlinearVCVS*>(dev.get()) != nullptr) {
            found_poly_vcvs = true;
        }
    }
    EXPECT_TRUE(found_poly_vcvs) << "Expected NonlinearVCVS device to be parsed correctly";
}

TEST(Parser, EElementPolyStrayLeadingParen) {
    // Some ADI macromodels (OP191/OP291/OP491 in OpAmp_AD.lib) glue a stray
    // '(' onto the POLY keyword: "E4 97 22 (POLY(1) (99,98) -0.765 1".
    // The leading '(' must not defeat POLY detection (which previously dropped
    // the device, leaving floating nodes and a singular DC system).
    std::string netlist = R"(
VCVS stray-paren POLY
R97 97 0 1k
R22 22 0 1k
R99 99 0 1k
R98 98 0 1k
E4 97 22 (POLY(1) (99,98) -0.765 1
.op
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);

    const NonlinearVCVS* poly = nullptr;
    for (const auto& dev : ckt.devices()) {
        if (auto* p = dynamic_cast<const NonlinearVCVS*>(dev.get())) {
            poly = p;
            break;
        }
    }
    ASSERT_NE(poly, nullptr) << "E4 (POLY(1) was dropped instead of parsed";

    // external_nodes() returns {np, nn, ctrl_pos, ctrl_neg, ...}.
    auto nodes = poly->external_nodes();
    ASSERT_EQ(nodes.size(), 4u);
    EXPECT_EQ(nodes[0], ckt.node_index("97"));  // np
    EXPECT_EQ(nodes[1], ckt.node_index("22"));  // nn
    EXPECT_EQ(nodes[2], ckt.node_index("99"));  // ctrl pos
    EXPECT_EQ(nodes[3], ckt.node_index("98"));  // ctrl neg
}

TEST(Parser, FElementPolyCommaGluedVsenseName) {
    // TI macromodels (TLC3702/TLC3704 in tex_inst.lib) write the F-source
    // POLY form with the sensing VSource name comma-glued onto the POLY
    // token: "FOUT 30 5 POLY(1),(V1) 4E-3 -40". ngspice's MIFgettok treats
    // ( ) , as whitespace and resolves V1; neospice previously consumed the
    // first coefficient ("4E-3") as the VS name and dropped the element.
    std::string netlist = R"(
F POLY comma-glued Vsense
V1 10 11 DC 0
R1 11 0 1k
FOUT 30 5 POLY(1),(V1) 4E-3 -40
R2 30 0 1k
R3 5 0 1k
.op
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);

    const NonlinearCCCS* poly = nullptr;
    for (const auto& dev : ckt.devices()) {
        if (auto* p = dynamic_cast<const NonlinearCCCS*>(dev.get())) {
            poly = p;
            break;
        }
    }
    ASSERT_NE(poly, nullptr)
        << "FOUT POLY(1),(V1) was dropped instead of resolving Vsense V1";
}

TEST(Parser, FElementPolySpaceSeparatedParenVsenseName) {
    // ti.lib writes the space-separated parenthesised form:
    // "FOUT 30 5 POLY(1) (V1) 4E-3 -40". The "(V1)" token must yield V1.
    std::string netlist = R"(
F POLY space-paren Vsense
V1 10 11 DC 0
R1 11 0 1k
FOUT 30 5 POLY(1) (V1) 4E-3 -40
R2 30 0 1k
R3 5 0 1k
.op
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);

    const NonlinearCCCS* poly = nullptr;
    for (const auto& dev : ckt.devices()) {
        if (auto* p = dynamic_cast<const NonlinearCCCS*>(dev.get())) {
            poly = p;
            break;
        }
    }
    ASSERT_NE(poly, nullptr)
        << "FOUT POLY(1) (V1) was dropped instead of resolving Vsense V1";
}

TEST(Parser, FElementPolyCommaGluedVsenseInSubckt) {
    // Same comma-glued POLY form inside a subckt: the Vsense name must be
    // prefixed with the instance hierarchy (x1.v1) during expansion so the
    // deferred F-source resolves against the prefixed VSource device.
    std::string netlist = R"(
F POLY comma-glued Vsense in subckt
.SUBCKT BUF a b
V1 10 11 DC 0
R1 11 0 1k
FOUT a b POLY(1),(V1) 4E-3 -40
.ENDS
X1 30 5 BUF
R2 30 0 1k
R3 5 0 1k
.op
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);

    const NonlinearCCCS* poly = nullptr;
    for (const auto& dev : ckt.devices()) {
        if (auto* p = dynamic_cast<const NonlinearCCCS*>(dev.get())) {
            poly = p;
            break;
        }
    }
    ASSERT_NE(poly, nullptr)
        << "Subckt FOUT POLY(1),(V1) was dropped — Vsense x1.v1 unresolved";
}

TEST(Parser, HElementPolyCommaGluedVsenseName) {
    // The H-source (CCVS) POLY parser shares the same code path; exercise the
    // comma-glued form for it too.
    std::string netlist = R"(
H POLY comma-glued Vsense
V1 10 11 DC 0
R1 11 0 1k
HOUT 30 5 POLY(1),(V1) 4E-3 -40
R2 30 0 1k
R3 5 0 1k
.op
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);

    const NonlinearCCVS* poly = nullptr;
    for (const auto& dev : ckt.devices()) {
        if (auto* p = dynamic_cast<const NonlinearCCVS*>(dev.get())) {
            poly = p;
            break;
        }
    }
    ASSERT_NE(poly, nullptr)
        << "HOUT POLY(1),(V1) was dropped instead of resolving Vsense V1";
}
