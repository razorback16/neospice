#include <gtest/gtest.h>
#include "core/topology.hpp"
#include "core/circuit.hpp"
#include "api/neospice.hpp"
#include "parser/netlist_parser.hpp"

using namespace neospice;

TEST(Topology, FloatingNodeDetected) {
    std::string netlist = R"(
Floating Node
V1 in 0 DC 1.0
R1 in float 1k
.op
.end
)";
    Simulator sim;
    auto ckt = sim.parse(netlist);
    auto diags = check_topology(ckt);
    bool found_floating = false;
    for (const auto& d : diags) {
        if (d.type == TopologyDiag::FLOATING_NODE)
            found_floating = true;
    }
    EXPECT_TRUE(found_floating);
}

TEST(Topology, VoltageSourceLoopDetected) {
    std::string netlist = R"(
Voltage Source Loop
V1 a 0 DC 1.0
V2 a b DC 2.0
V3 b 0 DC 3.0
R1 a 0 1k
.op
.end
)";
    Simulator sim;
    auto ckt = sim.parse(netlist);
    auto diags = check_topology(ckt);
    bool found_loop = false;
    for (const auto& d : diags) {
        if (d.type == TopologyDiag::VSOURCE_LOOP)
            found_loop = true;
    }
    EXPECT_TRUE(found_loop);
}

TEST(Topology, CleanCircuitNoDiags) {
    std::string netlist = R"(
Clean Circuit
V1 vdd 0 DC 5.0
R1 vdd out 1k
D1 out 0 DMOD
.model DMOD D(IS=1e-14 N=1)
.op
.end
)";
    Simulator sim;
    auto ckt = sim.parse(netlist);
    auto diags = check_topology(ckt);
    bool has_errors = false;
    for (const auto& d : diags) {
        if (d.severity == TopologyDiag::ERROR_SEV)
            has_errors = true;
    }
    EXPECT_FALSE(has_errors);
}
