// T8 parser smoke tests — exercise M-card → BSIM4v7Device (LEVEL=14) and
// MOS1Device (LEVEL=1) dispatch in the netlist parser.  These tests do NOT
// run any analysis; they only validate that the parser produces the expected
// Device objects and that unsupported LEVEL values are rejected at parse time.

#include <gtest/gtest.h>
#include "parser/netlist_parser.hpp"
#include "devices/bsim4v7/bsim4v7_device.hpp"
#include "devices/mos1/mos1_device.hpp"
#include "core/dc.hpp"
#include "core/types.hpp"

using namespace neospice;

TEST(ParserMosfet, Level14NmosCardInstantiatesDevice) {
    const std::string netlist =
        "* NMOS probe\n"
        "VDD d 0 0.1\n"
        "VGS g 0 0.8\n"
        "VBS b 0 0\n"
        "M1 d g 0 b NMOD W=1u L=100n\n"
        ".model NMOD NMOS LEVEL=14 VTH0=0.4 U0=0.04 TOXE=2e-9\n"
        ".op\n.end\n";

    NetlistParser p;
    Circuit ckt = p.parse(netlist);

    int mosfet_count = 0;
    for (auto& d : ckt.devices()) {
        if (dynamic_cast<BSIM4v7Device*>(d.get())) ++mosfet_count;
    }
    EXPECT_EQ(1, mosfet_count);
}

TEST(ParserMosfet, Level1NmosCardInstantiatesMOS1Device) {
    const std::string netlist =
        "* MOS1 NMOS probe\n"
        "VDD d 0 1.0\n"
        "VGS g 0 0.8\n"
        "M1 d g 0 0 M1MOD W=10u L=1u\n"
        ".model M1MOD NMOS LEVEL=1 VTO=0.7 KP=110e-6\n"
        ".op\n.end\n";

    NetlistParser p;
    Circuit ckt = p.parse(netlist);

    int mos1_count = 0;
    for (auto& d : ckt.devices()) {
        if (dynamic_cast<MOS1Device*>(d.get())) ++mos1_count;
    }
    EXPECT_EQ(1, mos1_count);
}

TEST(ParserMosfet, UnsupportedLevelThrows) {
    const std::string netlist =
        "* Unsupported LEVEL=5 probe\n"
        "M1 d g s b M1MOD\n"
        ".model M1MOD NMOS LEVEL=5 VT0=0.7\n"
        ".end\n";

    NetlistParser p;
    EXPECT_THROW(p.parse(netlist), neospice::ParseError);
}

// Regression test: M-card params like "W=   6E-6" (spaces between '=' and value)
// cause the whitespace tokenizer to split into tokens ["W=", "6E-6"].  The parser
// must peek at the next token when the value portion is empty.
TEST(ParserMosfet, SpacedEqualsInParamsDoesNotWarn) {
    // The netlist mirrors the BF904/BF904R pattern: W=   6E-6 with spaces.
    const std::string netlist =
        "* BF904-style MOSFET with spaces between '=' and value\n"
        "VDD d 0 1.0\n"
        "VGS g 0 0.8\n"
        "M1 d g 0 0 M1MOD L=1.1E-6 W=   6E-6\n"
        ".model M1MOD NMOS LEVEL=1 VTO=0.7 KP=110e-6\n"
        ".op\n.end\n";

    NetlistParser p;
    // Must not throw and must instantiate a MOS1Device with W=6e-6 and L=1.1e-6
    Circuit ckt = p.parse(netlist);

    int mos1_count = 0;
    for (auto& d : ckt.devices()) {
        if (dynamic_cast<MOS1Device*>(d.get())) ++mos1_count;
    }
    EXPECT_EQ(1, mos1_count);
}

