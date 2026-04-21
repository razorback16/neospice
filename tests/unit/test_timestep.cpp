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

    // 2 nodes, no branch currents => num_vars == num_nodes == 2
    std::vector<double> sol = {5.0, 3.0};
    std::vector<double> sol_prev = {4.99, 2.99};
    std::vector<double> sol_prev2 = {4.98, 2.98};
    SimOptions opts;
    opts.trtol = 7.0;

    bool accepted = ctrl.evaluate_step(sol, sol_prev, sol_prev2, 2, 2, opts);
    EXPECT_TRUE(accepted);
    EXPECT_GT(ctrl.proposed_dt(), 0.0);
}

TEST(TimeStepController, RejectLargeError) {
    TimeStepController ctrl;
    ctrl.init(1e-6, 1e-3);

    // 2 nodes, no branch currents => num_vars == num_nodes == 2
    std::vector<double> sol = {10.0, 5.0};
    std::vector<double> sol_prev = {0.0, 0.0};
    std::vector<double> sol_prev2 = {10.0, 5.0};
    SimOptions opts;
    opts.trtol = 7.0;

    bool accepted = ctrl.evaluate_step(sol, sol_prev, sol_prev2, 2, 2, opts);
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

// --- Current variable LTE tests ---

TEST(TimeStepController, CurrentLTE_DefaultMasked) {
    SimOptions opts;
    EXPECT_TRUE(opts.mask_ivars);
}

TEST(TimeStepController, CurrentLTE_RejectLargeCurrentError) {
    TimeStepController ctrl;
    ctrl.init(1e-6, 1e-3);

    std::vector<double> sol      = {1.0, 2.0, 10.0};
    std::vector<double> sol_prev = {0.99, 1.99, 0.0};
    std::vector<double> sol_prev2= {0.98, 1.98, 10.0};
    SimOptions opts;
    opts.trtol = 7.0;
    opts.reltol = 1e-3;
    opts.vntol = 1e-6;
    opts.abstol = 1e-12;
    opts.mask_ivars = false;

    bool accepted = ctrl.evaluate_step(sol, sol_prev, sol_prev2, 2, 3, opts);
    EXPECT_FALSE(accepted);
}

TEST(TimeStepController, CurrentLTE_AcceptWhenMasked) {
    TimeStepController ctrl;
    ctrl.init(1e-6, 1e-3);

    std::vector<double> sol      = {1.0, 2.0, 10.0};
    std::vector<double> sol_prev = {0.99, 1.99, 0.0};
    std::vector<double> sol_prev2= {0.98, 1.98, 10.0};
    SimOptions opts;
    opts.trtol = 7.0;
    opts.reltol = 1e-3;
    opts.vntol = 1e-6;
    opts.abstol = 1e-12;
    opts.mask_ivars = true;

    bool accepted = ctrl.evaluate_step(sol, sol_prev, sol_prev2, 2, 3, opts);
    EXPECT_TRUE(accepted);
}

TEST(TimeStepController, CurrentLTE_UsesAbstol) {
    TimeStepController ctrl;
    ctrl.init(1e-6, 1e-3);

    std::vector<double> sol      = {1.0, 1e-9};
    std::vector<double> sol_prev = {1.0, 0.0};
    std::vector<double> sol_prev2= {1.0, 1e-9};
    SimOptions opts;
    opts.trtol = 7.0;
    opts.reltol = 1e-3;
    opts.vntol = 1e-6;
    opts.mask_ivars = false;

    opts.abstol = 1e-12;
    bool accepted1 = ctrl.evaluate_step(sol, sol_prev, sol_prev2, 1, 2, opts);
    EXPECT_FALSE(accepted1);

    TimeStepController ctrl2;
    ctrl2.init(1e-6, 1e-3);
    opts.abstol = 1e-6;
    bool accepted2 = ctrl2.evaluate_step(sol, sol_prev, sol_prev2, 1, 2, opts);
    EXPECT_TRUE(accepted2);
}

TEST(TimeStepController, CurrentLTE_NoBranchCurrents) {
    TimeStepController ctrl;
    ctrl.init(1e-6, 1e-3);

    std::vector<double> sol      = {5.0, 3.0};
    std::vector<double> sol_prev = {4.99, 2.99};
    std::vector<double> sol_prev2= {4.98, 2.98};
    SimOptions opts;
    opts.trtol = 7.0;

    opts.mask_ivars = false;
    bool accepted1 = ctrl.evaluate_step(sol, sol_prev, sol_prev2, 2, 2, opts);
    EXPECT_TRUE(accepted1);

    TimeStepController ctrl2;
    ctrl2.init(1e-6, 1e-3);
    opts.mask_ivars = true;
    bool accepted2 = ctrl2.evaluate_step(sol, sol_prev, sol_prev2, 2, 2, opts);
    EXPECT_TRUE(accepted2);
}

// --- LTE reference mode tests ---

TEST(TimeStepController, Mode0DefaultBehavior) {
    TimeStepController ctrl;
    ctrl.init(1e-6, 1e-3);

    std::vector<double> sol      = {5.0, 3.0};
    std::vector<double> sol_prev = {4.99, 2.99};
    std::vector<double> sol_prev2= {4.98, 2.98};
    SimOptions opts;
    opts.trtol = 7.0;
    opts.lte_ref_mode = 0;

    bool accepted = ctrl.evaluate_step(sol, sol_prev, sol_prev2, 2, 2, opts);
    EXPECT_TRUE(accepted);
}

TEST(TimeStepController, Mode1UsesMaxOfAllSignals) {
    std::vector<double> sol      = {100.0, 0.001};
    std::vector<double> sol_prev = {100.0, 0.5};
    std::vector<double> sol_prev2= {100.0, 0.001};

    SimOptions opts;
    opts.trtol = 7.0;
    opts.reltol = 1e-3;
    opts.vntol = 1e-6;

    opts.lte_ref_mode = 0;
    TimeStepController ctrl0;
    ctrl0.init(1e-6, 1e-3);
    bool accepted0 = ctrl0.evaluate_step(sol, sol_prev, sol_prev2, 2, 2, opts);
    EXPECT_FALSE(accepted0);

    opts.lte_ref_mode = 1;
    TimeStepController ctrl1;
    ctrl1.init(1e-6, 1e-3);
    bool accepted1 = ctrl1.evaluate_step(sol, sol_prev, sol_prev2, 2, 2, opts);
    EXPECT_TRUE(accepted1);
}

// --- Breakpoint type classification tests ---

TEST(TimeStepController, BreakpointTypeDefaultHard) {
    TimeStepController ctrl;
    ctrl.init(1e-6, 1e-3);

    ctrl.add_source_breakpoint(5e-6);  // default type is HARD
    ctrl.advance(5e-6);
    EXPECT_TRUE(ctrl.crossed_source_breakpoint());
    EXPECT_EQ(ctrl.last_bp_type(), TimeStepController::BreakpointType::HARD);
}

TEST(TimeStepController, BreakpointTypeSoft) {
    TimeStepController ctrl;
    ctrl.init(1e-6, 1e-3);

    ctrl.add_source_breakpoint(5e-6, TimeStepController::BreakpointType::SOFT);
    ctrl.advance(5e-6);
    EXPECT_TRUE(ctrl.crossed_source_breakpoint());
    EXPECT_EQ(ctrl.last_bp_type(), TimeStepController::BreakpointType::SOFT);
}

TEST(TimeStepController, BreakpointTypeHardPromotesOverSoft) {
    // When both HARD and SOFT breakpoints are consumed in the same advance,
    // the result should be HARD (the stricter type wins).
    TimeStepController ctrl;
    ctrl.init(1e-6, 1e-3);

    ctrl.add_source_breakpoint(5e-6, TimeStepController::BreakpointType::SOFT);
    ctrl.add_source_breakpoint(5e-6, TimeStepController::BreakpointType::HARD);
    ctrl.advance(5e-6);
    EXPECT_TRUE(ctrl.crossed_source_breakpoint());
    EXPECT_EQ(ctrl.last_bp_type(), TimeStepController::BreakpointType::HARD);
}

TEST(TimeStepController, BreakpointTypeMultipleSoftRemainsSoft) {
    TimeStepController ctrl;
    ctrl.init(1e-6, 1e-3);

    // Two soft breakpoints at different times, both consumed
    ctrl.add_source_breakpoint(3e-6, TimeStepController::BreakpointType::SOFT);
    ctrl.add_source_breakpoint(4e-6, TimeStepController::BreakpointType::SOFT);
    ctrl.advance(5e-6);  // consumes both
    EXPECT_TRUE(ctrl.crossed_source_breakpoint());
    EXPECT_EQ(ctrl.last_bp_type(), TimeStepController::BreakpointType::SOFT);
}

TEST(TimeStepController, RestartStepScaleDefault) {
    SimOptions opts;
    EXPECT_DOUBLE_EQ(opts.restart_step_scale, 0.1);
}

TEST(TimeStepController, Mode2TracksHistoricalMax) {
    TimeStepController ctrl;
    ctrl.init(1e-6, 1e-3);

    SimOptions opts;
    opts.trtol = 7.0;
    opts.reltol = 1e-3;
    opts.vntol = 1e-6;
    opts.lte_ref_mode = 2;

    std::vector<double> sol1      = {100.0, 50.0};
    std::vector<double> sol1_prev = {100.0, 50.0};
    std::vector<double> sol1_prev2= {100.0, 50.0};
    ctrl.evaluate_step(sol1, sol1_prev, sol1_prev2, 2, 2, opts);

    std::vector<double> sol2      = {0.001, 0.001};
    std::vector<double> sol2_prev = {0.001, 0.5};
    std::vector<double> sol2_prev2= {0.001, 0.001};

    bool accepted = ctrl.evaluate_step(sol2, sol2_prev, sol2_prev2, 2, 2, opts);
    EXPECT_TRUE(accepted);

    opts.lte_ref_mode = 0;
    TimeStepController ctrl0;
    ctrl0.init(1e-6, 1e-3);
    bool accepted0 = ctrl0.evaluate_step(sol2, sol2_prev, sol2_prev2, 2, 2, opts);
    EXPECT_FALSE(accepted0);
}
