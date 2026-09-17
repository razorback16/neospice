#include <gtest/gtest.h>
#include "framework/comparator.hpp"
#include <limits>

using namespace neospice;

TEST(Comparator, DCRecordsPassingAndFailingVoltageAndCurrentEvidence) {
    DCResult ref, actual;
    ref.node_voltages["v(out)"] = 10;
    ref.branch_currents["i(v1)"] = -2;
    actual = ref;
    actual.branch_currents["i(v1)"] = -3;
    const auto result = compare_dc(ref, actual, {0.1, 1e-9});
    EXPECT_FALSE(result.passed);
    EXPECT_EQ(result.num_points_compared, 2);
    ASSERT_EQ(result.signals.size(), 2u);
    EXPECT_EQ(result.signals[0].name, "v(out)");
    EXPECT_EQ(result.signals[0].max_absolute_error, 0);
    const auto& current = result.signals[1];
    EXPECT_EQ(current.name, "i(v1)");
    EXPECT_EQ(current.points, 1);
    EXPECT_EQ(current.coordinate_unit, "OP");
    EXPECT_EQ(current.max_absolute_error, 1);
    EXPECT_EQ(current.max_normalized_error, 0.5);
    EXPECT_EQ(current.reference_at_worst, -2);
    EXPECT_EQ(current.actual_at_worst, -3);
}

TEST(Comparator, DCSweepRecordsRelativeWorstCoordinateSeparatelyFromAbsoluteMaximum) {
    DCSweepResult ref;
    ref.sweep_var = "i1";
    ref.sweep_values = {-1, 0, 1};
    ref.voltages["v(out)"] = {100, 1, 10};
    ref.currents["i(v1)"] = {0, 0, 0};
    auto actual = ref;
    actual.voltages["v(out)"] = {101, 1.5, 10};
    const auto result = compare_dc_sweep(ref, actual, {0.1, 1e-9});
    EXPECT_FALSE(result.passed);
    EXPECT_EQ(result.num_points_compared, 6);
    ASSERT_EQ(result.signals.size(), 2u);
    const auto& voltage = result.signals[0];
    EXPECT_EQ(voltage.points, 3);
    EXPECT_EQ(voltage.max_absolute_error, 1);
    EXPECT_EQ(voltage.max_normalized_error, 0.5);
    EXPECT_EQ(voltage.worst_coordinate, 0);
    EXPECT_EQ(voltage.coordinate_unit, "A");
    EXPECT_EQ(voltage.reference_at_worst, 1);
    EXPECT_EQ(voltage.actual_at_worst, 1.5);
    EXPECT_EQ(result.signals[1].points, 3);
    EXPECT_EQ(result.signals[1].max_normalized_error, 0);
    EXPECT_TRUE(compare_dc_sweep(ref, ref).passed);
    actual.sweep_values[0] = -2;
    EXPECT_FALSE(compare_dc_sweep(ref, actual).passed);
    actual = ref;
    actual.currents.clear();
    EXPECT_FALSE(compare_dc_sweep(ref, actual).passed);
    EXPECT_FALSE(compare_dc_sweep({}, {}).passed);
}

TEST(Comparator, RejectsEmptyAndUnconvergedDC) {
    EXPECT_FALSE(compare_dc({}, {}).passed);
    DCResult ref, actual;
    ref.node_voltages["v(out)"] = 1.0;
    actual = ref;
    EXPECT_TRUE(compare_dc(ref, actual).passed);
    actual.status.converged = false;
    EXPECT_FALSE(compare_dc(ref, actual).passed);
    EXPECT_FALSE(compare_dc(actual, ref).passed);
}

TEST(Comparator, RejectsNonfiniteDCOnEitherSide) {
    for (double value : {std::numeric_limits<double>::quiet_NaN(),
                         std::numeric_limits<double>::infinity(),
                         -std::numeric_limits<double>::infinity()}) {
        DCResult ref, actual;
        ref.node_voltages["v(out)"] = 1.0;
        actual.node_voltages["v(out)"] = value;
        EXPECT_FALSE(compare_dc(ref, actual).passed);
        EXPECT_FALSE(compare_dc(actual, ref).passed);
        EXPECT_FALSE(compare_dc(actual, actual).passed);
    }
}

