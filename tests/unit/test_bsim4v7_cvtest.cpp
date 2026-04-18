// Tests for Task 5.3: BSIM4v7 convergence test wiring + ic= parsing.
//
// 1. device_converged() virtual on Device base returns true by default.
// 2. BSIM4v7 device convergence flag is wired into Newton.
// 3. Parsing ic=VDS,VGS,VBS on M-card lines.
// 4. CMOS inverter DC with device convergence passes.

#include <gtest/gtest.h>
#include "core/dc.hpp"
#include "core/transient.hpp"
#include "core/types.hpp"
#include "devices/device.hpp"
#include "devices/bsim4v7/bsim4v7_device.hpp"
#include "parser/netlist_parser.hpp"
#include <cmath>

using namespace neospice;

// ---------------------------------------------------------------------------
// 1. Default device_converged() returns true
// ---------------------------------------------------------------------------
TEST(BSIM4v7CvTest, DefaultDeviceConvergedReturnsTrue) {
    struct DummyDevice : public Device {
        DummyDevice() : Device("dummy") {}
        void stamp_pattern(SparsityBuilder&) const override {}
        void assign_offsets(const SparsityPattern&) override {}
        void evaluate(const std::vector<double>&,
                      NumericMatrix&, std::vector<double>&) override {}
    };
    DummyDevice d;
    EXPECT_TRUE(d.device_converged());
}

// ---------------------------------------------------------------------------
// 2. BSIM4v7 DC solve converges (positive test: device convergence is wired)
// ---------------------------------------------------------------------------
TEST(BSIM4v7CvTest, DCBiasConvergesWithDeviceCheck) {
    std::string netlist = R"(
NMOS DC bias test
VDD d 0 1.0
VGS g 0 0.8
M1 d g 0 0 NMOD W=1u L=100n
.model NMOD NMOS LEVEL=14 VTH0=0.4 U0=0.04 TOXE=2e-9
.op
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    auto result = solve_dc(ckt);

    // The solver must return results (convergence achieved).
    // Drain voltage is forced to 1.0V by VDD.
    EXPECT_NEAR(result.node_voltages.at("v(d)"), 1.0, 1e-6);
    EXPECT_NEAR(result.node_voltages.at("v(g)"), 0.8, 1e-6);
}

// ---------------------------------------------------------------------------
// 3a. Parsing ic= with three values
// ---------------------------------------------------------------------------
TEST(BSIM4v7CvTest, ParseIcThreeValues) {
    std::string netlist = R"(
NMOS IC test
VDD d 0 1.8
VGS g 0 0.9
M1 d g 0 0 NMOD W=1u L=100n ic=0.5,0.9,0.0
.model NMOD NMOS LEVEL=14 VTH0=0.4 U0=0.04 TOXE=2e-9
.op
.end
)";
    // Should parse without throwing.
    NetlistParser parser;
    auto ckt = parser.parse(netlist);

    // Verify the BSIM4v7Device was created
    int mosfet_count = 0;
    for (auto& d : ckt.devices()) {
        if (dynamic_cast<BSIM4v7Device*>(d.get())) ++mosfet_count;
    }
    EXPECT_EQ(1, mosfet_count);
}

// ---------------------------------------------------------------------------
// 3b. Parsing ic= with one value
// ---------------------------------------------------------------------------
TEST(BSIM4v7CvTest, ParseIcOneValue) {
    std::string netlist = R"(
NMOS IC single
VDD d 0 1.8
VGS g 0 0.9
M1 d g 0 0 NMOD W=1u L=100n ic=0.5
.model NMOD NMOS LEVEL=14 VTH0=0.4 U0=0.04 TOXE=2e-9
.op
.end
)";
    NetlistParser parser;
    EXPECT_NO_THROW(parser.parse(netlist));
}

// ---------------------------------------------------------------------------
// 3c. Parsing ic= with two values
// ---------------------------------------------------------------------------
TEST(BSIM4v7CvTest, ParseIcTwoValues) {
    std::string netlist = R"(
NMOS IC double
VDD d 0 1.8
VGS g 0 0.9
M1 d g 0 0 NMOD W=1u L=100n ic=0.5,0.9
.model NMOD NMOS LEVEL=14 VTH0=0.4 U0=0.04 TOXE=2e-9
.op
.end
)";
    NetlistParser parser;
    EXPECT_NO_THROW(parser.parse(netlist));
}

// ---------------------------------------------------------------------------
// 4. CMOS inverter with ic= on MOSFETs — verify solve completes
// ---------------------------------------------------------------------------
TEST(BSIM4v7CvTest, InverterWithIcOnMosfets) {
    std::string netlist = R"(
CMOS Inverter with instance ic
VDD vdd 0 1.8
VIN in 0 0.9
M1p out in vdd vdd PMOD W=2u L=100n ic=0.9,0.9,0.0
M1n out in 0 0 NMOD W=1u L=100n ic=0.9,0.9,0.0
CL out 0 10f
.model NMOD NMOS LEVEL=14 VTH0=0.4 U0=0.04 TOXE=2e-9
.model PMOD PMOS LEVEL=14 VTH0=-0.4 U0=0.02 TOXE=2e-9
.op
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    auto result = solve_dc(ckt);

    // Inverter at Vin=0.9V should produce an output voltage.
    // Just verify convergence yields a valid result.
    EXPECT_TRUE(result.node_voltages.count("v(out)"));
}

// ---------------------------------------------------------------------------
// 5. ic= alongside other geometry params parses correctly
// ---------------------------------------------------------------------------
TEST(BSIM4v7CvTest, ParseIcWithGeomParams) {
    std::string netlist = R"(
NMOS IC with geometry
VDD d 0 1.8
VGS g 0 0.9
M1 d g 0 0 NMOD W=2u L=200n NF=2 ic=0.5,0.8,-0.1 AD=1p AS=1p
.model NMOD NMOS LEVEL=14 VTH0=0.4 U0=0.04 TOXE=2e-9
.op
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);

    // Verify at least one BSIM4v7 device was created
    bool found = false;
    for (auto& d : ckt.devices()) {
        if (dynamic_cast<BSIM4v7Device*>(d.get())) { found = true; break; }
    }
    EXPECT_TRUE(found);
}

// ---------------------------------------------------------------------------
// 6. Transient with ic= on MOSFET — verify the simulation runs
// ---------------------------------------------------------------------------
TEST(BSIM4v7CvTest, TransientWithMosfetIc) {
    std::string netlist = R"(
NMOS transient with instance IC
VDD vdd 0 1.8
VIN in 0 PULSE(0 1.8 0.1n 0.05n 0.05n 1n 2n)
M1p out in vdd vdd PMOD W=2u L=100n ic=1.8,0.0,0.0
M1n out in 0 0 NMOD W=1u L=100n ic=1.8,0.0,0.0
CL out 0 10f
.model NMOD NMOS LEVEL=14 VTH0=0.4 U0=0.04 TOXE=2e-9
.model PMOD PMOS LEVEL=14 VTH0=-0.4 U0=0.02 TOXE=2e-9
.tran 0.01n 2n
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    auto result = solve_transient(ckt, 0.01e-9, 2e-9);

    // Should produce output timepoints and the simulation should complete
    ASSERT_GT(result.time.size(), 5u);
    EXPECT_NEAR(result.time.front(), 0.0, 1e-18);

    // The output node should appear in voltages
    EXPECT_TRUE(result.voltages.count("v(out)"));
}
