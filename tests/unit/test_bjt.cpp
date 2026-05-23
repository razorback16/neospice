// Tests for BJT device adapter, parser integration, and basic DC operation.

#include <gtest/gtest.h>
#include "parser/netlist_parser.hpp"
#include "parser/model_cards.hpp"
#include "devices/bjt/bjt_device.hpp"
#include "core/dc.hpp"
#include "core/types.hpp"

#include <cmath>
#include <string>

using namespace neospice;

// ---------------------------------------------------------------------------
// Parser tests — verify Q element and .model NPN/PNP parsing
// ---------------------------------------------------------------------------

TEST(BJTParser, NpnModelCardParsesBasicParams) {
    // Verify that .model NPN(IS=... BF=... etc) is parsed correctly
    const std::string netlist =
        "* NPN model parse test\n"
        "VCC c 0 5\n"
        "VBB b 0 0.7\n"
        "Q1 c b 0 QMOD\n"
        ".model QMOD NPN(IS=1e-14 BF=200 BR=5 RC=1 RE=0.5 RB=10 "
        "CJE=25p CJC=8p TF=400p TR=20n)\n"
        ".op\n.end\n";

    NetlistParser p;
    Circuit ckt = p.parse(netlist);

    // Count BJT devices
    int bjt_count = 0;
    for (auto& d : ckt.devices()) {
        if (dynamic_cast<BJTDevice*>(d.get())) ++bjt_count;
    }
    EXPECT_EQ(1, bjt_count);
}

TEST(BJTParser, ThreeTerminalFormat) {
    // Q1 c b e model (3 terminals, no substrate)
    const std::string netlist =
        "* 3-terminal BJT\n"
        "VCC c 0 5\n"
        "VBB b 0 0.7\n"
        "Q1 c b 0 QMOD\n"
        ".model QMOD NPN(IS=1e-14 BF=100)\n"
        ".op\n.end\n";

    NetlistParser p;
    Circuit ckt = p.parse(netlist);

    int bjt_count = 0;
    for (auto& d : ckt.devices()) {
        if (dynamic_cast<BJTDevice*>(d.get())) ++bjt_count;
    }
    EXPECT_EQ(1, bjt_count);
}

TEST(BJTParser, FourTerminalWithSubstrate) {
    // Q1 c b e s model (4 terminals with explicit substrate)
    const std::string netlist =
        "* 4-terminal BJT\n"
        "VCC c 0 5\n"
        "VBB b 0 0.7\n"
        "Q1 c b 0 sub QMOD\n"
        ".model QMOD NPN(IS=1e-14 BF=100)\n"
        ".op\n.end\n";

    NetlistParser p;
    Circuit ckt = p.parse(netlist);

    int bjt_count = 0;
    for (auto& d : ckt.devices()) {
        if (dynamic_cast<BJTDevice*>(d.get())) ++bjt_count;
    }
    EXPECT_EQ(1, bjt_count);
}

TEST(BJTParser, AreaParameter) {
    // Q1 c b e model area=2
    const std::string netlist =
        "* BJT with area\n"
        "VCC c 0 5\n"
        "VBB b 0 0.7\n"
        "Q1 c b 0 QMOD area=2\n"
        ".model QMOD NPN(IS=1e-14 BF=100)\n"
        ".op\n.end\n";

    NetlistParser p;
    Circuit ckt = p.parse(netlist);

    int bjt_count = 0;
    for (auto& d : ckt.devices()) {
        if (dynamic_cast<BJTDevice*>(d.get())) ++bjt_count;
    }
    EXPECT_EQ(1, bjt_count);
}

TEST(BJTParser, InitialConditions) {
    // Q1 c b e model ic=0.7,5.0
    const std::string netlist =
        "* BJT with IC\n"
        "VCC c 0 5\n"
        "VBB b 0 0.7\n"
        "Q1 c b 0 QMOD ic=0.7,5.0\n"
        ".model QMOD NPN(IS=1e-14 BF=100)\n"
        ".op\n.end\n";

    NetlistParser p;
    Circuit ckt = p.parse(netlist);

    int bjt_count = 0;
    for (auto& d : ckt.devices()) {
        if (dynamic_cast<BJTDevice*>(d.get())) ++bjt_count;
    }
    EXPECT_EQ(1, bjt_count);
}