TEST(Comparator, DCSweepDataRejectsInvalidAndIncompleteResults) {
    DCSweepResult ref;
    ref.sweep_var = "v1";
    ref.sweep_values = {-1, 0, 1};
    ref.voltages["v(out)"] = {-1, 0, 1};
    ref.currents["i(v1)"] = {1, 0, -1};
    EXPECT_TRUE(validate_dc_sweep_data(ref, ref).empty());
    EXPECT_FALSE(validate_dc_sweep_data({}, {}).empty());
    auto actual = ref;
    actual.status.converged = false;
    EXPECT_FALSE(validate_dc_sweep_data(ref, actual).empty());
    EXPECT_FALSE(validate_dc_sweep_data(actual, ref).empty());
    actual = ref;
    actual.currents.clear();
    EXPECT_FALSE(validate_dc_sweep_data(ref, actual).empty());
    actual = ref;
    actual.voltages["v(out)"].pop_back();
    EXPECT_FALSE(validate_dc_sweep_data(ref, actual).empty());
    actual = ref;
    actual.sweep_values[1] = 1e-6;
    EXPECT_FALSE(validate_dc_sweep_data(ref, actual).empty());
    actual = ref;
    actual.sweep_values[1] = 1e-16;
    EXPECT_TRUE(validate_dc_sweep_data(ref, actual).empty()); // accumulation near zero
    for (double bad : {std::numeric_limits<double>::quiet_NaN(),
                       std::numeric_limits<double>::infinity()}) {
        actual = ref;
        actual.currents["i(v1)"][1] = bad;
        EXPECT_FALSE(validate_dc_sweep_data(ref, actual).empty());
        EXPECT_FALSE(validate_dc_sweep_data(actual, ref).empty());
        actual = ref;
        actual.sweep_values[1] = bad;
        EXPECT_FALSE(validate_dc_sweep_data(ref, actual).empty());
    }
    // Data integrity is separate from the test's numerical acceptance rule.
    actual = ref;
    actual.voltages["v(out)"][1] = 100;
    EXPECT_TRUE(validate_dc_sweep_data(ref, actual).empty());
}

TEST(Comparator, DCSweepDataAllowsDescendingAndNestedAxes) {
    for (const auto& axis : std::vector<std::vector<double>>{{1, 0, -1}, {0, 1, 0, 1}}) {
        DCSweepResult ref;
        ref.sweep_var = "v1";
        ref.sweep_values = axis;
        ref.voltages["v(out)"] = axis;
        EXPECT_TRUE(validate_dc_sweep_data(ref, ref).empty());
        auto truncated = ref;
        truncated.sweep_values.pop_back();
        truncated.voltages["v(out)"].pop_back();
        EXPECT_FALSE(validate_dc_sweep_data(ref, truncated).empty());
    }
}

TEST(Comparator, RejectsInvalidTransientDataBeforeInterpolation) {
    TransientResult ref;
    ref.time = {0.0, 1.0, 2.0};
    ref.voltages["v(out)"] = {0.0, 1.0, 2.0};
    EXPECT_FALSE(compare_transient({}, {}).passed);
    auto actual = ref;
    actual.time.clear();
    actual.voltages["v(out)"].clear();
    EXPECT_FALSE(compare_transient(ref, actual).passed);
    actual = ref;
    actual.voltages["v(out)"].pop_back();
    EXPECT_FALSE(compare_transient(ref, actual).passed);
    actual = ref;
    actual.time[1] = actual.time[0];
    EXPECT_FALSE(compare_transient(ref, actual).passed);
    actual = ref;
    actual.voltages["v(out)"][1] = std::numeric_limits<double>::quiet_NaN();
    EXPECT_FALSE(compare_transient(ref, actual).passed);
    EXPECT_FALSE(compare_transient(actual, ref).passed);
    actual = ref;
    actual.status.converged = false;
    EXPECT_FALSE(compare_transient(ref, actual).passed);
}

