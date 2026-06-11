#include <gtest/gtest.h>
#include "parser/netlist_parser.hpp"
#include "parser/subcircuit.hpp"
#include "parser/subcircuit_expand.hpp"
#include "parser/tokenizer.hpp"
#include "core/dc.hpp"
#include "devices/resistor.hpp"
#include "devices/vsource.hpp"
#include "devices/ccvs.hpp"
#include "devices/cccs.hpp"

using namespace neospice;

// Helper: build a minimal netlist with a title line and .end terminator
static std::string wrap(const std::string& body) {
    return "Test netlist\n" + body + "\n.end\n";
}

// -----------------------------------------------------------------------
// 1. Simple expansion: resistor divider subcircuit, instantiate once
// -----------------------------------------------------------------------
TEST(SubcircuitExpand, SimpleExpansion) {
    std::string netlist = wrap(R"(
.subckt rdiv in out
R1 in mid 1k
R2 mid out 1k
.ends rdiv

V1 inp 0 10
X1 inp outp rdiv
R3 outp 0 1Meg
.op
)");

    NetlistParser parser;
    auto ckt = parser.parse(netlist);

    // Should have: V1, x1.r1, x1.r2, R3 => 4 devices
    EXPECT_EQ(ckt.devices().size(), 4u);

    // Verify that internal node 'mid' was prefixed to 'x1.mid'
    // The device x1.r1 should connect inp to x1.mid
    // The device x1.r2 should connect x1.mid to outp
    bool found_r1 = false, found_r2 = false;
    for (const auto& dev : ckt.devices()) {
        if (dev->name() == "x1.r1") found_r1 = true;
        if (dev->name() == "x1.r2") found_r2 = true;
    }
    EXPECT_TRUE(found_r1) << "Expected device x1.r1 from expansion";
    EXPECT_TRUE(found_r2) << "Expected device x1.r2 from expansion";
}

// -----------------------------------------------------------------------
// 2. Multiple instances: same subcircuit instantiated 3 times
// -----------------------------------------------------------------------
TEST(SubcircuitExpand, MultipleInstances) {
    std::string netlist = wrap(R"(
.subckt rdiv in out
R1 in mid 1k
R2 mid out 1k
.ends rdiv

V1 a 0 10
X1 a b rdiv
X2 b c rdiv
X3 c d rdiv
R_load d 0 1Meg
.op
)");

    NetlistParser parser;
    auto ckt = parser.parse(netlist);

    // V1 + 3*(R1+R2) + R_load = 1 + 6 + 1 = 8 devices
    EXPECT_EQ(ckt.devices().size(), 8u);

    // Verify unique internal nodes: x1.mid, x2.mid, x3.mid
    // Check all 6 expanded resistors exist
    std::vector<std::string> expected_names = {
        "x1.r1", "x1.r2", "x2.r1", "x2.r2", "x3.r1", "x3.r2"
    };
    for (const auto& expected : expected_names) {
        bool found = false;
        for (const auto& dev : ckt.devices()) {
            if (dev->name() == expected) { found = true; break; }
        }
        EXPECT_TRUE(found) << "Expected device '" << expected << "' not found";
    }
}

// -----------------------------------------------------------------------
// 3. Parameter override: subcircuit with default R=1k, instance overrides R=2k
// -----------------------------------------------------------------------
TEST(SubcircuitExpand, ParameterOverride) {
    std::string netlist = wrap(R"(
.subckt rdiv in out r=1k
R1 in mid {r}
R2 mid out {r}
.ends rdiv

V1 inp 0 10
X1 inp outp rdiv r=2k
R_load outp 0 1Meg
.op
)");

    NetlistParser parser;
    auto ckt = parser.parse(netlist);

    // Check that expanded resistors use 2k (override), not 1k (default)
    for (const auto& dev : ckt.devices()) {
        auto* r = dynamic_cast<const Resistor*>(dev.get());
        if (r && (r->name() == "x1.r1" || r->name() == "x1.r2")) {
            EXPECT_NEAR(r->resistance(), 2000.0, 1e-6)
                << "Resistor " << r->name() << " should use overridden R=2k";
        }
    }
}

