// Tests for .options netlist card parsing and simulation wiring.
#include <gtest/gtest.h>
#include "parser/netlist_parser.hpp"
#include "core/dc.hpp"
#include "core/types.hpp"

using namespace neospice;

// ---------------------------------------------------------------------------
// Parsing tests
// ---------------------------------------------------------------------------

TEST(Options, DefaultValues) {
    // Without .options, all fields should be their defaults.
    std::string netlist = R"(
Default options test
V1 a 0 1
R1 a 0 1k
.op
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    EXPECT_DOUBLE_EQ(ckt.options.reltol,  1e-3);
    EXPECT_DOUBLE_EQ(ckt.options.abstol,  1e-12);
    EXPECT_DOUBLE_EQ(ckt.options.vntol,   1e-6);
    EXPECT_DOUBLE_EQ(ckt.options.trtol,   7.0);
    EXPECT_DOUBLE_EQ(ckt.options.chgtol,  1e-14);
    EXPECT_DOUBLE_EQ(ckt.options.gmin,    1e-12);
    EXPECT_DOUBLE_EQ(ckt.options.temp,    T_NOMINAL);  // 300.15 K
    EXPECT_DOUBLE_EQ(ckt.options.tnom,    T_NOMINAL);  // 300.15 K
    EXPECT_EQ       (ckt.options.max_iter, 100);
    EXPECT_EQ       (ckt.options.itl1,     100);
    EXPECT_EQ       (ckt.options.itl4,      50);
    EXPECT_EQ       (ckt.options.method,   "trap");
}

TEST(Options, ParseReltolAbstol) {
    std::string netlist = R"(
Options reltol abstol
V1 a 0 1
R1 a 0 1k
.options reltol=1e-4 abstol=1e-12
.op
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    EXPECT_DOUBLE_EQ(ckt.options.reltol, 1e-4);
    EXPECT_DOUBLE_EQ(ckt.options.abstol, 1e-12);
    // Other fields unchanged
    EXPECT_DOUBLE_EQ(ckt.options.vntol, 1e-6);
}

TEST(Options, ParseSingularOptionAlias) {
    std::string netlist = R"(
Options singular option alias
V1 a 0 1
R1 a 0 1k
.option reltol=2e-4 method=gear
.op
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    EXPECT_DOUBLE_EQ(ckt.options.reltol, 2e-4);
    EXPECT_EQ(ckt.options.method, "gear");
}

TEST(Options, ParseTempConvertedToKelvin) {
    // .options temp=85 should store 85 + 273.15 = 358.15 K
    std::string netlist = R"(
Options temp test
V1 a 0 1
R1 a 0 1k
.options temp=85
.op
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    EXPECT_DOUBLE_EQ(ckt.options.temp, 85.0 + 273.15);
}

TEST(Options, ParseTnomConvertedToKelvin) {
    // .options tnom=27 should store 27 + 273.15 = 300.15 K
    std::string netlist = R"(
Options tnom test
V1 a 0 1
R1 a 0 1k
.options tnom=27
.op
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    EXPECT_DOUBLE_EQ(ckt.options.tnom, 27.0 + 273.15);
}

TEST(Options, ParseMethodGear) {
    std::string netlist = R"(
Options method test
V1 a 0 1
R1 a 0 1k
.options method=gear
.op
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    EXPECT_EQ(ckt.options.method, "gear");
}

TEST(Options, ParseMethodTrap) {
    std::string netlist = R"(
Options method trap test
V1 a 0 1
R1 a 0 1k
.options method=trap
.op
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    EXPECT_EQ(ckt.options.method, "trap");
}

TEST(Options, ParseMethodCaseInsensitive) {
    std::string netlist = R"(
Options method case test
V1 a 0 1
R1 a 0 1k
.options METHOD=GEAR
.op
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    EXPECT_EQ(ckt.options.method, "gear");
}

TEST(Options, ParseItl1AndItl4) {
    std::string netlist = R"(
Options itl1 itl4 test
V1 a 0 1
R1 a 0 1k
.options itl1=200 itl4=50
.op
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    EXPECT_EQ(ckt.options.itl1,     200);
    EXPECT_EQ(ckt.options.max_iter, 200); // itl1 also updates max_iter
    EXPECT_EQ(ckt.options.itl4,      50);
}

