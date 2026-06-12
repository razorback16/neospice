#include <gtest/gtest.h>
#include "devices/vcvs_nonlinear.hpp"
#include "devices/vccs_nonlinear.hpp"
#include "devices/ccvs_nonlinear.hpp"
#include "devices/cccs_nonlinear.hpp"
#include "core/neo_solver.hpp"
#include "api/neospice.hpp"
#include <cmath>
#include <complex>

using namespace neospice;

// ===========================================================================
// NonlinearVCVS unit tests
// ===========================================================================

// ---------------------------------------------------------------------------
// POLY(1) squarer: coefficients [0, 0, 1] => f(V) = V^2
// ---------------------------------------------------------------------------
TEST(NonlinearVCVS, Poly1SquarerDirectEvaluate) {
    // np=0, nn=GROUND, ctrl_pos=1, ctrl_neg=GROUND, branch=2
    // Coefficients: c0=0, c1=0, c2=1  => f(V1) = V1^2
    NonlinearVCVS e("E1", 0, GROUND_INTERNAL,
                    {{1, GROUND_INTERNAL}},
                    {0.0, 0.0, 1.0});
    e.set_branch_index(2);

    SparsityBuilder builder(3);
    e.stamp_pattern(builder);
    builder.add(0, 0);
    builder.add(1, 1);
    auto pattern = builder.build();
    NumericMatrix mat(pattern);
    e.assign_offsets(pattern);

    // V(node 0) = 0, V(node 1) = 3.0, branch = 0
    std::vector<double> voltages = {0.0, 3.0, 0.0};
    std::vector<double> rhs(3, 0.0);
    e.evaluate(voltages, mat, rhs);

    // Jacobian: df/dV1 = 2*V1 = 6.0
    // mat[branch, ctrl_pos] = -df/dV1 = -6.0
    EXPECT_NEAR(mat.value(pattern.offset(2, 1)), -6.0, 1e-10);
    // mat[branch, np] = +1
    EXPECT_NEAR(mat.value(pattern.offset(2, 0)),  1.0, 1e-10);
    // KCL: mat[np, branch] = +1
    EXPECT_NEAR(mat.value(pattern.offset(0, 2)),  1.0, 1e-10);

    // RHS: f(Vc) - sum(df/dVk * Vk) = 9.0 - 6.0 * 3.0 = 9.0 - 18.0 = -9.0
    EXPECT_NEAR(rhs[2], -9.0, 1e-10);
}

// ---------------------------------------------------------------------------
// POLY(1) linear [0, 2]: equivalent to gain=2 linear VCVS
// ---------------------------------------------------------------------------
TEST(NonlinearVCVS, Poly1LinearEquivalent) {
    Simulator sim;
    std::string netlist = R"(
* POLY(1) linear equivalent to gain=2 VCVS
Vin in 0 DC 3.0
E1 out 0 POLY(1) in 0 0 2
R1 out 0 1k
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    // V(out) = 2 * V(in) = 2 * 3.0 = 6.0
    EXPECT_NEAR(std::get<DCResult>(result.analysis).voltage("out"), 6.0, 1e-6);
    EXPECT_NEAR(std::get<DCResult>(result.analysis).voltage("in"),  3.0, 1e-6);
}

// ---------------------------------------------------------------------------
// POLY(1) squarer: V(out) = V(in)^2
// For V(in) = 3V, V(out) should be 9V
// ---------------------------------------------------------------------------
TEST(NonlinearVCVS, Poly1Squarer) {
    Simulator sim;
    std::string netlist = R"(
* POLY(1) squarer: V(out) = V(in)^2
Vin in 0 DC 3.0
E1 out 0 POLY(1) in 0 0 0 1
R1 out 0 1k
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    // V(out) = V(in)^2 = 9.0
    EXPECT_NEAR(std::get<DCResult>(result.analysis).voltage("out"), 9.0, 1e-4);
}

// ---------------------------------------------------------------------------
// POLY(1) squarer at different input values
// ---------------------------------------------------------------------------
TEST(NonlinearVCVS, Poly1SquarerNegativeInput) {
    Simulator sim;
    std::string netlist = R"(
* POLY(1) squarer: V(out) = V(in)^2, Vin = -2V
Vin in 0 DC -2.0
E1 out 0 POLY(1) in 0 0 0 1
R1 out 0 1k
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    // V(out) = (-2)^2 = 4.0
    EXPECT_NEAR(std::get<DCResult>(result.analysis).voltage("out"), 4.0, 1e-4);
}