// -----------------------------------------------------------------------
// 4. Nested X instances: subcircuit A contains X instance of subcircuit B
// -----------------------------------------------------------------------
TEST(SubcircuitExpand, NestedExpansion) {
    std::string netlist = wrap(R"(
.subckt res2 a b
R1 a b 500
.ends res2

.subckt rdiv in out
X1 in mid res2
X2 mid out res2
.ends rdiv

V1 inp 0 10
Xdiv inp outp rdiv
R_load outp 0 1Meg
.op
)");

    NetlistParser parser;
    auto ckt = parser.parse(netlist);

    // After full expansion: V1, xdiv.x1.r1, xdiv.x2.r1, R_load => 4 devices
    EXPECT_EQ(ckt.devices().size(), 4u);

    // Check hierarchical device names
    bool found_nested_r1 = false, found_nested_r2 = false;
    for (const auto& dev : ckt.devices()) {
        if (dev->name() == "xdiv.x1.r1") found_nested_r1 = true;
        if (dev->name() == "xdiv.x2.r1") found_nested_r2 = true;
    }
    EXPECT_TRUE(found_nested_r1) << "Expected device xdiv.x1.r1 from nested expansion";
    EXPECT_TRUE(found_nested_r2) << "Expected device xdiv.x2.r1 from nested expansion";
}

// -----------------------------------------------------------------------
// 5. Port count mismatch => ParseError
// -----------------------------------------------------------------------
TEST(SubcircuitExpand, PortCountMismatchSkipsWithWarning) {
    std::string netlist = wrap(R"(
.subckt rdiv in out
R1 in out 1k
.ends rdiv

V1 a 0 10
X1 a b c rdiv
.op
)");

    NetlistParser parser;
    EXPECT_NO_THROW(parser.parse(netlist));
}

// -----------------------------------------------------------------------
// 6. Unknown subcircuit => warning + skip
// -----------------------------------------------------------------------
TEST(SubcircuitExpand, UnknownSubcircuitSkipsWithWarning) {
    std::string netlist = wrap(R"(
V1 a 0 10
X1 a b nonexistent
.op
)");

    NetlistParser parser;
    EXPECT_NO_THROW(parser.parse(netlist));
}

// -----------------------------------------------------------------------
// 7. Infinite recursion => ParseError (max depth)
// -----------------------------------------------------------------------
TEST(SubcircuitExpand, InfiniteRecursion) {
    std::string netlist = wrap(R"(
.subckt recursive a b
X1 a b recursive
.ends recursive

V1 inp 0 10
X1 inp outp recursive
.op
)");

    NetlistParser parser;
    EXPECT_THROW(parser.parse(netlist), ParseError);
}

// -----------------------------------------------------------------------
// 8. DC integration test: resistor divider via subcircuit, solve DC
// -----------------------------------------------------------------------
TEST(SubcircuitExpand, DCIntegration) {
    // Subcircuit is a divider: R1 from in to mid, R2 from mid to gnd (0).
    // 'out' is tapped from mid. This makes it a true voltage divider.
    std::string netlist = wrap(R"(
.subckt rdiv in out
R1 in out 1k
R2 out 0 1k
.ends rdiv

V1 inp 0 10
X1 inp outp rdiv
.op
)");

    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    DCResult result = solve_dc(ckt);

    // inp = 10V
    EXPECT_NEAR(result.voltage("inp"), 10.0, 1e-6);

    // outp = 10V * R2/(R1+R2) = 10 * 1k/2k = 5V
    EXPECT_NEAR(result.voltage("outp"), 5.0, 1e-3);
}

// -----------------------------------------------------------------------
// 9. Internal node naming: verify hierarchical prefixes
// -----------------------------------------------------------------------
TEST(SubcircuitExpand, InternalNodeNaming) {
    // 'inner' subcircuit has ports a,b and an internal node 'internal'
    std::string netlist = wrap(R"(
.subckt inner a b
R1 a internal 1k
R2 internal b 1k
.ends inner

.subckt outer x y
Xin x y inner
.ends outer

V1 top 0 5
Xouter top bot outer
R_load bot 0 1Meg
.op
)");

    NetlistParser parser;
    auto ckt = parser.parse(netlist);

    // The internal node 'internal' in 'inner', instantiated via
    // xouter -> outer -> xin -> inner, should be prefixed as:
    // xouter.xin.internal
    DCResult result = solve_dc(ckt);

    // Verify the hierarchical node exists
    EXPECT_TRUE(result.node_voltages.count("v(xouter.xin.internal)") > 0)
        << "Expected hierarchical internal node 'xouter.xin.internal'";
}

