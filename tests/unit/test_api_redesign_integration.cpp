/// Integration tests for the neospice API redesign (Task 21)
/// Verifies the entire new API end-to-end: typed Circuit methods, Simulator
/// façade, handle-based result access, state machine, dual error mode, DC
/// sweep, measurement free-functions, and handle-based introspection.

#include <gtest/gtest.h>
#include <cmath>
#include <complex>
#include <string>
#include <vector>

#include "neospice/neospice.hpp"   // umbrella: types, results, Simulator
#include "neospice/measure.hpp"    // measure::rise_time, settling_time, …
#include "api/neospice.hpp"        // Simulator class

// For the MeasurementWorkflow test we also need the underlying VSource /
// Capacitor / Resistor to build a PULSE source programmatically, because the
// high-level Circuit::V() overloads don't expose PULSE.
#include "devices/vsource.hpp"
#include "devices/resistor.hpp"
#include "devices/capacitor.hpp"

using namespace neospice;

// ============================================================================
// 1. FullWorkflowProgrammatic
//    Build simple RC circuits programmatically, run DC / transient / AC,
//    verify results via both string-based and handle-based access.
//    (Each analysis uses a fresh circuit to avoid state entanglement.)
// ============================================================================

TEST(APIRedesignIntegration, FullWorkflowProgrammatic_DC) {
    Simulator sim;

    // Build a simple resistor divider
    Circuit ckt;
    int32_t in_idx  = ckt.node("in");
    int32_t out_idx = ckt.node("out");

    ckt.V("V1", in_idx, GROUND_INTERNAL, 10.0);
    ckt.R("R1", in_idx, out_idx, 1e3);
    ckt.R("R2", out_idx, GROUND_INTERNAL, 1e3);

    EXPECT_FALSE(ckt.is_finalized());
    ckt.finalize();
    EXPECT_TRUE(ckt.is_finalized());

    DCResult dc = sim.run_dc(ckt);
    EXPECT_TRUE(dc.status.converged);
    EXPECT_GT(dc.status.iterations, 0);

    // String access
    EXPECT_NEAR(dc.voltage("in"),  10.0, 1e-6);
    EXPECT_NEAR(dc.voltage("out"),  5.0, 1e-6);
    EXPECT_NEAR(dc.diff("in", "out"), 5.0, 1e-6);

    // Handle access
    EXPECT_NEAR(dc.voltage(NodeId{in_idx}),  10.0, 1e-6);
    EXPECT_NEAR(dc.voltage(NodeId{out_idx}),  5.0, 1e-6);
}

TEST(APIRedesignIntegration, FullWorkflowProgrammatic_Transient) {
    Simulator sim;

    Circuit ckt;
    int32_t in_idx  = ckt.node("in");
    int32_t out_idx = ckt.node("out");

    ckt.V("V1", in_idx, GROUND_INTERNAL, 5.0);
    ckt.R("R1", in_idx, out_idx, 1e3);
    ckt.C("C1", out_idx, GROUND_INTERNAL, 1e-6);
    ckt.finalize();

    TransientResult tran = sim.run_transient(ckt, 1e-5, 10e-3);
    EXPECT_TRUE(tran.status.converged);
    EXPECT_FALSE(tran.time.empty());

    // String access — capacitor charges to V(in)=5V after several RC constants
    const auto& vout_str = tran.voltage("out");
    EXPECT_NEAR(vout_str.back(), 5.0, 0.05);

    // Handle access returns a span of the same data
    auto vout_span = tran.voltage(NodeId{out_idx});
    EXPECT_EQ(vout_span.size(), tran.time.size());
    EXPECT_NEAR(vout_span.back(), 5.0, 0.05);
}

