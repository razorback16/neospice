#include "core/node_classify.hpp"
#include "core/circuit.hpp"
#include "devices/resistor.hpp"
#include "devices/vsource.hpp"
#include <gtest/gtest.h>

using namespace neospice;

// Helper: build a simple rail circuit
//   V1 vdd 0 DC 5
//   R1 vdd out 1k
// and finalize it so num_vars()/node indices are valid.
static Circuit build_rail_circuit() {
    Circuit ckt;
    auto vdd = ckt.node("vdd");
    auto out = ckt.node("out");
    auto vdd_idx = static_cast<int32_t>(vdd);
    auto out_idx = static_cast<int32_t>(out);
    ckt.add_device(std::make_unique<VSource>("V1", vdd_idx, GROUND_INTERNAL, 5.0));
    ckt.add_device(std::make_unique<Resistor>("R1", vdd_idx, out_idx, 1000.0));
    ckt.finalize();
    return ckt;
}

// 1. A node tied to ground through a DC voltage source is seeded to that
//    source's DC value; ground stays 0; unrelated nodes stay 0.
TEST(NodeClassify, SeedsRailNodeFromVSource) {
    Circuit ckt = build_rail_circuit();
    std::vector<double> guess = compute_initial_guess(ckt);

    ASSERT_EQ(static_cast<int32_t>(guess.size()), ckt.num_vars());

    int32_t vdd_idx = ckt.node_index("vdd");
    int32_t out_idx = ckt.node_index("out");

    EXPECT_NEAR(guess[vdd_idx], 5.0, 1e-12);
    // "out" is only tied through a resistor — no direct rail; stays 0.
    EXPECT_NEAR(guess[out_idx], 0.0, 1e-12);
}

// Negative-rail capture: V2 vss 0 DC -5 -> seed vss to -5.
TEST(NodeClassify, SeedsNegativeRail) {
    Circuit ckt;
    auto vss = ckt.node("vss");
    auto vss_idx = static_cast<int32_t>(vss);
    ckt.add_device(std::make_unique<VSource>("V2", vss_idx, GROUND_INTERNAL, -5.0));
    // give the node a DC path so it isn't a dead node (not strictly required)
    ckt.add_device(std::make_unique<Resistor>("R1", vss_idx, GROUND_INTERNAL, 1000.0));
    ckt.finalize();

    std::vector<double> guess = compute_initial_guess(ckt);
    int32_t vss_idx2 = ckt.node_index("vss");
    EXPECT_NEAR(guess[vss_idx2], -5.0, 1e-12);
}

// Source written with ground as the positive node: V1 0 vneg DC 5
// means vneg = -5 (V(np) - V(nn) = 5, np = gnd = 0 -> V(nn) = -5).
TEST(NodeClassify, HandlesReversedSourcePolarity) {
    Circuit ckt;
    auto vneg = ckt.node("vneg");
    auto vneg_idx = static_cast<int32_t>(vneg);
    ckt.add_device(std::make_unique<VSource>("V1", GROUND_INTERNAL, vneg_idx, 5.0));
    ckt.add_device(std::make_unique<Resistor>("R1", vneg_idx, GROUND_INTERNAL, 1000.0));
    ckt.finalize();

    std::vector<double> guess = compute_initial_guess(ckt);
    int32_t idx = ckt.node_index("vneg");
    EXPECT_NEAR(guess[idx], -5.0, 1e-12);
}

// 2. .nodeset-pinned nodes must never be clobbered by the classifier.
TEST(NodeClassify, PreservesNodesetPins) {
    Circuit ckt = build_rail_circuit();
    // Pin vdd to a different value via .nodeset; classifier must not override.
    NodeId vdd = ckt.find_node("vdd");
    ckt.nodeset[vdd] = 2.5;

    std::vector<double> guess = compute_initial_guess(ckt);
    int32_t vdd_idx = ckt.node_index("vdd");
    EXPECT_NEAR(guess[vdd_idx], 2.5, 1e-12);
}

// .ic-pinned nodes are likewise preserved.
TEST(NodeClassify, PreservesIcPins) {
    Circuit ckt = build_rail_circuit();
    NodeId vdd = ckt.find_node("vdd");
    ckt.ic[vdd] = 1.25;

    std::vector<double> guess = compute_initial_guess(ckt);
    int32_t vdd_idx = ckt.node_index("vdd");
    EXPECT_NEAR(guess[vdd_idx], 1.25, 1e-12);
}

// A non-DC source (e.g. one whose function is not plain DC) should not be
// treated as a rail anchor unless it declared an explicit DC value.
TEST(NodeClassify, NonRailNodesStayZero) {
    Circuit ckt;
    auto a = ckt.node("a");
    auto b = ckt.node("b");
    auto a_idx = static_cast<int32_t>(a);
    auto b_idx = static_cast<int32_t>(b);
    ckt.add_device(std::make_unique<Resistor>("R1", a_idx, b_idx, 1000.0));
    ckt.add_device(std::make_unique<Resistor>("R2", b_idx, GROUND_INTERNAL, 1000.0));
    ckt.finalize();

    std::vector<double> guess = compute_initial_guess(ckt);
    EXPECT_EQ(static_cast<int32_t>(guess.size()), ckt.num_vars());
    for (double v : guess) EXPECT_NEAR(v, 0.0, 1e-12);
}