// -----------------------------------------------------------------------
// 10. Braced parameter expressions: R1 a b {R*2}
// -----------------------------------------------------------------------
TEST(SubcircuitExpand, BracedParameterExpressions) {
    std::string netlist = wrap(R"(
.subckt rdiv in out r=1k
R1 in mid {r}
R2 mid out {r*2}
.ends rdiv

V1 inp 0 10
X1 inp outp rdiv r=1k
R_load outp 0 1Meg
.op
)");

    NetlistParser parser;
    auto ckt = parser.parse(netlist);

    // R1 should be 1k, R2 should be 2k
    for (const auto& dev : ckt.devices()) {
        auto* r = dynamic_cast<const Resistor*>(dev.get());
        if (r && r->name() == "x1.r1") {
            EXPECT_NEAR(r->resistance(), 1000.0, 1e-6)
                << "R1 should be {r} = 1k";
        }
        if (r && r->name() == "x1.r2") {
            EXPECT_NEAR(r->resistance(), 2000.0, 1e-6)
                << "R2 should be {r*2} = 2k";
        }
    }
}

// -----------------------------------------------------------------------
// 11. Default parameter (no override) uses subcircuit default
// -----------------------------------------------------------------------
TEST(SubcircuitExpand, DefaultParameterUsed) {
    std::string netlist = wrap(R"(
.subckt rdiv in out r=1k
R1 in mid {r}
R2 mid out {r}
.ends rdiv

V1 inp 0 10
X1 inp outp rdiv
R_load outp 0 1Meg
.op
)");

    NetlistParser parser;
    auto ckt = parser.parse(netlist);

    // R1 and R2 should use default R=1k
    for (const auto& dev : ckt.devices()) {
        auto* r = dynamic_cast<const Resistor*>(dev.get());
        if (r && (r->name() == "x1.r1" || r->name() == "x1.r2")) {
            EXPECT_NEAR(r->resistance(), 1000.0, 1e-6)
                << "Resistor " << r->name() << " should use default R=1k";
        }
    }
}

// -----------------------------------------------------------------------
// 12. Multiple instances of same subcircuit with different param overrides
// -----------------------------------------------------------------------
TEST(SubcircuitExpand, MultipleInstancesDifferentParams) {
    // Two resistor blocks in series from inp to ground, forming a divider.
    // X1 has R=1k, X2 has R=3k.
    std::string netlist = wrap(R"(
.subckt rblock a b r=1k
R1 a b {r}
.ends rblock

V1 inp 0 10
X1 inp mid rblock r=1k
X2 mid 0 rblock r=3k
.op
)");

    NetlistParser parser;
    auto ckt = parser.parse(netlist);

    for (const auto& dev : ckt.devices()) {
        auto* r = dynamic_cast<const Resistor*>(dev.get());
        if (r && r->name() == "x1.r1") {
            EXPECT_NEAR(r->resistance(), 1000.0, 1e-6);
        }
        if (r && r->name() == "x2.r1") {
            EXPECT_NEAR(r->resistance(), 3000.0, 1e-6);
        }
    }

    // DC solve: voltage divider with 1k on top, 3k on bottom
    DCResult result = solve_dc(ckt);
    // V(mid) = 10 * 3k/(1k+3k) = 7.5V
    EXPECT_NEAR(result.voltage("mid"), 7.5, 1e-3);
}