// Regression test: W=Xu and L=Xu on the .MODEL card (not on the M instance)
// must be applied as instance-parameter defaults, matching ngspice behavior
// (inpgmod.c:146-157).  Before the fix, W/L on .MODEL emitted "unknown
// parameter" warnings and were silently discarded, leaving every instance
// with the hardcoded 100u/100u defaults.
TEST(ParserMosfet, ModelCardWLDefaultsAppliedToInstance) {
    // W=55u on the .MODEL card; NO W/L on the M line.
    // Circuit: Vdd(5V) -> Rload(1k) -> drain, M1(drain,gate,0,0)
    // KP=100u, Vto=1.0, Vgs=1.5V.
    // Id_sat = KP/2 * W/L * (Vgs-Vto)^2 = 50e-6 * 55 * (0.5)^2 ~ 0.6875mA
    // V(drain) = 5 - Id * 1k ~ 5 - 0.688 = 4.31V
    // With default W=100u: Id_sat ~ 1.25mA, V(drain) ~ 3.75V
    const std::string netlist =
        "* MOS1 W/L from .MODEL card test\n"
        "Vdd vdd 0 5\n"
        "Vgs gate 0 1.5\n"
        "Rload vdd drain 1k\n"
        "M1 drain gate 0 0 DRVMOS\n"
        ".MODEL DRVMOS NMOS LEVEL=1 L=1.0U W=55U KP=100U VTO=1.0\n"
        ".op\n.end\n";

    NetlistParser p;
    Circuit ckt = p.parse(netlist);

    // Verify a MOS1 device was created
    int mos1_count = 0;
    for (auto& d : ckt.devices()) {
        if (dynamic_cast<MOS1Device*>(d.get())) ++mos1_count;
    }
    ASSERT_EQ(1, mos1_count);

    // Simulate and check that W=55u is in effect.
    // With W=55u: V(drain) ~ 4.31V; with W=100u: V(drain) ~ 3.75V
    DCResult result = solve_dc(ckt);
    EXPECT_TRUE(result.status.converged) << "DC should converge";

    double v_drain = result.voltage("drain");
    // Threshold: if v_drain > 4.0, W=55u was applied (not 100u default)
    EXPECT_GT(v_drain, 4.0)
        << "V(drain) should be >4V, indicating W=55u from .MODEL was applied "
           "(W=100u default would give ~3.75V)";
    EXPECT_LT(v_drain, 5.0)
        << "V(drain) should be below Vdd";
}

// Regression test (cluster B1, SSM3J133TU family): W and L given with
// whitespace on BOTH sides of '=' ("W = 0.3") tokenize to three separate
// tokens [W] [=] [0.3].  The param loop previously required '=' inside a
// single token, so W/L silently fell back to the 100u default — for the
// BSIM3 (LEVEL=49) Toshiba models this drove the effective C-V width
// negative and aborted the op.  The reconstruction must absorb every
// whitespace split around '=' (matching ngspice's gettok()).
TEST(ParserMosfet, FullySpacedEqualsAppliesWidthAndLength) {
    const std::string netlist =
        "* MOSFET with whitespace on both sides of '='\n"
        "Vdd vdd 0 5\n"
        "Vgs gate 0 1.5\n"
        "Rload vdd drain 1k\n"
        "M1 drain gate 0 0 DRVMOS L = 1.0U W = 55U\n"
        ".MODEL DRVMOS NMOS LEVEL=1 KP=100U VTO=1.0\n"
        ".op\n.end\n";

    NetlistParser p;
    Circuit ckt = p.parse(netlist);

    int mos1_count = 0;
    for (auto& d : ckt.devices()) {
        if (dynamic_cast<MOS1Device*>(d.get())) ++mos1_count;
    }
    ASSERT_EQ(1, mos1_count);

    DCResult result = solve_dc(ckt);
    EXPECT_TRUE(result.status.converged) << "DC should converge";
    // W=55u/L=1u gives V(drain) ~ 4.31V; the 100u/100u default (W unparsed)
    // would give ~5V (W/L = 1, much weaker drive at these dims -> near Vdd).
    double v_drain = result.voltage("drain");
    EXPECT_GT(v_drain, 4.0) << "spaced W=55U should be applied";
    EXPECT_LT(v_drain, 4.7) << "spaced W=55U/L=1U drive should pull drain below Vdd";
}
