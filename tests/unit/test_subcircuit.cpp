#include <gtest/gtest.h>
#include "parser/netlist_parser.hpp"
#include "parser/subcircuit.hpp"
#include "devices/coupled_inductor.hpp"
#include "core/ac.hpp"
#include <cmath>

using namespace neospice;

// Helper: build a minimal netlist with a title line and .end terminator
static std::string wrap(const std::string& body) {
    return "Test netlist\n" + body + "\n.end\n";
}

// -----------------------------------------------------------------------
// 1. Basic parsing — simple subcircuit: name, ports, body
// -----------------------------------------------------------------------
TEST(Subcircuit, BasicParsing) {
    std::string netlist = wrap(R"(
.subckt myinv in out vdd gnd
M1 out in vdd vdd pmos W=2u L=100n
M2 out in gnd gnd nmos W=1u L=100n
.ends myinv
)");

    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    const auto& defs = parser.subcircuit_defs();

    ASSERT_EQ(defs.size(), 1u);
    ASSERT_NE(defs.find("myinv"), defs.end());

    const SubcircuitDef& def = defs.at("myinv");
    EXPECT_EQ(def.name, "myinv");
    ASSERT_EQ(def.ports.size(), 4u);
    EXPECT_EQ(def.ports[0], "in");
    EXPECT_EQ(def.ports[1], "out");
    EXPECT_EQ(def.ports[2], "vdd");
    EXPECT_EQ(def.ports[3], "gnd");

    // Body should contain 2 lines (M1 and M2)
    EXPECT_EQ(def.body.size(), 2u);

    // No default params
    EXPECT_TRUE(def.default_params.empty());
}

// -----------------------------------------------------------------------
// 2. Parameter defaults in the header
// -----------------------------------------------------------------------
TEST(Subcircuit, ParameterDefaults) {
    std::string netlist = wrap(R"(
.subckt inv in out wp=2u wn=1u
M1 out in vdd vdd pmos W=wp L=100n
M2 out in gnd gnd nmos W=wn L=100n
.ends
)");

    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    const auto& defs = parser.subcircuit_defs();

    ASSERT_NE(defs.find("inv"), defs.end());
    const SubcircuitDef& def = defs.at("inv");

    EXPECT_EQ(def.ports.size(), 2u);
    EXPECT_EQ(def.ports[0], "in");
    EXPECT_EQ(def.ports[1], "out");

    ASSERT_EQ(def.default_params.size(), 2u);
    EXPECT_EQ(def.default_params[0].first,  "wp");
    EXPECT_EQ(def.default_params[0].second, "2u");
    EXPECT_EQ(def.default_params[1].first,  "wn");
    EXPECT_EQ(def.default_params[1].second, "1u");
}

// -----------------------------------------------------------------------
// 3. Braced expression in parameter defaults
// -----------------------------------------------------------------------
TEST(Subcircuit, BracedParameterDefaults) {
    std::string netlist = wrap(R"(
.subckt inv in out wn=1u wp={2*wn}
.ends
)");

    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    const auto& defs = parser.subcircuit_defs();

    ASSERT_NE(defs.find("inv"), defs.end());
    const SubcircuitDef& def = defs.at("inv");

    ASSERT_EQ(def.default_params.size(), 2u);
    EXPECT_EQ(def.default_params[0].first,  "wn");
    EXPECT_EQ(def.default_params[0].second, "1u");
    EXPECT_EQ(def.default_params[1].first,  "wp");
    EXPECT_EQ(def.default_params[1].second, "{2*wn}");
}

// -----------------------------------------------------------------------
// 4. Nested subcircuit definitions
// -----------------------------------------------------------------------
TEST(Subcircuit, NestedSubcircuits) {
    std::string netlist = wrap(R"(
.subckt outer a b
.subckt inner x y
R1 x y 1k
.ends inner
Xinst inner a b
.ends outer
)");

    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    const auto& defs = parser.subcircuit_defs();

    // Only the outer subcircuit should be in the top-level defs map;
    // the inner definition is stored as raw lines in outer's body.
    ASSERT_EQ(defs.size(), 1u);
    ASSERT_NE(defs.find("outer"), defs.end());

    const SubcircuitDef& outer = defs.at("outer");
    EXPECT_EQ(outer.ports.size(), 2u);

    // The outer body must contain the inner .subckt block as raw lines:
    // reconstructed header (.subckt inner x y), body line(s), and .ends inner
    bool found_inner_subckt = false;
    bool found_ends_inner   = false;
    for (const auto& bl : outer.body) {
        if (!bl.tokens.empty()) {
            std::string first = bl.tokens[0];
            if (first == ".subckt" && bl.tokens.size() >= 2 && bl.tokens[1] == "inner")
                found_inner_subckt = true;
            if (first == ".ends")
                found_ends_inner = true;
        }
    }
    EXPECT_TRUE(found_inner_subckt) << "outer body should contain .subckt inner header";
    EXPECT_TRUE(found_ends_inner)   << "outer body should contain .ends for inner";
}

