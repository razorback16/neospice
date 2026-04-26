#include <gtest/gtest.h>
#include "devices/ccvs.hpp"
#include "devices/vsource.hpp"
#include "core/linear_solver.hpp"
#include "api/neospice.hpp"
#include <complex>
#include <cmath>
#include <stdexcept>

using namespace neospice;

// ---------------------------------------------------------------------------
// Unit test: stamp_pattern
//
// CCVS with np=0, nn=GROUND, branch=2; sense VSource with np=3 branch=1.
// Active stamp entries:
//   (0,2): KCL np -> branch
//   (2,0): branch eq V(np) coefficient
//   (2,1): branch eq -Rm * I_sense
// nn is ground -> (nn, branch) and (branch, nn) skipped
// Expected nnz = 3
// ---------------------------------------------------------------------------
TEST(CCVS, StampPatternPartialGround) {
    VSource vsense("Vsense", 3, GROUND_INTERNAL, 0.0);
    vsense.set_branch_index(1);

    CCVS h("H1", 0, GROUND_INTERNAL, 500.0, &vsense);
    h.set_branch_index(2);

    SparsityBuilder builder(4);  // vars: 0,1(sense branch),2(H branch),3
    h.stamp_pattern(builder);
    auto pattern = builder.build();
    // (0,2), (2,0), (2,1)  → nnz = 3
    EXPECT_EQ(pattern.nnz(), 3);
}

// ---------------------------------------------------------------------------
// Unit test: stamp_pattern with all 4 terminals active
//
// np=0, nn=1, branch=3; sense VSource branch=2
// Active stamps: (0,3), (1,3), (3,0), (3,1), (3,2) → nnz = 5
// ---------------------------------------------------------------------------
TEST(CCVS, StampPatternAllTerminals) {
    VSource vsense("Vsense", 4, GROUND_INTERNAL, 0.0);
    vsense.set_branch_index(2);

    CCVS h("H1", 0, 1, 100.0, &vsense);
    h.set_branch_index(3);

    SparsityBuilder builder(5);
    h.stamp_pattern(builder);
    auto pattern = builder.build();
    // (0,3), (1,3), (3,0), (3,1), (3,2)
    EXPECT_EQ(pattern.nnz(), 5);
}

// ---------------------------------------------------------------------------
// Unit test: evaluate stamps correct values
//
// np=0, nn=GROUND, Rm=500.0, branch=2; sense branch=1
// Matrix size 3: indices {0, 1(sense), 2(H branch)}
//
// Expected after evaluate:
//   mat[0,2]  = +1      (KCL np)
//   mat[2,0]  = +1      (branch eq V(np) coeff)
//   mat[2,1]  = -500.0  (branch eq -Rm * I_sense)
// ---------------------------------------------------------------------------
TEST(CCVS, EvaluateValues) {
    VSource vsense("Vsense", 3, GROUND_INTERNAL, 0.0);
    vsense.set_branch_index(1);

    CCVS h("H1", 0, GROUND_INTERNAL, 500.0, &vsense);
    h.set_branch_index(2);

    SparsityBuilder builder(3);
    h.stamp_pattern(builder);
    // Add diagonals so matrix is buildable
    builder.add(0, 0);
    builder.add(1, 1);
    builder.add(2, 2);
    auto pattern = builder.build();

    NumericMatrix mat(pattern);
    h.assign_offsets(pattern);

    std::vector<double> voltages(3, 0.0);
    std::vector<double> rhs(3, 0.0);
    h.evaluate(voltages, mat, rhs);

    EXPECT_DOUBLE_EQ(mat.value(pattern.offset(0, 2)),  1.0);     // KCL np
    EXPECT_DOUBLE_EQ(mat.value(pattern.offset(2, 0)),  1.0);     // branch eq V(np)
    EXPECT_DOUBLE_EQ(mat.value(pattern.offset(2, 1)), -500.0);   // branch eq -Rm * I_sense
    // RHS must stay zero
    for (double v : rhs) EXPECT_DOUBLE_EQ(v, 0.0);
}

// ---------------------------------------------------------------------------
// Unit test: extra_vars and output_currents
// ---------------------------------------------------------------------------
TEST(CCVS, ExtraVarsAndOutputCurrents) {
    VSource vsense("Vsense", 1, GROUND_INTERNAL, 0.0);
    vsense.set_branch_index(2);

    CCVS h("H1", 0, GROUND_INTERNAL, 100.0, &vsense);
    EXPECT_EQ(h.extra_vars(), 1);

    auto oc = h.output_currents();
    ASSERT_EQ(oc.size(), 1u);
    EXPECT_EQ(oc[0], "I(H1)");
}

// ---------------------------------------------------------------------------
// Unit test: stamp_pattern throws before set_branch_index
// ---------------------------------------------------------------------------
TEST(CCVS, StampPatternThrowsWithoutBranchIndex) {
    VSource vsense("Vsense", 1, GROUND_INTERNAL, 0.0);
    vsense.set_branch_index(2);

    CCVS h("H1", 0, GROUND_INTERNAL, 100.0, &vsense);
    // branch_idx_ is still -1 (not yet set)
    SparsityBuilder builder(3);
    EXPECT_THROW(h.stamp_pattern(builder), std::logic_error);
}

// ---------------------------------------------------------------------------
// Unit test: constructor throws on null VSource pointer
// ---------------------------------------------------------------------------
TEST(CCVS, ConstructorThrowsOnNullVSource) {
    EXPECT_THROW(
        CCVS h("H1", 0, GROUND_INTERNAL, 100.0, nullptr),
        std::invalid_argument
    );
}

