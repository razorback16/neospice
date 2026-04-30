#include "core/circuit.hpp"
#include "devices/resistor.hpp"
#include "devices/vsource.hpp"
#include <gtest/gtest.h>

using namespace neospice;

TEST(Circuit, NodeMapping) {
    Circuit ckt;
    auto n1  = ckt.node("net1");
    auto n2  = ckt.node("net2");
    auto gnd = ckt.node("0");
    EXPECT_EQ(n1,  NodeId{0});
    EXPECT_EQ(n2,  NodeId{1});
    EXPECT_EQ(gnd, GND);
    EXPECT_EQ(ckt.num_nodes(), 2);
    EXPECT_EQ(ckt.node_name(0), "net1");
    EXPECT_EQ(ckt.node_name(1), "net2");
}

TEST(Circuit, BuildAndFinalize) {
    Circuit ckt;
    auto n1 = ckt.node("net1");
    auto n1_idx = static_cast<int32_t>(n1);
    ckt.add_device(std::make_unique<Resistor>("R1", n1_idx, GROUND_INTERNAL, 1000.0));
    ckt.add_device(std::make_unique<VSource>("V1", n1_idx, GROUND_INTERNAL, 5.0));
    ckt.finalize();
    EXPECT_EQ(ckt.num_nodes(), 1);
    EXPECT_EQ(ckt.num_vars(), 2);  // 1 node + 1 branch current
    EXPECT_GT(ckt.pattern().nnz(), 0);
}

TEST(Circuit, GroundAliases) {
    Circuit ckt;
    EXPECT_EQ(ckt.node("0"),   GND);
    EXPECT_EQ(ckt.node("gnd"), GND);
    EXPECT_EQ(ckt.node("GND"), GND);
}

TEST(Circuit, DuplicateNodeReturnsExistingIndex) {
    Circuit ckt;
    auto a = ckt.node("nodeA");
    auto b = ckt.node("nodeA");
    EXPECT_EQ(a, b);
    EXPECT_EQ(ckt.num_nodes(), 1);
}

TEST(Circuit, NodeIndex) {
    Circuit ckt;
    ckt.node("n1");
    ckt.node("n2");
    EXPECT_EQ(ckt.node_index("n1"), 0);
    EXPECT_EQ(ckt.node_index("n2"), 1);
    EXPECT_EQ(ckt.node_index("0"),  GROUND_INTERNAL);
    EXPECT_THROW(ckt.node_index("nonexistent"), std::out_of_range);
}
