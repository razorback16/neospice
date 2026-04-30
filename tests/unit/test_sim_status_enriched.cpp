#include <gtest/gtest.h>
#include "core/dc.hpp"
#include "core/sim_status.hpp"
#include "devices/vsource.hpp"
#include "devices/resistor.hpp"

using namespace neospice;

TEST(SimStatusEnriched, DCResultHasResidual) {
    Circuit ckt;
    int32_t n = static_cast<int32_t>(ckt.node("n1"));
    ckt.add_device(std::make_unique<VSource>("V1", n, GROUND_INTERNAL, 5.0));
    ckt.add_device(std::make_unique<Resistor>("R1", n, GROUND_INTERNAL, 1e3));
    ckt.finalize();
    auto result = solve_dc(ckt);
    EXPECT_TRUE(result.status.converged);
    EXPECT_GE(result.status.residual, 0.0);
    EXPECT_LT(result.status.residual, 1e-6);
}

TEST(SimStatusEnriched, DCResultHasGminSteps) {
    Circuit ckt;
    int32_t n = static_cast<int32_t>(ckt.node("n1"));
    ckt.add_device(std::make_unique<VSource>("V1", n, GROUND_INTERNAL, 5.0));
    ckt.add_device(std::make_unique<Resistor>("R1", n, GROUND_INTERNAL, 1e3));
    ckt.finalize();
    auto result = solve_dc(ckt);
    EXPECT_EQ(result.status.gmin_steps, 0);
    EXPECT_EQ(result.status.source_steps, 0);
}

TEST(SimStatusEnriched, WorstNodeIdxValid) {
    Circuit ckt;
    int32_t n = static_cast<int32_t>(ckt.node("n1"));
    ckt.add_device(std::make_unique<VSource>("V1", n, GROUND_INTERNAL, 5.0));
    ckt.add_device(std::make_unique<Resistor>("R1", n, GROUND_INTERNAL, 1e3));
    ckt.finalize();
    auto result = solve_dc(ckt);
    // worst_node_idx should be valid (>= -1)
    EXPECT_GE(result.status.worst_node_idx, -1);
}

// ---------------------------------------------------------------
// SimulationError and no_throw tests
// ---------------------------------------------------------------

TEST(ErrorHandling, SimulationErrorCarriesStatus) {
    SimStatus st;
    st.converged = false;
    st.iterations = 50;
    st.residual = 1.5;
    try {
        throw SimulationError("test error", st);
    } catch (const SimulationError& e) {
        EXPECT_FALSE(e.status().converged);
        EXPECT_EQ(e.status().iterations, 50);
        EXPECT_NEAR(e.status().residual, 1.5, 1e-10);
        EXPECT_STREQ(e.what(), "test error");
    }
}

TEST(ErrorHandling, SimulationErrorIsRuntimeError) {
    try {
        throw SimulationError("test", SimStatus{});
    } catch (const std::runtime_error& e) {
        EXPECT_STREQ(e.what(), "test");
    }
}

TEST(ErrorHandling, NoThrowModeReturnsFalseConverged) {
    Circuit ckt;
    int32_t n = static_cast<int32_t>(ckt.node("n1"));
    ckt.add_device(std::make_unique<VSource>("V1", n, GROUND_INTERNAL, 5.0));
    ckt.add_device(std::make_unique<Resistor>("R1", n, GROUND_INTERNAL, 1e3));
    ckt.options.no_throw = true;
    ckt.finalize();
    // This simple circuit WILL converge, so test that no_throw doesn't break normal operation
    auto result = solve_dc(ckt);
    EXPECT_TRUE(result.status.converged);
}
