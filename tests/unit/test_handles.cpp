#include <gtest/gtest.h>
#include "neospice/types.hpp"
#include <unordered_set>

using namespace neospice;

TEST(HandleTypes, GNDValueMatchesGroundInternal) {
    EXPECT_EQ(static_cast<int32_t>(GND), -1);
}

TEST(HandleTypes, NodeIdComparable) {
    NodeId a{0}, b{1}, c{0};
    EXPECT_NE(a, b);
    EXPECT_EQ(a, c);
}

TEST(HandleTypes, DevIdComparable) {
    DevId a{0}, b{1};
    EXPECT_NE(a, b);
}

TEST(HandleTypes, ModelIdComparable) {
    ModelId a{0}, b{0};
    EXPECT_EQ(a, b);
}

TEST(HandleTypes, NodeIdHashable) {
    std::unordered_set<NodeId, NodeIdHash> s;
    s.insert(NodeId{0});
    s.insert(NodeId{1});
    s.insert(NodeId{0});
    EXPECT_EQ(s.size(), 2u);
}

TEST(HandleTypes, DevIdHashable) {
    std::unordered_set<DevId, DevIdHash> s;
    s.insert(DevId{0});
    s.insert(DevId{1});
    EXPECT_EQ(s.size(), 2u);
}