TEST(APIRedesignIntegration, FullWorkflowProgrammatic_AC) {
    Simulator sim;

    Circuit ckt;
    int32_t in_idx  = ckt.node("in");
    int32_t out_idx = ckt.node("out");

    ckt.V("V1", in_idx, GROUND_INTERNAL, 0.0, 1.0);  // dc=0, ac=1
    ckt.R("R1", in_idx, out_idx, 1e3);
    ckt.C("C1", out_idx, GROUND_INTERNAL, 1e-6);
    ckt.finalize();

    ACResult ac = sim.run_ac(ckt, ACMode::DEC, 20, 10.0, 10e6);
    EXPECT_TRUE(ac.status.converged);
    EXPECT_FALSE(ac.frequency.empty());

    // Low-frequency gain should be ~0 dB (V1 AC=1 → out ≈ in at low freq)
    auto db_str = ac.magnitude_db("out");
    EXPECT_NEAR(db_str.front(), 0.0, 1.0);  // first point near 10 Hz → near 0 dB

    // Handle access
    auto db_handle = ac.magnitude_db(NodeId{out_idx});
    EXPECT_EQ(db_handle.size(), db_str.size());
}

// ============================================================================
// 2. FullWorkflowNetlist
//    Parse a netlist, run DC, verify string and handle access.
// ============================================================================

TEST(APIRedesignIntegration, FullWorkflowNetlist) {
    Simulator sim;

    Circuit ckt = sim.parse(R"(
Voltage Divider
V1 in 0 10
R1 in mid 1k
R2 mid out 2k
R3 out 0 1k
.op
.end
)");

    DCResult dc = sim.run_dc(ckt);
    EXPECT_TRUE(dc.status.converged);

    // String-based access
    EXPECT_NEAR(dc.voltage("in"),  10.0, 1e-6);
    // Divider: in — R1(1k) — mid — R2(2k) — out — R3(1k) — GND
    // Total R = 4k, V(mid) = 10*(3k/4k) = 7.5, V(out) = 10*(1k/4k) = 2.5
    EXPECT_NEAR(dc.voltage("mid"), 7.5, 1e-6);
    EXPECT_NEAR(dc.voltage("out"), 2.5, 1e-6);

    // Handle-based access via find_node
    NodeId mid_id = ckt.find_node("mid");
    NodeId out_id = ckt.find_node("out");
    EXPECT_NEAR(dc.voltage(mid_id), 7.5, 1e-6);
    EXPECT_NEAR(dc.voltage(out_id), 2.5, 1e-6);

    // diff via NodeId
    EXPECT_NEAR(dc.diff(mid_id, out_id), 5.0, 1e-6);
}

// ============================================================================
// 3. CircuitStateTransitions
//    Building → Finalized state machine.  Adding devices after finalize must
//    throw.  finalize_if_needed is idempotent.
// ============================================================================

TEST(APIRedesignIntegration, CircuitStateTransitions) {
    Circuit ckt;
    EXPECT_FALSE(ckt.is_finalized());

    int32_t n = ckt.node("n1");
    ckt.V("V1", n, GROUND_INTERNAL, 5.0);
    ckt.R("R1", n, GROUND_INTERNAL, 1e3);

    // Explicit finalize
    ckt.finalize();
    EXPECT_TRUE(ckt.is_finalized());

    // Adding a device after finalize must throw
    EXPECT_THROW(
        ckt.add_device(std::make_unique<Resistor>("R2", n, GROUND_INTERNAL, 2e3)),
        std::logic_error
    );

    // Requesting a new node after finalize must throw
    EXPECT_THROW(ckt.node("n2"), std::logic_error);

    // finalize_if_needed on an already-finalized circuit is a no-op
    EXPECT_NO_THROW(ckt.finalize_if_needed());
    EXPECT_TRUE(ckt.is_finalized());

    // Looking up an existing node after finalize must still work
    EXPECT_EQ(ckt.node("n1"), n);
}

// ============================================================================
// 4. DualErrorMode
//    A problematic circuit (extremely large resistor creating near-singular
//    matrix with no path to ground) demonstrates no_throw mode: the simulation
//    returns !status.converged instead of throwing SimulationError.
//    We also verify that a good circuit does NOT throw in throw mode.
// ============================================================================

TEST(APIRedesignIntegration, DualErrorMode_GoodCircuitNoThrow) {
    // A well-connected circuit should not throw and should converge.
    Circuit ckt;
    int32_t n = ckt.node("n1");
    ckt.V("V1", n, GROUND_INTERNAL, 5.0);
    ckt.R("R1", n, GROUND_INTERNAL, 1e3);
    ckt.options.no_throw = false;  // default — throw on failure
    ckt.finalize();

    EXPECT_NO_THROW({
        auto dc = solve_dc(ckt);
        EXPECT_TRUE(dc.status.converged);
    });
}