// ---------------------------------------------------------------------------
// POLY(2) adder: V(out) = 0 + 1*V1 + 1*V2 = V(in1) + V(in2)
// Coefficients: c0=0, c1=1, c2=1
// ---------------------------------------------------------------------------
TEST(NonlinearVCVS, Poly2Adder) {
    Simulator sim;
    std::string netlist = R"(
* POLY(2) adder: V(out) = V(in1) + V(in2)
V1 in1 0 DC 2.0
V2 in2 0 DC 3.0
E1 out 0 POLY(2) in1 0 in2 0 0 1 1
R1 out 0 1k
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    // V(out) = 2.0 + 3.0 = 5.0
    EXPECT_NEAR(std::get<DCResult>(result.analysis).voltage("out"), 5.0, 1e-6);
}

// ---------------------------------------------------------------------------
// TABLE VCVS: piecewise-linear, 3 points
// (0,0) (1,5) (2,5) — ramp from 0→5 for x in [0,1], flat at 5 for x in [1,2]
// ---------------------------------------------------------------------------
TEST(NonlinearVCVS, TableInterpolation) {
    Simulator sim;
    std::string netlist = R"(
* TABLE VCVS: 3-point table
* (0,0) (1,5) (2,5)
Vin in 0 DC 0.5
E1 out 0 TABLE {V(in)} = (0,0) (1,5) (2,5)
R1 out 0 1k
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    // V(in)=0.5, interpolate between (0,0) and (1,5): y = 0 + 0.5*(5-0) = 2.5
    EXPECT_NEAR(std::get<DCResult>(result.analysis).voltage("out"), 2.5, 1e-4);
}

TEST(NonlinearVCVS, TableClampLow) {
    Simulator sim;
    std::string netlist = R"(
* TABLE VCVS: clamp at low end
Vin in 0 DC -1.0
E1 out 0 TABLE {V(in)} = (0,0) (1,5) (2,5)
R1 out 0 1k
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    // Clamped to table[0].y = 0.0
    EXPECT_NEAR(std::get<DCResult>(result.analysis).voltage("out"), 0.0, 1e-4);
}

TEST(NonlinearVCVS, TableClampHigh) {
    Simulator sim;
    std::string netlist = R"(
* TABLE VCVS: clamp at high end
Vin in 0 DC 3.0
E1 out 0 TABLE {V(in)} = (0,0) (1,5) (2,5)
R1 out 0 1k
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    // Clamped to table[last].y = 5.0
    EXPECT_NEAR(std::get<DCResult>(result.analysis).voltage("out"), 5.0, 1e-4);
}

TEST(NonlinearVCVS, TableExactPoint) {
    Simulator sim;
    std::string netlist = R"(
* TABLE VCVS: exactly at a table point
Vin in 0 DC 1.0
E1 out 0 TABLE {V(in)} = (0,0) (1,5) (2,5)
R1 out 0 1k
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    // ngspice rewrites E/G TABLE to an XSPICE pwl code model that rounds every
    // corner with a parabola over ±0.1·segment (limit=TRUE). At the (1,5) corner
    // the slope drops 5→0, so the smoothed output is 4.875, NOT the raw 5.0.
    // Verified against ngspice on this exact circuit: v(out) = 4.875.
    EXPECT_NEAR(std::get<DCResult>(result.analysis).voltage("out"), 4.875, 1e-4);
}

// ---------------------------------------------------------------------------
// Expression-controlled TABLE VCVS: the control braces hold a full scalar
// expression, not a bare V(node). Previously the parser dropped everything
// but a single V(node) (collapsing scaled/compound controls to ground); the
// expression must now be evaluated each Newton iteration. The control here is
// {V(in)*5000}: with V(in)=0.5 the table argument is 2500, well past the last
// breakpoint, so the output clamps to the last table value. Matches ngspice.
// ---------------------------------------------------------------------------
TEST(NonlinearVCVS, TableScaledExpressionControl) {
    Simulator sim;
    std::string netlist = R"(
* Expression-controlled TABLE VCVS — scaled gain inside the control braces
Vin in 0 DC 0.5
Rin in 0 1meg
E1 out 0 TABLE {V(in)*5000} = (0,0) (3.2,3.2)
R1 out 0 1k
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    // V(in)*5000 = 2500 >> 3.2 → clamp to last breakpoint y = 3.2.
    EXPECT_NEAR(std::get<DCResult>(result.analysis).voltage("out"), 3.2, 1e-4);
}

