#include <gtest/gtest.h>
#include "devices/resistor.hpp"

using namespace neospice;

TEST(Resistor, StampPattern) {
    Resistor r("R1", 0, 1, 1000.0);
    SparsityBuilder builder(2);
    r.stamp_pattern(builder);
    auto pattern = builder.build();
    EXPECT_EQ(pattern.nnz(), 4);
}

TEST(Resistor, Evaluate) {
    Resistor r("R1", 0, 1, 1000.0);
    SparsityBuilder builder(2);
    r.stamp_pattern(builder);
    auto pattern = builder.build();
    NumericMatrix mat(pattern);
    r.assign_offsets(pattern);
    std::vector<double> voltages = {5.0, 3.0};
    std::vector<double> rhs(2, 0.0);
    r.evaluate(voltages, mat, rhs);
    double g = 1.0 / 1000.0;
    EXPECT_DOUBLE_EQ(mat.value(pattern.offset(0, 0)),  g);
    EXPECT_DOUBLE_EQ(mat.value(pattern.offset(0, 1)), -g);
    EXPECT_DOUBLE_EQ(mat.value(pattern.offset(1, 0)), -g);
    EXPECT_DOUBLE_EQ(mat.value(pattern.offset(1, 1)),  g);
}

TEST(Resistor, GroundNode) {
    Resistor r("R1", 0, GROUND_INTERNAL, 500.0);
    SparsityBuilder builder(1);
    r.stamp_pattern(builder);
    auto pattern = builder.build();
    EXPECT_EQ(pattern.nnz(), 1);
    NumericMatrix mat(pattern);
    r.assign_offsets(pattern);
    std::vector<double> voltages = {3.0};
    std::vector<double> rhs(1, 0.0);
    r.evaluate(voltages, mat, rhs);
    EXPECT_DOUBLE_EQ(mat.value(pattern.offset(0, 0)), 1.0 / 500.0);
}
