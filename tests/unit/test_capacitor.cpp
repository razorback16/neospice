#include <gtest/gtest.h>
#include "devices/capacitor.hpp"
#include "core/matrix.hpp"

using namespace cudaspice;

TEST(Capacitor, StampPattern) {
    // Two non-ground nodes: expect 4 non-zeros
    Capacitor cap("C1", 0, 1, 1e-9);
    SparsityBuilder builder(2);
    cap.stamp_pattern(builder);
    auto pattern = builder.build();
    EXPECT_EQ(pattern.nnz(), 4);
}

TEST(Capacitor, DCIsOpenCircuit) {
    // Without transient mode, evaluate should be a no-op
    Capacitor cap("C1", 0, 1, 1e-9);
    SparsityBuilder builder(2);
    cap.stamp_pattern(builder);
    auto pattern = builder.build();
    NumericMatrix mat(pattern);
    cap.assign_offsets(pattern);

    std::vector<double> voltages = {1.0, 0.0};
    std::vector<double> rhs(2, 0.0);
    cap.evaluate(voltages, mat, rhs);

    // Matrix and RHS should remain zero
    for (int i = 0; i < pattern.nnz(); ++i) {
        EXPECT_DOUBLE_EQ(mat.value(i), 0.0) << "Matrix entry " << i << " should be zero";
    }
    EXPECT_DOUBLE_EQ(rhs[0], 0.0);
    EXPECT_DOUBLE_EQ(rhs[1], 0.0);
}

TEST(Capacitor, TransientCompanionModel) {
    // Trapezoidal: G_eq = 2C/dt
    // After accept_step(0.0) with v_prev=0, i_prev=0 => i_eq = 0
    // Stamps G_eq into matrix, stamps -i_eq=0 into RHS
    const double C = 1e-6;
    const double dt = 1e-9;
    const double g_eq = 2.0 * C / dt;

    Capacitor cap("C1", 0, 1, C);
    SparsityBuilder builder(2);
    cap.stamp_pattern(builder);
    auto pattern = builder.build();
    NumericMatrix mat(pattern);
    cap.assign_offsets(pattern);

    cap.set_transient(dt);
    cap.accept_step(0.0);  // initialize state: v_prev=0 after this call

    std::vector<double> voltages = {1.0, 0.0};
    std::vector<double> rhs(2, 0.0);
    cap.evaluate(voltages, mat, rhs);

    // Verify matrix entries via pattern offsets
    EXPECT_DOUBLE_EQ(mat.value(pattern.offset(0, 0)),  g_eq);
    EXPECT_DOUBLE_EQ(mat.value(pattern.offset(0, 1)), -g_eq);
    EXPECT_DOUBLE_EQ(mat.value(pattern.offset(1, 0)), -g_eq);
    EXPECT_DOUBLE_EQ(mat.value(pattern.offset(1, 1)),  g_eq);

    // After accept_step(0.0): v_prev_=0, i_prev_=0 => i_eq = 0
    EXPECT_DOUBLE_EQ(rhs[0], 0.0);
    EXPECT_DOUBLE_EQ(rhs[1], 0.0);
}

TEST(Capacitor, ACStamp) {
    // AC: C matrix gets capacitance stamps, G matrix stays zero
    const double C = 2.2e-12;

    Capacitor cap("C1", 0, 1, C);
    SparsityBuilder builder(2);
    cap.stamp_pattern(builder);
    auto pattern = builder.build();
    cap.assign_offsets(pattern);

    NumericMatrix G(pattern);
    NumericMatrix Cmat(pattern);

    std::vector<double> voltages = {1.0, 0.0};
    cap.ac_stamp(voltages, G, Cmat);

    // G matrix must be all zeros
    for (int i = 0; i < pattern.nnz(); ++i) {
        EXPECT_DOUBLE_EQ(G.value(i), 0.0) << "G matrix entry " << i << " should be zero";
    }

    // C matrix: +C at (0,0) and (1,1), -C at (0,1) and (1,0)
    EXPECT_DOUBLE_EQ(Cmat.value(pattern.offset(0, 0)),  C);
    EXPECT_DOUBLE_EQ(Cmat.value(pattern.offset(0, 1)), -C);
    EXPECT_DOUBLE_EQ(Cmat.value(pattern.offset(1, 0)), -C);
    EXPECT_DOUBLE_EQ(Cmat.value(pattern.offset(1, 1)),  C);
}
