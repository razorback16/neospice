#include <gtest/gtest.h>
#include "core/timestep.hpp"

using namespace neospice;

TEST(TimeStepController, InitialStep) {
    TimeStepController ctrl;
    ctrl.init(1e-6, 1e-3);
    EXPECT_DOUBLE_EQ(ctrl.current_dt(), 1e-6);
    EXPECT_DOUBLE_EQ(ctrl.current_time(), 0.0);
}

TEST(TimeStepController, AcceptStep) {
    TimeStepController ctrl;
    ctrl.init(1e-6, 1e-3);

    std::vector<double> sol = {5.0, 3.0};
    std::vector<double> sol_prev = {4.99, 2.99};
    std::vector<double> sol_prev2 = {4.98, 2.98};
    SimOptions opts;
    opts.trtol = 7.0;

    bool accepted = ctrl.evaluate_step(sol, sol_prev, sol_prev2, 2, opts);
    EXPECT_TRUE(accepted);
    EXPECT_GT(ctrl.proposed_dt(), 0.0);
}

TEST(TimeStepController, RejectLargeError) {
    TimeStepController ctrl;
    ctrl.init(1e-6, 1e-3);

    std::vector<double> sol = {10.0, 5.0};
    std::vector<double> sol_prev = {0.0, 0.0};
    std::vector<double> sol_prev2 = {10.0, 5.0};
    SimOptions opts;
    opts.trtol = 7.0;

    bool accepted = ctrl.evaluate_step(sol, sol_prev, sol_prev2, 2, opts);
    EXPECT_FALSE(accepted);
    EXPECT_LT(ctrl.proposed_dt(), ctrl.current_dt());
}

TEST(TimeStepController, BreakpointScheduling) {
    TimeStepController ctrl;
    ctrl.init(1e-6, 1e-3);

    ctrl.add_breakpoint(5e-6);
    ctrl.add_breakpoint(15e-6);

    ctrl.set_dt(3e-6);
    ctrl.advance(3e-6);  // t=3e-6
    double next = ctrl.clamp_to_breakpoint(3e-6);
    EXPECT_NEAR(next, 2e-6, 1e-15);  // clamped to hit t=5e-6
}

TEST(TimeStepController, DoesNotExceedTstop) {
    TimeStepController ctrl;
    ctrl.init(1e-6, 10e-6);

    ctrl.advance(9.5e-6);  // t=9.5e-6
    double next = ctrl.clamp_to_end(1e-6);
    EXPECT_NEAR(next, 0.5e-6, 1e-15);
}

// ---------------------------------------------------------------------------
// LTE reference mode tests
// ---------------------------------------------------------------------------

TEST(TimeStepController, Mode0DefaultBehavior) {
    // Mode 0 (default) should behave identically to the original implementation.
    TimeStepController ctrl;
    ctrl.init(1e-6, 1e-3);

    // Small changes relative to signal magnitude -> accepted
    std::vector<double> sol      = {5.0, 3.0};
    std::vector<double> sol_prev = {4.99, 2.99};
    std::vector<double> sol_prev2= {4.98, 2.98};
    SimOptions opts;
    opts.trtol = 7.0;
    opts.lte_ref_mode = 0;

    bool accepted = ctrl.evaluate_step(sol, sol_prev, sol_prev2, 2, opts);
    EXPECT_TRUE(accepted);
}

TEST(TimeStepController, Mode1UsesMaxOfAllSignals) {
    // A small signal near zero would be rejected by mode 0 (only vntol protects it)
    // but mode 1 uses the max of all signals so the tolerance is much larger.

    // Node 0: large signal (100V), Node 1: small signal with oscillation
    // Node 1: sol=0.001, prev=0.5, prev2=0.001 -> delta2 = 0.001 - 1.0 + 0.001 = -0.998
    // LTE = |delta2| * 1/12 = 0.998/12 ~ 0.0832
    // Mode 0 tol for node1: reltol*|0.001| + vntol = 1e-3*1e-3 + 1e-6 = 2e-6
    //   ratio = 0.0832/2e-6 = 41600 >> trtol -> rejected
    // Mode 1 tol: reltol*max(100,0.001) + vntol = 1e-3*100 + 1e-6 = 0.1000010
    //   ratio = 0.0832/0.100001 ~ 0.832 < trtol -> accepted

    std::vector<double> sol      = {100.0, 0.001};
    std::vector<double> sol_prev = {100.0, 0.5};
    std::vector<double> sol_prev2= {100.0, 0.001};

    SimOptions opts;
    opts.trtol = 7.0;
    opts.reltol = 1e-3;
    opts.vntol = 1e-6;

    // Mode 0: should reject (small signal node has huge ratio)
    opts.lte_ref_mode = 0;
    TimeStepController ctrl0;
    ctrl0.init(1e-6, 1e-3);
    bool accepted0 = ctrl0.evaluate_step(sol, sol_prev, sol_prev2, 2, opts);
    EXPECT_FALSE(accepted0);

    // Mode 1: should accept (tolerance boosted by large signal)
    opts.lte_ref_mode = 1;
    TimeStepController ctrl1;
    ctrl1.init(1e-6, 1e-3);
    bool accepted1 = ctrl1.evaluate_step(sol, sol_prev, sol_prev2, 2, opts);
    EXPECT_TRUE(accepted1);
}

TEST(TimeStepController, Mode2TracksHistoricalMax) {
    // Mode 2 remembers the max |value| over time for each node.
    // First call establishes the max, second call uses it even if signal has decayed.
    TimeStepController ctrl;
    ctrl.init(1e-6, 1e-3);

    SimOptions opts;
    opts.trtol = 7.0;
    opts.reltol = 1e-3;
    opts.vntol = 1e-6;
    opts.lte_ref_mode = 2;

    // Step 1: Large signal to establish max_seen
    std::vector<double> sol1      = {100.0, 50.0};
    std::vector<double> sol1_prev = {100.0, 50.0};
    std::vector<double> sol1_prev2= {100.0, 50.0};
    ctrl.evaluate_step(sol1, sol1_prev, sol1_prev2, 2, opts);

    // Step 2: Signal has decayed to near zero, but with oscillation.
    // Node 1: sol=0.001, prev=0.5, prev2=0.001 -> delta2 = -0.998
    // LTE = 0.998/12 ~ 0.0832
    // Mode 0 would give tol = 1e-3*0.001 + 1e-6 = 2e-6 -> ratio huge -> reject
    // Mode 2 uses max_seen[1]=50 -> tol = 1e-3*50 + 1e-6 = 0.050001
    //   ratio = 0.0832/0.050001 ~ 1.664 < trtol -> accept
    std::vector<double> sol2      = {0.001, 0.001};
    std::vector<double> sol2_prev = {0.001, 0.5};
    std::vector<double> sol2_prev2= {0.001, 0.001};

    bool accepted = ctrl.evaluate_step(sol2, sol2_prev, sol2_prev2, 2, opts);
    EXPECT_TRUE(accepted);

    // Verify mode 0 would have rejected this
    opts.lte_ref_mode = 0;
    TimeStepController ctrl0;
    ctrl0.init(1e-6, 1e-3);
    bool accepted0 = ctrl0.evaluate_step(sol2, sol2_prev, sol2_prev2, 2, opts);
    EXPECT_FALSE(accepted0);
}