// -----------------------------------------------------------------------
// 13. Ground nodes ('0' and 'gnd') are global and never prefixed
// -----------------------------------------------------------------------
TEST(SubcircuitExpand, GroundIsGlobal) {
    std::string netlist = wrap(R"(
.subckt bypass in
R1 in 0 1k
.ends bypass

V1 inp 0 10
X1 inp bypass
.op
)");

    NetlistParser parser;
    auto ckt = parser.parse(netlist);

    // Should have V1 and x1.r1
    EXPECT_EQ(ckt.devices().size(), 2u);

    // DC solve: R1 connects inp to ground via 0
    DCResult result = solve_dc(ckt);
    EXPECT_NEAR(result.voltage("inp"), 10.0, 1e-6);
}

// -----------------------------------------------------------------------
// 14. Nested subcircuit definition within body
// -----------------------------------------------------------------------
TEST(SubcircuitExpand, NestedSubcircuitInBody) {
    std::string netlist = wrap(R"(
.subckt outer a b
.subckt inner x y
R1 x y 1k
.ends inner
Xin a b inner
.ends outer

V1 inp 0 10
Xout inp outp outer
R_load outp 0 1Meg
.op
)");

    NetlistParser parser;
    auto ckt = parser.parse(netlist);

    // Should expand to: V1, xout.xin.r1, R_load => 3 devices
    EXPECT_EQ(ckt.devices().size(), 3u);

    bool found = false;
    for (const auto& dev : ckt.devices()) {
        if (dev->name() == "xout.xin.r1") { found = true; break; }
    }
    EXPECT_TRUE(found) << "Expected device xout.xin.r1";
}

// -----------------------------------------------------------------------
// 15. Three-instance resistor divider integration test
// -----------------------------------------------------------------------
TEST(SubcircuitExpand, ThreeInstanceDCTest) {
    // Create a resistor divider subcircuit and instantiate 3 times
    // in series to form a voltage divider chain
    std::string netlist = wrap(R"(
.subckt rdiv in out
R1 in out 1k
.ends rdiv

V1 a 0 12
X1 a b rdiv
X2 b c rdiv
X3 c 0 rdiv
.op
)");

    NetlistParser parser;
    auto ckt = parser.parse(netlist);

    // 4 devices: V1, x1.r1, x2.r1, x3.r1
    EXPECT_EQ(ckt.devices().size(), 4u);

    DCResult result = solve_dc(ckt);

    // Three 1k resistors in series: 12V / 3 = 4V per resistor
    EXPECT_NEAR(result.voltage("a"), 12.0, 1e-6);
    EXPECT_NEAR(result.voltage("b"), 8.0, 1e-3);
    EXPECT_NEAR(result.voltage("c"), 4.0, 1e-3);
}

// -----------------------------------------------------------------------
// 16. Case-insensitive subcircuit matching
// -----------------------------------------------------------------------
TEST(SubcircuitExpand, CaseInsensitive) {
    std::string netlist = wrap(R"(
.subckt MyDiv IN OUT
R1 IN OUT 1k
.ends MyDiv

V1 a 0 5
X1 a b MYDIV
R_load b 0 1Meg
.op
)");

    NetlistParser parser;
    auto ckt = parser.parse(netlist);

    // Should find the subcircuit despite case differences
    EXPECT_EQ(ckt.devices().size(), 3u);
}

// -----------------------------------------------------------------------
// 17. Model inside subcircuit body is passed through
// -----------------------------------------------------------------------
TEST(SubcircuitExpand, ModelInBody) {
    std::string netlist = wrap(R"(
.subckt dcell a k
.model mydiode D IS=1e-14 N=1.0
D1 a k mydiode
.ends dcell

V1 inp 0 0.7
X1 inp 0 dcell
.op
)");

    NetlistParser parser;
    auto ckt = parser.parse(netlist);

    // Should have V1 and x1.d1
    EXPECT_EQ(ckt.devices().size(), 2u);
}

// -----------------------------------------------------------------------
// 18. Subcircuit with .param inside body
// -----------------------------------------------------------------------
TEST(SubcircuitExpand, ParamInBody) {
    std::string netlist = wrap(R"(
.subckt rdiv in out
.param rval=1k
R1 in mid {rval}
R2 mid out {rval}
.ends rdiv

V1 inp 0 10
X1 inp outp rdiv
R_load outp 0 1Meg
.op
)");

    NetlistParser parser;
    auto ckt = parser.parse(netlist);

    for (const auto& dev : ckt.devices()) {
        auto* r = dynamic_cast<const Resistor*>(dev.get());
        if (r && (r->name() == "x1.r1" || r->name() == "x1.r2")) {
            EXPECT_NEAR(r->resistance(), 1000.0, 1e-6)
                << "Resistor " << r->name() << " should use .param rval=1k";
        }
    }
}

