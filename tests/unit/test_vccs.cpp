#include <gtest/gtest.h>
#include "devices/vccs.hpp"
#include "core/neo_solver.hpp"
#include "api/neospice.hpp"
#include <complex>
#include <cmath>

using namespace neospice;

// ---------------------------------------------------------------------------
// Unit test: stamp_pattern with output grounded on one side
//
// VCCS with np=0, nn=GROUND, ncp=1, ncn=GROUND.
// Active stamp positions: (np,ncp) = (0,1) only — all others involve ground.
// Expected nnz = 1
// ---------------------------------------------------------------------------
TEST(VCCS, StampPatternPartialGround) {
    VCCS g("G1", 0, GROUND_INTERNAL, 1, GROUND_INTERNAL, 0.01);
    SparsityBuilder builder(2);
    g.stamp_pattern(builder);
    auto pattern = builder.build();
    // (0,1) is the only non-ground pair
    EXPECT_EQ(pattern.nnz(), 1);
}

// ---------------------------------------------------------------------------
// Unit test: stamp_pattern with all 4 terminals active
//
// np=0, nn=1, ncp=2, ncn=3 → 4 non-zero entries
// ---------------------------------------------------------------------------
TEST(VCCS, StampPatternAllTerminals) {
    VCCS g("G1", 0, 1, 2, 3, 0.01);
    SparsityBuilder builder(4);
    g.stamp_pattern(builder);
    auto pattern = builder.build();
    // (0,2), (0,3), (1,2), (1,3)
    EXPECT_EQ(pattern.nnz(), 4);
}

// ---------------------------------------------------------------------------
// Unit test: evaluate stamps correct values
//
// np=0, nn=1, ncp=2, ncn=3, gm=0.005, matrix size=4
//
// Expected after evaluate:
//   mat[0,2] = +gm = +0.005   (KCL np, control+)
//   mat[0,3] = -gm = -0.005   (KCL np, control-)
//   mat[1,2] = -gm = -0.005   (KCL nn, control+)
//   mat[1,3] = +gm = +0.005   (KCL nn, control-)
//   RHS unchanged (all 0)
// ---------------------------------------------------------------------------
TEST(VCCS, EvaluateValues) {
    const double gm = 0.005;
    VCCS g("G1", 0, 1, 2, 3, gm);

    SparsityBuilder builder(4);
    g.stamp_pattern(builder);
    // Add diagonals to allow matrix creation
    builder.add(0, 0);
    builder.add(1, 1);
    builder.add(2, 2);
    builder.add(3, 3);
    auto pattern = builder.build();

    NumericMatrix mat(pattern);
    g.assign_offsets(pattern);

    std::vector<double> voltages(4, 0.0);
    std::vector<double> rhs(4, 0.0);
    g.evaluate(voltages, mat, rhs);

    EXPECT_DOUBLE_EQ(mat.value(pattern.offset(0, 2)),  gm);  // np,  ncp
    EXPECT_DOUBLE_EQ(mat.value(pattern.offset(0, 3)), -gm);  // np,  ncn
    EXPECT_DOUBLE_EQ(mat.value(pattern.offset(1, 2)), -gm);  // nn,  ncp
    EXPECT_DOUBLE_EQ(mat.value(pattern.offset(1, 3)),  gm);  // nn,  ncn

    // RHS should remain 0 — VCCS has no independent source term
    for (double v : rhs) EXPECT_DOUBLE_EQ(v, 0.0);
}

// ---------------------------------------------------------------------------
// Unit test: extra_vars returns 0 (no branch variable needed)
// ---------------------------------------------------------------------------
TEST(VCCS, ExtraVarsIsZero) {
    VCCS g("G1", 0, GROUND_INTERNAL, 1, GROUND_INTERNAL, 0.01);
    EXPECT_EQ(g.extra_vars(), 0);
}

// ---------------------------------------------------------------------------
// Integration test: simple transconductance amplifier
//
// V1 in 0 DC 1.0
// G1 out 0 in 0 0.01
// R1 out 0 1k
// .op
//
// Expected: I = gm * V(in) = 0.01 A leaves N+ (out)
//           V(out) = -I * R = -0.01 * 1000 = -10.0 V
//
// Verified against ngspice 40 (.op analysis).
// ---------------------------------------------------------------------------
TEST(VCCS, SimpleTransconductance) {
    Simulator sim;
    std::string netlist = R"(
Simple transconductance amplifier
V1 in 0 DC 1.0
G1 out 0 in 0 0.01
R1 out 0 1k
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    EXPECT_NEAR(std::get<DCResult>(result.analysis).node_voltages["v(in)"],   1.0,  1e-6);
    EXPECT_NEAR(std::get<DCResult>(result.analysis).node_voltages["v(out)"], -10.0, 1e-6);
}

