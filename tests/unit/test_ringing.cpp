#include <gtest/gtest.h>
#include "core/transient.hpp"
#include "core/timestep.hpp"
#include "parser/netlist_parser.hpp"
#include <cmath>
#include <algorithm>

using namespace neospice;

// ---------------------------------------------------------------
// Unit tests for TimeStepController ringing detection
// ---------------------------------------------------------------

TEST(RingingDetection, DetectsSignAlternation) {
    // Construct solution histories with sign-alternating second differences
    // (simulates trapezoidal ringing).
    TimeStepController ctrl;
    ctrl.init(1e-9, 100e-9);
    SimOptions opts;

    // Oscillating pattern: 0, 1, 0, 1  ->  d2_curr = 1 - 2*0 + 1 = 2
    //                                       d2_prev = 0 - 2*1 + 0 = -2
    // Sign alternation with amplitude >> tolerance
    std::vector<double> sol       = {1.0};
    std::vector<double> sol_prev  = {0.0};
    std::vector<double> sol_prev2 = {1.0};
    std::vector<double> sol_prev3 = {0.0};

    ctrl.check_ringing(sol, sol_prev, sol_prev2, sol_prev3, 1, opts);
    EXPECT_TRUE(ctrl.ringing_detected());
    EXPECT_EQ(ctrl.ringing_cooldown(), 3);
}

TEST(RingingDetection, NoRingingForSmoothSignal) {
    // Monotonically increasing signal: 0, 1, 2, 3
    // d2_curr = 3 - 2*2 + 1 = 0,  d2_prev = 2 - 2*1 + 0 = 0
    // No sign alternation
    TimeStepController ctrl;
    ctrl.init(1e-9, 100e-9);
    SimOptions opts;

    std::vector<double> sol       = {3.0};
    std::vector<double> sol_prev  = {2.0};
    std::vector<double> sol_prev2 = {1.0};
    std::vector<double> sol_prev3 = {0.0};

    ctrl.check_ringing(sol, sol_prev, sol_prev2, sol_prev3, 1, opts);
    EXPECT_FALSE(ctrl.ringing_detected());
    EXPECT_EQ(ctrl.ringing_cooldown(), 0);
}

TEST(RingingDetection, CooldownDecrementsWithoutRinging) {
    TimeStepController ctrl;
    ctrl.init(1e-9, 100e-9);
    SimOptions opts;

    // Trigger ringing first
    std::vector<double> sol       = {1.0};
    std::vector<double> sol_prev  = {0.0};
    std::vector<double> sol_prev2 = {1.0};
    std::vector<double> sol_prev3 = {0.0};

    ctrl.check_ringing(sol, sol_prev, sol_prev2, sol_prev3, 1, opts);
    EXPECT_TRUE(ctrl.ringing_detected());
    EXPECT_EQ(ctrl.ringing_cooldown(), 3);

    // Now check with a smooth signal (no ringing) and tick cooldown
    std::vector<double> smooth       = {4.0};
    std::vector<double> smooth_prev  = {3.0};
    std::vector<double> smooth_prev2 = {2.0};
    std::vector<double> smooth_prev3 = {1.0};

    ctrl.check_ringing(smooth, smooth_prev, smooth_prev2, smooth_prev3, 1, opts);
    EXPECT_FALSE(ctrl.ringing_detected());
    ctrl.tick_cooldown();
    EXPECT_EQ(ctrl.ringing_cooldown(), 2);

    ctrl.check_ringing(smooth, smooth_prev, smooth_prev2, smooth_prev3, 1, opts);
    ctrl.tick_cooldown();
    EXPECT_EQ(ctrl.ringing_cooldown(), 1);

    ctrl.check_ringing(smooth, smooth_prev, smooth_prev2, smooth_prev3, 1, opts);
    ctrl.tick_cooldown();
    EXPECT_EQ(ctrl.ringing_cooldown(), 0);
}

TEST(RingingDetection, CooldownDoesNotDecrementDuringRinging) {
    TimeStepController ctrl;
    ctrl.init(1e-9, 100e-9);
    SimOptions opts;

    // Trigger ringing
    std::vector<double> sol       = {1.0};
    std::vector<double> sol_prev  = {0.0};
    std::vector<double> sol_prev2 = {1.0};
    std::vector<double> sol_prev3 = {0.0};

    ctrl.check_ringing(sol, sol_prev, sol_prev2, sol_prev3, 1, opts);
    EXPECT_EQ(ctrl.ringing_cooldown(), 3);

    // Ringing continues — cooldown should stay at 3
    ctrl.check_ringing(sol, sol_prev, sol_prev2, sol_prev3, 1, opts);
    ctrl.tick_cooldown();
    EXPECT_EQ(ctrl.ringing_cooldown(), 3);  // reset by check_ringing
}