// -----------------------------------------------------------------------
// 19. CCVS/CCCS Vsense names are hierarchically prefixed
// -----------------------------------------------------------------------
TEST(SubcircuitExpand, CCVSVsenseHierarchicalPrefix) {
    // Subcircuit contains a VSource (Vsense) and a CCVS that references it.
    // After expansion as X1, the CCVS should reference "x1.vsense" not "vsense".
    std::string netlist = wrap(R"(
.subckt ccvs_block np nn
Vsense np mid 0
H1 mid nn Vsense 100
.ends ccvs_block

V1 inp 0 1
X1 inp outp ccvs_block
R_load outp 0 1k
.op
)");

    NetlistParser parser;
    auto ckt = parser.parse(netlist);

    // Should have: V1, x1.vsense, x1.h1, R_load => 4 devices
    EXPECT_EQ(ckt.devices().size(), 4u);

    // Verify the CCVS device x1.h1 exists and references x1.vsense
    bool found_ccvs = false;
    for (const auto& dev : ckt.devices()) {
        if (dev->name() == "x1.h1") {
            found_ccvs = true;
            auto* h = dynamic_cast<const CCVS*>(dev.get());
            ASSERT_NE(h, nullptr) << "x1.h1 should be a CCVS device";
        }
    }
    EXPECT_TRUE(found_ccvs) << "Expected CCVS device x1.h1 from expansion";

    // Verify the Vsense x1.vsense exists
    bool found_vsense = false;
    for (const auto& dev : ckt.devices()) {
        if (dev->name() == "x1.vsense") {
            found_vsense = true;
        }
    }
    EXPECT_TRUE(found_vsense) << "Expected VSource x1.vsense from expansion";
}

// -----------------------------------------------------------------------
// 20. CCCS Vsense names are hierarchically prefixed
// -----------------------------------------------------------------------
TEST(SubcircuitExpand, CCCSVsenseHierarchicalPrefix) {
    std::string netlist = wrap(R"(
.subckt cccs_block np nn
Vsense np mid 0
F1 mid nn Vsense 50
.ends cccs_block

V1 inp 0 1
X1 inp outp cccs_block
R_load outp 0 1k
.op
)");

    NetlistParser parser;
    auto ckt = parser.parse(netlist);

    // Should have: V1, x1.vsense, x1.f1, R_load => 4 devices
    EXPECT_EQ(ckt.devices().size(), 4u);

    bool found_cccs = false;
    for (const auto& dev : ckt.devices()) {
        if (dev->name() == "x1.f1") {
            found_cccs = true;
            auto* f = dynamic_cast<const CCCS*>(dev.get());
            ASSERT_NE(f, nullptr) << "x1.f1 should be a CCCS device";
        }
    }
    EXPECT_TRUE(found_cccs) << "Expected CCCS device x1.f1 from expansion";
}

// -----------------------------------------------------------------------
// 21. Top-level .param values visible during subcircuit expansion
// -----------------------------------------------------------------------
TEST(SubcircuitExpand, TopLevelParamVisibleDuringExpansion) {
    std::string netlist = wrap(R"(
.param myR=2k

.subckt rdiv in out r=1k
R1 in mid {r}
R2 mid out {r}
.ends rdiv

V1 inp 0 10
X1 inp outp rdiv r={myR}
R_load outp 0 1Meg
.op
)");

    NetlistParser parser;
    auto ckt = parser.parse(netlist);

    // R1 and R2 should use 2k from top-level .param myR=2k
    for (const auto& dev : ckt.devices()) {
        auto* r = dynamic_cast<const Resistor*>(dev.get());
        if (r && (r->name() == "x1.r1" || r->name() == "x1.r2")) {
            EXPECT_NEAR(r->resistance(), 2000.0, 1e-6)
                << "Resistor " << r->name()
                << " should use top-level .param myR=2k";
        }
    }
}