// -----------------------------------------------------------------------
// 5. Multiple independent subcircuit definitions
// -----------------------------------------------------------------------
TEST(Subcircuit, MultipleSubcircuits) {
    std::string netlist = wrap(R"(
.subckt inv in out
M1 out in vdd vdd pmos
.ends inv
.subckt buf in out
Xinv1 inv in mid
Xinv2 inv mid out
.ends buf
)");

    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    const auto& defs = parser.subcircuit_defs();

    ASSERT_EQ(defs.size(), 2u);
    EXPECT_NE(defs.find("inv"), defs.end());
    EXPECT_NE(defs.find("buf"), defs.end());

    EXPECT_EQ(defs.at("inv").ports.size(), 2u);
    EXPECT_EQ(defs.at("buf").ports.size(), 2u);
    EXPECT_EQ(defs.at("inv").body.size(), 1u);
    EXPECT_EQ(defs.at("buf").body.size(), 2u);
}

// -----------------------------------------------------------------------
// 6. Unmatched .subckt (missing .ends) => ParseError
// -----------------------------------------------------------------------
TEST(Subcircuit, UnmatchedSubckt) {
    std::string netlist = wrap(R"(
.subckt orphan a b
R1 a b 1k
)");

    NetlistParser parser;
    EXPECT_THROW(parser.parse(netlist), ParseError);
}

// -----------------------------------------------------------------------
// 7. Unmatched .ends (no .subckt) => ParseError
// -----------------------------------------------------------------------
TEST(Subcircuit, UnmatchedEnds) {
    std::string netlist = wrap(R"(
R1 a b 1k
.ends
)");

    NetlistParser parser;
    EXPECT_THROW(parser.parse(netlist), ParseError);
}

// -----------------------------------------------------------------------
// 8. .ends with name
// -----------------------------------------------------------------------
TEST(Subcircuit, EndsWithName) {
    std::string netlist = wrap(R"(
.subckt myblock a b
R1 a b 1k
.ends myblock
)");

    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    const auto& defs = parser.subcircuit_defs();

    ASSERT_NE(defs.find("myblock"), defs.end());
    EXPECT_EQ(defs.at("myblock").body.size(), 1u);
}

// -----------------------------------------------------------------------
// 9. .ends without name (bare .ends)
// -----------------------------------------------------------------------
TEST(Subcircuit, EndsWithoutName) {
    std::string netlist = wrap(R"(
.subckt bareends a b
R1 a b 1k
.ends
)");

    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    const auto& defs = parser.subcircuit_defs();

    ASSERT_NE(defs.find("bareends"), defs.end());
    EXPECT_EQ(defs.at("bareends").body.size(), 1u);
}

// -----------------------------------------------------------------------
// 10. .model and .param inside subcircuit body are NOT consumed by Pass 1
// -----------------------------------------------------------------------
TEST(Subcircuit, ModelAndParamInBody) {
    std::string netlist = wrap(R"(
.subckt diode_cell a k
.model mydiode D IS=1e-14
.param rleak=1Meg
D1 a k mydiode
R1 a k rleak
.ends diode_cell
)");

    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    const auto& defs = parser.subcircuit_defs();

    ASSERT_NE(defs.find("diode_cell"), defs.end());
    const SubcircuitDef& def = defs.at("diode_cell");

    // Body should contain all 4 lines (model, param, D1, R1)
    EXPECT_EQ(def.body.size(), 4u);

    // The top-level circuit should NOT have the subcircuit's .model or .param
    // visible as top-level models or params (they're only in the body).
    // (Top-level circuit has no devices — no X elements parsed yet.)
    EXPECT_EQ(ckt.devices().size(), 0u);
}

// -----------------------------------------------------------------------
// 11. X element referencing unknown subcircuit throws ParseError
// -----------------------------------------------------------------------
TEST(Subcircuit, XElementUnknownSubcircuitThrows) {
    // X element referencing a subcircuit that doesn't exist should throw
    // a ParseError now that expansion is implemented.
    std::string netlist = wrap(R"(
R1 a b 1k
Xinv inv a b
.op
)");

    NetlistParser parser;
    EXPECT_THROW(parser.parse(netlist), ParseError);
}

// -----------------------------------------------------------------------
// 11b. X element with defined subcircuit expands correctly
// -----------------------------------------------------------------------
TEST(Subcircuit, XElementExpandsWhenDefined) {
    // X element referencing a defined subcircuit should expand without error
    std::string netlist = wrap(R"(
.subckt inv a b
R1 a b 1k
.ends inv

R_top a b 1k
Xinv a b inv
.op
)");

    NetlistParser parser;
    EXPECT_NO_THROW({
        auto ckt = parser.parse(netlist);
        EXPECT_EQ(ckt.devices().size(), 2u); // R_top + xinv.r1
    });
}

// -----------------------------------------------------------------------
// 12. Subcircuit names are case-insensitive (stored lowercase)
// -----------------------------------------------------------------------
TEST(Subcircuit, NamesAreLowercase) {
    std::string netlist = wrap(R"(
.subckt MyBlock A B
R1 A B 1k
.ends MyBlock
)");

    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    const auto& defs = parser.subcircuit_defs();

    // Name should be stored in lowercase
    EXPECT_NE(defs.find("myblock"), defs.end());
    EXPECT_EQ(defs.find("MyBlock"), defs.end());

    // Ports stored in lowercase too
    EXPECT_EQ(defs.at("myblock").ports[0], "a");
    EXPECT_EQ(defs.at("myblock").ports[1], "b");
}

