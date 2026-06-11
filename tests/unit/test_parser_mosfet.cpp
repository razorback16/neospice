// T8 parser smoke tests — exercise M-card → BSIM4v7Device (LEVEL=14) and
// MOS1Device (LEVEL=1) dispatch in the netlist parser.  These tests do NOT
// run any analysis; they only validate that the parser produces the expected
// Device objects and that unsupported LEVEL values are rejected at parse time.

#include <gtest/gtest.h>
#include "parser/netlist_parser.hpp"
#include "devices/bsim4v7/bsim4v7_device.hpp"
#include "devices/mos1/mos1_device.hpp"
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