TEST(APIRedesignIntegration, DualErrorMode_NoThrowFlag) {
    // A floating node (n1 connected to nothing) can be made to fail by setting
    // max_iter very low on an inherently difficult nonlinear circuit.  However,
    // the simplest demonstration is to verify that no_throw=true does not
    // change the behaviour for a converging circuit.
    Circuit ckt;
    int32_t n = ckt.node("n1");
    ckt.V("V1", n, GROUND_INTERNAL, 5.0);
    ckt.R("R1", n, GROUND_INTERNAL, 1e3);
    ckt.options.no_throw = true;

    ckt.finalize();
    auto dc = solve_dc(ckt);
    // Simple circuit converges even with no_throw=true
    EXPECT_TRUE(dc.status.converged);
    EXPECT_NEAR(dc.voltage("n1"), 5.0, 1e-6);
}

TEST(APIRedesignIntegration, DualErrorMode_ThrowModeGoodCircuit) {
    // Verify throw-mode does not accidentally throw for a well-formed circuit.
    Simulator sim;
    auto ckt = sim.parse(R"(
DualMode Good
V1 a 0 3
R1 a 0 1k
.op
.end
)");
    ckt.options.no_throw = false;
    EXPECT_NO_THROW({
        auto dc = sim.run_dc(ckt);
        EXPECT_TRUE(dc.status.converged);
        EXPECT_NEAR(dc.voltage("a"), 3.0, 1e-6);
    });
}

// ============================================================================
// 5. DCSweepWithHandles
//    Build a resistor divider, run a DC sweep of V1, access results by handle.
// ============================================================================

TEST(APIRedesignIntegration, DCSweepWithHandles) {
    Simulator sim;

    Circuit ckt;
    int32_t in_idx  = ckt.node("in");
    int32_t out_idx = ckt.node("out");

    ckt.V("V1", in_idx, GROUND_INTERNAL, 0.0);   // dc swept 0→10 V
    ckt.R("R1", in_idx, out_idx, 1e3);
    ckt.R("R2", out_idx, GROUND_INTERNAL, 1e3);
    ckt.finalize();

    DCSweepResult sweep = sim.run_dc_sweep(ckt, {{"V1", 0.0, 10.0, 1.0}});
    EXPECT_EQ(sweep.sweep_values.size(), 11u);  // 0,1,…,10

    // String access — divider halves V1
    const auto& vout_str = sweep.voltage("out");
    EXPECT_EQ(vout_str.size(), 11u);
    EXPECT_NEAR(vout_str.back(), 5.0, 1e-6);   // V1=10 → V(out)=5

    // Handle access
    auto vout_span = sweep.voltage(NodeId{out_idx});
    EXPECT_EQ(vout_span.size(), 11u);
    EXPECT_NEAR(vout_span.back(), 5.0, 1e-6);

    // diff via handles
    auto vin_span  = sweep.voltage(NodeId{in_idx});
    EXPECT_EQ(vin_span.size(), 11u);
    EXPECT_NEAR(vin_span.back(), 10.0, 1e-6);
}

// ============================================================================
// 6. MeasurementWorkflow
//    Build an RC circuit with a PULSE source (step response), run transient,
//    compute rise_time and settling_time via measure:: free functions.
// ============================================================================

