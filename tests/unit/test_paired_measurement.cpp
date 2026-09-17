#include <gtest/gtest.h>
#include "bench/paired_measurement.hpp"
#include "framework/comparator.hpp"
#include <string>
#include <vector>

using namespace neospice;

TEST(PairedMeasurement, RetainsResultsAfterCleanupAndIncludesAllPhases) {
    bool loaded = false;
    auto [value, timing] = bench::measure(
        [&] { loaded = true; },
        [&] { EXPECT_TRUE(loaded); return std::vector<double>{1, 2, 3}; },
        [&] { loaded = false; });
    EXPECT_FALSE(loaded);
    EXPECT_EQ(value, (std::vector<double>{1, 2, 3}));
    EXPECT_GE(timing.load_us, 0);
    EXPECT_GE(timing.analysis_us, 0);
    EXPECT_GE(timing.cleanup_us, 0);
    EXPECT_NEAR(timing.total_us, timing.load_us + timing.analysis_us + timing.cleanup_us, 1e-6);
}

TEST(PairedMeasurement, CleansUpAfterFailedAnalysisAndPreservesItsError) {
    bool cleaned = false;
    try {
        bench::measure([] {}, []() -> int { throw std::runtime_error("analysis failure"); },
            [&] { cleaned = true; throw std::runtime_error("cleanup failure"); });
        FAIL() << "Failure was lost";
    } catch (const std::runtime_error& e) {
        EXPECT_EQ(std::string(e.what()), "analysis failure");
    }
    EXPECT_TRUE(cleaned);
}

TEST(PairedMeasurement, RunsBothOrdersAndValidatesReferenceAgainstActual) {
    std::vector<std::string> order;
    auto neo = [&] { order.push_back("neo"); return std::pair{2, bench::Phases{}}; };
    auto ref = [&] { order.push_back("ref"); return std::pair{1, bench::Phases{}}; };
    int validated = 0;
    auto check = [&](int expected, int actual) {
        EXPECT_EQ(expected, 1); EXPECT_EQ(actual, 2); ++validated;
        return CompareResult{true, "", 0, 1};
    };
    bench::paired(true, neo, ref, check);
    bench::paired(false, neo, ref, check);
    EXPECT_EQ(order, (std::vector<std::string>{"neo", "ref", "ref", "neo"}));
    EXPECT_EQ(validated, 2);
}

TEST(PairedMeasurement, DoesNotAcceptMismatchOrZeroComparisonPoints) {
    auto sample = [] { return std::pair{1, bench::Phases{}}; };
    EXPECT_THROW(bench::paired(true, sample, sample, [](int, int) {
        return CompareResult{false, "wrong output", 1, 10};
    }), std::runtime_error);
    EXPECT_THROW(bench::paired(true, sample, sample, [](int, int) {
        return CompareResult{true, "", 0, 0};
    }), std::runtime_error);
}

TEST(PairedMeasurement, AttemptsBothSimulatorsButNeverValidatesFailedRun) {
    int calls = 0;
    auto failed = [&]() -> std::pair<int, bench::Phases> {
        ++calls; throw std::runtime_error("reference aborted");
    };
    auto good = [&] { ++calls; return std::pair{1, bench::Phases{}}; };
    auto validate = [](int, int) { ADD_FAILURE() << "A failed pair reached validation";
        return CompareResult{true, "", 0, 1}; };
    EXPECT_THROW(bench::paired(false, good, failed, validate), std::runtime_error);
    EXPECT_EQ(calls, 2);
}

TEST(NgspiceRunnerPhases, ExtractsExplicitCommandsInsteadOfNetlistAnalysis) {
    NgspiceRunner ng;
    ng.load(std::string(TEST_CIRCUITS_DIR) + "/resistor_divider.cir");
    ng.command("dc v1 -5 5 1");
    auto sweep = ng.read_dc_sweep();
    ASSERT_EQ(sweep.sweep_values.size(), 11);
    EXPECT_DOUBLE_EQ(sweep.voltage("mid").front(), -2.5);
    EXPECT_DOUBLE_EQ(sweep.voltage("mid").back(), 2.5);
    ng.reset();
    EXPECT_EQ(sweep.sweep_values.size(), 11); // copied result survives plot deletion
    EXPECT_THROW(ng.read_dc_sweep(), std::runtime_error);
}

TEST(PairedMeasurement, FrequencyCompletionAllowsAccumulatedRoundoffButRejectsTruncation) {
    std::vector<double> frequency;
    double f = 1;
    const double ratio = std::exp(std::log(10.0)/1000);
    for (int i = 0; i <= 8000; ++i, f *= ratio) frequency.push_back(f);
    EXPECT_TRUE(bench::complete_frequency_request(frequency, 1, 1e8, 8001));
    frequency.pop_back();
    EXPECT_FALSE(bench::complete_frequency_request(frequency, 1, 1e8, 8001));
    frequency.push_back(1e8 * 0.9999);
    EXPECT_FALSE(bench::complete_frequency_request(frequency, 1, 1e8, 8001));
    frequency.back() = std::numeric_limits<double>::quiet_NaN();
    EXPECT_FALSE(bench::complete_frequency_request(frequency, 1, 1e8, 8001));
}