// Compound differential control {(V(inp)-V(inm))*5000} — both control nodes
// must be carried through the expression (not collapsed to ground). Here the
// argument 0.5*5000 = 2500 again clamps the high-gain comparator to 3.2.
TEST(NonlinearVCVS, TableDifferentialExpressionControl) {
    Simulator sim;
    std::string netlist = R"(
* Compound differential expression control
Vinp inp 0 DC 0.5
Vinm inm 0 DC 0.0
Rinp inp 0 1meg
Rinm inm 0 1meg
E1 out 0 TABLE {(V(inp)-V(inm))*5000} = (0,0) (3.2,3.2)
R1 out 0 1k
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    EXPECT_NEAR(std::get<DCResult>(result.analysis).voltage("out"), 3.2, 1e-4);
}

// Low-gain interpolating case: prove the scale factor is genuinely applied to
// the table argument (not dropped to the bare V(in)). Gain ×2 with V(in)=1.0
// gives arg = 2.0, the midpoint of (0,0)-(4,8) → y = 4.0. The dropped-gain bug
// would use arg = 1.0 → y ≈ 2.0. Mid-segment, so corner smoothing is nil.
TEST(NonlinearVCVS, TableExpressionInterpolatesWithGain) {
    Simulator sim;
    std::string netlist = R"(
* Low-gain interpolating expression control
Vin in 0 DC 1.0
Rin in 0 1meg
E1 out 0 TABLE {V(in)*2} = (0,0) (4,8)
R1 out 0 1k
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    EXPECT_NEAR(std::get<DCResult>(result.analysis).voltage("out"), 4.0, 1e-4);
}

// ---------------------------------------------------------------------------
// Parser: verify POLY syntax is correctly parsed
// ---------------------------------------------------------------------------
TEST(NonlinearVCVS, ParserPolyLinear) {
    Simulator sim;
    // Linear POLY(1) [0, 5] should give gain=5
    std::string netlist = R"(
Parser poly linear test
Vin in 0 DC 1.0
E1 out 0 POLY(1) in 0 0 5
R1 out 0 1k
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    EXPECT_NEAR(std::get<DCResult>(result.analysis).voltage("out"), 5.0, 1e-6);
}

TEST(NonlinearVCVS, ParserTableSyntax) {
    Simulator sim;
    // Identity table: (0,0) (10,10) => V(out) = V(in) for V(in) in [0,10]
    std::string netlist = R"(
Parser table test
Vin in 0 DC 4.0
E1 out 0 TABLE {V(in)} = (0,0) (10,10)
R1 out 0 1k
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    EXPECT_NEAR(std::get<DCResult>(result.analysis).voltage("out"), 4.0, 1e-4);
}

// ---------------------------------------------------------------------------
// Existing linear E still works
// ---------------------------------------------------------------------------
TEST(NonlinearVCVS, LinearFallthrough) {
    Simulator sim;
    std::string netlist = R"(
Linear VCVS still works
Vin in 0 DC 2.0
E1 out 0 in 0 3.0
R1 out 0 1k
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    EXPECT_NEAR(std::get<DCResult>(result.analysis).voltage("out"), 6.0, 1e-9);
}

// ---------------------------------------------------------------------------
// AC analysis: POLY(1) linear at DC operating point gives flat gain
// ---------------------------------------------------------------------------
TEST(NonlinearVCVS, Poly1ACGain) {
    Simulator sim;
    // POLY(1) [0, 4] = gain of 4; DC=0 so linearization gives 4 everywhere
    std::string netlist = R"(
* POLY(1) AC test
V1 in 0 DC 0 AC 1
E1 out 0 POLY(1) in 0 0 4
R1 out 0 1k
.ac dec 5 1 100k
.end
)";
    auto ckt = sim.parse(netlist);
    auto ac_result = sim.run_ac(ckt, AnalysisCommand::DEC, 5, 1.0, 1e5);
    ASSERT_FALSE(ac_result.frequency.empty());
    auto it = ac_result.voltages.find("v(out)");
    ASSERT_NE(it, ac_result.voltages.end());
    for (const auto& v : it->second) {
        EXPECT_NEAR(std::abs(v), 4.0, 1e-6);
    }
}