TEST(Comparator, RejectsTruncatedTransientEvenWhenCommonPointsAgree) {
    TransientResult ref;
    ref.time = {0.0, 1.0, 2.0};
    ref.voltages["v(out)"] = {0.0, 1.0, 2.0};
    auto actual = ref;
    actual.time.pop_back();
    actual.voltages["v(out)"].pop_back();
    EXPECT_FALSE(compare_transient(ref, actual).passed);
    EXPECT_FALSE(compare_transient_oscillator(ref, actual).passed);
}

TEST(Comparator, ACRequiresFiniteCompleteDataOnMatchingFrequencies) {
    ACResult ref;
    ref.frequency = {1.0, 10.0};
    ref.voltages["v(out)"] = {{1.0, 0.0}, {0.5, -0.5}};
    EXPECT_TRUE(compare_ac(ref, ref).passed);
    EXPECT_FALSE(compare_ac({}, {}).passed);
    auto actual = ref;
    actual.frequency[1] = 20.0;
    EXPECT_FALSE(compare_ac(ref, actual).passed);
    actual = ref;
    actual.voltages["v(out)"].pop_back();
    EXPECT_FALSE(compare_ac(ref, actual).passed);
    actual = ref;
    actual.voltages["v(out)"][0] = {1.0, std::numeric_limits<double>::quiet_NaN()};
    EXPECT_FALSE(compare_ac(ref, actual).passed);
    actual = ref;
    actual.status.converged = false;
    EXPECT_FALSE(compare_ac(ref, actual).passed);
}

TEST(Comparator, NoiseRejectsNegativeDensityAndIncompleteOrDifferentGrids) {
    NgspiceNoiseResult ref{{1.0, 10.0}, {2.0, 2.0}, {3.0, 3.0}};
    NoiseResult actual;
    actual.frequency = ref.frequency;
    actual.output_noise_density = {4.0, 4.0};
    actual.input_noise_density = {9.0, 9.0};
    EXPECT_TRUE(compare_noise(ref, actual).passed);
    actual.output_noise_density[1] = -1.0;
    EXPECT_FALSE(compare_noise(ref, actual).passed);
    actual.output_noise_density = {4.0};
    EXPECT_FALSE(compare_noise(ref, actual).passed);
    actual.output_noise_density = {4.0, 4.0};
    actual.frequency[1] = 11.0;
    EXPECT_FALSE(compare_noise(ref, actual).passed);
}

TEST(Comparator, ACRejectsEqualMagnitudeWithIncorrectPhase) {
    ACResult ref;
    ref.frequency = {1.0};
    ref.voltages["v(out)"] = {{1.0, 0.0}};
    ref.currents["i(v1)"] = {{0.0, -1.0}};
    auto actual = ref;
    actual.voltages["v(out)"][0] = {0.0, 1.0};
    auto cmp = compare_ac(ref, actual);
    EXPECT_FALSE(cmp.passed);
    EXPECT_DOUBLE_EQ(cmp.worst_error, std::sqrt(2.0));
    actual = ref;
    actual.currents["i(v1)"][0] = {0.0, 1.0};
    cmp = compare_ac(ref, actual);
    EXPECT_FALSE(cmp.passed);
    EXPECT_DOUBLE_EQ(cmp.worst_error, 2.0);
    EXPECT_EQ(cmp.worst_signal, "i(v1)");
}

TEST(Comparator, ACRejectsOverflowInDerivedError) {
    ACResult ref;
    ref.frequency = {1.0};
    const double large = std::numeric_limits<double>::max();
    ref.voltages["v(out)"] = {{large, large}};
    auto actual = ref;
    actual.voltages["v(out)"][0] = {-large, -large};
    EXPECT_FALSE(compare_ac(ref, actual).passed);
}

TEST(Comparator, EdgesRejectEmptyAndNonfiniteMetricsOrMalformedWaveforms) {
    EXPECT_FALSE(compare_edges({}, {}).passed);
    std::vector<EdgeMetrics> ref{{1.0, 0.1, 1.0, 0.0}};
    auto actual = ref;
    actual[0].rise_time = std::numeric_limits<double>::quiet_NaN();
    EXPECT_FALSE(compare_edges(ref, actual).passed);
    EXPECT_THROW(extract_edges({0.0, 1.0}, {0.0}, 0.0, 1.0, 0.1),
                 std::invalid_argument);
}

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