// -----------------------------------------------------------------------
// 22. Line numbers in depth-exceeded and port-mismatch errors
// -----------------------------------------------------------------------
TEST(SubcircuitExpand, ErrorMessagesContainLineNumbers) {
    // Port count mismatch now truncates with warning (no throw)
    {
        std::string netlist = wrap(R"(
.subckt rdiv in out
R1 in out 1k
.ends rdiv

V1 a 0 10
X1 a b c rdiv
.op
)");
        NetlistParser parser;
        EXPECT_NO_THROW(parser.parse(netlist));
    }

    // Infinite recursion should include "Line N:"
    {
        std::string netlist = wrap(R"(
.subckt recursive a b
X1 a b recursive
.ends recursive

V1 inp 0 10
X1 inp outp recursive
.op
)");
        NetlistParser parser;
        try {
            parser.parse(netlist);
            FAIL() << "Expected ParseError for infinite recursion";
        } catch (const ParseError& e) {
            std::string msg = e.what();
            EXPECT_TRUE(msg.find("Line ") != std::string::npos)
                << "Error should contain 'Line ': " << msg;
        }
    }
}

// -----------------------------------------------------------------------
// 23. .global node declaration: global nodes are not prefixed
// -----------------------------------------------------------------------
TEST(SubcircuitExpand, GlobalNodeNotPrefixed) {
    // vdd is declared .global — it should NOT be prefixed inside subcircuits.
    // Without .global, vdd inside the subcircuit would become x1.vdd and
    // x2.vdd, disconnected from the top-level V1.
    std::string netlist = wrap(R"(
.global vdd
.subckt inv in out
R1 in mid 1k
R2 mid out 1k
R3 mid vdd 1k
.ends inv
V1 vdd 0 5
X1 a b inv
X2 b c inv
R4 a 0 1k
R5 c 0 1k
.op
)");

    NetlistParser parser;
    auto ckt = parser.parse(netlist);

    // V1 + R4 + R5 + 2*(R1+R2+R3) = 1 + 1 + 1 + 6 = 9 devices
    EXPECT_EQ(ckt.devices().size(), 9u);

    // DC solve: vdd = 5V
    DCResult result = solve_dc(ckt);
    EXPECT_NEAR(result.voltage("vdd"), 5.0, 1e-6);

    // Verify that x1.mid and x2.mid exist (internal nodes ARE prefixed)
    EXPECT_TRUE(result.node_voltages.count("v(x1.mid)") > 0)
        << "Internal node x1.mid should exist";
    EXPECT_TRUE(result.node_voltages.count("v(x2.mid)") > 0)
        << "Internal node x2.mid should exist";

    // Verify that x1.vdd does NOT exist (global node should NOT be prefixed)
    EXPECT_TRUE(result.node_voltages.count("v(x1.vdd)") == 0)
        << "Global node vdd should NOT be prefixed as x1.vdd";
    EXPECT_TRUE(result.node_voltages.count("v(x2.vdd)") == 0)
        << "Global node vdd should NOT be prefixed as x2.vdd";
}

// -----------------------------------------------------------------------
// 24. .global is case-insensitive
// -----------------------------------------------------------------------
TEST(SubcircuitExpand, GlobalNodeCaseInsensitive) {
    // .GLOBAL VDD should work the same as .global vdd
    std::string netlist = wrap(R"(
.GLOBAL VDD
.subckt res_to_vdd in
R1 in VDD 1k
.ends res_to_vdd
V1 VDD 0 3.3
X1 a res_to_vdd
R2 a 0 1k
.op
)");

    NetlistParser parser;
    auto ckt = parser.parse(netlist);

    DCResult result = solve_dc(ckt);
    EXPECT_NEAR(result.voltage("vdd"), 3.3, 1e-6);

    // vdd should not be prefixed
    EXPECT_TRUE(result.node_voltages.count("v(x1.vdd)") == 0)
        << "Global node vdd should NOT be prefixed as x1.vdd";
}