// ===========================================================================
// NonlinearVCCS unit tests
// ===========================================================================

// ---------------------------------------------------------------------------
// POLY(1) linear VCCS [0, 0.01]: equivalent to gm=0.01
// ---------------------------------------------------------------------------
TEST(NonlinearVCCS, Poly1LinearEquivalent) {
    Simulator sim;
    std::string netlist = R"(
* POLY(1) VCCS linear: I = 0.01 * V(in)
Vin in 0 DC 1.0
G1 out 0 POLY(1) in 0 0 0.01
R1 out 0 1k
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    // I = 0.01 * 1.0 = 0.01 A leaves out, V(out) = -0.01 * 1000 = -10.0
    EXPECT_NEAR(std::get<DCResult>(result.analysis).voltage("out"), -10.0, 1e-6);
}

// ---------------------------------------------------------------------------
// POLY(1) squarer VCCS: I(out) = V(in)^2 / R
// For V(in) = 2V: I = 4 / 1000 = 4 mA, but wait — VCCS sets I = f(Vc)
// I = V(in)^2 = 4.0 A... unreasonably large; use small coefficients
// I = 0.001 * V(in)^2, V(in)=2 => I = 0.004 A, V(out) = 0.004*1000 = 4.0
// ---------------------------------------------------------------------------
TEST(NonlinearVCCS, Poly1Squarer) {
    Simulator sim;
    std::string netlist = R"(
* POLY(1) quadratic VCCS: I = 0.001 * V(in)^2
Vin in 0 DC 2.0
G1 out 0 POLY(1) in 0 0 0 0.001
R1 out 0 1k
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    // I = 0.001 * 4.0 = 0.004 A leaves out, V(out) = -0.004 * 1000 = -4.0
    EXPECT_NEAR(std::get<DCResult>(result.analysis).voltage("out"), -4.0, 1e-4);
}

// ---------------------------------------------------------------------------
// Linear VCCS G still works after parser change
// ---------------------------------------------------------------------------
TEST(NonlinearVCCS, LinearFallthrough) {
    Simulator sim;
    std::string netlist = R"(
Linear VCCS still works
Vin in 0 DC 2.0
G1 out 0 in 0 0.005
R1 out 0 2k
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    // I = 0.005 * 2.0 = 0.01 A leaves out, V(out) = -0.01 * 2000 = -20.0
    EXPECT_NEAR(std::get<DCResult>(result.analysis).voltage("out"), -20.0, 1e-6);
}

// ---------------------------------------------------------------------------
// TABLE VCCS basic interpolation
// ---------------------------------------------------------------------------
TEST(NonlinearVCCS, TableInterpolation) {
    Simulator sim;
    std::string netlist = R"(
* TABLE VCCS: I = linear ramp
Vin in 0 DC 0.5
G1 out 0 TABLE {V(in)} = (0,0) (1,0.01) (2,0.01)
R1 out 0 1k
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    // V(in)=0.5 interpolates to I = 0 + 0.5*0.01 = 0.005 A leaves out
    // V(out) = -0.005 * 1000 = -5.0 V
    EXPECT_NEAR(std::get<DCResult>(result.analysis).voltage("out"), -5.0, 1e-4);
}

// ---------------------------------------------------------------------------
// POLY(2) VCCS adder: I = V(in1) + V(in2) * 0.001
// Coefficients [0, 0.001, 0.001]: I = 0.001*V1 + 0.001*V2
// V1=1, V2=1: I = 0.002, V(out) = 0.002*1000 = 2.0
// ---------------------------------------------------------------------------
TEST(NonlinearVCCS, Poly2Adder) {
    Simulator sim;
    std::string netlist = R"(
* POLY(2) VCCS adder
V1 in1 0 DC 1.0
V2 in2 0 DC 1.0
G1 out 0 POLY(2) in1 0 in2 0 0 0.001 0.001
R1 out 0 1k
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    // I = 0.001*1 + 0.001*1 = 0.002 leaves out, V(out) = -0.002*1000 = -2.0
    EXPECT_NEAR(std::get<DCResult>(result.analysis).voltage("out"), -2.0, 1e-6);
}