TEST(Comparator, TransientNormalizationAlwaysUsesReferenceValue) {
    TransientResult ref, actual;
    ref.time = {0, 0.5, 1};
    actual.time = {0, 1};
    ref.voltages["v(out)"] = {1, 1, 1};
    actual.voltages["v(out)"] = {1.01, 1.01};
    auto cmp = compare_transient(ref, actual, {0.00995, 1e-9});
    EXPECT_FALSE(cmp.passed); // Normalizing by actual=1.01 would falsely pass.
    EXPECT_NEAR(cmp.worst_error, 0.01, 1e-16);
    actual.voltages["v(out)"] = {0.99, 0.99};
    cmp = compare_transient(ref, actual, {0.01005, 1e-9});
    EXPECT_TRUE(cmp.passed); // Same absolute deviation from the reference.
    EXPECT_NEAR(cmp.worst_error, 0.01, 1e-16);
}

TEST(Comparator, RejectsExcursionsPresentOnlyOnTheDenserGrid) {
    TransientResult sparse, dense;
    sparse.time = {0, 1};
    sparse.voltages["v(out)"] = {0, 0};
    dense.time = {0, 0.5, 1};
    dense.voltages["v(out)"] = {0, 1, 0};
    auto cmp = compare_transient(sparse, dense);
    EXPECT_FALSE(cmp.passed);
    EXPECT_EQ(cmp.num_points_compared, 3);
    ASSERT_EQ(cmp.signals.size(), 1u);
    EXPECT_EQ(cmp.signals[0].name, "v(out)");
    EXPECT_EQ(cmp.signals[0].points, 3);
    EXPECT_DOUBLE_EQ(cmp.signals[0].max_absolute_error, 1);
    EXPECT_DOUBLE_EQ(cmp.signals[0].max_normalized_error, 1e9);
    EXPECT_DOUBLE_EQ(cmp.signals[0].worst_coordinate, 0.5);
    EXPECT_DOUBLE_EQ(cmp.signals[0].reference_at_worst, 0);
    EXPECT_DOUBLE_EQ(cmp.signals[0].actual_at_worst, 1);
    EXPECT_FALSE(compare_transient(dense, sparse).passed);
    sparse.voltages.clear();
    dense.voltages.clear();
    sparse.currents["i(v1)"] = {0, 0};
    dense.currents["i(v1)"] = {0, 1, 0};
    EXPECT_FALSE(compare_transient(sparse, dense).passed);
    EXPECT_FALSE(compare_transient(dense, sparse).passed);
}

TEST(Comparator, CountsUnionOfBothTimeGridsWithinReferenceCoverage) {
    TransientResult ref, actual;
    ref.time = {1, 2, 3};
    actual.time = {0, 1, 1.5, 2, 2.5, 3, 4};
    ref.voltages["v(out)"] = {1, 1, 1};
    actual.voltages["v(out)"].assign(actual.time.size(), 1);
    auto cmp = compare_transient(ref, actual);
    EXPECT_TRUE(cmp.passed);
    EXPECT_EQ(cmp.num_points_compared, 5);
    ASSERT_EQ(cmp.signals.size(), 1u);
    EXPECT_EQ(cmp.signals[0].points, 5);
    EXPECT_DOUBLE_EQ(cmp.signals[0].max_absolute_error, 0);
    EXPECT_DOUBLE_EQ(cmp.signals[0].max_normalized_error, 0);
    EXPECT_EQ(compare_transient(ref, ref).num_points_compared, 3);
}

