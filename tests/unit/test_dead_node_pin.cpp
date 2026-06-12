#include <gtest/gtest.h>
#include "core/dc.hpp"
#include "core/circuit.hpp"
#include "api/neospice.hpp"

using namespace neospice;

// A node connected ONLY to a zero-valued current source has no DC current path
// and therefore no organic matrix entry — the Jacobian is structurally
// singular at that node.  ngspice always allocates a per-node diagonal and its
// direct solver settles such a node to exactly 0 V; neospice must match by
// pinning the structurally-isolated ("dead") node with a negligible gmin.
//
// This mirrors the malformed `IOS 1 75E-9` line inside the AD8631_AD subckt of
// spice_complete/analog.lib, which both ngspice and neospice parse as a current
// source whose value token is missing (node "75e-9", DC 0 assumed), leaving a
// floating node that previously made neospice's DC solve fail at iteration 0.
TEST(DeadNodePin, ZeroCurrentSourceFloatingNodeConverges) {
    std::string netlist = R"(
Dead-node pin regression
V1 in 0 DC 1.0
R1 in out 1k
R2 out 0 1k
* Current source whose value token is missing -> node "dead", DC 0 assumed.
* "dead" is connected to nothing else: a structurally isolated node.
I1 dead 0
.op
.end
)";
    Simulator sim;
    auto ckt = sim.parse(netlist);
    DCResult result = sim.run_dc(ckt);

    // Live divider unaffected: out = 1.0 * (1k / 2k) = 0.5 V.
    EXPECT_NEAR(result.voltage("out"), 0.5, 1e-9);
    // The isolated node is pinned to exactly 0 V (matches ngspice).
    EXPECT_NEAR(result.voltage("dead"), 0.0, 1e-9);
}

// A node connected only to a voltage source owns off-diagonal (node,branch)
// entries — it is coupled to the system and must NOT be treated as dead, so its
// voltage is taken from the source, not pinned to 0 V.  This guards against the
// over-broad "no diagonal => dead" classification that perturbed i(Vsource).
TEST(DeadNodePin, VoltageSourceNodeNotPinned) {
    std::string netlist = R"(
Voltage-source node is not dead
V1 a 0 DC 3.0
R1 a 0 2k
.op
.end
)";
    Simulator sim;
    auto ckt = sim.parse(netlist);
    DCResult result = sim.run_dc(ckt);

    // Node 'a' is held at 3 V by the source (it has off-diagonal coupling and
    // is therefore not a dead node).
    EXPECT_NEAR(result.voltage("a"), 3.0, 1e-9);
    // Source current = -V/R = -1.5 mA; the dead-node pin must not perturb it.
    EXPECT_NEAR(result.current("v1"), -1.5e-3, 1e-9);
}