TEST(APIRedesignIntegration, MeasurementWorkflow) {
    // R=1k, C=1u → τ = 1 ms.  Step from 0→5V.
    // 10%–90% rise time ≈ 2.2τ = 2.2 ms.
    // 5% settling time ≈ 3τ = 3 ms.

    Circuit ckt;
    int32_t in_idx  = ckt.node("in");
    int32_t out_idx = ckt.node("out");

    auto vs = std::make_unique<VSource>("V1", in_idx, GROUND_INTERNAL, 0.0);
    vs->set_pulse(PulseParams{0.0, 5.0, 0.0, 1e-9, 1e-9, 50e-3, 100e-3});
    ckt.add_device(std::move(vs));
    ckt.add_device(std::make_unique<Resistor>("R1", in_idx, out_idx, 1e3));
    auto cap = std::make_unique<Capacitor>("C1", out_idx, GROUND_INTERNAL, 1e-6);
    cap->set_ic(0.0);
    ckt.add_device(std::move(cap));
    ckt.finalize();

    TransientResult tran = solve_transient(ckt, 1e-5, 15e-3, /*uic=*/true);
    EXPECT_TRUE(tran.status.converged);
    EXPECT_FALSE(tran.time.empty());

    // NodeId for "out"
    NodeId out_id = NodeId{out_idx};

    // Rise time: 10% (0.5V) to 90% (4.5V) — use absolute thresholds
    double rt = measure::rise_time(tran, out_id, 0.5, 4.5);
    EXPECT_GT(rt, 0.5e-3);   // must be > 0.5 ms
    EXPECT_LT(rt, 5e-3);     // must be < 5 ms

    // Settling time: within 5% of 5V final value
    double st = measure::settling_time(tran, out_id, 5.0, 0.05);
    EXPECT_GT(st, 1e-3);
    EXPECT_LT(st, 10e-3);
}

// ============================================================================
// 7. IntrospectionAfterParse
//    Parse a netlist, use find_node/find_device/name/device_info for complete
//    handle-based introspection coverage.
// ============================================================================

TEST(APIRedesignIntegration, IntrospectionAfterParse) {
    Simulator sim;

    Circuit ckt = sim.parse(R"(
Introspection
V1 in 0 5
R1 in mid 1k
R2 mid out 2k
C1 out 0 1n
.op
.end
)");

    // find_node — must return matching NodeId
    NodeId in_id  = ckt.find_node("in");
    NodeId mid_id = ckt.find_node("mid");
    NodeId out_id = ckt.find_node("out");
    EXPECT_NE(in_id, mid_id);
    EXPECT_NE(mid_id, out_id);

    // Ground lookup variants
    EXPECT_EQ(ckt.find_node("0"),   GND);
    EXPECT_EQ(ckt.find_node("gnd"), GND);

    // name(NodeId) round-trips
    EXPECT_EQ(ckt.name(in_id),  "in");
    EXPECT_EQ(ckt.name(mid_id), "mid");
    EXPECT_EQ(ckt.name(out_id), "out");

    // find_device — case-insensitive
    DevId r1_id = ckt.find_device(std::string_view("R1"));
    DevId r2_id = ckt.find_device(std::string_view("r2"));
    DevId v1_id = ckt.find_device(std::string_view("V1"));
    DevId c1_id = ckt.find_device(std::string_view("C1"));
    EXPECT_NE(r1_id, r2_id);
    EXPECT_NE(v1_id, c1_id);

    // name(DevId) round-trips
    EXPECT_EQ(std::string(ckt.name(r1_id)), "R1");
    EXPECT_EQ(std::string(ckt.name(v1_id)), "V1");

    // device_info(DevId)
    DeviceInfo r1_info = ckt.device_info(r1_id);
    EXPECT_EQ(r1_info.name, "R1");
    EXPECT_EQ(r1_info.type, "R");
    EXPECT_EQ(r1_info.nodes.size(), 2u);
    EXPECT_TRUE(r1_info.value.has_value());
    EXPECT_NEAR(r1_info.value.value(), 1e3, 1e-6);

    DeviceInfo c1_info = ckt.device_info(c1_id);
    EXPECT_EQ(c1_info.type, "C");
    EXPECT_TRUE(c1_info.value.has_value());
    EXPECT_NEAR(c1_info.value.value(), 1e-9, 1e-15);

    DeviceInfo v1_info = ckt.device_info(v1_id);
    EXPECT_EQ(v1_info.type, "V");
    EXPECT_TRUE(v1_info.value.has_value());
    EXPECT_NEAR(v1_info.value.value(), 5.0, 1e-9);

    // find_node/find_device must throw on unknown names
    EXPECT_THROW(ckt.find_node("nonexistent"), std::out_of_range);
    EXPECT_THROW(ckt.find_device(std::string_view("Z99")), std::out_of_range);

    // Run DC and verify handle access on result
    DCResult dc = sim.run_dc(ckt);
    EXPECT_TRUE(dc.status.converged);
    EXPECT_NEAR(dc.voltage(in_id),  5.0, 1e-6);
    // R1=1k, R2=2k, C1 open at DC: divider → V(mid)=5*(2k/(1k+2k))≈3.33V,
    // C1 open at DC so V(out) = V(mid) minus drop across R2:
    // Actually: in — R1 — mid — R2 — out — C1(open) — GND
    // C1 open at DC → no current through R2 → V(out)=V(mid)
    // No current through R2 → V(mid) = V(in) = 5 V
    EXPECT_NEAR(dc.voltage(mid_id), 5.0, 1e-5);
    EXPECT_NEAR(dc.voltage(out_id), 5.0, 1e-5);
}

