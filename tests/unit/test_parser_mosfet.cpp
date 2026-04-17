// T8 parser smoke tests — exercise M-card → BSIM4v7Device and LEVEL=14
// dispatch in the netlist parser.  These tests do NOT run any analysis;
// they only validate that the parser produces the expected Device objects
// and that unsupported LEVEL values are rejected at parse time.

#include <gtest/gtest.h>
#include "parser/netlist_parser.hpp"
#include "devices/bsim4v7/bsim4v7_device.hpp"
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

TEST(ParserMosfet, UnsupportedLevelThrows) {
    const std::string netlist =
        "* Unsupported LEVEL=1 probe\n"
        "M1 d g s b M1MOD\n"
        ".model M1MOD NMOS LEVEL=1 VT0=0.7\n"
        ".end\n";

    NetlistParser p;
    EXPECT_THROW(p.parse(netlist), ParseError);
}
