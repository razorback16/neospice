#include <gtest/gtest.h>
#include "devices/vcvs.hpp"
#include "core/neo_solver.hpp"
#include "api/neospice.hpp"
#include <complex>
#include <cmath>

using namespace neospice;

// ---------------------------------------------------------------------------
// Unit test: stamp_pattern
//
// VCVS with np=0, nn=GROUND, ncp=1, ncn=GROUND, branch=2.
// Expected non-zero entries: (0,2),(GROUND,2)skip,(2,0),(2,GROUND)skip,(2,1),(2,GROUND)skip
// Active entries: (0,2), (2,0), (2,1) → nnz = 3
// ---------------------------------------------------------------------------
TEST(VCVS, StampPattern) {
    VCVS e("E1", 0, GROUND_INTERNAL, 1, GROUND_INTERNAL, 1.0);
    e.set_branch_index(2);
    SparsityBuilder builder(3);
    e.stamp_pattern(builder);
    auto pattern = builder.build();
    // (0,2): KCL np->branch
    // (2,0): branch eq np
    // (2,1): branch eq ncp
    // nn and ncn are ground → skipped
    EXPECT_EQ(pattern.nnz(), 3);
}

// ---------------------------------------------------------------------------
// Unit test: stamp_pattern with all 4 terminals active
//
// np=0, nn=1, ncp=2, ncn=3, branch=4  → 6 non-zero entries
// ---------------------------------------------------------------------------
TEST(VCVS, StampPatternAllTerminals) {
    VCVS e("E1", 0, 1, 2, 3, 2.0);
    e.set_branch_index(4);
    SparsityBuilder builder(5);
    e.stamp_pattern(builder);
    auto pattern = builder.build();
    // (0,4), (1,4), (4,0), (4,1), (4,2), (4,3)
    EXPECT_EQ(pattern.nnz(), 6);
}

// ---------------------------------------------------------------------------
// Unit test: evaluate stamps correct values
//
// np=0, nn=GROUND, ncp=1, ncn=GROUND, gain=3.0, branch=2
// Matrix size 3: nodes {0, 1} + branch {2}
// Expected after evaluate:
//   mat[0,2] = +1       (KCL np)
//   mat[2,0] = +1       (branch eq: V(np) coeff)
//   mat[2,1] = -3.0     (branch eq: -gain * V(ncp))
//   rhs unchanged (all 0)
// ---------------------------------------------------------------------------
TEST(VCVS, EvaluateValues) {
    VCVS e("E1", 0, GROUND_INTERNAL, 1, GROUND_INTERNAL, 3.0);
    e.set_branch_index(2);

    SparsityBuilder builder(3);
    e.stamp_pattern(builder);
    // Add diagonal entries to make matrix non-singular for inspection
    builder.add(0, 0);
    builder.add(1, 1);
    auto pattern = builder.build();

    NumericMatrix mat(pattern);
    e.assign_offsets(pattern);

    std::vector<double> voltages(3, 0.0);
    std::vector<double> rhs(3, 0.0);
    e.evaluate(voltages, mat, rhs);

    EXPECT_DOUBLE_EQ(mat.value(pattern.offset(0, 2)),  1.0);   // KCL np
    EXPECT_DOUBLE_EQ(mat.value(pattern.offset(2, 0)),  1.0);   // branch eq V(np)
    EXPECT_DOUBLE_EQ(mat.value(pattern.offset(2, 1)), -3.0);   // branch eq -gain*V(ncp)
    // RHS should remain 0
    EXPECT_DOUBLE_EQ(rhs[2], 0.0);
}

// ---------------------------------------------------------------------------
// Integration test: unity-gain buffer
//
// V1 in 0 DC 2.5
// E1 out 0 in 0 1.0
// R1 out 0 1k
// .op
//
// Expected: V(out) = 1.0 * 2.5 = 2.5 V
//
// NOTE: Results verified manually against ngspice 40 (.op analysis).
//       ngspice reports v(in) = 2.5 V, v(out) = 2.5 V.
// ---------------------------------------------------------------------------
TEST(VCVS, UnityGainBuffer) {
    Simulator sim;
    std::string netlist = R"(
Unity-gain buffer
V1 in 0 DC 2.5
E1 out 0 in 0 1.0
R1 out 0 1k
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    EXPECT_NEAR(std::get<DCResult>(result.analysis).voltage("in"),  2.5, 1e-9);
    EXPECT_NEAR(std::get<DCResult>(result.analysis).voltage("out"), 2.5, 1e-9);
}

// ---------------------------------------------------------------------------
// Integration test: inverting amplifier with gain = -2
//
// V1 in 0 DC 1.0
// E1 out 0 in 0 -2.0
// R1 out 0 1k
// .op
//
// Expected: V(out) = -2.0 * 1.0 = -2.0 V
// ---------------------------------------------------------------------------
TEST(VCVS, InvertingAmplifier) {
    Simulator sim;
    std::string netlist = R"(
Inverting amplifier
V1 in 0 DC 1.0
E1 out 0 in 0 -2.0
R1 out 0 1k
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    EXPECT_NEAR(std::get<DCResult>(result.analysis).voltage("out"), -2.0, 1e-9);
}

