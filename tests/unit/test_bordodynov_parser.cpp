// Regression tests for the bordodynov-behavioral parser cluster fixes:
//   A) brace/param-expression source-function args (PULSE/EXP/...)
//   B) implicit POLY(1) for E/G with multiple trailing coefficients
//   C) space-glued ".param key =val" form
//   D) '$'-led token is a comment only at line start (ngbehavior=psa)
// Each reproduces the minimal case proven against ngspice -D ngbehavior=psa.

#include <gtest/gtest.h>
#include "api/neospice.hpp"
#include <variant>

using namespace neospice;

// ---------------------------------------------------------------------------
// A) PULSE arg given as a {param} expression must be evaluated (not -> 0).
//    DC/op value of a PULSE source is its first arg (V1). With V0=1.35 over a
//    10-ohm load and 100k pulls, V(nplus) = 6.749663 (matches ngspice psa).
// ---------------------------------------------------------------------------
TEST(BordodynovParser, PulseBraceArgEvaluated) {
    Simulator sim;
    std::string netlist = R"(
* PULSE brace arg
.param V0=1.35
I2 nminus nplus PULSE({V0} 0 1m 1u 1u 200m 0.5)
R2 nplus nminus 10
Rp nplus 0 100k
Rm nminus 0 100k
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    EXPECT_NEAR(std::get<DCResult>(result.analysis).voltage("nplus"), 6.749663, 1e-4);
}

// A literal arg (no brace) must still parse identically.
TEST(BordodynovParser, PulseLiteralArgUnchanged) {
    Simulator sim;
    std::string netlist = R"(
* PULSE literal arg
I2 nminus nplus PULSE(1.35 0 1m 1u 1u 200m 0.5)
R2 nplus nminus 10
Rp nplus 0 100k
Rm nminus 0 100k
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    EXPECT_NEAR(std::get<DCResult>(result.analysis).voltage("nplus"), 6.749663, 1e-4);
}

// Brace arg glued to the keyword inside a subcircuit (the ISO7637-2.lib form).
TEST(BordodynovParser, PulseBraceArgInSubckt) {
    Simulator sim;
    std::string netlist = R"(
* subckt PULSE brace arg
.subckt src + -
.param Ua = 13.5
.param Ri = 10
.param t0 = 1m
.param t2 = 200m
.param t1 = 0.5
I2 - + PULSE({Ua/Ri} 0 {t0} 1u 1u {t2} {t1})
.ends
X1 nplus nminus src
R2 nplus nminus 10
Rp nplus 0 100k
Rm nminus 0 100k
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    EXPECT_NEAR(std::get<DCResult>(result.analysis).voltage("nplus"), 6.749663, 1e-4);
}

// ---------------------------------------------------------------------------
// B) Implicit POLY(1) VCCS: "G n+ n- nc+ nc- c0 c1 c2" with no POLY keyword.
//    I = c0 + c1*Vc + c2*Vc^2. With Vc=0.5, coeffs {1,2,3}:
//    I = 1 + 2*0.5 + 3*0.25 = 2.75A => V(out) = -2.75*1k = -2750.
// ---------------------------------------------------------------------------
TEST(BordodynovParser, ImplicitPolyVCCS) {
    Simulator sim;
    std::string netlist = R"(
* implicit poly VCCS
V1 nc 0 DC 0.5
Rc nc 0 1
G1 out 0 nc 0 1.0 2.0 3.0
Rout out 0 1k
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    EXPECT_NEAR(std::get<DCResult>(result.analysis).voltage("out"), -2750.0, 1e-3);
}

// Implicit POLY(1) VCVS mirror: V = c0 + c1*Vc with single output gain pair.
TEST(BordodynovParser, ImplicitPolyVCVS) {
    Simulator sim;
    std::string netlist = R"(
* implicit poly VCVS
V1 nc 0 DC 0.5
Rc nc 0 1
E1 out 0 nc 0 1.0 2.0 3.0
Rout out 0 1k
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    // V(out) = 1 + 2*0.5 + 3*0.25 = 2.75
    EXPECT_NEAR(std::get<DCResult>(result.analysis).voltage("out"), 2.75, 1e-4);
}

