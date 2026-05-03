#include <gtest/gtest.h>
#include "core/circuit.hpp"
#include "core/dc.hpp"
#include "core/transient.hpp"
#include "core/ac.hpp"
#include "neospice/types.hpp"
#include "devices/resistor.hpp"

using namespace neospice;

TEST(CircuitTyped, ResistorDivider) {
    Circuit ckt;
    auto in = ckt.node("in");
    auto out = ckt.node("out");
    ckt.V("V1", in, GND, 10.0);
    ckt.R("R1", in, out, 1e3);
    ckt.R("R2", out, GND, 1e3);
    ckt.finalize();
    auto result = solve_dc(ckt);
    EXPECT_NEAR(result.voltage(out), 5.0, 1e-6);
}

TEST(CircuitTyped, DevIdReturnedByMethods) {
    Circuit ckt;
    auto in = ckt.node("in");
    auto out = ckt.node("out");
    DevId v = ckt.V("V1", in, GND, 5.0);
    DevId r = ckt.R("R1", in, out, 1e3);
    EXPECT_NE(v, r);
    EXPECT_EQ(static_cast<int32_t>(v), 0);
    EXPECT_EQ(static_cast<int32_t>(r), 1);
}

TEST(CircuitTyped, RCTransient) {
    Circuit ckt;
    auto in = ckt.node("in");
    auto out = ckt.node("out");
    ckt.V("V1", in, GND, 5.0);
    ckt.R("R1", in, out, 1e3);
    ckt.C("C1", out, GND, 1e-6);
    ckt.finalize();
    auto result = solve_transient(ckt, 1e-6, 5e-3);
    auto v = result.voltage(out);
    EXPECT_NEAR(v.back(), 5.0, 0.01);
}

TEST(CircuitTyped, VSourceWithAC) {
    Circuit ckt;
    auto in = ckt.node("in");
    auto out = ckt.node("out");
    ckt.V("V1", in, GND, 5.0, 1.0);  // dc=5, ac=1
    ckt.R("R1", in, out, 1e3);
    ckt.C("C1", out, GND, 1e-9);
    ckt.finalize();
    auto dc = solve_dc(ckt);
    EXPECT_NEAR(dc.voltage(in), 5.0, 1e-6);
    auto ac = solve_ac(ckt, ACMode::DEC, 10, 1e3, 1e6);
    EXPECT_FALSE(ac.voltage("out").empty());
}

TEST(CircuitTyped, CurrentSource) {
    Circuit ckt;
    auto n1 = ckt.node("n1");
    ckt.I("I1", GND, n1, 1e-3);
    ckt.R("R1", n1, GND, 1e3);
    ckt.finalize();
    auto result = solve_dc(ckt);
    EXPECT_NEAR(result.voltage(n1), 1.0, 1e-6);
}

TEST(CircuitTyped, VCVS) {
    Circuit ckt;
    auto ctrl = ckt.node("ctrl");
    auto out = ckt.node("out");
    ckt.V("V1", ctrl, GND, 2.0);
    ckt.R("R1", ctrl, GND, 1e3);
    ckt.E("E1", out, GND, ctrl, GND, 5.0);
    ckt.R("R2", out, GND, 1e3);
    ckt.finalize();
    auto dc = solve_dc(ckt);
    EXPECT_NEAR(dc.voltage(out), 10.0, 1e-6);
}

TEST(CircuitTyped, VCCS) {
    Circuit ckt;
    auto in = ckt.node("in");
    auto out = ckt.node("out");
    ckt.V("V1", in, GND, 1.0);
    ckt.R("R1", in, GND, 1e3);
    // G1 out GND in GND 0.001 => I = gm * V(in) = 0.001 * 1.0 = 1mA from out to GND
    // V(out) = -I * R2 = ... Actually, current exits out => V(out) = -I * R2
    // Let's use: G1 out GND in GND 0.001
    ckt.G("G1", out, GND, in, GND, 0.001);
    ckt.R("R2", out, GND, 1e3);
    ckt.finalize();
    auto dc = solve_dc(ckt);
    // I = gm * V(in) = 0.001 * 1.0 = 1mA flows from out to GND through G1
    // R2 sees current flowing into out from GND => V(out) = -I_R2 * R2
    // KCL at out: I_G1 + I_R2 = 0; I_G1 flows from out to GND = +1mA leaving out
    // So I_R2 enters out: V(out)/R2 + gm*V(in) = 0 => V(out) = -gm*V(in)*R2 = -1.0
    EXPECT_NEAR(dc.voltage(out), -1.0, 1e-6);
}

TEST(CircuitTyped, CCCS) {
    Circuit ckt;
    auto in = ckt.node("in");
    auto out = ckt.node("out");
    DevId v1 = ckt.V("V1", in, GND, 5.0);
    ckt.R("R1", in, GND, 1e3);
    ckt.F("F1", out, GND, v1, 2.0);
    ckt.R("R2", out, GND, 1e3);
    ckt.finalize();
    auto dc = solve_dc(ckt);
    EXPECT_TRUE(dc.status.converged);
}

