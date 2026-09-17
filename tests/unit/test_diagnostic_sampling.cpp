#include "bench/diagnostic_sampling.hpp"
#include <gtest/gtest.h>
#include <limits>

using namespace neospice;

TEST(DiagnosticSampling, RejectsUnconvergedEmptyAndNonfiniteResults) {
    DCResult result;
    result.status.converged = true;
    EXPECT_THROW(bench::require_diagnostic_dc(result), std::runtime_error);
    result.node_voltages["v(out)"] = 0;
    result.branch_currents["i(v1)"] = 0;
    EXPECT_NO_THROW(bench::require_diagnostic_dc(result));
    result.status.converged = false;
    EXPECT_THROW(bench::require_diagnostic_dc(result), std::runtime_error);
    result.status.converged = true;
    for (double invalid : {std::numeric_limits<double>::quiet_NaN(),
                           std::numeric_limits<double>::infinity(),
                           -std::numeric_limits<double>::infinity()}) {
        result.node_voltages["v(out)"] = invalid;
        EXPECT_THROW(bench::require_diagnostic_dc(result), std::runtime_error);
        result.node_voltages["v(out)"] = 0;
        result.branch_currents["i(v1)"] = invalid;
        EXPECT_THROW(bench::require_diagnostic_dc(result), std::runtime_error);
        result.branch_currents["i(v1)"] = 0;
    }
}

TEST(DiagnosticSampling, RejectsFailureAtEveryWarmupAndSamplePosition) {
    for (int fail_at = 0; fail_at < 5; ++fail_at) {
        int calls = 0;
        bool returned = false;
        auto sample = [&] {
            DCResult result;
            result.node_voltages["v(out)"] = 1;
            result.status.converged = calls++ != fail_at;
            bench::require_diagnostic_dc(result);
            return calls;
        };
        EXPECT_THROW({
            auto samples = bench::diagnostic_samples(2, 3, sample);
            returned = true;
        }, std::runtime_error);
        EXPECT_FALSE(returned);
        EXPECT_EQ(calls, fail_at + 1);
    }
}

TEST(DiagnosticSampling, PropagatesExceptionsAndRetainsSuccessfulSampleOrder) {
    int calls = 0;
    const auto samples = bench::diagnostic_samples(2, 3, [&] { return ++calls; });
    EXPECT_EQ(samples, (std::vector<int>{3, 4, 5}));
    EXPECT_THROW(bench::diagnostic_samples(0, 3, []() -> int {
        throw std::runtime_error("parse failed");
    }), std::runtime_error);
    EXPECT_THROW(bench::diagnostic_samples(-1, 3, [] { return 0; }), std::invalid_argument);
    EXPECT_THROW(bench::diagnostic_samples(0, 0, [] { return 0; }), std::invalid_argument);
}