// A single-coefficient (genuinely linear) G must stay a linear VCCS.
TEST(BordodynovParser, SingleCoeffStaysLinearVCCS) {
    Simulator sim;
    std::string netlist = R"(
* linear VCCS
V1 nc 0 DC 0.5
Rc nc 0 1
G1 out 0 nc 0 2.0
Rout out 0 1k
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    // I = 2.0*0.5 = 1A => V(out) = -1*1k = -1000
    EXPECT_NEAR(std::get<DCResult>(result.analysis).voltage("out"), -1000.0, 1e-6);
}

// ---------------------------------------------------------------------------
// C) ".param Vpp =525mV" (space before '=', value glued to '=') must resolve.
// ---------------------------------------------------------------------------
TEST(BordodynovParser, GluedEqualsParamInSubckt) {
    Simulator sim;
    std::string netlist = R"(
* glued-equals param
.subckt dt a c
.Param Gm =2m
G1 c 0 a 0 {Gm}
.ends
V1 a 0 0.5
X1 a c dt
Rc c 0 1k
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    // Gm=2m, I = 2m*0.5 = 1mA => V(c) = -1mA*1k = -1.0  (Gm dropped -> 0)
    EXPECT_NEAR(std::get<DCResult>(result.analysis).voltage("c"), -1.0, 1e-6);
}

TEST(BordodynovParser, GluedEqualsParamTopLevel) {
    Simulator sim;
    std::string netlist = R"(
* glued-equals top-level param
.param Rv =3k
V1 a 0 1
R1 a c {Rv}
Rc c 0 1k
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    // Divider 1k/(3k+1k) = 0.25  (Rv dropped -> R1 open -> V(c) != 0.25)
    EXPECT_NEAR(std::get<DCResult>(result.analysis).voltage("c"), 0.25, 1e-4);
}

// ---------------------------------------------------------------------------
// D) '$'-led token: a mid-line '$N_xxxx' is a valid node (ngbehavior=psa),
//    only a line-START '$' begins a comment.
// ---------------------------------------------------------------------------
TEST(BordodynovParser, DollarNodeSurvives) {
    Simulator sim;
    std::string netlist = R"(
* dollar node
V1 a 0 1
R1 a $N_99 1k
R2 $N_99 0 1k
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    // Divider midpoint: 0.5
    EXPECT_NEAR(std::get<DCResult>(result.analysis).voltage("$n_99"), 0.5, 1e-6);
}

TEST(BordodynovParser, DollarLineStartIsComment) {
    Simulator sim;
    std::string netlist = R"(
* dollar comment
V1 a 0 1
R1 a 0 1k
$ this entire line is a comment and must be ignored
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    EXPECT_NEAR(std::get<DCResult>(result.analysis).voltage("a"), 1.0, 1e-6);
}

// ---------------------------------------------------------------------------
// E) Forward I(E)-reference: a current-mode behavioral source (G/B) that reads
//    I() of a VOLTAGE-mode behavioral E-source defined LATER in parse order.
//    Deferred-ASRC resolution must be order-independent: the E-source owns an
//    MNA branch, so I(Emeas) is a valid current even though Emeas is added to
//    the circuit after Glate. Before the two-pass fix, branch_provider_for only
//    scanned already-added devices, so Glate was dropped with the warning
//    "B element 'glate' references unknown voltage source 'emeas' in I() —
//    skipping" and v(out) collapsed to 0. ngspice -D ngbehavior=psa gives
//    v(out)=0.5 (Emeas branch current -0.5 A * 1m * 1k Rout). This mirrors the
//    UCC28C42_TRANS GB6->I(EMY19) ordering.
// ---------------------------------------------------------------------------
TEST(BordodynovParser, ForwardCurrentRefToLaterVoltageSource) {
    Simulator sim;
    std::string netlist = R"(
* forward I(E) reference
Vin in 0 2
Glate out 0 Value = { 1m * I(Emeas) }
Rout out 0 1k
Emeas s 0 Value = { V(in) }
Rsense s 0 4
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    // Emeas forces V(s)=V(in)=2 across Rsense=4 -> branch current -0.5 A.
    // Glate = 1m * I(Emeas) = -0.5e-3 A; |v(out)| = 0.5e-3 * 1k = 0.5 V.
    EXPECT_NEAR(std::get<DCResult>(result.analysis).voltage("out"), 0.5, 1e-6);
}
