#include <gtest/gtest.h>
#include "core/circuit.hpp"
#include "devices/vsource.hpp"
#include "devices/resistor.hpp"

using namespace neospice;

TEST(CircuitState, NotFinalizedInitially) {
    Circuit ckt;
    EXPECT_FALSE(ckt.is_finalized());
}

TEST(CircuitState, FinalizedAfterExplicitFinalize) {
    Circuit ckt;
    auto n = ckt.node("n1");
    ckt.add_device(std::make_unique<VSource>("V1", n, GROUND_INTERNAL, 5.0));
    ckt.add_device(std::make_unique<Resistor>("R1", n, GROUND_INTERNAL, 1e3));
    ckt.finalize();
    EXPECT_TRUE(ckt.is_finalized());
}

TEST(CircuitState, FinalizeIfNeededIsIdempotent) {
    Circuit ckt;
    auto n = ckt.node("n1");
    ckt.add_device(std::make_unique<VSource>("V1", n, GROUND_INTERNAL, 5.0));
    ckt.add_device(std::make_unique<Resistor>("R1", n, GROUND_INTERNAL, 1e3));
    ckt.finalize_if_needed();
    EXPECT_TRUE(ckt.is_finalized());
    ckt.finalize_if_needed();  // should not throw or crash
    EXPECT_TRUE(ckt.is_finalized());
}

TEST(CircuitState, AddDeviceAfterFinalizeThrows) {
    Circuit ckt;
    auto n = ckt.node("n1");
    ckt.add_device(std::make_unique<VSource>("V1", n, GROUND_INTERNAL, 5.0));
    ckt.finalize();
    EXPECT_THROW(
        ckt.add_device(std::make_unique<Resistor>("R2", n, GROUND_INTERNAL, 1e3)),
        std::logic_error
    );
}

TEST(CircuitState, AddNewNodeAfterFinalizeThrows) {
    Circuit ckt;
    auto n = ckt.node("n1");
    ckt.add_device(std::make_unique<VSource>("V1", n, GROUND_INTERNAL, 5.0));
    ckt.finalize();
    EXPECT_THROW(ckt.node("new_node"), std::logic_error);
}

TEST(CircuitState, ExistingNodeLookupAfterFinalizeWorks) {
    Circuit ckt;
    auto n = ckt.node("n1");
    ckt.add_device(std::make_unique<VSource>("V1", n, GROUND_INTERNAL, 5.0));
    ckt.add_device(std::make_unique<Resistor>("R1", n, GROUND_INTERNAL, 1e3));
    ckt.finalize();
    // Looking up existing node should still work
    EXPECT_EQ(ckt.node("n1"), n);
    // Ground lookup should still work
    EXPECT_EQ(ckt.node("0"), GROUND_INTERNAL);
}
