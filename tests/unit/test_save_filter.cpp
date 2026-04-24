#include <gtest/gtest.h>
#include "api/neospice.hpp"
#include "parser/netlist_parser.hpp"
#include "devices/vsource.hpp"
#include "devices/resistor.hpp"
#include "devices/capacitor.hpp"
#include "devices/inductor.hpp"
#include <cmath>

using namespace neospice;
using std::make_unique;

// ─────────────────────────────────────────────────────────────────────────────
// 1.  Parser: .save tokens stored in ckt.save_signals
// ─────────────────────────────────────────────────────────────────────────────
TEST(SaveFilter, ParserStoresSignals) {
    const char* netlist = R"(
* .save parse test
V1 in 0 DC 5
R1 in out 1k
R2 out 0 1k
.save V(out) I(V1)
.op
.end
)";
    NetlistParser parser;
    Circuit ckt = parser.parse(netlist);
    ASSERT_EQ(ckt.save_signals.size(), 2u);
    EXPECT_EQ(ckt.save_signals[0], "v(out)");
    EXPECT_EQ(ckt.save_signals[1], "i(v1)");
}

// ─────────────────────────────────────────────────────────────────────────────
// 2.  DC .op: .save filters to requested signals only
// ─────────────────────────────────────────────────────────────────────────────
TEST(SaveFilter, DCSaveFilters) {
    // V1–R1–R2–R3 ladder: in → mid → out → 0
    const char* netlist = R"(
* .save filter DC test
V1 in 0 DC 5
R1 in mid 1k
R2 mid out 1k
R3 out 0 1k
.save V(out) I(V1)
.op
.end
)";
    Simulator sim;
    Circuit ckt = sim.parse(netlist);
    SimulationResult res = sim.run(ckt);

    ASSERT_TRUE(std::holds_alternative<DCResult>(res.analysis));
    const DCResult& dc = std::get<DCResult>(res.analysis);

    // Only V(out) and I(V1) should be present
    EXPECT_EQ(dc.node_voltages.count("v(out)"),  1u);
    EXPECT_EQ(dc.branch_currents.count("i(v1)"), 1u);

    // V(in) and V(mid) must be absent
    EXPECT_EQ(dc.node_voltages.count("v(in)"),  0u);
    EXPECT_EQ(dc.node_voltages.count("v(mid)"), 0u);

    // Sanity-check values: ladder divides 5V equally across 3 resistors
    EXPECT_NEAR(dc.node_voltages.at("v(out)"), 5.0 / 3.0, 1e-6);
    // current: 5V / 3kΩ ≈ 1.667 mA
    EXPECT_NEAR(std::abs(dc.branch_currents.at("i(v1)")), 5.0 / 3000.0, 1e-9);
}

