#include <gtest/gtest.h>
#include "devices/cccs.hpp"
#include "devices/vsource.hpp"
#include "core/neo_solver.hpp"
#include "api/neospice.hpp"
#include <complex>
#include <cmath>
#include <stdexcept>

using namespace neospice;

// ---------------------------------------------------------------------------
// Unit test: stamp_pattern with output grounded on one side
//
// CCCS with np=0, nn=GROUND; sense VSource with branch=1.
// Active stamp entries: (0,1) only — nn is ground, skipped.
// Expected nnz = 1
// ---------------------------------------------------------------------------
TEST(CCCS, StampPatternPartialGround) {
    VSource vsense("Vsense", 2, GROUND_INTERNAL, 0.0);
    vsense.set_branch_index(1);

    CCCS f("F1", 0, GROUND_INTERNAL, 2.0, &vsense);

    SparsityBuilder builder(2);  // vars: 0, 1(sense branch)
    f.stamp_pattern(builder);
    auto pattern = builder.build();
    // (0,1) only — nn is ground
    EXPECT_EQ(pattern.nnz(), 1);
}

// ---------------------------------------------------------------------------
// Unit test: stamp_pattern with both output terminals active
//
// np=0, nn=1; sense VSource branch=2
// Active stamps: (0,2) and (1,2) → nnz = 2
// ---------------------------------------------------------------------------
TEST(CCCS, StampPatternAllTerminals) {
    VSource vsense("Vsense", 3, GROUND_INTERNAL, 0.0);
    vsense.set_branch_index(2);

    CCCS f("F1", 0, 1, 2.0, &vsense);

    SparsityBuilder builder(3);
    f.stamp_pattern(builder);
    auto pattern = builder.build();
    // (0,2), (1,2)
    EXPECT_EQ(pattern.nnz(), 2);
}

// ---------------------------------------------------------------------------
// Unit test: evaluate stamps correct values
//
// np=0, nn=1, gain=3.0; sense VSource branch=2
// Matrix size 3: indices {0, 1, 2(sense branch)}
//
// Expected after evaluate:
//   mat[0,2] = +gain = +3.0  (KCL np: current leaves)
//   mat[1,2] = -gain = -3.0  (KCL nn: current enters)
// RHS remains 0
// ---------------------------------------------------------------------------
TEST(CCCS, EvaluateValues) {
    VSource vsense("Vsense", 3, GROUND_INTERNAL, 0.0);
    vsense.set_branch_index(2);

    const double gain = 3.0;
    CCCS f("F1", 0, 1, gain, &vsense);

    SparsityBuilder builder(3);
    f.stamp_pattern(builder);
    // Add diagonals so matrix is buildable
    builder.add(0, 0);
    builder.add(1, 1);
    builder.add(2, 2);
    auto pattern = builder.build();

    NumericMatrix mat(pattern);
    f.assign_offsets(pattern);

    std::vector<double> voltages(3, 0.0);
    std::vector<double> rhs(3, 0.0);
    f.evaluate(voltages, mat, rhs);

    EXPECT_DOUBLE_EQ(mat.value(pattern.offset(0, 2)),  gain);  // np, sense_branch
    EXPECT_DOUBLE_EQ(mat.value(pattern.offset(1, 2)), -gain);  // nn, sense_branch

    // RHS should remain 0 — CCCS has no independent source term
    for (double v : rhs) EXPECT_DOUBLE_EQ(v, 0.0);
}

// ---------------------------------------------------------------------------
// Unit test: extra_vars returns 0 (no branch variable needed)
// ---------------------------------------------------------------------------
TEST(CCCS, ExtraVarsIsZero) {
    VSource vsense("Vsense", 1, GROUND_INTERNAL, 0.0);
    vsense.set_branch_index(0);

    CCCS f("F1", 1, GROUND_INTERNAL, 2.0, &vsense);
    EXPECT_EQ(f.extra_vars(), 0);
}

// ---------------------------------------------------------------------------
// Unit test: constructor throws on null VSource pointer
// ---------------------------------------------------------------------------
TEST(CCCS, ConstructorThrowsOnNullVSource) {
    EXPECT_THROW(
        CCCS f("F1", 0, GROUND_INTERNAL, 2.0, nullptr),
        std::invalid_argument
    );
}

// ---------------------------------------------------------------------------
// Unit test: stamp_pattern throws if sense VSource branch index not assigned
// ---------------------------------------------------------------------------
TEST(CCCS, StampPatternThrowsWithoutSenseBranchIndex) {
    VSource vsense("Vsense", 1, GROUND_INTERNAL, 0.0);
    // branch_index not set (remains -1)

    CCCS f("F1", 0, GROUND_INTERNAL, 2.0, &vsense);
    SparsityBuilder builder(2);
    EXPECT_THROW(f.stamp_pattern(builder), std::logic_error);
}