TEST(Options, ParseChgtol) {
    std::string netlist = R"(
Options chgtol test
V1 a 0 1
R1 a 0 1k
.options chgtol=1e-14
.op
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    EXPECT_DOUBLE_EQ(ckt.options.chgtol, 1e-14);
}

TEST(Options, ParseGmin) {
    std::string netlist = R"(
Options gmin test
V1 a 0 1
R1 a 0 1k
.options gmin=1e-12
.op
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    EXPECT_DOUBLE_EQ(ckt.options.gmin, 1e-12);
}

TEST(Options, ParseMultipleOptionsOnOneLine) {
    std::string netlist = R"(
Options multiple on one line
V1 a 0 1
R1 a 0 1k
.options reltol=1e-4 abstol=1e-12 vntol=1e-6 trtol=7 chgtol=1e-14 gmin=1e-12 temp=85 tnom=27 method=gear itl1=200 itl4=50
.op
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    EXPECT_DOUBLE_EQ(ckt.options.reltol, 1e-4);
    EXPECT_DOUBLE_EQ(ckt.options.abstol, 1e-12);
    EXPECT_DOUBLE_EQ(ckt.options.vntol,  1e-6);
    EXPECT_DOUBLE_EQ(ckt.options.trtol,  7.0);
    EXPECT_DOUBLE_EQ(ckt.options.chgtol, 1e-14);
    EXPECT_DOUBLE_EQ(ckt.options.gmin,   1e-12);
    EXPECT_DOUBLE_EQ(ckt.options.temp,   85.0 + 273.15);
    EXPECT_DOUBLE_EQ(ckt.options.tnom,   27.0 + 273.15);
    EXPECT_EQ       (ckt.options.method, "gear");
    EXPECT_EQ       (ckt.options.itl1,   200);
    EXPECT_EQ       (ckt.options.itl4,    50);
}

TEST(Options, CaseInsensitiveKeyNames) {
    std::string netlist = R"(
Options case insensitive keys
V1 a 0 1
R1 a 0 1k
.OPTIONS RELTOL=1e-4 ABSTOL=1e-11
.op
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    EXPECT_DOUBLE_EQ(ckt.options.reltol, 1e-4);
    EXPECT_DOUBLE_EQ(ckt.options.abstol, 1e-11);
}

// ---------------------------------------------------------------------------
// Simulation integration tests
// ---------------------------------------------------------------------------

TEST(Options, ReltolAffectsDCConvergence) {
    // A simple resistor divider should converge even with a tighter reltol.
    std::string netlist = R"(
Reltol DC test
V1 in 0 DC 5
R1 in out 1k
R2 out 0 1k
.options reltol=1e-6
.op
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    ASSERT_DOUBLE_EQ(ckt.options.reltol, 1e-6);

    // Should converge without throwing
    ASSERT_NO_THROW({
        auto result = solve_dc(ckt);
        EXPECT_NEAR(result.voltage("out"), 2.5, 1e-6);
    });
}

TEST(Options, ParseLteRefMode) {
    std::string netlist = R"(
Options lte_ref_mode test
V1 a 0 1
R1 a 0 1k
.options lte_ref_mode=1
.op
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    EXPECT_EQ(ckt.options.lte_ref_mode, 1);
}

TEST(Options, ParseLteRefMode2) {
    std::string netlist = R"(
Options lte_ref_mode=2 test
V1 a 0 1
R1 a 0 1k
.options lte_ref_mode=2
.op
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    EXPECT_EQ(ckt.options.lte_ref_mode, 2);
}

TEST(Options, DefaultLteRefModeIsZero) {
    std::string netlist = R"(
Default lte_ref_mode test
V1 a 0 1
R1 a 0 1k
.op
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    EXPECT_EQ(ckt.options.lte_ref_mode, 0);
}

TEST(Options, DefaultTempNoOptionsCard) {
    // When no .options card is present, temp defaults to T_NOMINAL (300.15 K).
    std::string netlist = R"(
No options card
V1 a 0 1
R1 a 0 1k
.op
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    EXPECT_DOUBLE_EQ(ckt.options.temp, T_NOMINAL);
}
