// Tests for JFET device adapter, parser integration, and basic DC/AC operation.

#include <gtest/gtest.h>
#include "parser/netlist_parser.hpp"
#include "parser/model_cards.hpp"
#include "devices/jfet/jfet_device.hpp"
#include "core/dc.hpp"
#include "core/ac.hpp"
#include "core/types.hpp"

#include <cmath>
#include <complex>
#include <string>

using namespace neospice;

// ---------------------------------------------------------------------------
// Parser tests -- verify J element and .model NJF/PJF parsing
// ---------------------------------------------------------------------------

TEST(JFETParser, NjfThreeTerminal) {
    const std::string netlist =
        "* NJF parse test\n"
        "VDD d 0 10\n"
        "VGG g 0 -1\n"
        "J1 d g 0 JMOD\n"
        ".model JMOD NJF(VTO=-2 BETA=1e-4 LAMBDA=0.02 IS=1e-14 RD=10 RS=10)\n"
        ".op\n.end\n";

    NetlistParser p;
    Circuit ckt = p.parse(netlist);

    int jfet_count = 0;
    for (auto& d : ckt.devices()) {
        if (dynamic_cast<JFETDevice*>(d.get())) ++jfet_count;
    }
    EXPECT_EQ(1, jfet_count);
}

TEST(JFETParser, AreaParameter) {
    const std::string netlist =
        "* JFET with area\n"
        "VDD d 0 10\n"
        "VGG g 0 -1\n"
        "J1 d g 0 JMOD area=2\n"
        ".model JMOD NJF(VTO=-2 BETA=1e-4)\n"
        ".op\n.end\n";

    NetlistParser p;
    Circuit ckt = p.parse(netlist);

    int jfet_count = 0;
    for (auto& d : ckt.devices()) {
        if (dynamic_cast<JFETDevice*>(d.get())) ++jfet_count;
    }
    EXPECT_EQ(1, jfet_count);
}

TEST(JFETParser, InitialConditions) {
    const std::string netlist =
        "* JFET with IC\n"
        "VDD d 0 10\n"
        "VGG g 0 -1\n"
        "J1 d g 0 JMOD ic=5,-1\n"
        ".model JMOD NJF(VTO=-2 BETA=1e-4)\n"
        ".op\n.end\n";

    NetlistParser p;
    Circuit ckt = p.parse(netlist);

    int jfet_count = 0;
    for (auto& d : ckt.devices()) {
        if (dynamic_cast<JFETDevice*>(d.get())) ++jfet_count;
    }
    EXPECT_EQ(1, jfet_count);
}

TEST(JFETParser, PjfModel) {
    const std::string netlist =
        "* PJF model test\n"
        "VDD 0 d 10\n"
        "VGG g 0 1\n"
        "J1 d g 0 JPJF\n"
        ".model JPJF PJF(VTO=2 BETA=1e-4)\n"
        ".op\n.end\n";

    NetlistParser p;
    Circuit ckt = p.parse(netlist);

    int jfet_count = 0;
    for (auto& d : ckt.devices()) {
        if (dynamic_cast<JFETDevice*>(d.get())) ++jfet_count;
    }
    EXPECT_EQ(1, jfet_count);
}

TEST(JFETParser, UnknownModelThrows) {
    const std::string netlist =
        "* Unknown model\n"
        "J1 d g 0 BOGUS\n"
        ".end\n";

    NetlistParser p;
    EXPECT_THROW(p.parse(netlist), ParseError);
}

TEST(JFETParser, BareAreaNumber) {
    // Legacy syntax: J1 d g s model 2.0
    const std::string netlist =
        "* JFET bare area\n"
        "VDD d 0 10\n"
        "J1 d 0 0 JMOD 2.0\n"
        ".model JMOD NJF(VTO=-2 BETA=1e-4)\n"
        ".op\n.end\n";

    NetlistParser p;
    Circuit ckt = p.parse(netlist);

    int jfet_count = 0;
    for (auto& d : ckt.devices()) {
        if (dynamic_cast<JFETDevice*>(d.get())) ++jfet_count;
    }
    EXPECT_EQ(1, jfet_count);
}