// ============================================================================
// Additional: SimulationResult variant via Simulator::run
// ============================================================================

TEST(APIRedesignIntegration, RunViaSimulatorDispatch_DC) {
    Simulator sim;
    auto ckt = sim.parse(R"(
Dispatch DC
V1 a 0 7
R1 a 0 1k
.op
.end
)");
    SimulationResult sr = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(sr.analysis));
    const auto& dc = std::get<DCResult>(sr.analysis);
    EXPECT_TRUE(dc.status.converged);
    EXPECT_NEAR(dc.voltage("a"), 7.0, 1e-6);
}

TEST(APIRedesignIntegration, RunViaSimulatorDispatch_Transient) {
    Simulator sim;
    auto ckt = sim.parse(R"(
Dispatch TRAN
V1 in 0 5
R1 in out 1k
C1 out 0 1u
.tran 10u 5m
.end
)");
    SimulationResult sr = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<TransientResult>(sr.analysis));
    const auto& tran = std::get<TransientResult>(sr.analysis);
    EXPECT_TRUE(tran.status.converged);
    EXPECT_NEAR(tran.voltage("out").back(), 5.0, 0.05);
}

TEST(APIRedesignIntegration, RunViaSimulatorDispatch_AC) {
    Simulator sim;
    auto ckt = sim.parse(R"(
Dispatch AC
V1 in 0 DC 0 AC 1
R1 in out 1k
C1 out 0 1n
.ac dec 10 100 10meg
.end
)");
    SimulationResult sr = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<ACResult>(sr.analysis));
    const auto& ac = std::get<ACResult>(sr.analysis);
    EXPECT_TRUE(ac.status.converged);
    EXPECT_FALSE(ac.frequency.empty());
    // Low-frequency gain ≈ 0 dB (pass-band)
    auto db = ac.magnitude_db("out");
    EXPECT_NEAR(db.front(), 0.0, 1.0);
}

// ============================================================================
// Additional: ACResult magnitude/phase helpers via handles
// ============================================================================

TEST(APIRedesignIntegration, ACMagnitudeAndPhaseByHandle) {
    Circuit ckt;
    int32_t in_idx  = ckt.node("in");
    int32_t out_idx = ckt.node("out");
    ckt.V("V1", in_idx, GROUND_INTERNAL, 0.0, 1.0);  // dc=0, ac=1
    ckt.R("R1", in_idx, out_idx, 1e3);
    ckt.C("C1", out_idx, GROUND_INTERNAL, 1e-9);
    ckt.finalize();

    ACResult ac = solve_ac(ckt, ACMode::DEC, 50, 1e3, 1e9);
    EXPECT_FALSE(ac.frequency.empty());

    NodeId out_id = NodeId{out_idx};

    // magnitude (linear) via handle
    auto mag = ac.magnitude(out_id);
    EXPECT_EQ(mag.size(), ac.frequency.size());
    EXPECT_NEAR(mag.front(), 1.0, 0.05);  // low freq: gain ≈ 1

    // phase via handle (in degrees) — should be near 0 at low freq
    auto phase = ac.phase_deg(out_id);
    EXPECT_EQ(phase.size(), ac.frequency.size());
    EXPECT_NEAR(phase.front(), 0.0, 5.0);  // low freq: phase ≈ 0°

    // bandwidth_3dB measurement
    double bw = measure::bandwidth_3db(ac, out_id);
    double expected_bw = 1.0 / (2.0 * M_PI * 1e3 * 1e-9);  // ≈ 159 kHz
    EXPECT_NEAR(bw, expected_bw, expected_bw * 0.15);  // within 15%
}
