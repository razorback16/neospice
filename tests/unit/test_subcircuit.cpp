#include <gtest/gtest.h>
#include "parser/netlist_parser.hpp"
#include "parser/subcircuit.hpp"

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
// 11. X element no longer throws unsupported
// -----------------------------------------------------------------------
TEST(Subcircuit, XElementNoLongerThrows) {
    // After removing 'x' from the unsupported list, an X element line
    // should be silently ignored (not throw).
    std::string netlist = wrap(R"(
R1 a b 1k
Xinv inv a b
.op
)");

    NetlistParser parser;
    // Should NOT throw
    EXPECT_NO_THROW({
        auto ckt = parser.parse(netlist);
        EXPECT_EQ(ckt.devices().size(), 1u); // only R1
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