// ---------------------------------------------------------------------------
// Model card conversion tests
// ---------------------------------------------------------------------------

TEST(JFETModelCard, NjfParamsDispatched) {
    ModelCard card;
    card.name = "JTEST";
    card.type = "njf";
    card.params["vto"] = -2.0;
    card.params["beta"] = 1e-4;
    card.params["lambda"] = 0.02;
    card.params["rd"] = 10.0;
    card.params["rs"] = 10.0;
    card.params["cgs"] = 5e-12;
    card.params["cgd"] = 2e-12;
    card.params["is"] = 1e-14;
    card.params["pb"] = 0.8;

    auto jfet_card = to_jfet_card(card);
    EXPECT_EQ(jfet_card->ucb.JFETtype, 1);  // NJF
    EXPECT_NEAR(jfet_card->ucb.JFETthreshold, -2.0, 1e-10);
    EXPECT_NEAR(jfet_card->ucb.JFETbeta, 1e-4, 1e-10);
    EXPECT_NEAR(jfet_card->ucb.JFETlModulation, 0.02, 1e-10);
    EXPECT_NEAR(jfet_card->ucb.JFETdrainResist, 10.0, 1e-10);
    EXPECT_NEAR(jfet_card->ucb.JFETsourceResist, 10.0, 1e-10);
    EXPECT_NEAR(jfet_card->ucb.JFETcapGS, 5e-12, 1e-18);
    EXPECT_NEAR(jfet_card->ucb.JFETcapGD, 2e-12, 1e-18);
    EXPECT_NEAR(jfet_card->ucb.JFETgateSatCurrent, 1e-14, 1e-20);
    EXPECT_NEAR(jfet_card->ucb.JFETgatePotential, 0.8, 1e-10);
}

TEST(JFETModelCard, PjfType) {
    ModelCard card;
    card.name = "JPJF";
    card.type = "pjf";
    card.params["vto"] = 2.0;

    auto jfet_card = to_jfet_card(card);
    EXPECT_EQ(jfet_card->ucb.JFETtype, -1);  // PJF
}

TEST(JFETModelCard, InvalidTypeThrows) {
    ModelCard card;
    card.name = "JBAD";
    card.type = "nmos";  // not NJF/PJF

    EXPECT_THROW(to_jfet_card(card), ParseError);
}

// ---------------------------------------------------------------------------
// DC operating point test -- NJF common-source
// ---------------------------------------------------------------------------

TEST(JFETDevice, NjfCommonSourceDCBias) {
    // Simple NJF common-source circuit:
    //   VDD (10V) -> RD (1k) -> drain
    //   VGG (-1V) -> gate
    //   source -> ground
    //
    // With VTO=-2, BETA=1e-4:
    //   In saturation (Vgs > Vto, Vds > Vgs - Vto):
    //   Id = BETA * (Vgs - Vto)^2 * (1 + LAMBDA*Vds)
    //   Id ~ 1e-4 * (-1 - (-2))^2 = 1e-4 * 1 = 0.1mA
    //   Vd = VDD - Id * RD = 10 - 0.1mA * 1k = 9.9V
    const std::string netlist =
        "* NJF CS bias\n"
        "VDD vdd 0 10.0\n"
        "VGG gate 0 -1.0\n"
        "RD vdd drain 1k\n"
        "J1 drain gate 0 JMOD\n"
        ".model JMOD NJF(VTO=-2 BETA=1e-4 LAMBDA=0.02 IS=1e-14 "
        "RD=10 RS=10 CGS=5p CGD=2p)\n"
        ".op\n.end\n";

    NetlistParser p;
    Circuit ckt = p.parse(netlist);
    DCResult result = solve_dc(ckt);

    // Check that we have a physically reasonable operating point
    double v_drain = result.voltage("drain");

    // Drain should be near VDD (transistor is conducting ~0.1mA)
    // Id ~ 0.1mA, Vd = 10 - 0.1*1k = 9.9V
    EXPECT_GT(v_drain, 5.0);    // Not saturated/pinched off heavily
    EXPECT_LT(v_drain, 10.0);   // Some current flowing

    // Query operating point parameters from the JFET
    for (auto& d : ckt.devices()) {
        auto* jfet = dynamic_cast<JFETDevice*>(d.get());
        if (!jfet) continue;

        auto id = jfet->query_param("id");
        auto vgs = jfet->query_param("vgs");
        auto gm = jfet->query_param("gm");
        auto gds_val = jfet->query_param("gds");

        ASSERT_TRUE(id.has_value());
        ASSERT_TRUE(vgs.has_value());
        ASSERT_TRUE(gm.has_value());
        ASSERT_TRUE(gds_val.has_value());

        // Drain current should be positive and reasonable
        EXPECT_GT(*id, 0.0);
        EXPECT_LT(*id, 0.01);  // less than 10mA

        // Vgs should be approximately -1V
        EXPECT_NEAR(*vgs, -1.0, 0.2);

        // Transconductance and output conductance should be positive
        EXPECT_GT(*gm, 0.0);
        EXPECT_GT(*gds_val, 0.0);
    }
}