TEST(Comparator, DoesNotBendLinearRampAtFlatSourceCorner) {
    TransientResult ref, actual;
    ref.time = {0, 0.5, 1, 1.5};
    ref.voltages["v(in)"] = {0, 0.5, 1, 1};
    actual.time = {0, 0.75, 1.5};
    actual.voltages["v(in)"] = {0, 0.75, 1};
    // These original samples omit the corner in actual. The old sparser-grid
    // check hid the inability to reconstruct reference samples near t=1.
    EXPECT_FALSE(compare_transient(ref, actual, {1e-12, 1e-12}).passed);
    EXPECT_FALSE(compare_transient(actual, ref, {1e-12, 1e-12}).passed);
    // Retain every original sample and add enough data to resolve the ramp
    // and its corner on both sides. No waveform value or tolerance changes.
    ref.time = {0, 0.25, 0.5, 1, 1.5};
    ref.voltages["v(in)"] = {0, 0.25, 0.5, 1, 1};
    actual.time = {0, 0.125, 0.75, 1, 1.5};
    actual.voltages["v(in)"] = {0, 0.125, 0.75, 1, 1};
    EXPECT_TRUE(compare_transient(ref, actual, {1e-12, 1e-12}).passed);
    EXPECT_TRUE(compare_transient(actual, ref, {1e-12, 1e-12}).passed);
}

TEST(Comparator, InterpolatesPolynomialsAcrossSmoothExtrema) {
    for (double time_scale : {1e-9, 1.0, 1e9}) {
        for (int degree : {1, 2, 3}) {
            TransientResult ref, actual;
            auto sample = [&](TransientResult& result, std::vector<double> grid) {
                for (double t : grid) {
                    result.time.push_back(t * time_scale);
                    result.voltages["v(out)"].push_back(std::pow(t - 1.7, degree));
                }
            };
            sample(ref, {0, 0.4, 0.9, 1.5, 2.1, 2.7, 3.2, 4});
            sample(actual, {0, 0.1, 1.1, 1.4, 2.4, 3.6, 4});
            SCOPED_TRACE("degree=" + std::to_string(degree) +
                         " scale=" + std::to_string(time_scale));
            EXPECT_TRUE(compare_transient(ref, actual, {1e-12, 1}).passed);
            EXPECT_TRUE(compare_transient(actual, ref, {1e-12, 1}).passed);
            actual.voltages["v(out)"][3] += 0.01;
            EXPECT_FALSE(compare_transient(ref, actual, {1e-12, 1}).passed);
            EXPECT_FALSE(compare_transient(actual, ref, {1e-12, 1}).passed);
        }
    }
}

TEST(Comparator, KeepsInterpolationOnSmoothSideOfCurvatureCorner) {
    // Integral of a linear ramp that becomes constant at t=1. The function
    // and first derivative are continuous; the second derivative jumps.
    const auto value = [](double t) { return t <= 1 ? t*t : 2*t-1; };
    TransientResult ref, actual;
    ref.time = {0, 0.1, 0.2, 0.4, 0.7, 1, 1.1, 1.2, 1.5, 2};
    actual.time = {0, 0.3, 0.8, 1.4, 2};
    for (double t : ref.time) ref.voltages["v(out)"].push_back(value(t));
    for (double t : actual.time) actual.voltages["v(out)"].push_back(value(t));
    EXPECT_FALSE(compare_transient(ref, actual, {1e-12, 1}).passed);
    EXPECT_FALSE(compare_transient(actual, ref, {1e-12, 1}).passed);
    actual.time = {0, 0.1, 0.2, 0.3, 0.8, 1, 1.1, 1.4, 1.7, 2};
    actual.voltages["v(out)"].clear();
    for (double t : actual.time) actual.voltages["v(out)"].push_back(value(t));
    EXPECT_TRUE(compare_transient(ref, actual, {1e-12, 1}).passed);
    EXPECT_TRUE(compare_transient(actual, ref, {1e-12, 1}).passed);
    actual.voltages["v(out)"][2] += 1e-4;
    EXPECT_FALSE(compare_transient(ref, actual, {1e-12, 1}).passed);
}