// ---------------------------------------------------------------------------
// Integration test: differential control inputs
//
// V1 inp 0 DC 3.0
// V2 inn 0 DC 1.0
// G1 out 0 inp inn 0.005
// R1 out 0 2k
// .op
//
// Expected: I = 0.005 * (3.0 - 1.0) = 0.01 A leaves N+ (out)
//           V(out) = -0.01 * 2000 = -20.0 V
// ---------------------------------------------------------------------------
TEST(VCCS, DifferentialInput) {
    Simulator sim;
    std::string netlist = R"(
Differential transconductance
V1 inp 0 DC 3.0
V2 inn 0 DC 1.0
G1 out 0 inp inn 0.005
R1 out 0 2k
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    EXPECT_NEAR(std::get<DCResult>(result.analysis).node_voltages["v(inp)"],  3.0,  1e-6);
    EXPECT_NEAR(std::get<DCResult>(result.analysis).node_voltages["v(inn)"],  1.0,  1e-6);
    EXPECT_NEAR(std::get<DCResult>(result.analysis).node_voltages["v(out)"], -20.0, 1e-6);
}

// ---------------------------------------------------------------------------
// Integration test: zero transconductance (output should be 0 V)
// ---------------------------------------------------------------------------
TEST(VCCS, ZeroTransconductance) {
    Simulator sim;
    std::string netlist = R"(
Zero gm VCCS
V1 in 0 DC 5.0
G1 out 0 in 0 0.0
R1 out 0 1k
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    EXPECT_NEAR(std::get<DCResult>(result.analysis).node_voltages["v(out)"], 0.0, 1e-9);
}

// ---------------------------------------------------------------------------
// Integration test: negative transconductance (inverting)
//
// V1 in 0 DC 2.0
// G1 out 0 in 0 -0.002
// R1 out 0 1k
// .op
//
// Expected: I = -0.002 * 2.0 = -0.004 A leaves N+ (out)
//           V(out) = 0.004 * 1000 = 4.0 V
// ---------------------------------------------------------------------------
TEST(VCCS, NegativeTransconductance) {
    Simulator sim;
    std::string netlist = R"(
Inverting VCCS
V1 in 0 DC 2.0
G1 out 0 in 0 -0.002
R1 out 0 1k
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    EXPECT_NEAR(std::get<DCResult>(result.analysis).node_voltages["v(out)"], 4.0, 1e-6);
}

// ---------------------------------------------------------------------------
// Integration test: cascaded VCCS stages
//
// V1 in 0 DC 1.0
// G1 mid 0 in 0 0.01      → V(mid) = -0.01 * 1.0 * 500 = -5.0 V
// R1 mid 0 500
// G2 out 0 mid 0 0.002    → V(out) = -0.002 * (-5.0) * 1000 = 10.0 V
// R2 out 0 1k
// .op
// ---------------------------------------------------------------------------
TEST(VCCS, CascadedStages) {
    Simulator sim;
    std::string netlist = R"(
Cascaded VCCS
V1 in 0 DC 1.0
G1 mid 0 in 0 0.01
R1 mid 0 500
G2 out 0 mid 0 0.002
R2 out 0 1k
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    EXPECT_NEAR(std::get<DCResult>(result.analysis).node_voltages["v(mid)"], -5.0,  1e-6);
    EXPECT_NEAR(std::get<DCResult>(result.analysis).node_voltages["v(out)"], 10.0, 1e-6);
}

// ---------------------------------------------------------------------------
// Integration test: AC analysis — |V(out)| = gm * R at all frequencies
//
// The VCCS is a linear, frequency-independent element.  With gm=0.01 and
// R=1k, |V(out)| = gm * R * |V(in)| = 0.01 * 1000 * 1 = 10.0 V at every
// frequency across the swept range (1 Hz to 1 MHz, dec 10).
//
// V1 in 0 DC 0 AC 1
// G1 out 0 in 0 0.01
// R1 out 0 1k
// .ac dec 10 1 1Meg
// .end
// ---------------------------------------------------------------------------
TEST(VCCS, ACResponseFlat) {
    Simulator sim;
    std::string netlist = R"(
* VCCS AC test
V1 in 0 DC 0 AC 1
G1 out 0 in 0 0.01
R1 out 0 1k
.ac dec 10 1 1Meg
.end
)";
    auto ckt = sim.parse(netlist);
    auto ac_result = sim.run_ac(ckt, AnalysisCommand::DEC, 10, 1.0, 1e6);

    // Verify frequencies were generated
    ASSERT_FALSE(ac_result.frequency.empty());

    // V(out) must be present in the result
    auto it = ac_result.voltages.find("v(out)");
    ASSERT_NE(it, ac_result.voltages.end());

    const auto& vout = it->second;
    ASSERT_EQ(vout.size(), ac_result.frequency.size());

    // |V(out)| = gm * R = 0.01 * 1000 = 10.0 at every frequency
    for (std::size_t i = 0; i < vout.size(); ++i) {
        EXPECT_NEAR(std::abs(vout[i]), 10.0, 1e-6)
            << "at frequency " << ac_result.frequency[i] << " Hz";
    }

    // Explicitly check first and last points
    EXPECT_NEAR(std::abs(vout.front()), 10.0, 1e-6);
    EXPECT_NEAR(std::abs(vout.back()),  10.0, 1e-6);
}
