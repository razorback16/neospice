#include <gtest/gtest.h>
#include "api/neospice.hpp"
#include "framework/ngspice_runner.hpp"
#include "framework/comparator.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <vector>

using namespace neospice;

class NgspiceCompareTest : public ::testing::Test {
protected:
    void SetUp() override {
        ngspice_ = std::make_unique<NgspiceRunner>(NGSPICE_BINARY);
    }
    std::unique_ptr<NgspiceRunner> ngspice_;
    Simulator sim_;
};

// ---------------------------------------------------------------------------
// Diode DC test
// ---------------------------------------------------------------------------

TEST_F(NgspiceCompareTest, DiodeDC) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/diode_iv.cir";
    auto ng_result = ngspice_->run_dc(path);
    auto ckt = sim_.load(path);
    auto cs_result = sim_.run_dc(ckt);
    auto cmp = compare_dc(ng_result, cs_result, {1e-4, 1e-12});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}

// ---------------------------------------------------------------------------
// Diode transient test
// ---------------------------------------------------------------------------

TEST_F(NgspiceCompareTest, DiodeRectifierTransient) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/diode_rectifier.cir";
    auto ng_result = ngspice_->run_transient(path);
    auto ckt = sim_.load(path);
    auto cs_result = sim_.run(ckt);
    ASSERT_TRUE(std::holds_alternative<TransientResult>(cs_result.analysis));
    auto cmp = compare_transient(std::get<TransientResult>(cs_result.analysis), ng_result, {1.5e-1, 1e-1});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}

// ---------------------------------------------------------------------------
// Diode noise test
// ---------------------------------------------------------------------------

TEST_F(NgspiceCompareTest, DiodeNoise) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/diode_noise.cir";
    auto ng_result = ngspice_->run_noise(path);
    auto ckt = sim_.load(path);
    auto cs_result = sim_.run(ckt);
    ASSERT_TRUE(std::holds_alternative<NoiseResult>(cs_result.analysis));
    ASSERT_EQ(ng_result.frequency.size(), std::get<NoiseResult>(cs_result.analysis).frequency.size());
    // Diode shot noise + resistor thermal noise; both white (flat)
    // Tolerance wider than resistor-only because ngspice includes Rs thermal
    // noise and flicker noise that our simplified model omits.
    auto cmp = compare_noise(ng_result, std::get<NoiseResult>(cs_result.analysis), {5e-4, 1e-15});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}