// -----------------------------------------------------------------------
// 25. Multiple .global lines accumulate
// -----------------------------------------------------------------------
TEST(SubcircuitExpand, MultipleGlobalLines) {
    std::string netlist = wrap(R"(
.global vdd
.global vss
.subckt buf in out
R1 in vdd 1k
R2 in vss 1k
R3 in out 100
.ends buf
V1 vdd 0 5
V2 vss 0 -5
X1 a b buf
R4 a 0 1k
R5 b 0 1k
.op
)");

    NetlistParser parser;
    auto ckt = parser.parse(netlist);

    DCResult result = solve_dc(ckt);
    EXPECT_NEAR(result.voltage("vdd"), 5.0, 1e-6);
    EXPECT_NEAR(result.voltage("vss"), -5.0, 1e-6);

    // Neither vdd nor vss should be prefixed
    EXPECT_TRUE(result.node_voltages.count("v(x1.vdd)") == 0)
        << "Global node vdd should NOT be prefixed";
    EXPECT_TRUE(result.node_voltages.count("v(x1.vss)") == 0)
        << "Global node vss should NOT be prefixed";
}

// -----------------------------------------------------------------------
// 26. .global with multiple nodes on one line
// -----------------------------------------------------------------------
TEST(SubcircuitExpand, GlobalMultipleNodesOneLine) {
    std::string netlist = wrap(R"(
.global vdd vss
.subckt buf in out
R1 in vdd 1k
R2 in vss 1k
R3 in out 100
.ends buf
V1 vdd 0 5
V2 vss 0 -5
X1 a b buf
R4 a 0 1k
R5 b 0 1k
.op
)");

    NetlistParser parser;
    auto ckt = parser.parse(netlist);

    DCResult result = solve_dc(ckt);
    EXPECT_NEAR(result.voltage("vdd"), 5.0, 1e-6);
    EXPECT_NEAR(result.voltage("vss"), -5.0, 1e-6);

    EXPECT_TRUE(result.node_voltages.count("v(x1.vdd)") == 0)
        << "Global node vdd should NOT be prefixed";
    EXPECT_TRUE(result.node_voltages.count("v(x1.vss)") == 0)
        << "Global node vss should NOT be prefixed";
}

// -----------------------------------------------------------------------
// 27. VALUE expression parameter substitution
// -----------------------------------------------------------------------
TEST(SubcircuitExpand, ValueExpressionParamSubstitution) {
    // Subcircuit with a VALUE expression referencing a parameter by name.
    // After expansion, bare param name 'gain' must be replaced with its
    // numeric value so the expression compiler can evaluate it.
    std::string netlist = wrap(R"(
.subckt myamp in out params: gain=2
E1 out 0 VALUE {V(in)*gain}
.ends
X1 inp outp myamp gain=3
VIN inp 0 DC 1
RLOAD outp 0 1k
.op
)");

    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    DCResult result = solve_dc(ckt);

    // V(in) = 1V, gain = 3 => V(out) = 3V
    EXPECT_NEAR(result.voltage("outp"), 3.0, 0.01);
}

// -----------------------------------------------------------------------
// 28. .global in nested subcircuit hierarchy
// -----------------------------------------------------------------------
TEST(SubcircuitExpand, GlobalNodeNestedHierarchy) {
    // vdd should remain unprefixed even in nested subcircuit expansion
    std::string netlist = wrap(R"(
.global vdd
.subckt inner a b
R1 a vdd 1k
R2 a b 1k
.ends inner
.subckt outer x y
Xin x y inner
.ends outer
V1 vdd 0 5
Xouter top bot outer
R_load top 0 1k
R_load2 bot 0 1k
.op
)");

    NetlistParser parser;
    auto ckt = parser.parse(netlist);

    DCResult result = solve_dc(ckt);
    EXPECT_NEAR(result.voltage("vdd"), 5.0, 1e-6);

    // vdd should NOT appear as xouter.xin.vdd
    EXPECT_TRUE(result.node_voltages.count("v(xouter.xin.vdd)") == 0)
        << "Global node vdd should NOT be prefixed in nested hierarchy";
}