// ---------------------------------------------------------------------------
// POLY(1) constant: coefficients [5] => I = 5A regardless of Vc
// This is an independent current source
// V(out) = 5 * R = 5000V (for R=1k), but unreasonable...
// Use I = 0.001 (c0=0.001) => V(out) = 1.0
// ---------------------------------------------------------------------------
TEST(NonlinearVCCS, Poly1Constant) {
    Simulator sim;
    std::string netlist = R"(
* POLY(1) constant term only
Vin in 0 DC 0.0
G1 out 0 POLY(1) in 0 0.001
R1 out 0 1k
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    // I = 0.001 A (constant) leaves out, V(out) = -0.001 * 1000 = -1.0
    EXPECT_NEAR(std::get<DCResult>(result.analysis).voltage("out"), -1.0, 1e-6);
}

// ===========================================================================
// NonlinearCCVS (H POLY) unit tests
// ===========================================================================

// ---------------------------------------------------------------------------
// POLY(1) linear: H1 out 0 POLY(1) Vsense1 0 1000
// Equivalent to transresistance 1000
// I(Vsense1) = 1V/1k = 1mA, V(out) = 1000 * 1mA = 1.0V
// ---------------------------------------------------------------------------
TEST(NonlinearCCVS, Poly1LinearEquivalent) {
    Simulator sim;
    std::string netlist = R"(
* POLY(1) linear CCVS: V(out) = 1000 * I(Vsense1)
Vin in 0 DC 1.0
R1 in a 1k
Vsense1 a 0 0
H1 out 0 POLY(1) Vsense1 0 1000
R2 out 0 1k
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    // I(Vsense1) = 1mA, V(out) = 0 + 1000*0.001 = 1.0V
    EXPECT_NEAR(std::get<DCResult>(result.analysis).voltage("out"), 1.0, 1e-6);
}

// ---------------------------------------------------------------------------
// POLY(1) with offset: H1 out 0 POLY(1) Vsense1 0.5 1000
// I(Vsense1) = 1mA, V(out) = 0.5 + 1000*0.001 = 1.5V
// ---------------------------------------------------------------------------
TEST(NonlinearCCVS, Poly1WithOffset) {
    Simulator sim;
    std::string netlist = R"(
* POLY(1) CCVS with offset
Vin in 0 DC 1.0
R1 in a 1k
Vsense1 a 0 0
H1 out 0 POLY(1) Vsense1 0.5 1000
R2 out 0 1k
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    EXPECT_NEAR(std::get<DCResult>(result.analysis).voltage("out"), 1.5, 1e-4);
}

// ---------------------------------------------------------------------------
// POLY(1) quadratic: V(out) = 1e6 * I(Vs)^2
// I(Vsense1) = 1V/1k = 1mA, V(out) = 1e6 * (1e-3)^2 = 1.0V
// ---------------------------------------------------------------------------
TEST(NonlinearCCVS, Poly1Quadratic) {
    Simulator sim;
    std::string netlist = R"(
* POLY(1) quadratic CCVS: V(out) = 1e6 * I(Vs)^2
Vin in 0 DC 1.0
R1 in a 1k
Vsense1 a 0 0
H1 out 0 POLY(1) Vsense1 0 0 1e6
R2 out 0 1k
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    // I = 1mA, V(out) = 1e6 * (1e-3)^2 = 1.0
    EXPECT_NEAR(std::get<DCResult>(result.analysis).voltage("out"), 1.0, 1e-4);
}

// ---------------------------------------------------------------------------
// Linear H still works after parser change
// ---------------------------------------------------------------------------
TEST(NonlinearCCVS, LinearFallthrough) {
    Simulator sim;
    std::string netlist = R"(
Linear CCVS still works
Vin in 0 DC 1.0
R1 in a 1k
Vsense1 a 0 0
H1 out 0 Vsense1 2000
R2 out 0 1k
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    // I(Vsense1) = 1mA, V(out) = 2000 * 0.001 = 2.0V
    EXPECT_NEAR(std::get<DCResult>(result.analysis).voltage("out"), 2.0, 1e-6);
}