// ---------------------------------------------------------------------------
// Integration test: differential input
//
// V1 inp 0 DC 3.0
// V2 inn 0 DC 1.0
// E1 out 0 inp inn 5.0
// R1 out 0 1k
// .op
//
// Expected: V(out) = 5.0 * (3.0 - 1.0) = 10.0 V
// ---------------------------------------------------------------------------
TEST(VCVS, DifferentialInput) {
    Simulator sim;
    std::string netlist = R"(
Differential input VCVS
V1 inp 0 DC 3.0
V2 inn 0 DC 1.0
E1 out 0 inp inn 5.0
R1 out 0 1k
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    EXPECT_NEAR(std::get<DCResult>(result.analysis).voltage("inp"),  3.0,  1e-9);
    EXPECT_NEAR(std::get<DCResult>(result.analysis).voltage("inn"),  1.0,  1e-9);
    EXPECT_NEAR(std::get<DCResult>(result.analysis).voltage("out"), 10.0,  1e-9);
}

// ---------------------------------------------------------------------------
// Integration test: gain = 0 (output is always 0 regardless of input)
// ---------------------------------------------------------------------------
TEST(VCVS, ZeroGain) {
    Simulator sim;
    std::string netlist = R"(
Zero gain VCVS
V1 in 0 DC 5.0
E1 out 0 in 0 0.0
R1 out 0 1k
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    EXPECT_NEAR(std::get<DCResult>(result.analysis).voltage("out"), 0.0, 1e-9);
}

// ---------------------------------------------------------------------------
// Integration test: cascaded VCVS (gain chain)
//
// V1 in 0 DC 1.0
// E1 mid 0 in 0 3.0   → V(mid) = 3.0 V
// E2 out 0 mid 0 2.0  → V(out) = 6.0 V
// R1 out 0 1k
// ---------------------------------------------------------------------------
TEST(VCVS, CascadedGain) {
    Simulator sim;
    std::string netlist = R"(
Cascaded VCVS
V1 in 0 DC 1.0
E1 mid 0 in 0 3.0
E2 out 0 mid 0 2.0
R1 out 0 1k
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    EXPECT_NEAR(std::get<DCResult>(result.analysis).voltage("mid"),  3.0, 1e-9);
    EXPECT_NEAR(std::get<DCResult>(result.analysis).voltage("out"),  6.0, 1e-9);
}

// ---------------------------------------------------------------------------
// Unit test: extra_vars and output_currents
// ---------------------------------------------------------------------------
TEST(VCVS, ExtraVarsAndOutputCurrents) {
    VCVS e("E1", 0, GROUND_INTERNAL, 1, GROUND_INTERNAL, 1.0);
    e.set_branch_index(2);
    EXPECT_EQ(e.extra_vars(), 1);
    auto oc = e.output_currents();
    ASSERT_EQ(oc.size(), 1u);
    EXPECT_EQ(oc[0], "I(E1)");
}

// ---------------------------------------------------------------------------
// Unit test: stamp_pattern throws before set_branch_index
// ---------------------------------------------------------------------------
TEST(VCVS, StampPatternThrowsWithoutBranchIndex) {
    VCVS e("E1", 0, GROUND_INTERNAL, 1, GROUND_INTERNAL, 1.0);
    SparsityBuilder builder(3);
    EXPECT_THROW(e.stamp_pattern(builder), std::logic_error);
}

// ---------------------------------------------------------------------------
// Integration test: AC analysis — gain should be flat across all frequencies
//
// The VCVS is a linear, frequency-independent element. With a gain of 5.0
// and a unit AC stimulus on the input, |V(out)| must equal 5.0 at every
// frequency point across the swept range (1 Hz to 1 MHz, dec 10).
//
// * VCVS AC test
// V1 in 0 DC 0 AC 1
// E1 out 0 in 0 5.0
// R1 out 0 1k
// .ac dec 10 1 1Meg
// .end
// ---------------------------------------------------------------------------
TEST(VCVS, ACGainFlat) {
    Simulator sim;
    std::string netlist = R"(
* VCVS AC test
V1 in 0 DC 0 AC 1
E1 out 0 in 0 5.0
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

    // |V(out)| = gain * |V(in)| = 5.0 * 1.0 = 5.0 at every frequency
    for (std::size_t i = 0; i < vout.size(); ++i) {
        EXPECT_NEAR(std::abs(vout[i]), 5.0, 1e-6)
            << "at frequency " << ac_result.frequency[i] << " Hz";
    }

    // Explicitly check first and last points
    EXPECT_NEAR(std::abs(vout.front()), 5.0, 1e-6);
    EXPECT_NEAR(std::abs(vout.back()),  5.0, 1e-6);
}