TEST(Comparator, ResolvesSmoothSineMinimumWithoutLinearChordError) {
    const double pi = std::acos(-1.0);
    TransientResult ref, actual;
    ref.time.push_back(0);
    for (int i = 1; i < 256; ++i)
        ref.time.push_back((i - 0.37) * 2*pi/256);
    ref.time.push_back(2*pi);
    actual.time = {0, 0.5*pi, pi, 1.5*pi, 2*pi};
    for (double t : ref.time) ref.voltages["v(out)"].push_back(1 + std::sin(t));
    for (double t : actual.time) actual.voltages["v(out)"].push_back(1 + std::sin(t));
    EXPECT_FALSE(compare_transient(ref, actual, {1e-6, 1}).passed);
    EXPECT_FALSE(compare_transient(actual, ref, {1e-6, 1}).passed);
    actual.time.clear();
    actual.voltages["v(out)"].clear();
    for (int i = 0; i <= 128; ++i) {
        const double t = i * 2*pi/128;
        actual.time.push_back(t);
        actual.voltages["v(out)"].push_back(1 + std::sin(t));
    }
    EXPECT_TRUE(compare_transient(ref, actual, {1e-6, 1}).passed);
    EXPECT_TRUE(compare_transient(actual, ref, {1e-6, 1}).passed);
    actual.voltages["v(out)"][96] += 0.001;
    EXPECT_FALSE(compare_transient(ref, actual, {1e-6, 1}).passed);
}

TEST(Comparator, OscillatorRejectsInvalidLimitsAndDerivedOverflow) {
    TransientResult ref;
    ref.time = {0, 1, 2, 3};
    ref.voltages["v(out)"] = {1, 1, 1, 1};
    OscillatorTolerance tol;
    tol.min_periods = 0;
    EXPECT_FALSE(compare_transient_oscillator(ref, ref, tol).passed);
    tol = {};
    tol.dc_absolute = std::numeric_limits<double>::quiet_NaN();
    EXPECT_FALSE(compare_transient_oscillator(ref, ref, tol).passed);
    ref.voltages["v(out)"].assign(4, std::numeric_limits<double>::max());
    EXPECT_FALSE(compare_transient_oscillator(ref, ref).passed);
}

TEST(Comparator, EdgeComparisonRequiresCompleteConsistentMetrics) {
    std::vector<EdgeMetrics> ref{{1, -1, 0, 0}, {3, 1, 1, 0}};
    EXPECT_EQ(compare_edges(ref, ref).num_edges_compared, 2);
    auto actual = ref;
    actual[0].rise_time = -2;
    EXPECT_FALSE(compare_edges(ref, actual).passed); // -1 is a real fall duration.
    actual = ref;
    actual[0].rise_time = 1;
    EXPECT_FALSE(compare_edges(ref, actual).passed);
    actual = ref;
    actual[1].cross_time = 0.5;
    EXPECT_FALSE(compare_edges(ref, actual).passed);
    EdgeTolerance tol;
    tol.settled_absolute = std::numeric_limits<double>::quiet_NaN();
    EXPECT_FALSE(compare_edges(ref, ref, tol).passed);

    auto missing_threshold = extract_edges({0, 1, 2, 3}, {0.4, 0.5, 0.7, 0.7}, 0, 1, 0.1);
    ASSERT_FALSE(missing_threshold.empty());
    EXPECT_FALSE(compare_edges(missing_threshold, missing_threshold).passed);
    auto missing_settle = extract_edges({0, 1, 2}, {0, 0.5, 1}, 0, 1, 1);
    ASSERT_FALSE(missing_settle.empty());
    EXPECT_FALSE(compare_edges(missing_settle, missing_settle).passed);

    const std::vector<double> time{0, 1, 2, 3, 4, 5, 6, 7, 8};
    auto rising = extract_edges(time, {0, 0, 0.5, 1, 1, 1, 1, 1, 1}, 0, 1, 1);
    auto falling = extract_edges(time, {1, 1, 0.5, 0, 0, 0, 0, 0, 0}, 0, 1, 1);
    ASSERT_EQ(rising.size(), 1u);
    ASSERT_EQ(falling.size(), 1u);
    EXPECT_TRUE(compare_edges(rising, rising).passed);
    EXPECT_TRUE(compare_edges(falling, falling).passed);
    EXPECT_NEAR(rising[0].rise_time, 1.6, 1e-15);
    EXPECT_NEAR(falling[0].rise_time, -1.6, 1e-15);
}

