#include <gtest/gtest.h>
#include "devices/capacitor.hpp"
#include "devices/inductor.hpp"
#include "core/matrix.hpp"

using namespace neospice;

TEST(GearCapacitor, CompanionConductance) {
    Capacitor cap("C1", 0, -1, 1e-6);
    SparsityBuilder builder(1);
    cap.stamp_pattern(builder);
    auto pattern = builder.build();
    NumericMatrix mat(pattern);
    cap.assign_offsets(pattern);

    double dt = 1e-6;
    cap.set_transient(dt);
    cap.set_integration_method(1);

    cap.init_dc_state_gear(0.0, 0.0, 0.0, 0.0);

    std::vector<double> voltages = {1.0};
    std::vector<double> rhs(1, 0.0);
    cap.evaluate(voltages, mat, rhs);

    // G_eq = 3 * 1e-6 / (2 * 1e-6) = 1.5
    EXPECT_NEAR(mat.value(pattern.offset(0, 0)), 1.5, 1e-12);
}

TEST(GearCapacitor, FallsBackToBEOnFirstStep) {
    Capacitor cap("C1", 0, -1, 1e-6);
    SparsityBuilder builder(1);
    cap.stamp_pattern(builder);
    auto pattern = builder.build();
    NumericMatrix mat(pattern);
    cap.assign_offsets(pattern);

    double dt = 1e-6;
    cap.set_transient(dt);
    cap.set_integration_method(1);

    std::vector<double> dc_sol = {0.0};
    cap.init_dc_state(dc_sol);

    std::vector<double> voltages = {1.0};
    std::vector<double> rhs(1, 0.0);
    cap.evaluate(voltages, mat, rhs);

    // Backward Euler fallback (order 1): G_eq = C/dt = 1.0
    EXPECT_NEAR(mat.value(pattern.offset(0, 0)), 1.0, 1e-12);
}

TEST(GearCapacitor, AcceptStepBuildsHistory) {
    Capacitor cap("C1", 0, -1, 1e-6);
    SparsityBuilder builder(1);
    cap.stamp_pattern(builder);
    auto pattern = builder.build();
    cap.assign_offsets(pattern);

    double dt = 1e-6;
    cap.set_transient(dt);
    cap.set_integration_method(1);

    std::vector<double> dc_sol = {0.0};
    cap.init_dc_state(dc_sol);

    cap.accept_step(1.0);

    NumericMatrix mat(pattern);
    std::vector<double> voltages = {2.0};
    std::vector<double> rhs(1, 0.0);
    cap.evaluate(voltages, mat, rhs);

    // Now Gear: G_eq = 1.5
    EXPECT_NEAR(mat.value(pattern.offset(0, 0)), 1.5, 1e-12);
}

TEST(GearInductor, CompanionResistance) {
    Inductor ind("L1", 0, -1, 1e-3);
    ind.set_branch_index(1);
    SparsityBuilder builder(2);
    ind.stamp_pattern(builder);
    auto pattern = builder.build();
    NumericMatrix mat(pattern);
    ind.assign_offsets(pattern);

    double dt = 1e-6;
    ind.set_transient(dt);
    ind.set_integration_method(1);

    ind.init_dc_state_gear(0.0, 0.0, 0.0, 0.0);

    std::vector<double> voltages = {0.0, 0.0};
    std::vector<double> rhs(2, 0.0);
    ind.evaluate(voltages, mat, rhs);

    // R_eq = 3 * 1e-3 / (2 * 1e-6) = 1500, stamped as -1500
    EXPECT_NEAR(mat.value(pattern.offset(1, 1)), -1500.0, 1e-6);
}

TEST(GearInductor, FallsBackToBEOnFirstStep) {
    Inductor ind("L1", 0, -1, 1e-3);
    ind.set_branch_index(1);
    SparsityBuilder builder(2);
    ind.stamp_pattern(builder);
    auto pattern = builder.build();
    NumericMatrix mat(pattern);
    ind.assign_offsets(pattern);

    double dt = 1e-6;
    ind.set_transient(dt);
    ind.set_integration_method(1);

    std::vector<double> dc_sol = {0.0, 0.0};
    ind.init_dc_state(dc_sol);

    std::vector<double> voltages = {0.0, 0.0};
    std::vector<double> rhs(2, 0.0);
    ind.evaluate(voltages, mat, rhs);

    // Backward Euler (order 1): R_eq = 1e-3/1e-6 = 1000, stamped as -1000
    EXPECT_NEAR(mat.value(pattern.offset(1, 1)), -1000.0, 1e-6);
}