TEST(RingingDetection, IgnoresSmallAmplitudeOscillations) {
    // Very small oscillations below the tolerance threshold should not
    // trigger ringing detection.
    TimeStepController ctrl;
    ctrl.init(1e-9, 100e-9);
    SimOptions opts;
    opts.vntol = 1e-6;
    opts.reltol = 1e-3;

    // Tiny oscillation: amplitude 1e-10, well below vntol = 1e-6
    double tiny = 1e-10;
    std::vector<double> sol       = {tiny};
    std::vector<double> sol_prev  = {0.0};
    std::vector<double> sol_prev2 = {tiny};
    std::vector<double> sol_prev3 = {0.0};

    ctrl.check_ringing(sol, sol_prev, sol_prev2, sol_prev3, 1, opts);
    EXPECT_FALSE(ctrl.ringing_detected());
}

TEST(RingingDetection, InitClearsRingingState) {
    TimeStepController ctrl;
    ctrl.init(1e-9, 100e-9);
    SimOptions opts;

    // Trigger ringing
    std::vector<double> sol       = {1.0};
    std::vector<double> sol_prev  = {0.0};
    std::vector<double> sol_prev2 = {1.0};
    std::vector<double> sol_prev3 = {0.0};

    ctrl.check_ringing(sol, sol_prev, sol_prev2, sol_prev3, 1, opts);
    EXPECT_TRUE(ctrl.ringing_detected());
    EXPECT_EQ(ctrl.ringing_cooldown(), 3);

    // Re-init should clear all ringing state
    ctrl.init(1e-9, 100e-9);
    EXPECT_FALSE(ctrl.ringing_detected());
    EXPECT_EQ(ctrl.ringing_cooldown(), 0);
}

// ---------------------------------------------------------------
// Integration tests: full transient with known circuits
// ---------------------------------------------------------------

TEST(RingingIntegration, LCStepResponseCompletes) {
    // LC circuit driven by a fast PULSE — known to cause trap ringing.
    // The ringing detection should automatically switch to Gear-2 and
    // produce a reasonable result without oscillation artifacts.
    std::string netlist = R"(
* LC step response - known trap ringing circuit
V1 in 0 PULSE(0 1 0 1p 1p 100n 200n)
R1 in n1 50
L1 n1 out 1u
C1 out 0 1n
R2 out 0 1k
.tran 0.1n 50n
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    auto result = solve_transient(ckt, 0.1e-9, 50e-9);

    // Verify simulation completed and produced output
    EXPECT_GT(result.time.size(), 10u);
    EXPECT_NEAR(result.time.back(), 50e-9, 1e-9);

    // Check that output voltage stays bounded (no wild ringing artifacts).
    // The input is 0-1V, so the LC response should be bounded by roughly
    // 0 to 2V (overshoot from LC resonance is physical; ringing artifacts
    // would produce much larger values or rapid sign alternation).
    auto& vout = result.voltage("out");
    ASSERT_FALSE(vout.empty());
    for (size_t i = 0; i < vout.size(); ++i) {
        EXPECT_GT(vout[i], -0.5) << "Output too negative at t=" << result.time[i];
        EXPECT_LT(vout[i], 2.5) << "Output too positive at t=" << result.time[i];
    }
}

TEST(RingingIntegration, RCCircuitStaysOnTrap) {
    // Simple RC circuit (no ringing expected).  Verify it completes
    // correctly — this is a regression test to ensure the ringing
    // detection doesn't incorrectly fire on smooth waveforms.
    std::string netlist = R"(
RC Step Response
V1 in 0 5
R1 in out 1k
C1 out 0 1u
.ic v(out)=0
.tran 10u 5m
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    auto result = solve_transient(ckt, 10e-6, 5e-3);

    // Find index nearest to t=1ms (1 tau)
    int idx_1ms = 0;
    double best_1ms = 1e30;
    for (size_t i = 0; i < result.time.size(); ++i) {
        double d = std::abs(result.time[i] - 1e-3);
        if (d < best_1ms) { best_1ms = d; idx_1ms = static_cast<int>(i); }
    }
    double expected_1tau = 5.0 * (1.0 - std::exp(-1.0));
    EXPECT_NEAR(result.voltage("out")[idx_1ms], expected_1tau, 0.1);

    // At t=5ms (5tau): should be close to 5V
    EXPECT_NEAR(result.voltage("out").back(), 5.0, 0.05);
}