TEST(Comparator, ACRecordsComplexErrorsAndPassingSignals) {
    ACResult reference, actual;
    reference.frequency = actual.frequency = {1e3, 2e3};
    reference.voltages["v(out)"] = {{1, 1}, {0, 2}};
    actual.voltages["v(out)"] = {{1, 1}, {0, 4}};
    reference.currents["i(v1)"] = actual.currents["i(v1)"] = {{1, 0}, {1, 0}};
    const auto result = compare_ac(reference, actual, {0.5, 1});
    EXPECT_FALSE(result.passed);
    EXPECT_EQ(result.num_points_compared, 4);
    ASSERT_EQ(result.signals.size(), 2u);
    const auto& error = result.signals[0];
    EXPECT_EQ(error.name, "v(out)");
    EXPECT_EQ(error.coordinate_unit, "Hz");
    EXPECT_EQ(error.points, 2);
    EXPECT_DOUBLE_EQ(error.max_absolute_error, 2);
    EXPECT_DOUBLE_EQ(error.max_normalized_error, 1);
    EXPECT_DOUBLE_EQ(error.worst_coordinate, 2e3);
    EXPECT_DOUBLE_EQ(error.reference_at_worst, 0);
    EXPECT_DOUBLE_EQ(error.reference_imag_at_worst, 2);
    EXPECT_DOUBLE_EQ(error.actual_imag_at_worst, 4);
    EXPECT_EQ(result.signals[1].name, "i(v1)");
    EXPECT_DOUBLE_EQ(result.signals[1].max_absolute_error, 0);
    EXPECT_EQ(result.signals[1].points, 2);
    EXPECT_TRUE(compare_ac(reference, reference, {0.5, 1}).passed);
}

TEST(Comparator, NoiseRecordsAmplitudeDensityErrorsAndPassingSignals) {
    NgspiceNoiseResult reference;
    NoiseResult actual;
    reference.frequency = actual.frequency = {1e3, 2e3};
    reference.onoise_spectrum = {2, 4};
    reference.inoise_spectrum = {1, 2};
    actual.output_noise_density = {4, 64};
    actual.input_noise_density = {1, 4};
    const auto result = compare_noise(reference, actual, {0.5, 1});
    EXPECT_FALSE(result.passed);
    EXPECT_EQ(result.num_points_compared, 4);
    ASSERT_EQ(result.signals.size(), 2u);
    const auto& error = result.signals[0];
    EXPECT_EQ(error.name, "onoise_spectrum");
    EXPECT_EQ(error.coordinate_unit, "Hz");
    EXPECT_EQ(error.points, 2);
    EXPECT_DOUBLE_EQ(error.max_absolute_error, 4);
    EXPECT_DOUBLE_EQ(error.max_normalized_error, 1);
    EXPECT_DOUBLE_EQ(error.worst_coordinate, 2e3);
    EXPECT_DOUBLE_EQ(error.reference_at_worst, 4);
    EXPECT_DOUBLE_EQ(error.actual_at_worst, 8);
    EXPECT_EQ(result.signals[1].name, "inoise_spectrum");
    EXPECT_DOUBLE_EQ(result.signals[1].max_absolute_error, 0);
    EXPECT_EQ(result.signals[1].points, 2);
    actual.output_noise_density = {4, 16};
    EXPECT_TRUE(compare_noise(reference, actual, {0.5, 1}).passed);
}

TEST(Comparator, InterpolatesDistinctSubAttosecondSamplesWithoutSnapping) {
    // The same straight line on two grids must not depend on the time unit.
    // A fixed 1e-18 s snap compares different values even for an exact ramp.
    for (double scale : {1e-18, 1e-9, 1.0}) {
        TransientResult ref, actual;
        ref.time = {0, 0.5*scale, 2*scale};
        ref.voltages["v(out)"] = {0, 0.5, 2};
        actual.time = {0, scale, 2*scale};
        actual.voltages["v(out)"] = {0, 1, 2};
        SCOPED_TRACE(scale);
        EXPECT_TRUE(compare_transient(ref, actual, {1e-12, 1}).passed);
        EXPECT_TRUE(compare_transient(actual, ref, {1e-12, 1}).passed);
        actual.voltages["v(out)"][1] += 0.1;
        EXPECT_FALSE(compare_transient(ref, actual, {1e-12, 1}).passed);
        EXPECT_FALSE(compare_transient(actual, ref, {1e-12, 1}).passed);
    }
}
