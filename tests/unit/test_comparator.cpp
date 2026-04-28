#include <gtest/gtest.h>
#include "framework/comparator.hpp"

using namespace neospice;

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

TEST(Comparator, SnapsNearlyIdenticalTimesBeforeInterpolating) {
    TransientResult ref, test;
    ref.time = {0.0, 1.0 + 1e-19};
    ref.voltages["v(in)"] = {0.0, 0.0};
    test.time = {0.0, 1.0, 1.0 + 1e-10};
    test.voltages["v(in)"] = {0.0, 0.0, 5.0};

    auto cmp = compare_transient(ref, test, {1e-12, 1e-12});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}

TEST(Comparator, CompareEdgesChecksFallTime) {
    std::vector<EdgeMetrics> expected = {
        EdgeMetrics{1.0, -2e-9, 0.0, 0.0}
    };
    std::vector<EdgeMetrics> actual = {
        EdgeMetrics{1.0, -3e-9, 0.0, 0.0}
    };

    auto cmp = compare_edges(expected, actual,
        {/*crossing_relative=*/1e-3,
         /*rise_fall_relative=*/1e-2,
         /*settled_absolute=*/1e-3,
         /*overshoot_absolute=*/1e-3});
    EXPECT_FALSE(cmp.passed);
    EXPECT_NE(cmp.detail.find("fall_time"), std::string::npos);
}
