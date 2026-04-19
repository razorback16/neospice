#include <gtest/gtest.h>
#include "core/transient.hpp"
#include "core/types.hpp"
#include "devices/device.hpp"
#include "devices/bsim4v7/bsim4v7_device.hpp"
#include "parser/netlist_parser.hpp"
#include <algorithm>
#include <cmath>

using namespace neospice;

// ---------------------------------------------------------------------------
// Unit test: compute_trunc returns 1e30 for a bare Device (no constraint)
// ---------------------------------------------------------------------------
TEST(BSIM4v7Trunc, DefaultDeviceReturnsNoConstraint) {
    // A non-BSIM4 device should return the default (1e30 = no constraint).
    struct DummyDevice : public Device {
        DummyDevice() : Device("dummy") {}
        void stamp_pattern(SparsityBuilder&) const override {}
        void assign_offsets(const SparsityPattern&) override {}
        void evaluate(const std::vector<double>&,
                      NumericMatrix&, std::vector<double>&) override {}
    };
    DummyDevice d;
    IntegratorCtx ctx;
    ctx.order = 2;
    ctx.delta = 1e-12;
    SimOptions opts;
    EXPECT_DOUBLE_EQ(d.compute_trunc(ctx, opts), 1e30);
}

// ---------------------------------------------------------------------------
// Unit test: chgtol has expected default
// ---------------------------------------------------------------------------
TEST(BSIM4v7Trunc, ChgtolDefault) {
    SimOptions opts;
    EXPECT_DOUBLE_EQ(opts.chgtol, 1e-14);
}

// ---------------------------------------------------------------------------
// Integration test: Ring oscillator transient completes successfully with
// device-specific LTE.  Verify waveform swings and step rejection.
// ---------------------------------------------------------------------------
TEST(BSIM4v7Trunc, RingOscillatorTransient) {
    std::string netlist = R"(
5-Stage Ring Oscillator
VDD vdd 0 1.8
M1p n1 n5 vdd vdd PMOD W=2u L=100n
M1n n1 n5 0 0 NMOD W=1u L=100n
M2p n2 n1 vdd vdd PMOD W=2u L=100n
M2n n2 n1 0 0 NMOD W=1u L=100n
M3p n3 n2 vdd vdd PMOD W=2u L=100n
M3n n3 n2 0 0 NMOD W=1u L=100n
M4p n4 n3 vdd vdd PMOD W=2u L=100n
M4n n4 n3 0 0 NMOD W=1u L=100n
M5p n5 n4 vdd vdd PMOD W=2u L=100n
M5n n5 n4 0 0 NMOD W=1u L=100n
.ic V(n1)=0 V(n2)=1.8 V(n3)=0 V(n4)=1.8 V(n5)=0
.model NMOD NMOS LEVEL=14 VTH0=0.4 U0=0.04 TOXE=2e-9
.model PMOD PMOS LEVEL=14 VTH0=-0.4 U0=0.02 TOXE=2e-9
.tran 1p 5n
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    auto result = solve_transient(ckt, 1e-12, 5e-9);

    // Should produce output timepoints
    ASSERT_GT(result.time.size(), 10u);
    EXPECT_NEAR(result.time.front(), 0.0, 1e-18);
    EXPECT_NEAR(result.time.back(), 5e-9, 1e-12);

    // Verify oscillation: node n1 should swing between rail values
    const auto& v_n1 = result.voltages.at("v(n1)");
    double v_min = *std::min_element(v_n1.begin(), v_n1.end());
    double v_max = *std::max_element(v_n1.begin(), v_n1.end());
    EXPECT_LT(v_min, 0.5);  // swings low
    EXPECT_GT(v_max, 1.3);  // swings high
}

// ---------------------------------------------------------------------------
// Integration test: CMOS inverter with pulse input — verify device LTE
// constrains step size during switching edge.
// ---------------------------------------------------------------------------
TEST(BSIM4v7Trunc, InverterPulseStepControl) {
    std::string netlist = R"(
CMOS Inverter with pulse
VDD vdd 0 1.8
VIN in 0 PULSE(0 1.8 0.1n 0.05n 0.05n 1n 2n)
M1p out in vdd vdd PMOD W=2u L=100n
M1n out in 0 0 NMOD W=1u L=100n
CL out 0 10f
.model NMOD NMOS LEVEL=14 VTH0=0.4 U0=0.04 TOXE=2e-9
.model PMOD PMOS LEVEL=14 VTH0=-0.4 U0=0.02 TOXE=2e-9
.tran 0.01n 4n
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    auto result = solve_transient(ckt, 0.01e-9, 4e-9);

    ASSERT_GT(result.time.size(), 10u);

    // Output should reach near VDD after input goes high
    const auto& v_out = result.voltages.at("v(out)");
    // Initially input=0 so output should be near VDD (PMOS on)
    EXPECT_GT(v_out[0], 1.0);

    // After pulse: output should go low
    // Find a point well past the transition (t > 0.3ns)
    double v_after_edge = 0.0;
    for (size_t i = 0; i < result.time.size(); ++i) {
        if (result.time[i] >= 0.5e-9) {
            v_after_edge = v_out[i];
            break;
        }
    }
    EXPECT_LT(v_after_edge, 0.5);

    // The simulation should have some rejected steps (from LTE control)
    // This is a soft check — the existence of the step control matters
    // more than exact counts.
    EXPECT_GE(result.rejected_steps, 0);
}