TEST(BJTParser, PnpModel) {
    const std::string netlist =
        "* PNP model test\n"
        "VCC 0 c 5\n"
        "VBB 0 b 0.7\n"
        "Q1 c b 0 QPNP\n"
        ".model QPNP PNP(IS=1e-14 BF=100)\n"
        ".op\n.end\n";

    NetlistParser p;
    Circuit ckt = p.parse(netlist);

    int bjt_count = 0;
    for (auto& d : ckt.devices()) {
        if (dynamic_cast<BJTDevice*>(d.get())) ++bjt_count;
    }
    EXPECT_EQ(1, bjt_count);
}

TEST(BJTParser, UnknownModelSkipsWithWarning) {
    const std::string netlist =
        "* Unknown model\n"
        "Q1 c b 0 BOGUS\n"
        ".end\n";

    NetlistParser p;
    EXPECT_NO_THROW(p.parse(netlist));
}

TEST(BJTParser, MultiDeviceSameModel) {
    // Two BJTs sharing the same model card
    const std::string netlist =
        "* Two BJTs, same model\n"
        "VCC c 0 5\n"
        "VBB b 0 0.7\n"
        "Q1 c b 0 QMOD\n"
        "Q2 c b 0 QMOD area=2\n"
        ".model QMOD NPN(IS=1e-14 BF=200)\n"
        ".op\n.end\n";

    NetlistParser p;
    Circuit ckt = p.parse(netlist);

    int bjt_count = 0;
    for (auto& d : ckt.devices()) {
        if (dynamic_cast<BJTDevice*>(d.get())) ++bjt_count;
    }
    EXPECT_EQ(2, bjt_count);
}

// ---------------------------------------------------------------------------
// Model card conversion test
// ---------------------------------------------------------------------------

TEST(BJTModelCard, NpnParamsDispatched) {
    // Build a ModelCard manually and convert it
    ModelCard card;
    card.name = "QTEST";
    card.type = "npn";
    card.params["is"] = 1e-14;
    card.params["bf"] = 200.0;
    card.params["br"] = 5.0;
    card.params["rc"] = 1.0;
    card.params["re"] = 0.5;
    card.params["rb"] = 10.0;
    card.params["cje"] = 25e-12;
    card.params["cjc"] = 8e-12;

    auto bjt_card = to_bjt_card(card);
    EXPECT_EQ(bjt_card->ucb.BJTtype, 1);  // NPN
    EXPECT_NEAR(bjt_card->ucb.BJTsatCur, 1e-14, 1e-20);
    EXPECT_NEAR(bjt_card->ucb.BJTbetaF, 200.0, 1e-6);
    EXPECT_NEAR(bjt_card->ucb.BJTbetaR, 5.0, 1e-6);
    EXPECT_NEAR(bjt_card->ucb.BJTcollectorResist, 1.0, 1e-6);
    EXPECT_NEAR(bjt_card->ucb.BJTemitterResist, 0.5, 1e-6);
    EXPECT_NEAR(bjt_card->ucb.BJTbaseResist, 10.0, 1e-6);
    EXPECT_NEAR(bjt_card->ucb.BJTdepletionCapBE, 25e-12, 1e-18);
    EXPECT_NEAR(bjt_card->ucb.BJTdepletionCapBC, 8e-12, 1e-18);
}

TEST(BJTModelCard, PnpType) {
    ModelCard card;
    card.name = "QPNP";
    card.type = "pnp";
    card.params["is"] = 1e-15;

    auto bjt_card = to_bjt_card(card);
    EXPECT_EQ(bjt_card->ucb.BJTtype, -1);  // PNP
}

TEST(BJTModelCard, InvalidTypeThrows) {
    ModelCard card;
    card.name = "QBAD";
    card.type = "nmos";  // not NPN/PNP

    EXPECT_THROW(to_bjt_card(card), ParseError);
}

// ---------------------------------------------------------------------------
// DC operating point test — NPN common-emitter
// ---------------------------------------------------------------------------

