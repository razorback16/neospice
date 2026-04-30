#include <gtest/gtest.h>
#include "core/dc.hpp"
#include "devices/vsource.hpp"
#include "devices/resistor.hpp"

using namespace neospice;

TEST(SimStatusEnriched, DCResultHasResidual) {
    Circuit ckt;
    auto n = ckt.node("n1");
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
    auto n = ckt.node("n1");
    ckt.add_device(std::make_unique<VSource>("V1", n, GROUND_INTERNAL, 5.0));
    ckt.add_device(std::make_unique<Resistor>("R1", n, GROUND_INTERNAL, 1e3));
    ckt.finalize();
    auto result = solve_dc(ckt);
    EXPECT_EQ(result.status.gmin_steps, 0);
    EXPECT_EQ(result.status.source_steps, 0);
}

TEST(SimStatusEnriched, WorstNodeIdxValid) {
    Circuit ckt;
    auto n = ckt.node("n1");
    ckt.add_device(std::make_unique<VSource>("V1", n, GROUND_INTERNAL, 5.0));
    ckt.add_device(std::make_unique<Resistor>("R1", n, GROUND_INTERNAL, 1e3));
    ckt.finalize();
    auto result = solve_dc(ckt);
    // worst_node_idx should be valid (>= -1)
    EXPECT_GE(result.status.worst_node_idx, -1);
}