// ---------------------------------------------------------------------------
// Integration test: current mirror with gain=2
//
// V1 in 0 DC 5.0
// R1 in sense_node 1k
// Vsense sense_node 0 DC 0    ← 0V source as ammeter; I(Vsense) = 5/1k = 5mA
// F1 out 0 Vsense 2.0         ← I_out = 2 * 5mA = 10mA
// R2 out 0 1k
// .op
//
// V(out) = -10mA * 1k = -10.0 V
// ---------------------------------------------------------------------------
TEST(CCCS, SimpleCurrentMirror) {
    Simulator sim;
    std::string netlist = R"(
Current mirror with gain=2
V1 in 0 DC 5.0
R1 in sense_node 1k
Vsense sense_node 0 DC 0
F1 out 0 Vsense 2.0
R2 out 0 1k
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    EXPECT_NEAR(std::get<DCResult>(result.analysis).voltage("in"),         5.0,   1e-6);
    EXPECT_NEAR(std::get<DCResult>(result.analysis).voltage("sense_node"), 0.0,   1e-6);
    EXPECT_NEAR(std::get<DCResult>(result.analysis).voltage("out"),        -10.0, 1e-6);
}

// ---------------------------------------------------------------------------
// Integration test: negative gain (inverting current mirror)
//
// Same topology but gain = -2
// I_out = -2 * 5mA = -10mA leaving out; current enters out → V(out) = +10.0 V
// ---------------------------------------------------------------------------
TEST(CCCS, NegativeGain) {
    Simulator sim;
    std::string netlist = R"(
Inverting current mirror
V1 in 0 DC 5.0
R1 in sense_node 1k
Vsense sense_node 0 DC 0
F1 out 0 Vsense -2.0
R2 out 0 1k
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    EXPECT_NEAR(std::get<DCResult>(result.analysis).voltage("out"), 10.0, 1e-6);
}

// ---------------------------------------------------------------------------
// Integration test: zero gain (output should be 0 V)
// ---------------------------------------------------------------------------
TEST(CCCS, ZeroGain) {
    Simulator sim;
    std::string netlist = R"(
Zero gain CCCS
V1 in 0 DC 5.0
R1 in sense_node 1k
Vsense sense_node 0 DC 0
F1 out 0 Vsense 0.0
R2 out 0 1k
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    EXPECT_NEAR(std::get<DCResult>(result.analysis).voltage("out"), 0.0, 1e-6);
}

// ---------------------------------------------------------------------------
// Integration test: unity gain (gain=1.0)
//
// I_out = 1.0 * I(Vsense) = 5mA leaves out
// V(out) = -5mA * 1k = -5.0 V
// ---------------------------------------------------------------------------
TEST(CCCS, UnityGain) {
    Simulator sim;
    std::string netlist = R"(
Unity gain CCCS
V1 in 0 DC 5.0
R1 in sense_node 1k
Vsense sense_node 0 DC 0
F1 out 0 Vsense 1.0
R2 out 0 1k
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    EXPECT_NEAR(std::get<DCResult>(result.analysis).voltage("out"), -5.0, 1e-6);
}

// ---------------------------------------------------------------------------
// Integration test: AC analysis — CCCS is linear, response must be flat
//
// V1 in 0 DC 0 AC 1
// R1 in sense_node 1k
// Vsense sense_node 0 DC 0
// F1 out 0 Vsense 2.0
// R2 out 0 1k
// .ac dec 10 1 1Meg
//
// I(Vsense) = V(in) / R1 = 1/1k A (constant across all freq)
// I_out = 2 * I(Vsense) = 2/1k A
// V(out) = I_out * R2 = 2/1k * 1k = 2.0 V at every frequency
// ---------------------------------------------------------------------------
TEST(CCCS, ACResponseFlat) {
    Simulator sim;
    std::string netlist = R"(
* CCCS AC test
V1 in 0 DC 0 AC 1
R1 in sense_node 1k
Vsense sense_node 0 DC 0
F1 out 0 Vsense 2.0
R2 out 0 1k
.ac dec 10 1 1Meg
.end
)";
    auto ckt = sim.parse(netlist);
    auto ac_result = sim.run_ac(ckt, AnalysisCommand::DEC, 10, 1.0, 1e6);

    ASSERT_FALSE(ac_result.frequency.empty());

    auto it = ac_result.voltages.find("v(out)");
    ASSERT_NE(it, ac_result.voltages.end());

    const auto& vout = it->second;
    ASSERT_EQ(vout.size(), ac_result.frequency.size());

    // |V(out)| = gain * R2 / R1 = 2.0 * 1000 / 1000 = 2.0 at every frequency
    for (std::size_t i = 0; i < vout.size(); ++i) {
        EXPECT_NEAR(std::abs(vout[i]), 2.0, 1e-6)
            << "at frequency " << ac_result.frequency[i] << " Hz";
    }

    EXPECT_NEAR(std::abs(vout.front()), 2.0, 1e-6);
    EXPECT_NEAR(std::abs(vout.back()),  2.0, 1e-6);
}

// ---------------------------------------------------------------------------
// Error test: referencing a non-existent voltage source
// ---------------------------------------------------------------------------
TEST(CCCS, ParseErrorUnknownVSourceSkipsWithWarning) {
    Simulator sim;
    std::string netlist = R"(
Bad CCCS reference
V1 in 0 DC 5.0
R1 in sense_node 1k
F1 out 0 Vdoesnotexist 2.0
R2 out 0 1k
.op
.end
)";
    EXPECT_NO_THROW(sim.parse(netlist));
}

// ---------------------------------------------------------------------------
// Error test: too few tokens on F line
// ---------------------------------------------------------------------------
TEST(CCCS, ParseErrorTooFewTokens) {
    Simulator sim;
    std::string netlist = R"(
Too few tokens
V1 in 0 DC 1.0
Vsense mid 0 DC 0
F1 out 0 Vsense
.op
.end
)";
    EXPECT_NO_THROW(sim.parse(netlist));
}