// ===========================================================================
// NonlinearCCCS (F POLY) unit tests
// ===========================================================================

// ---------------------------------------------------------------------------
// POLY(1) linear: F1 out 0 POLY(1) Vsense1 0 1000
// Equivalent to gain 1000
// I(Vsense1) = 1mA, I(F1) = 1000*1mA = 1A leaves out
// V(out) = -1A * 1k = -1000V
// ---------------------------------------------------------------------------
TEST(NonlinearCCCS, Poly1LinearEquivalent) {
    Simulator sim;
    std::string netlist = R"(
* POLY(1) linear CCCS: I = 1000 * I(Vsense1)
Vin in 0 DC 1.0
R1 in a 1k
Vsense1 a 0 0
F1 out 0 POLY(1) Vsense1 0 1000
R2 out 0 1k
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    // I = 1000*0.001 = 1A leaves out, V(out) = -1*1000 = -1000
    EXPECT_NEAR(std::get<DCResult>(result.analysis).voltage("out"), -1000.0, 1e-3);
}

// ---------------------------------------------------------------------------
// POLY(1) with offset: F1 out 0 POLY(1) Vsense1 0.5 1000
// I(Vsense1) = 1mA, I(F1) = 0.5 + 1000*1mA = 1.5A leaves out
// V(out) = -1.5 * 1k = -1500V
// ---------------------------------------------------------------------------
TEST(NonlinearCCCS, Poly1WithOffset) {
    Simulator sim;
    std::string netlist = R"(
* POLY(1) CCCS with offset
Vin in 0 DC 1.0
R1 in a 1k
Vsense1 a 0 0
F1 out 0 POLY(1) Vsense1 0.5 1000
R2 out 0 1k
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    EXPECT_NEAR(std::get<DCResult>(result.analysis).voltage("out"), -1500.0, 1e-3);
}

// ---------------------------------------------------------------------------
// POLY(2) adder: F1 out 0 POLY(2) Vs1 Vs2 0 500 500
// I(Vs1) = 2mA, I(Vs2) = 3mA
// I(F1) = 0 + 500*0.002 + 500*0.003 = 2.5A leaves out
// V(out) = -2.5 * 1k = -2500V
// ---------------------------------------------------------------------------
TEST(NonlinearCCCS, Poly2Adder) {
    Simulator sim;
    std::string netlist = R"(
* POLY(2) CCCS adder
V1 in1 0 2
R1 in1 a 1k
Vsense1 a 0 0
V2 in2 0 3
R2 in2 b 1k
Vsense2 b 0 0
F1 out 0 POLY(2) Vsense1 Vsense2 0 500 500
R3 out 0 1k
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    EXPECT_NEAR(std::get<DCResult>(result.analysis).voltage("out"), -2500.0, 1e-3);
}

// ---------------------------------------------------------------------------
// POLY(1) constant: F1 out 0 POLY(1) Vsense1 0.002
// I(F1) = 0.002 A constant, V(out) = -0.002 * 1k = -2.0V
// ---------------------------------------------------------------------------
TEST(NonlinearCCCS, Poly1Constant) {
    Simulator sim;
    std::string netlist = R"(
* POLY(1) CCCS constant term only
Vin in 0 DC 1.0
R1 in a 1k
Vsense1 a 0 0
F1 out 0 POLY(1) Vsense1 0.002
R2 out 0 1k
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    EXPECT_NEAR(std::get<DCResult>(result.analysis).voltage("out"), -2.0, 1e-6);
}

// ---------------------------------------------------------------------------
// Linear F still works after parser change
// ---------------------------------------------------------------------------
TEST(NonlinearCCCS, LinearFallthrough) {
    Simulator sim;
    std::string netlist = R"(
Linear CCCS still works
Vin in 0 DC 1.0
R1 in a 1k
Vsense1 a 0 0
F1 out 0 Vsense1 500
R2 out 0 1k
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    // I(Vsense1) = 1mA, I(F1) = 500*1mA = 0.5A, V(out) = -0.5*1000 = -500
    EXPECT_NEAR(std::get<DCResult>(result.analysis).voltage("out"), -500.0, 1e-6);
}
