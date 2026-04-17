#include <gtest/gtest.h>
#include "api/neospice.hpp"
#include "core/circuit.hpp"

namespace neospice {

// Verify that a BSIM4 device with RDSMOD=1 allocates internal nodes
// and produces a Circuit with more MNA vars than external nodes alone.
TEST(InternalNodes, RdsModAllocatesInternalDrainSource) {
    Simulator sim;
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/nmos_rdsmod.cir";
    auto ckt = sim.load(path);
    // nmos_rdsmod.cir has nodes: drain, gate, 0 (ground) -> 2 external nodes.
    // RDSMOD=1 allocates dNodePrime + sNodePrime -> 2 internal nodes.
    // VSource adds 2 branch vars.  Total MNA vars = 2 + 2 + 2 = 6.
    // Without internal nodes it would be 2 + 2 = 4.
    EXPECT_GT(ckt.num_vars(), 4)
        << "RDSMOD=1 should allocate internal drain/source nodes";
}

TEST(InternalNodes, RgateModAllocatesInternalGate) {
    Simulator sim;
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/nmos_rgatemod.cir";
    auto ckt = sim.load(path);
    // drain, gate = 2 external nodes + 1 internal (gNodePrime) + 2 VSource branches = 5
    EXPECT_GT(ckt.num_vars(), 4)
        << "RGATEMOD=1 should allocate internal gate node";
}

TEST(InternalNodes, IntrinsicPathNoExtraNodes) {
    Simulator sim;
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/nmos_iv.cir";
    auto ckt = sim.load(path);
    // nmos_iv.cir: drain, gate -> 2 external nodes, 2 VSource branches.
    EXPECT_EQ(ckt.num_vars(), 4)
        << "Intrinsic path (no resistance models) should not add internal nodes";
}

} // namespace neospice
