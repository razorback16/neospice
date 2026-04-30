#include <gtest/gtest.h>
#include "core/dc.hpp"
#include "core/transient.hpp"
#include "core/ac.hpp"
#include "neospice/types.hpp"
#include "devices/vsource.hpp"
#include "devices/resistor.hpp"
#include "devices/capacitor.hpp"

using namespace neospice;
using std::make_unique;

// --- DC ---

TEST(ResultHandles, DCVoltageByNodeId) {
    Circuit ckt;
    auto n_top = ckt.node("top");
    auto n_mid = ckt.node("mid");
    ckt.add_device(make_unique<VSource>("V1", n_top, GROUND_INTERNAL, 10.0));
    ckt.add_device(make_unique<Resistor>("R1", n_top, n_mid, 1000.0));
    ckt.add_device(make_unique<Resistor>("R2", n_mid, GROUND_INTERNAL, 1000.0));
    ckt.finalize();
    DCResult result = solve_dc(ckt);
    EXPECT_NEAR(result.voltage(NodeId{n_top}), 10.0, 1e-6);
    EXPECT_NEAR(result.voltage(NodeId{n_mid}), 5.0, 1e-6);
}

TEST(ResultHandles, DCVoltageByStringStillWorks) {
    Circuit ckt;
    auto n_top = ckt.node("top");
    auto n_mid = ckt.node("mid");
    ckt.add_device(make_unique<VSource>("V1", n_top, GROUND_INTERNAL, 10.0));
    ckt.add_device(make_unique<Resistor>("R1", n_top, n_mid, 1000.0));
    ckt.add_device(make_unique<Resistor>("R2", n_mid, GROUND_INTERNAL, 1000.0));
    ckt.finalize();
    DCResult result = solve_dc(ckt);
    EXPECT_NEAR(result.voltage("top"), 10.0, 1e-6);
    EXPECT_NEAR(result.voltage("mid"), 5.0, 1e-6);
}

TEST(ResultHandles, DCCurrentByDevId) {
    Circuit ckt;
    auto n_top = ckt.node("top");
    auto n_mid = ckt.node("mid");
    ckt.add_device(make_unique<VSource>("V1", n_top, GROUND_INTERNAL, 10.0));
    ckt.add_device(make_unique<Resistor>("R1", n_top, n_mid, 1000.0));
    ckt.add_device(make_unique<Resistor>("R2", n_mid, GROUND_INTERNAL, 1000.0));
    ckt.finalize();
    DCResult result = solve_dc(ckt);
    double i = result.current(DevId{0});
    EXPECT_NEAR(std::abs(i), 5e-3, 1e-9);
}

TEST(ResultHandles, DCDiffByNodeId) {
    Circuit ckt;
    auto n_top = ckt.node("top");
    auto n_mid = ckt.node("mid");
    ckt.add_device(make_unique<VSource>("V1", n_top, GROUND_INTERNAL, 10.0));
    ckt.add_device(make_unique<Resistor>("R1", n_top, n_mid, 1000.0));
    ckt.add_device(make_unique<Resistor>("R2", n_mid, GROUND_INTERNAL, 1000.0));
    ckt.finalize();
    DCResult result = solve_dc(ckt);
    EXPECT_NEAR(result.diff(NodeId{n_top}, NodeId{n_mid}), 5.0, 1e-6);
}

// --- Transient ---

TEST(ResultHandles, TransientVoltageByNodeId) {
    Circuit ckt;
    auto n_in = ckt.node("in");
    auto n_out = ckt.node("out");
    ckt.add_device(make_unique<VSource>("V1", n_in, GROUND_INTERNAL, 5.0));
    ckt.add_device(make_unique<Resistor>("R1", n_in, n_out, 1e3));
    ckt.add_device(make_unique<Capacitor>("C1", n_out, GROUND_INTERNAL, 1e-6));
    ckt.finalize();
    auto result = solve_transient(ckt, 1e-6, 5e-3);
    auto v = result.voltage(NodeId{n_out});
    EXPECT_FALSE(v.empty());
    EXPECT_NEAR(v.back(), 5.0, 0.01);
}

TEST(ResultHandles, TransientTimeSpan) {
    Circuit ckt;
    auto n_in = ckt.node("in");
    auto n_out = ckt.node("out");
    ckt.add_device(make_unique<VSource>("V1", n_in, GROUND_INTERNAL, 5.0));
    ckt.add_device(make_unique<Resistor>("R1", n_in, n_out, 1e3));
    ckt.add_device(make_unique<Capacitor>("C1", n_out, GROUND_INTERNAL, 1e-6));
    ckt.finalize();
    auto result = solve_transient(ckt, 1e-6, 5e-3);
    auto t = result.time_span();
    EXPECT_GT(t.size(), 0u);
    EXPECT_NEAR(t.front(), 0.0, 1e-12);
}