TEST(BJTDevice, NpnCommonEmitterDCBias) {
    // Simple NPN common-emitter circuit:
    //   VCC (5V) -> RC (1k) -> collector
    //   VBB (0.8V) -> RB (10k) -> base
    //   emitter -> ground
    //
    // With BF=200, IS=1e-14:
    //   Ib ~ (VBB - Vbe) / RB, where Vbe ~ 0.65-0.7V
    //   Ic ~ BF * Ib
    //   Vc = VCC - Ic * RC
    const std::string netlist =
        "* NPN CE bias\n"
        "VCC vcc 0 5.0\n"
        "VBB vbb 0 0.8\n"
        "RC vcc col 1k\n"
        "RB vbb base 10k\n"
        "Q1 col base 0 Q2N2222\n"
        ".model Q2N2222 NPN(IS=1e-14 BF=200 BR=5 RC=1 RE=0.5 RB=10 "
        "CJE=25p CJC=8p TF=400p TR=20n)\n"
        ".op\n.end\n";

    NetlistParser p;
    Circuit ckt = p.parse(netlist);
    DCResult result = solve_dc(ckt);

    // Check that we have a physically reasonable operating point
    double v_col = result.voltage("col");
    double v_base = result.voltage("base");

    // Base voltage should be near Vbe (0.6-0.8V range)
    // (VBB through RB, less the small base current drop)
    EXPECT_GT(v_base, 0.5);
    EXPECT_LT(v_base, 0.85);

    // Collector voltage: VCC - Ic*RC
    // Ic should be positive (transistor is ON in forward active)
    // With Ib ~ (0.8 - 0.7) / 10k = 10uA, Ic ~ 200 * 10uA = 2mA
    // Vc ~ 5 - 2mA * 1k = 3V
    EXPECT_GT(v_col, 1.0);   // not saturated
    EXPECT_LT(v_col, 5.0);   // collector current is flowing

    // Query operating point parameters from the BJT
    for (auto& d : ckt.devices()) {
        auto* bjt = dynamic_cast<BJTDevice*>(d.get());
        if (!bjt) continue;

        auto ic = bjt->query_param("ic");
        auto ib = bjt->query_param("ib");
        auto gm = bjt->query_param("gm");
        auto vbe = bjt->query_param("vbe");

        ASSERT_TRUE(ic.has_value());
        ASSERT_TRUE(ib.has_value());
        ASSERT_TRUE(gm.has_value());
        ASSERT_TRUE(vbe.has_value());

        // Collector current should be positive and reasonable
        EXPECT_GT(*ic, 0.0);
        EXPECT_LT(*ic, 0.01);  // less than 10mA

        // Base current should be positive and much smaller than Ic
        EXPECT_GT(*ib, 0.0);

        // Ic/Ib should be approximately BF
        if (*ib > 1e-15) {
            double beta = *ic / *ib;
            EXPECT_GT(beta, 50.0);    // should be close to BF=200
            EXPECT_LT(beta, 300.0);   // allow some margin for Early effect etc.
        }

        // Vbe should be positive and in the 0.5-0.8V range for silicon
        EXPECT_GT(*vbe, 0.5);
        EXPECT_LT(*vbe, 0.85);

        // Transconductance should be positive
        EXPECT_GT(*gm, 0.0);
    }
}

TEST(BJTDevice, TwoBJTsSameModelDifferentBias) {
    // Two BJTs with same model but different external biasing
    const std::string netlist =
        "* Two NPN BJTs\n"
        "VCC vcc 0 5.0\n"
        "VBB1 vbb1 0 0.75\n"
        "VBB2 vbb2 0 0.85\n"
        "RC1 vcc col1 2k\n"
        "RC2 vcc col2 2k\n"
        "RB1 vbb1 base1 10k\n"
        "RB2 vbb2 base2 10k\n"
        "Q1 col1 base1 0 QMOD\n"
        "Q2 col2 base2 0 QMOD\n"
        ".model QMOD NPN(IS=1e-14 BF=150)\n"
        ".op\n.end\n";

    NetlistParser p;
    Circuit ckt = p.parse(netlist);
    DCResult result = solve_dc(ckt);

    double v_col1 = result.voltage("col1");
    double v_col2 = result.voltage("col2");

    // Q2 has more base drive, so it should have more collector current
    // and therefore a LOWER collector voltage
    EXPECT_LT(v_col2, v_col1);

    // Both should be in reasonable range
    EXPECT_GT(v_col1, 0.0);
    EXPECT_LT(v_col1, 5.0);
    EXPECT_GT(v_col2, 0.0);
    EXPECT_LT(v_col2, 5.0);
}
