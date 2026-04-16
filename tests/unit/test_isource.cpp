#include <gtest/gtest.h>
#include "devices/isource.hpp"
#include "core/matrix.hpp"

using namespace neospice;

TEST(ISource, NoMatrixEntries) {
    ISource is("I1", 0, GROUND_INTERNAL, 0.001);
    SparsityBuilder builder(1);
    is.stamp_pattern(builder);
    auto pattern = builder.build();
    EXPECT_EQ(pattern.nnz(), 0);
}

TEST(ISource, EvaluateRHS) {
    // 1mA from ground to node 0 (np=0, nn=ground)
    // Convention: current flows from np to nn through source
    // So current exits np: RHS[np] += -I
    ISource is("I1", 0, GROUND_INTERNAL, 0.001);
    SparsityBuilder builder(1);
    is.stamp_pattern(builder);
    auto pattern = builder.build();
    NumericMatrix mat(pattern);
    is.assign_offsets(pattern);

    std::vector<double> voltages = {0.0};
    std::vector<double> rhs(1, 0.0);
    is.evaluate(voltages, mat, rhs);

    EXPECT_DOUBLE_EQ(rhs[0], -0.001);
}

TEST(ISource, BetweenTwoNodes) {
    // 2mA flowing from node 0 to node 1
    ISource is("I1", 0, 1, 0.002);
    SparsityBuilder builder(2);
    is.stamp_pattern(builder);
    auto pattern = builder.build();
    NumericMatrix mat(pattern);
    is.assign_offsets(pattern);

    std::vector<double> voltages = {0.0, 0.0};
    std::vector<double> rhs(2, 0.0);
    is.evaluate(voltages, mat, rhs);

    EXPECT_DOUBLE_EQ(rhs[0], -0.002);  // current exits node 0
    EXPECT_DOUBLE_EQ(rhs[1],  0.002);  // current enters node 1
}

TEST(ISource, ValueAtDC) {
    ISource is("I1", 0, GROUND_INTERNAL, 0.005);
    EXPECT_DOUBLE_EQ(is.value_at(0.0), 0.005);
    EXPECT_DOUBLE_EQ(is.value_at(1.0), 0.005);
}