TEST(JFETDevice, NjfDeviceConverged) {
    const std::string netlist =
        "* NJF convergence test\n"
        "VDD vdd 0 10.0\n"
        "VGG gate 0 -1.0\n"
        "RD vdd drain 1k\n"
        "J1 drain gate 0 JMOD\n"
        ".model JMOD NJF(VTO=-2 BETA=1e-4 LAMBDA=0.02 IS=1e-14 RD=10 RS=10)\n"
        ".op\n.end\n";

    NetlistParser p;
    Circuit ckt = p.parse(netlist);
    DCResult result = solve_dc(ckt);

    // After a successful DC solve, all devices should report converged
    for (auto& d : ckt.devices()) {
        auto* jfet = dynamic_cast<JFETDevice*>(d.get());
        if (!jfet) continue;
        EXPECT_TRUE(jfet->device_converged());
    }
}

// ---------------------------------------------------------------------------
// AC test -- JFET common-source amplifier gain
// ---------------------------------------------------------------------------

TEST(JFETDevice, NjfCommonSourceACGain) {
    // NJF common-source amplifier:
    //   VDD = 10V via RD = 1k to drain
    //   VGG = -1V DC bias on gate
    //   Vin = AC 1V on gate (via coupling capacitor)
    //
    // Expected AC gain magnitude: |Av| = gm * RD
    // gm ~ 2 * BETA * (Vgs - Vto) = 2 * 1e-4 * 1 = 2e-4
    // |Av| ~ 2e-4 * 1k = 0.2
    //
    // This is a simple gain check -- we verify gain is in the right ballpark
    const std::string netlist =
        "* NJF CS amplifier AC\n"
        "VDD vdd 0 10.0\n"
        "VGG gate 0 DC -1.0 AC 1.0\n"
        "RD vdd drain 1k\n"
        "J1 drain gate 0 JMOD\n"
        ".model JMOD NJF(VTO=-2 BETA=1e-4 LAMBDA=0.02 IS=1e-14 "
        "RD=10 RS=10 CGS=5p CGD=2p)\n"
        ".ac dec 10 100 1e6\n"
        ".end\n";

    NetlistParser p;
    Circuit ckt = p.parse(netlist);

    // First solve DC operating point
    DCResult dc = solve_dc(ckt);

    // Then run AC analysis
    ACResult ac = solve_ac(ckt, AnalysisCommand::DEC, 10, 100, 1e6);

    ASSERT_FALSE(ac.frequency.empty());
    ASSERT_TRUE(ac.voltages.count("v(drain)") > 0);

    // Check gain at a mid-band frequency (e.g. 10kHz)
    // Find index closest to 10kHz
    int idx_10k = -1;
    for (size_t i = 0; i < ac.frequency.size(); ++i) {
        if (ac.frequency[i] >= 9000 && ac.frequency[i] <= 11000) {
            idx_10k = static_cast<int>(i);
            break;
        }
    }

    if (idx_10k >= 0) {
        std::complex<double> v_drain_ac = ac.voltage("drain")[idx_10k];
        double gain_mag = std::abs(v_drain_ac);

        // Gain should be positive (inverting, but magnitude)
        EXPECT_GT(gain_mag, 0.01);
        // Gain should not be unreasonably large
        EXPECT_LT(gain_mag, 10.0);
    }
}