// -----------------------------------------------------------------------
// Subcircuit-local .func calls are inlined into device VALUE expressions.
//
// Regression: expand_instance previously dropped every '.func' line in a
// subckt body at the "skip dot commands" continue, so a G/B/E VALUE
// expression that called a subckt-local user function reached the ASRC
// parser unexpanded and was skipped with "unknown function", leaving the
// source electrically dead. (Infineon OptiMOS3_80V: G_chan/G_diode use
// .FUNC J / .FUNC Idiod defined inside the same subckt.)
// -----------------------------------------------------------------------
TEST(SubcircuitExpand, LocalFuncInlinedIntoGSource) {
    // jj(d,g) = d*g+1. With V(din)=3, the G source drives 3*2+1 = 7 A into the
    // 'dout' port, which the test wires to a 1 ohm load to ground => V=7. If
    // the subckt-local .func were dropped, the G source would be skipped and
    // V(dout) would be 0.
    std::string netlist = wrap(R"(
.subckt mysub din dout
.func jj(d,g) {d*g+1}
G1 0 dout VALUE={jj(V(din),2)}
.ends
V1 n1 0 3
X1 n1 out mysub
Rload out 0 1
.op
)");

    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    DCResult result = solve_dc(ckt);
    EXPECT_TRUE(result.status.converged);
    EXPECT_NEAR(result.voltage("out"), 7.0, 1e-6);
}

// -----------------------------------------------------------------------
// Hygienic func substitution: a formal parameter's actual argument text must
// not be re-scanned for *other* formal parameter names.
//
// Regression: expand_funcs substituted formals sequentially, so for
// f(g,s) = g + s with call f(V(a,s), x), substituting g -> (V(a,s)) inserted
// an 's' that the subsequent 's' pass then captured, corrupting the
// expression into V(a,(x)). The single-pass substitution fixes this.
// -----------------------------------------------------------------------
TEST(SubcircuitExpand, FuncSubstitutionIsHygienic) {
    // f(g,s) = g + s. Call f(V(a,b), 10). When g -> (V(a,b)) is substituted,
    // the 'b' inside the inserted V(a,b) must NOT be re-captured by the later
    // 's' substitution (the old sequential pass corrupted V(a,b) -> V(a,(10))).
    // Result current = V(a,b) + 10 = (5-0) + 10 = 15, driven into a 1 ohm load.
    std::string netlist = wrap(R"(
.subckt mysub a b out
.func f(g,s) {g+s}
G1 0 out VALUE={f(V(a,b),10)}
.ends
V1 n1 0 5
X1 n1 0 outp mysub
Rload outp 0 1
.op
)");

    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    DCResult result = solve_dc(ckt);
    EXPECT_TRUE(result.status.converged);
    // V(a,b) = V(n1)-V(0) = 5; current = 5 + 10 = 15 into 1 ohm => 15 V.
    EXPECT_NEAR(result.voltage("outp"), 15.0, 1e-6);
}

// -----------------------------------------------------------------------
// Whitespace-indented '+' continuation lines must be merged.
//
// Regression: the tokenizer only treated a line as a continuation when
// raw_line[0] == '+', so vendor libraries that indent continuations
// (e.g. "        +Rmax=...") silently dropped every parameter on the
// continuation, leaving subcircuit defaults in force. Here the second
// resistor value lives on an indented continuation of the X-line params.
// -----------------------------------------------------------------------
TEST(SubcircuitExpand, IndentedContinuationLineIsMerged) {
    std::string netlist =
        "Test netlist\n"
        ".subckt rr a b PARAMS: r1=1 r2=1\n"
        "Ra a m {r1}\n"
        "Rb m b {r2}\n"
        ".ends\n"
        "V1 inp 0 4\n"
        "X1 inp 0 rr PARAMS: r1=1k\n"
        "          +r2=3k\n"
        ".op\n"
        ".end\n";

    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    DCResult result = solve_dc(ckt);
    EXPECT_TRUE(result.status.converged);
    // Divider: V(x1.m) = 4 * r2/(r1+r2) = 4 * 3k/4k = 3.0. If the indented
    // continuation were dropped, r2 would default to 1 and give 4*1/(1000+1).
    EXPECT_NEAR(result.voltage("x1.m"), 3.0, 1e-3);
}
