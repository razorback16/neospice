#include <gtest/gtest.h>
#include "framework/comparator.hpp"

using namespace cudaspice;

TEST(Comparator, IdenticalResultsPass) {
    TransientResult a, b;
    a.time = {0.0, 1.0, 2.0};
    a.voltages["v(out)"] = {0.0, 1.0, 2.0};
    b = a;
    auto cmp = compare_transient(a, b);
    EXPECT_TRUE(cmp.passed);
}

TEST(Comparator, DifferentResultsFail) {
    TransientResult a, b;
    a.time = {0.0, 1.0, 2.0};
    a.voltages["v(out)"] = {0.0, 1.0, 2.0};
    b.time = {0.0, 1.0, 2.0};
    b.voltages["v(out)"] = {0.0, 2.0, 4.0};
    Tolerance tol{1e-3, 1e-9};
    auto cmp = compare_transient(a, b, tol);
    EXPECT_FALSE(cmp.passed);
}

TEST(Comparator, InterpolatesTimeGrids) {
    TransientResult ref, test;
    ref.time = {0.0, 0.5, 1.0, 1.5, 2.0};
    ref.voltages["v(out)"] = {0.0, 0.5, 1.0, 1.5, 2.0};
    test.time = {0.0, 1.0, 2.0};
    test.voltages["v(out)"] = {0.0, 1.0, 2.0};
    auto cmp = compare_transient(ref, test);
    EXPECT_TRUE(cmp.passed);
}