TEST(ResultHandles, TransientDiffByNodeId) {
    Circuit ckt;
    auto n_in = ckt.node("in");
    auto n_out = ckt.node("out");
    ckt.add_device(make_unique<VSource>("V1", n_in, GROUND_INTERNAL, 5.0));
    ckt.add_device(make_unique<Resistor>("R1", n_in, n_out, 1e3));
    ckt.add_device(make_unique<Capacitor>("C1", n_out, GROUND_INTERNAL, 1e-6));
    ckt.finalize();
    auto result = solve_transient(ckt, 1e-6, 5e-3);
    auto d = result.diff(NodeId{n_in}, NodeId{n_out});
    EXPECT_FALSE(d.empty());
    EXPECT_GT(d.front(), 0.0);  // V_in - V_out > 0 initially
}

// --- AC ---

TEST(ResultHandles, ACVoltageByNodeId) {
    Circuit ckt;
    auto n_in = ckt.node("in");
    auto n_out = ckt.node("out");
    auto vs = make_unique<VSource>("V1", n_in, GROUND_INTERNAL, 0.0);
    vs->set_ac(1.0, 0.0);
    ckt.add_device(std::move(vs));
    ckt.add_device(make_unique<Resistor>("R1", n_in, n_out, 1e3));
    ckt.add_device(make_unique<Capacitor>("C1", n_out, GROUND_INTERNAL, 1e-9));
    ckt.finalize();
    auto result = solve_ac(ckt, ACMode::DEC, 10, 1e3, 1e6);
    auto v = result.voltage(NodeId{n_out});
    EXPECT_FALSE(v.empty());
    EXPECT_EQ(v.size(), result.frequency.size());
}

TEST(ResultHandles, ACMagnitudeDbByNodeId) {
    Circuit ckt;
    auto n_in = ckt.node("in");
    auto n_out = ckt.node("out");
    auto vs = make_unique<VSource>("V1", n_in, GROUND_INTERNAL, 0.0);
    vs->set_ac(1.0, 0.0);
    ckt.add_device(std::move(vs));
    ckt.add_device(make_unique<Resistor>("R1", n_in, n_out, 1e3));
    ckt.add_device(make_unique<Capacitor>("C1", n_out, GROUND_INTERNAL, 1e-9));
    ckt.finalize();
    auto result = solve_ac(ckt, ACMode::DEC, 10, 1e3, 1e6);
    auto db = result.magnitude_db(NodeId{n_out});
    EXPECT_EQ(db.size(), result.frequency.size());
    EXPECT_LE(db.back(), db.front());  // RC rolloff
}

TEST(ResultHandles, ACDiffMagnitudeDb) {
    Circuit ckt;
    auto n_in = ckt.node("in");
    auto n_out = ckt.node("out");
    auto vs = make_unique<VSource>("V1", n_in, GROUND_INTERNAL, 0.0);
    vs->set_ac(1.0, 0.0);
    ckt.add_device(std::move(vs));
    ckt.add_device(make_unique<Resistor>("R1", n_in, n_out, 1e3));
    ckt.add_device(make_unique<Capacitor>("C1", n_out, GROUND_INTERNAL, 1e-9));
    ckt.finalize();
    auto result = solve_ac(ckt, ACMode::DEC, 10, 1e3, 1e6);
    auto diff_db = result.diff_magnitude_db(NodeId{n_in}, NodeId{n_out});
    EXPECT_EQ(diff_db.size(), result.frequency.size());
}

// --- DC Sweep ---

TEST(ResultHandles, DCSweepVoltageByNodeId) {
    Circuit ckt;
    auto n_in = ckt.node("in");
    auto n_out = ckt.node("out");
    ckt.add_device(make_unique<VSource>("V1", n_in, GROUND_INTERNAL, 0.0));
    ckt.add_device(make_unique<Resistor>("R1", n_in, n_out, 1e3));
    ckt.add_device(make_unique<Resistor>("R2", n_out, GROUND_INTERNAL, 1e3));
    ckt.finalize();
    auto result = solve_dc_sweep(ckt, {{"v1", 0, 10, 1}});
    auto v = result.voltage(NodeId{n_out});
    EXPECT_EQ(v.size(), result.sweep_values.size());
    EXPECT_NEAR(v.back(), 5.0, 1e-6);
}