TEST(CircuitTyped, CCVS) {
    Circuit ckt;
    auto in = ckt.node("in");
    auto out = ckt.node("out");
    DevId v1 = ckt.V("V1", in, GND, 5.0);
    ckt.R("R1", in, GND, 1e3);
    ckt.H("H1", out, GND, v1, 1000.0);
    ckt.R("R2", out, GND, 1e3);
    ckt.finalize();
    auto dc = solve_dc(ckt);
    EXPECT_TRUE(dc.status.converged);
}

TEST(CircuitTyped, Inductor) {
    Circuit ckt;
    auto in = ckt.node("in");
    auto out = ckt.node("out");
    ckt.V("V1", in, GND, 5.0);
    ckt.R("R1", in, out, 100.0);
    DevId l = ckt.L("L1", out, GND, 1e-3);
    EXPECT_NE(static_cast<int32_t>(l), -1);
    ckt.finalize();
    auto dc = solve_dc(ckt);
    EXPECT_TRUE(dc.status.converged);
}

TEST(CircuitTyped, AddDevReturnsDevId) {
    Circuit ckt;
    auto n = ckt.node("n1");
    DevId id = ckt.add_dev(std::make_unique<Resistor>(
        "R1", static_cast<int32_t>(n), GROUND_INTERNAL, 1e3));
    EXPECT_EQ(static_cast<int32_t>(id), 0);
}

TEST(CircuitTyped, CoupledInductors) {
    Circuit ckt;
    auto in = ckt.node("in");
    auto mid = ckt.node("mid");
    auto out = ckt.node("out");
    ckt.V("V1", in, GND, 5.0);
    ckt.R("R1", in, mid, 100.0);
    DevId l1 = ckt.L("L1", mid, GND, 1e-3);
    DevId l2 = ckt.L("L2", out, GND, 1e-3);
    ckt.K("K1", l1, l2, 0.5);
    ckt.R("R2", out, GND, 1e3);
    ckt.finalize();
    auto dc = solve_dc(ckt);
    EXPECT_TRUE(dc.status.converged);
}

TEST(CircuitTyped, ISourceWithAC) {
    Circuit ckt;
    auto n1 = ckt.node("n1");
    ckt.I("I1", GND, n1, 1e-3, 1e-3);  // dc=1mA, ac=1mA
    ckt.R("R1", n1, GND, 1e3);
    ckt.finalize();
    auto dc = solve_dc(ckt);
    EXPECT_NEAR(dc.voltage(n1), 1.0, 1e-6);
}

TEST(CircuitTyped, BehavioralVoltageDoubler) {
    Circuit ckt;
    auto in = ckt.node("in");
    auto out = ckt.node("out");
    ckt.V("V1", in, GND, 3.0);
    ckt.R("R1", in, GND, 1e3);
    ckt.B("B1", out, GND, "V={2*V(in)}");
    ckt.R("R2", out, GND, 1e3);
    ckt.finalize();
    auto dc = solve_dc(ckt);
    EXPECT_TRUE(dc.status.converged);
    EXPECT_NEAR(dc.voltage(out), 6.0, 1e-4);
}

TEST(CircuitTyped, BehavioralCurrentMode) {
    Circuit ckt;
    auto in = ckt.node("in");
    auto out = ckt.node("out");
    ckt.V("V1", in, GND, 2.0);
    ckt.R("R1", in, GND, 1e3);
    ckt.B("B1", out, GND, "I={V(in)*1e-3}");
    ckt.R("R2", out, GND, 1e3);
    ckt.finalize();
    auto dc = solve_dc(ckt);
    EXPECT_TRUE(dc.status.converged);
    EXPECT_NEAR(dc.voltage(out), -2.0, 1e-4);
}

TEST(CircuitTyped, BehavioralWithCurrentRef) {
    Circuit ckt;
    auto in = ckt.node("in");
    auto out = ckt.node("out");
    ckt.V("V1", in, GND, 5.0);
    ckt.R("R1", in, GND, 1e3);
    ckt.B("B1", out, GND, "V={I(V1)*1000}");
    ckt.R("R2", out, GND, 1e3);
    ckt.finalize();
    auto dc = solve_dc(ckt);
    EXPECT_TRUE(dc.status.converged);
}

TEST(CircuitTyped, BehavioralCurrentRefNotFound) {
    Circuit ckt;
    auto out = ckt.node("out");
    EXPECT_THROW(ckt.B("B1", out, GND, "V={I(Vbogus)*100}"), std::invalid_argument);
}

TEST(CircuitTyped, BehavioralDiffVoltage) {
    Circuit ckt;
    auto a = ckt.node("a");
    auto b = ckt.node("b");
    auto out = ckt.node("out");
    ckt.V("V1", a, GND, 3.0);
    ckt.V("V2", b, GND, 1.0);
    ckt.B("B1", out, GND, "V={V(a,b)}");
    ckt.R("R1", out, GND, 1e3);
    ckt.finalize();
    auto dc = solve_dc(ckt);
    EXPECT_TRUE(dc.status.converged);
    EXPECT_NEAR(dc.voltage(out), 2.0, 1e-4);
}

TEST(CircuitTyped, BehavioralBadFormat) {
    Circuit ckt;
    auto n = ckt.node("n");
    EXPECT_THROW(ckt.B("B1", n, GND, "X={1+2}"), std::invalid_argument);
}