// ─────────────────────────────────────────────────────────────────────────────
// 3.  DC .op: no .save → all node voltages + branch currents returned
// ─────────────────────────────────────────────────────────────────────────────
TEST(SaveFilter, DCNoSaveReturnsAll) {
    const char* netlist = R"(
* No .save: all signals
V1 in 0 DC 5
R1 in out 1k
R2 out 0 1k
.op
.end
)";
    Simulator sim;
    Circuit ckt = sim.parse(netlist);
    SimulationResult res = sim.run(ckt);

    ASSERT_TRUE(std::holds_alternative<DCResult>(res.analysis));
    const DCResult& dc = std::get<DCResult>(res.analysis);

    // All nodes (excluding ground which is index 0 — v(0) is gnd)
    EXPECT_GE(dc.node_voltages.size(), 2u);  // at least v(in), v(out)
    EXPECT_TRUE(dc.node_voltages.count("v(in)")  > 0);
    EXPECT_TRUE(dc.node_voltages.count("v(out)") > 0);

    // Branch current for V1
    EXPECT_TRUE(dc.branch_currents.count("i(v1)") > 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// 4.  DC .op via run_dc: filtering applied when save_signals set
// ─────────────────────────────────────────────────────────────────────────────
TEST(SaveFilter, RunDcFilters) {
    Circuit ckt;
    auto n_in  = ckt.node("in");
    auto n_out = ckt.node("out");
    ckt.add_device(make_unique<VSource>("V1", n_in, GROUND_INTERNAL, 5.0));
    ckt.add_device(make_unique<Resistor>("R1", n_in, n_out, 1000.0));
    ckt.add_device(make_unique<Resistor>("R2", n_out, GROUND_INTERNAL, 1000.0));
    ckt.save_signals = {"v(out)"};
    ckt.finalize();

    Simulator sim;
    DCResult dc = sim.run_dc(ckt);

    EXPECT_EQ(dc.node_voltages.count("v(out)"), 1u);
    EXPECT_EQ(dc.node_voltages.count("v(in)"),  0u);   // filtered out
    EXPECT_EQ(dc.branch_currents.count("i(v1)"), 0u);  // filtered out
    EXPECT_NEAR(dc.node_voltages.at("v(out)"), 2.5, 1e-6);
}

// ─────────────────────────────────────────────────────────────────────────────
// 5.  Transient: .save filters voltages and currents
// ─────────────────────────────────────────────────────────────────────────────
TEST(SaveFilter, TransientFilters) {
    // RC circuit: V1 – R1 – C1. Save only V(out).
    const char* netlist = R"(
* Transient .save filter
V1 in 0 DC 1
R1 in out 1k
C1 out 0 1n
.save V(out)
.tran 1n 5n
.end
)";
    Simulator sim;
    Circuit ckt = sim.parse(netlist);
    SimulationResult res = sim.run(ckt);

    ASSERT_TRUE(std::holds_alternative<TransientResult>(res.analysis));
    const TransientResult& tr = std::get<TransientResult>(res.analysis);

    // v(out) must be present
    EXPECT_TRUE(tr.voltages.count("v(out)") > 0);
    // v(in) must be absent
    EXPECT_EQ(tr.voltages.count("v(in)"), 0u);
    // No branch-current signals saved
    EXPECT_EQ(tr.currents.size(), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// 6.  Transient: no .save → all signals
// ─────────────────────────────────────────────────────────────────────────────
TEST(SaveFilter, TransientNoSaveReturnsAll) {
    const char* netlist = R"(
* Transient no .save
V1 in 0 DC 1
R1 in out 1k
C1 out 0 1n
.tran 1n 5n
.end
)";
    Simulator sim;
    Circuit ckt = sim.parse(netlist);
    SimulationResult res = sim.run(ckt);

    ASSERT_TRUE(std::holds_alternative<TransientResult>(res.analysis));
    const TransientResult& tr = std::get<TransientResult>(res.analysis);

    EXPECT_TRUE(tr.voltages.count("v(in)")  > 0);
    EXPECT_TRUE(tr.voltages.count("v(out)") > 0);
    EXPECT_TRUE(tr.currents.count("i(v1)")  > 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// 7.  AC: .save filters voltages and currents
// ─────────────────────────────────────────────────────────────────────────────
TEST(SaveFilter, ACFilters) {
    const char* netlist = R"(
* AC .save filter
V1 in 0 AC 1
R1 in out 1k
C1 out 0 1n
.save V(out) I(V1)
.ac dec 5 1 1meg
.end
)";
    Simulator sim;
    Circuit ckt = sim.parse(netlist);
    SimulationResult res = sim.run(ckt);

    ASSERT_TRUE(std::holds_alternative<ACResult>(res.analysis));
    const ACResult& ac = std::get<ACResult>(res.analysis);

    EXPECT_TRUE(ac.voltages.count("v(out)") > 0);
    EXPECT_TRUE(ac.currents.count("i(v1)")  > 0);
    EXPECT_EQ(ac.voltages.count("v(in)"),   0u);  // not saved
}

// ─────────────────────────────────────────────────────────────────────────────
// 8.  AC: no .save → all signals
// ─────────────────────────────────────────────────────────────────────────────
TEST(SaveFilter, ACNoSaveReturnsAll) {
    const char* netlist = R"(
* AC no .save
V1 in 0 AC 1
R1 in out 1k
C1 out 0 1n
.ac lin 3 100 10000
.end
)";
    Simulator sim;
    Circuit ckt = sim.parse(netlist);
    SimulationResult res = sim.run(ckt);

    ASSERT_TRUE(std::holds_alternative<ACResult>(res.analysis));
    const ACResult& ac = std::get<ACResult>(res.analysis);

    EXPECT_TRUE(ac.voltages.count("v(in)")  > 0);
    EXPECT_TRUE(ac.voltages.count("v(out)") > 0);
    EXPECT_TRUE(ac.currents.count("i(v1)")  > 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// 9.  DC Sweep: .save filters sweep results
// ─────────────────────────────────────────────────────────────────────────────
TEST(SaveFilter, DCSweepFilters) {
    const char* netlist = R"(
* DC sweep .save filter
V1 in 0 DC 0
R1 in mid 1k
R2 mid out 1k
R3 out 0 1k
.save V(out) I(V1)
.dc V1 0 3 1
.end
)";
    Simulator sim;
    Circuit ckt = sim.parse(netlist);
    SimulationResult res = sim.run(ckt);

    ASSERT_TRUE(std::holds_alternative<DCSweepResult>(res.analysis));
    const DCSweepResult& sw = std::get<DCSweepResult>(res.analysis);

    EXPECT_TRUE(sw.voltages.count("v(out)") > 0);
    EXPECT_TRUE(sw.currents.count("i(v1)")  > 0);
    EXPECT_EQ(sw.voltages.count("v(in)"),   0u);
    EXPECT_EQ(sw.voltages.count("v(mid)"),  0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// 10. DC Sweep: no .save → all signals
// ─────────────────────────────────────────────────────────────────────────────
TEST(SaveFilter, DCSweepNoSaveReturnsAll) {
    const char* netlist = R"(
* DC sweep no .save
V1 in 0 DC 0
R1 in out 1k
R2 out 0 1k
.dc V1 0 2 1
.end
)";
    Simulator sim;
    Circuit ckt = sim.parse(netlist);
    SimulationResult res = sim.run(ckt);

    ASSERT_TRUE(std::holds_alternative<DCSweepResult>(res.analysis));
    const DCSweepResult& sw = std::get<DCSweepResult>(res.analysis);

    EXPECT_TRUE(sw.voltages.count("v(in)")  > 0);
    EXPECT_TRUE(sw.voltages.count("v(out)") > 0);
    EXPECT_TRUE(sw.currents.count("i(v1)")  > 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// 11. Multiple .save lines are cumulative (both lines' signals kept)
// ─────────────────────────────────────────────────────────────────────────────
TEST(SaveFilter, MultipleSaveLines) {
    const char* netlist = R"(
* Multiple .save lines
V1 in 0 DC 5
R1 in mid 1k
R2 mid out 1k
R3 out 0 1k
.save V(mid)
.save I(V1)
.op
.end
)";
    Simulator sim;
    Circuit ckt = sim.parse(netlist);
    SimulationResult res = sim.run(ckt);

    ASSERT_TRUE(std::holds_alternative<DCResult>(res.analysis));
    const DCResult& dc = std::get<DCResult>(res.analysis);

    EXPECT_TRUE(dc.node_voltages.count("v(mid)")  > 0);
    EXPECT_TRUE(dc.branch_currents.count("i(v1)") > 0);
    EXPECT_EQ(dc.node_voltages.count("v(out)"), 0u);
    EXPECT_EQ(dc.node_voltages.count("v(in)"),  0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// 12. run_transient with save_signals set programmatically
// ─────────────────────────────────────────────────────────────────────────────
TEST(SaveFilter, RunTransientFilters) {
    Circuit ckt;
    auto n_in  = ckt.node("in");
    auto n_out = ckt.node("out");
    ckt.add_device(make_unique<VSource>("V1", n_in, GROUND_INTERNAL, 1.0));
    ckt.add_device(make_unique<Resistor>("R1", n_in, n_out, 1000.0));
    ckt.add_device(make_unique<Capacitor>("C1", n_out, GROUND_INTERNAL, 1e-9));
    ckt.save_signals = {"v(out)"};
    ckt.finalize();

    Simulator sim;
    TransientResult tr = sim.run_transient(ckt, 1e-9, 5e-9);

    EXPECT_TRUE(tr.voltages.count("v(out)") > 0);
    EXPECT_EQ(tr.voltages.count("v(in)"), 0u);
    EXPECT_EQ(tr.currents.size(), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// 13. run_ac with save_signals set programmatically
// ─────────────────────────────────────────────────────────────────────────────
TEST(SaveFilter, RunACFilters) {
    Circuit ckt;
    auto n_in  = ckt.node("in");
    auto n_out = ckt.node("out");
    auto* vs = new VSource("V1", n_in, GROUND_INTERNAL, 0.0);
    vs->set_ac(1.0, 0.0);
    ckt.add_device(std::unique_ptr<VSource>(vs));
    ckt.add_device(make_unique<Resistor>("R1", n_in, n_out, 1000.0));
    ckt.add_device(make_unique<Capacitor>("C1", n_out, GROUND_INTERNAL, 1e-9));
    ckt.save_signals = {"v(out)"};
    ckt.finalize();

    Simulator sim;
    ACResult ac = sim.run_ac(ckt, AnalysisCommand::DEC, 3, 1e3, 1e6);

    EXPECT_TRUE(ac.voltages.count("v(out)") > 0);
    EXPECT_EQ(ac.voltages.count("v(in)"), 0u);
    EXPECT_EQ(ac.currents.size(), 0u);
}