// ---------------------------------------------------------------------------
// Ngspice comparison test -- DC bias point
// ---------------------------------------------------------------------------

TEST(JFETNgspiceCompare, NjfDCBiasPoint) {
    // Compare neospice JFET DC operating point against expected values
    // derived from SPICE hand calculations.
    //
    // Circuit: VDD=10V, RD=1k, VGG=-1V, NJF with VTO=-2, BETA=1e-4
    // Expected: Id ~ 0.1mA, Vd ~ 9.9V (saturation region)
    const std::string netlist =
        "* NJF DC comparison\n"
        "VDD vdd 0 10.0\n"
        "VGG gate 0 -1.0\n"
        "RD vdd drain 1k\n"
        "J1 drain gate 0 JMOD\n"
        ".model JMOD NJF(VTO=-2 BETA=1e-4 LAMBDA=0.02 IS=1e-14 "
        "RD=10 RS=10)\n"
        ".op\n.end\n";

    NetlistParser p;
    Circuit ckt = p.parse(netlist);
    DCResult result = solve_dc(ckt);

    double v_drain = result.voltage("drain");

    // Analytical: Id = BETA*(Vgs-Vto)^2*(1+LAMBDA*Vds)
    // In saturation with Vgs=-1, Vto=-2:
    //   Id ~ 1e-4 * (1)^2 = 0.1mA (ignoring lambda and series R)
    //   Vd ~ 10 - 0.1mA * 1k = 9.9V
    //
    // With series resistance and lambda, exact value will differ slightly.
    // We check against a reasonable tolerance.
    EXPECT_NEAR(v_drain, 9.9, 0.5);

    // Check drain current through query_param
    for (auto& d : ckt.devices()) {
        auto* jfet = dynamic_cast<JFETDevice*>(d.get());
        if (!jfet) continue;

        auto id = jfet->query_param("id");
        ASSERT_TRUE(id.has_value());

        // Id should be approximately 0.1mA
        EXPECT_NEAR(*id, 1e-4, 5e-5);
    }
}

// ---------------------------------------------------------------------------
// reset_temp() tests -- verify that clearing temp_done_ allows re-evaluation
// ---------------------------------------------------------------------------

TEST(JFETDevice, ResetTempAllowsReEvaluation) {
    // Verify that reset_temp() clears the cached temperature state so the
    // device re-runs JFETtemp() on the next evaluate() call.  We confirm
    // this indirectly: after reset_temp() the device must still produce a
    // valid (non-crashing) operating point with correct results.
    const std::string netlist =
        "* NJF reset_temp test\n"
        "VDD vdd 0 10.0\n"
        "VGG gate 0 -1.0\n"
        "RD vdd drain 1k\n"
        "J1 drain gate 0 JMOD\n"
        ".model JMOD NJF(VTO=-2 BETA=1e-4 LAMBDA=0.02 IS=1e-14 "
        "RD=10 RS=10)\n"
        ".op\n.end\n";

    NetlistParser p;
    Circuit ckt = p.parse(netlist);

    // First DC solve -- sets temp_done_ = true inside the JFET device.
    DCResult result1 = solve_dc(ckt);
    double v_drain1 = result1.voltage("drain");
    EXPECT_GT(v_drain1, 5.0);

    // Call reset_temp() on all JFET devices.
    int reset_count = 0;
    for (auto& d : ckt.devices()) {
        auto* jfet = dynamic_cast<JFETDevice*>(d.get());
        if (!jfet) continue;
        jfet->reset_temp();
        ++reset_count;
    }
    EXPECT_EQ(1, reset_count);

    // Second DC solve after reset -- should recompute temperature params and
    // still converge to the same operating point.
    DCResult result2 = solve_dc(ckt);
    double v_drain2 = result2.voltage("drain");
    EXPECT_GT(v_drain2, 5.0);

    // Results should be consistent between solves.
    EXPECT_NEAR(v_drain1, v_drain2, 1e-3);
}