// -----------------------------------------------------------------------
// 13. Port after parameter default in header => ParseError
// -----------------------------------------------------------------------
TEST(Subcircuit, PortAfterParamThrows) {
    // "out" appears after "wp=2u" — invalid per SPICE standard
    std::string netlist = wrap(R"(
.subckt bad_order in wp=2u out
R1 in out 1k
.ends
)");

    NetlistParser parser;
    EXPECT_THROW(parser.parse(netlist), ParseError);
}

// -----------------------------------------------------------------------
// 14. Subcircuit lines are not processed by Pass 1/Pass 2
// -----------------------------------------------------------------------
TEST(Subcircuit, SubcircuitLinesNotInTopLevel) {
    // This netlist has a .op analysis at top level and a subcircuit with
    // its own R inside. The top-level circuit should have no devices
    // (the R inside is not instantiated at top level).
    std::string netlist = wrap(R"(
.subckt block a b
R1 a b 100
.ends
.op
)");

    NetlistParser parser;
    auto ckt = parser.parse(netlist);

    EXPECT_EQ(ckt.devices().size(), 0u);
    ASSERT_EQ(ckt.analyses.size(), 1u);
    EXPECT_EQ(ckt.analyses[0].type, AnalysisCommand::OP);
}

// -----------------------------------------------------------------------
// 15. K element inside subcircuit — inductor names get prefixed
// -----------------------------------------------------------------------
TEST(Subcircuit, KElementInductorNamesPrefixed) {
    // After expansion, the K element inside the subcircuit should reference
    // "xinv.l1" and "xinv.l2" — not the bare "l1"/"l2".
    std::string netlist = wrap(R"(
.subckt xfmr pri sec
L1 pri 0 1mH
L2 sec 0 4mH
K1 L1 L2 0.99
.ends xfmr

Xinv pri sec xfmr
.op
)");

    NetlistParser parser;
    auto ckt = parser.parse(netlist);

    // Find the CoupledInductor device
    const CoupledInductor* ki = nullptr;
    for (const auto& dev : ckt.devices()) {
        if (const auto* k = dynamic_cast<const CoupledInductor*>(dev.get())) {
            ki = k;
            break;
        }
    }
    ASSERT_NE(ki, nullptr) << "No CoupledInductor found after subcircuit expansion";

    // The K element references the expanded inductor names
    ASSERT_NE(ki->inductor1(), nullptr);
    ASSERT_NE(ki->inductor2(), nullptr);

    // Inductor names should carry the instance prefix "xinv."
    std::string l1_name = ki->inductor1()->name();
    std::string l2_name = ki->inductor2()->name();
    EXPECT_EQ(l1_name, "xinv.l1");
    EXPECT_EQ(l2_name, "xinv.l2");

    EXPECT_DOUBLE_EQ(ki->coupling(), 0.99);
}

// -----------------------------------------------------------------------
// 16. K element in subcircuit — integration: transformer AC simulation
// -----------------------------------------------------------------------
TEST(Subcircuit, KElementInSubcircuitACTransformer) {
    // A transformer (L1, L2, K1) wrapped in a subcircuit should produce the
    // same AC voltage-ratio behaviour as a flat (non-subcircuit) equivalent.
    //
    //   .subckt xfmr pri sec
    //   L1 pri 0 1mH
    //   L2 sec 0 4mH
    //   K1 L1 L2 0.999
    //   .ends xfmr
    //
    //   V1 in 0 AC 1
    //   R1 in pri 1
    //   Xtr pri sec xfmr      ; expands to xtr.l1, xtr.l2, coupling k
    //   R2 sec 0 1k
    //   .ac dec 1 1k 1k
    //
    // Expected: V(sec) ~ 2 * V(pri) (sqrt(L2/L1) = 2) at 1 kHz with k~1.
    std::string netlist = wrap(R"(
.subckt xfmr pri sec
L1 pri 0 1mH
L2 sec 0 4mH
K1 L1 L2 0.999
.ends xfmr

V1 in 0 DC 0 AC 1
R1 in pri 1
Xtr pri sec xfmr
R2 sec 0 1k
.ac dec 1 1k 1k
)");

    NetlistParser parser;
    auto ckt = parser.parse(netlist);

    auto result = solve_ac(ckt, AnalysisCommand::DEC, 1, 1e3, 1e3);
    ASSERT_EQ(result.frequency.size(), 1u);

    auto it_sec = result.voltages.find("v(sec)");
    ASSERT_NE(it_sec, result.voltages.end())
        << "v(sec) not found in AC result (keys: check node names after expansion)";

    double v_sec_mag = std::abs(it_sec->second[0]);
    EXPECT_NEAR(v_sec_mag, 2.0, 0.3)
        << "Transformer gain is " << v_sec_mag << "; expected ~2 (sqrt(L2/L1))";
}