// ---------------------------------------------------------------------------
// Integration test: simple transimpedance
//
// V1 in 0 DC 1.0
// R1 in out1 1k
// Vsense out1 0 DC 0    ← 0V source as ammeter
// H1 out 0 Vsense 500
// R2 out 0 1k
// .op
//
// Analysis:
//   I(Vsense) = V1 / R1 = 1.0 / 1000 = 1 mA
//   V(out) = Rm * I(Vsense) = 500 * 0.001 = 0.5 V
// ---------------------------------------------------------------------------
TEST(CCVS, SimpleTransimpedance) {
    Simulator sim;
    std::string netlist = R"(
Simple transimpedance
V1 in 0 DC 1.0
R1 in out1 1k
Vsense out1 0 DC 0
H1 out 0 Vsense 500
R2 out 0 1k
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    EXPECT_NEAR(std::get<DCResult>(result.analysis).node_voltages["v(in)"],   1.0, 1e-9);
    EXPECT_NEAR(std::get<DCResult>(result.analysis).node_voltages["v(out1)"], 0.0, 1e-9);
    EXPECT_NEAR(std::get<DCResult>(result.analysis).node_voltages["v(out)"],  0.5, 1e-9);
}

// ---------------------------------------------------------------------------
// Integration test: negative transresistance (inverting)
//
// Same topology but Rm = -500
// V(out) = -500 * 0.001 = -0.5 V
// ---------------------------------------------------------------------------
TEST(CCVS, NegativeTransresistance) {
    Simulator sim;
    std::string netlist = R"(
Negative transresistance
V1 in 0 DC 1.0
R1 in out1 1k
Vsense out1 0 DC 0
H1 out 0 Vsense -500
R2 out 0 1k
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    EXPECT_NEAR(std::get<DCResult>(result.analysis).node_voltages["v(out)"], -0.5, 1e-9);
}

// ---------------------------------------------------------------------------
// Integration test: zero transresistance
//
// V(out) = 0 * I_sense = 0 V regardless of input
// ---------------------------------------------------------------------------
TEST(CCVS, ZeroTransresistance) {
    Simulator sim;
    std::string netlist = R"(
Zero transresistance
V1 in 0 DC 5.0
R1 in mid 1k
Vsense mid 0 DC 0
H1 out 0 Vsense 0
R2 out 0 1k
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    EXPECT_NEAR(std::get<DCResult>(result.analysis).node_voltages["v(out)"], 0.0, 1e-9);
}

// ---------------------------------------------------------------------------
// Integration test: CCVS with differential output nodes
//
// V1 in 0 DC 2.0
// R1 in mid 1k
// Vsense mid 0 DC 0       I(Vsense) = 2.0/1000 = 2 mA
// H1 outp outn Vsense 250  V(outp) - V(outn) = 250 * 0.002 = 0.5 V
// R2 outp 0 1k
// R3 outn 0 1k             V(outp) = V(outn) + 0.5; symmetric → V(outp) = 0.25, V(outn) = -0.25
// .op
// ---------------------------------------------------------------------------
TEST(CCVS, DifferentialOutputNodes) {
    Simulator sim;
    std::string netlist = R"(
CCVS differential output
V1 in 0 DC 2.0
R1 in mid 1k
Vsense mid 0 DC 0
H1 outp outn Vsense 250
R2 outp 0 1k
R3 outn 0 1k
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    double vp = std::get<DCResult>(result.analysis).node_voltages["v(outp)"];
    double vn = std::get<DCResult>(result.analysis).node_voltages["v(outn)"];
    EXPECT_NEAR(vp - vn, 0.5, 1e-9);
}

// ---------------------------------------------------------------------------
// Error test: referencing a non-existent voltage source
// ---------------------------------------------------------------------------
TEST(CCVS, ParseErrorUnknownVSource) {
    Simulator sim;
    std::string netlist = R"(
Bad CCVS reference
V1 in 0 DC 1.0
H1 out 0 Vdoesnotexist 100
R1 out 0 1k
.op
.end
)";
    EXPECT_THROW(sim.parse(netlist), ParseError);
}

// ---------------------------------------------------------------------------
// Error test: too few tokens on H line
// ---------------------------------------------------------------------------
TEST(CCVS, ParseErrorTooFewTokens) {
    Simulator sim;
    std::string netlist = R"(
Too few tokens
V1 in 0 DC 1.0
H1 out 0 Vsense
.op
.end
)";
    EXPECT_THROW(sim.parse(netlist), ParseError);
}

// ---------------------------------------------------------------------------
// Integration test: AC analysis — CCVS is linear, response must be flat
//
// V1 in 0 DC 0 AC 1
// R1 in mid 1k
// Vsense mid 0 DC 0
// H1 out 0 Vsense 500
// R2 out 0 1k
// .ac dec 10 1 1Meg
//
// I(Vsense) = V(in) / R1 = 1 / 1000 A (constant across all freq)
// V(out) = Rm * I_sense = 500/1000 = 0.5 at every frequency
// ---------------------------------------------------------------------------
TEST(CCVS, ACResponseFlat) {
    Simulator sim;
    std::string netlist = R"(
* CCVS AC test
V1 in 0 DC 0 AC 1
R1 in mid 1k
Vsense mid 0 DC 0
H1 out 0 Vsense 500
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

    // |V(out)| = Rm / R1 = 500 / 1000 = 0.5 at every frequency
    for (std::size_t i = 0; i < vout.size(); ++i) {
        EXPECT_NEAR(std::abs(vout[i]), 0.5, 1e-6)
            << "at frequency " << ac_result.frequency[i] << " Hz";
    }

    EXPECT_NEAR(std::abs(vout.front()), 0.5, 1e-6);
    EXPECT_NEAR(std::abs(vout.back()),  0.5, 1e-6);
}
