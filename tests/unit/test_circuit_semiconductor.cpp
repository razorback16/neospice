#include <gtest/gtest.h>

#include "api/neospice.hpp"
#include "core/circuit.hpp"
#include "core/dc.hpp"

using namespace neospice;

TEST(CircuitSemiconductor, DiodeBuilderMatchesParserDc) {
    Circuit built;
    built.model(".model DMOD D(IS=1e-14 N=1)");
    auto vin = built.node("vin");
    auto out = built.node("out");
    built.V("V1", vin, GND, 1.0);
    built.R("R1", vin, out, 1e3);
    built.D("D1", out, GND, "DMOD");
    built.finalize();
    auto built_dc = solve_dc(built);

    Simulator sim;
    auto parsed = sim.parse(R"(
Diode builder equivalence
.model DMOD D(IS=1e-14 N=1)
V1 vin 0 1
R1 vin out 1k
D1 out 0 DMOD
.op
.end
)");
    auto parsed_dc = solve_dc(parsed);

    ASSERT_TRUE(built_dc.status.converged);
    ASSERT_TRUE(parsed_dc.status.converged);
    EXPECT_NEAR(built_dc.voltage("out"), parsed_dc.voltage("out"), 1e-9);
}

TEST(CircuitSemiconductor, BjtBuilderConstructsDevice) {
    Circuit ckt;
    ckt.model(".model QMOD NPN(IS=1e-14 BF=100)");
    auto c = ckt.node("c");
    auto b = ckt.node("b");
    auto e = ckt.node("e");
    DevId q = ckt.Q("Q1", c, b, e, "QMOD");
    EXPECT_EQ(static_cast<int32_t>(q), 0);
    EXPECT_EQ(ckt.devices().size(), 1u);
}

TEST(CircuitSemiconductor, MosfetBuilderConstructsInverterDevices) {
    Circuit ckt;
    ckt.model(".model NMOD NMOS LEVEL=1 VTO=0.7 KP=110u");
    ckt.model(".model PMOD PMOS LEVEL=1 VTO=-0.7 KP=50u");
    auto vdd = ckt.node("vdd");
    auto in = ckt.node("in");
    auto out = ckt.node("out");
    ckt.M("MN1", out, in, GND, GND, "NMOD", 10e-6, 1e-6);
    ckt.M("MP1", out, in, vdd, vdd, "PMOD", 20e-6, 1e-6);
    EXPECT_EQ(ckt.devices().size(), 2u);
}

TEST(CircuitSemiconductor, ModelLookupErrors) {
    Circuit ckt;
    auto n1 = ckt.node("n1");
    auto n2 = ckt.node("n2");
    EXPECT_THROW(ckt.D("D1", n1, n2, "DMISSING"), std::invalid_argument);

    ckt.model(".model DMOD D(IS=1e-14)");
    EXPECT_THROW(ckt.Q("Q1", n1, n2, GND, "DMOD"), std::invalid_argument);
}

TEST(CircuitSemiconductor, SharedModelCard) {
    Circuit ckt;
    ckt.model(".model DMOD D(IS=1e-14)");
    auto a = ckt.node("a");
    auto b = ckt.node("b");
    ckt.D("D1", a, GND, "DMOD");
    ckt.D("D2", b, GND, "DMOD");
    EXPECT_EQ(ckt.devices().size(), 2u);
}
