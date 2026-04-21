#include <gtest/gtest.h>
#include "parser/expression.hpp"
#include <cmath>
#include <numeric>
#include <unordered_map>

using namespace neospice;

static const std::unordered_map<std::string, double> empty_params;

TEST(ExpressionRandom, GaussReturnsNearNominal) {
    // gauss(1000, 0.1, 3) => nominal 1000, 10% variation, 3-sigma
    // Run many trials, check mean ~ 1000 and stddev ~ 1000*0.1/3 ~ 33.3
    std::vector<double> vals;
    for (int i = 0; i < 10000; ++i) {
        double v = eval_expression("gauss(1000, 0.1, 3)", empty_params);
        vals.push_back(v);
    }
    double mean = std::accumulate(vals.begin(), vals.end(), 0.0) / vals.size();
    double sq_sum = 0;
    for (double v : vals) sq_sum += (v - mean) * (v - mean);
    double stddev = std::sqrt(sq_sum / vals.size());
    EXPECT_NEAR(mean, 1000.0, 5.0);    // within 5 of 1000
    EXPECT_NEAR(stddev, 33.3, 5.0);    // within 5 of 33.3
}

TEST(ExpressionRandom, AGaussReturnsNearNominal) {
    // agauss(100, 5, 3) => nominal 100, abs variation 5, 3-sigma
    // stdvar = 5/3 ~ 1.667
    std::vector<double> vals;
    for (int i = 0; i < 10000; ++i) {
        double v = eval_expression("agauss(100, 5, 3)", empty_params);
        vals.push_back(v);
    }
    double mean = std::accumulate(vals.begin(), vals.end(), 0.0) / vals.size();
    double sq_sum = 0;
    for (double v : vals) sq_sum += (v - mean) * (v - mean);
    double stddev = std::sqrt(sq_sum / vals.size());
    EXPECT_NEAR(mean, 100.0, 0.5);
    EXPECT_NEAR(stddev, 1.667, 0.3);
}

TEST(ExpressionRandom, UnifBounded) {
    // unif(1000, 0.1) => 1000 * (1 + 0.1 * U(-1,1))
    // Range: [900, 1100]
    for (int i = 0; i < 1000; ++i) {
        double v = eval_expression("unif(1000, 0.1)", empty_params);
        EXPECT_GE(v, 900.0);
        EXPECT_LE(v, 1100.0);
    }
}

TEST(ExpressionRandom, AUnifBounded) {
    // aunif(50, 5) => 50 + 5 * U(-1,1)
    // Range: [45, 55]
    for (int i = 0; i < 1000; ++i) {
        double v = eval_expression("aunif(50, 5)", empty_params);
        EXPECT_GE(v, 45.0);
        EXPECT_LE(v, 55.0);
    }
}

TEST(ExpressionRandom, GaussZeroSigmaReturnsNominal) {
    double v = eval_expression("gauss(1000, 0.1, 0)", empty_params);
    EXPECT_DOUBLE_EQ(v, 1000.0);
}