TEST(JFETDevice, ResetTempBaseClassDefaultIsNoOp) {
    // The Device base class reset_temp() is a no-op; call it via a pointer to
    // confirm it compiles and doesn't crash.
    const std::string netlist =
        "* NJF base reset_temp\n"
        "VDD vdd 0 10.0\n"
        "VGG gate 0 -1.0\n"
        "RD vdd drain 1k\n"
        "J1 drain gate 0 JMOD\n"
        ".model JMOD NJF(VTO=-2 BETA=1e-4)\n"
        ".op\n.end\n";

    NetlistParser p;
    Circuit ckt = p.parse(netlist);

    // Call via Device* base pointer -- exercises the virtual dispatch.
    for (auto& d : ckt.devices()) {
        d->reset_temp();  // must not crash or throw
    }

    DCResult result = solve_dc(ckt);
    EXPECT_GT(result.voltage("drain"), 5.0);
}

TEST(JFETNgspiceCompare, NjfACMidBandGain) {
    // Verify AC gain of a JFET common-source amplifier
    // Analytical gm = 2*BETA*(Vgs-Vto) = 2*1e-4*1 = 2e-4 S
    // Mid-band gain |Av| = gm * (RD || rds) ~ gm * RD = 0.2
    const std::string netlist =
        "* NJF AC gain comparison\n"
        "VDD vdd 0 10.0\n"
        "VGG gate 0 DC -1.0 AC 1.0\n"
        "RD vdd drain 1k\n"
        "J1 drain gate 0 JMOD\n"
        ".model JMOD NJF(VTO=-2 BETA=1e-4 LAMBDA=0.02 IS=1e-14 "
        "RD=10 RS=10 CGS=5p CGD=2p)\n"
        ".ac dec 5 1k 100k\n"
        ".end\n";

    NetlistParser p;
    Circuit ckt = p.parse(netlist);

    // DC operating point first
    DCResult dc = solve_dc(ckt);

    // Get gm from the JFET device
    double gm_val = 0;
    for (auto& d : ckt.devices()) {
        auto* jfet = dynamic_cast<JFETDevice*>(d.get());
        if (!jfet) continue;
        auto gm_opt = jfet->query_param("gm");
        if (gm_opt.has_value()) gm_val = *gm_opt;
    }
    EXPECT_GT(gm_val, 0.0);

    // Run AC
    ACResult ac = solve_ac(ckt, AnalysisCommand::DEC, 5, 1e3, 1e5);
    ASSERT_FALSE(ac.frequency.empty());
    ASSERT_TRUE(ac.voltages.count("v(drain)") > 0);

    // At mid-band frequencies, gain should be approximately gm * RD
    // (The exact value will differ due to rds, rd_series, but should be
    // in the right order of magnitude)
    double expected_gain = gm_val * 1000.0;  // RD = 1k

    // Find a mid-band frequency point (avoid too-low and too-high frequencies
    // where parasitic caps matter)
    for (size_t i = 0; i < ac.frequency.size(); ++i) {
        double f = ac.frequency[i];
        if (f >= 5000 && f <= 20000) {
            double gain = std::abs(ac.voltage("drain")[i]);
            // Should be within a factor of 3 of expected
            EXPECT_GT(gain, expected_gain * 0.3);
            EXPECT_LT(gain, expected_gain * 3.0);
            break;
        }
    }
}
