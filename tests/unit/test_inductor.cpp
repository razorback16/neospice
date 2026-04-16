#include <gtest/gtest.h>
#include "devices/inductor.hpp"
#include "core/matrix.hpp"

using namespace neospice;

TEST(Inductor, StampPattern) {
    // Between nodes 0 and 1, branch index 2 → 5 entries
    Inductor ind("L1", 0, 1, 1e-3);
    ind.set_branch_index(2);
    SparsityBuilder builder(3);
    ind.stamp_pattern(builder);
    auto pattern = builder.build();
    EXPECT_EQ(pattern.nnz(), 5);
}

TEST(Inductor, DCIsShortCircuit) {
    // Branch equation: V(np) - V(nn) = 0
    Inductor ind("L1", 0, GROUND_INTERNAL, 1e-3);
    ind.set_branch_index(1);
    SparsityBuilder builder(2);
    ind.stamp_pattern(builder);
    auto pattern = builder.build();
    NumericMatrix mat(pattern);
    ind.assign_offsets(pattern);
    std::vector<double> voltages = {0.0, 0.0};
    std::vector<double> rhs(2, 0.0);
    ind.evaluate(voltages, mat, rhs);
    EXPECT_DOUBLE_EQ(mat.value(pattern.offset(0, 1)), 1.0);   // (np, br)
    EXPECT_DOUBLE_EQ(mat.value(pattern.offset(1, 0)), 1.0);   // (br, np)
    EXPECT_DOUBLE_EQ(rhs[1], 0.0);  // V_eq = 0 in DC
}

TEST(Inductor, ExtraVars) {
    Inductor ind("L1", 0, 1, 1e-3);
    EXPECT_EQ(ind.extra_vars(), 1);
}

TEST(Inductor, OutputCurrents) {
    Inductor ind("L1", 0, 1, 1e-3);
    auto names = ind.output_currents();
    EXPECT_EQ(names.size(), 1u);
    EXPECT_EQ(names[0], "i(L1)");
}

TEST(Inductor, Inductance) {
    Inductor ind("L1", 0, 1, 2.5e-6);
    EXPECT_DOUBLE_EQ(ind.inductance(), 2.5e-6);
}

TEST(Inductor, TransientStamp) {
    // nodes 0,1; branch 2 — full 3-node system
    Inductor ind("L1", 0, 1, 1e-3);
    ind.set_branch_index(2);
    SparsityBuilder builder(3);
    ind.stamp_pattern(builder);
    auto pattern = builder.build();
    NumericMatrix mat(pattern);
    ind.assign_offsets(pattern);

    // Enable transient with dt=1e-3 s, i_prev=0, v_prev=0
    ind.set_transient(1e-3);

    std::vector<double> voltages(3, 0.0);
    std::vector<double> rhs(3, 0.0);
    ind.evaluate(voltages, mat, rhs);

    // R_eq = 2L/dt = 2*1e-3/1e-3 = 2.0
    const double r_eq = 2.0;

    // KCL coupling
    EXPECT_DOUBLE_EQ(mat.value(pattern.offset(0, 2)),  1.0);  // (np, br)
    EXPECT_DOUBLE_EQ(mat.value(pattern.offset(1, 2)), -1.0);  // (nn, br)
    // Branch equation
    EXPECT_DOUBLE_EQ(mat.value(pattern.offset(2, 0)),  1.0);  // (br, np)
    EXPECT_DOUBLE_EQ(mat.value(pattern.offset(2, 1)), -1.0);  // (br, nn)
    // -R_eq at (br, br)
    EXPECT_DOUBLE_EQ(mat.value(pattern.offset(2, 2)), -r_eq);
    // V_eq = r_eq * i_prev + v_prev = 0
    EXPECT_DOUBLE_EQ(rhs[2], 0.0);
}

TEST(Inductor, TransientRHSWithState) {
    // Verify V_eq = R_eq * I_prev + V_prev is correctly placed in RHS
    Inductor ind("L1", 0, GROUND_INTERNAL, 1e-3);
    ind.set_branch_index(1);
    SparsityBuilder builder(2);
    ind.stamp_pattern(builder);
    auto pattern = builder.build();
    NumericMatrix mat(pattern);
    ind.assign_offsets(pattern);

    double dt = 1e-3;
    ind.set_transient(dt);

    // Set previous state: i_prev=0.5 A, v_prev=1.0 V
    ind.accept_step(0.5, 1.0);

    std::vector<double> voltages(2, 0.0);
    std::vector<double> rhs(2, 0.0);
    ind.evaluate(voltages, mat, rhs);

    // R_eq = 2L/dt = 2.0
    const double r_eq = 2.0 * 1e-3 / dt;
    const double v_eq = r_eq * 0.5 + 1.0;
    EXPECT_DOUBLE_EQ(rhs[1], v_eq);
}

TEST(Inductor, BranchIndex) {
    Inductor ind("L1", 0, 1, 1e-3);
    ind.set_branch_index(5);
    EXPECT_EQ(ind.branch_index(), 5);
}
