#include <gtest/gtest.h>
#include "neospice/neospice.hpp"
#include "neospice/measure.hpp"
#include "devices/vsource.hpp"
#include "devices/resistor.hpp"
#include "devices/capacitor.hpp"

using namespace neospice;

// Helper: build an RC step-response circuit using a PULSE source (0 -> 5V step)
// and run transient with UIC so the capacitor starts at 0V.
static TransientResult rc_step_transient(double tstep, double tstop) {
    Circuit ckt;
    auto in = ckt.node("in");
    auto out = ckt.node("out");

    // PULSE source: 0V -> 5V step at t=0 with very fast rise time
    auto in_idx = static_cast<int32_t>(in);
    auto out_idx = static_cast<int32_t>(out);
    auto vs = std::make_unique<VSource>("V1", in_idx, GROUND_INTERNAL, 0.0);
    vs->set_pulse(PulseParams{0.0, 5.0, /*td=*/0.0, /*tr=*/1e-9, /*tf=*/1e-9,
                               /*pw=*/tstop, /*per=*/2*tstop});
    ckt.add_device(std::move(vs));
    ckt.add_device(std::make_unique<Resistor>("R1", in_idx, out_idx, 1e3));
    auto cap = std::make_unique<Capacitor>("C1", out_idx, GROUND_INTERNAL, 1e-6);
    cap->set_ic(0.0);
    ckt.add_device(std::move(cap));
    ckt.finalize();

    return solve_transient(ckt, tstep, tstop, /*uic=*/true);
}

TEST(MeasureUtils, RiseTime) {
    auto result = rc_step_transient(1e-6, 10e-3);
    // out node is node index 1 (in=0, out=1)
    auto v = result.voltage(NodeId{1});
    ASSERT_GT(v.size(), 0u);
    // Verify the waveform actually rises
    EXPECT_LT(v.front(), 1.0);
    EXPECT_GT(v.back(), 4.0);

    double rt = measure::rise_time(result, NodeId{1}, 0.5, 4.5);
    // RC = 1e3 * 1e-6 = 1ms, rise time (10%-90%) ~ 2.2*RC = 2.2ms
    EXPECT_GT(rt, 1e-3);
    EXPECT_LT(rt, 5e-3);
}

TEST(MeasureUtils, SettlingTime) {
    auto result = rc_step_transient(1e-6, 20e-3);
    double st = measure::settling_time(result, NodeId{1}, 5.0, 0.05);
    // Should settle within ~5*RC = 5ms, but definitely before 10ms
    EXPECT_GT(st, 1e-3);
    EXPECT_LT(st, 10e-3);
}

TEST(MeasureUtils, Overshoot) {
    // For a simple RC (first-order), there's no overshoot, so it should be <= 0%
    auto result = rc_step_transient(1e-6, 10e-3);
    double os = measure::overshoot(result, NodeId{1}, 5.0);
    EXPECT_LE(os, 1.0);  // <= 1% for first-order system
}

TEST(MeasureUtils, RMS) {
    Circuit ckt;
    auto in = ckt.node("in");
    ckt.V("V1", in, GND, 5.0);
    ckt.R("R1", in, GND, 1e3);
    ckt.finalize();
    auto result = solve_transient(ckt, 1e-6, 1e-3);
    double rms_val = measure::rms(result, in, 0.0, 1e-3);
    EXPECT_NEAR(rms_val, 5.0, 0.01);
}

TEST(MeasureUtils, Bandwidth3dB) {
    Circuit ckt;
    auto in = ckt.node("in");
    auto out = ckt.node("out");
    ckt.V("V1", in, GND, 0.0, 1.0);  // ac=1
    ckt.R("R1", in, out, 1e3);
    ckt.C("C1", out, GND, 1e-9);
    ckt.finalize();
    auto result = solve_ac(ckt, ACMode::DEC, 100, 1e3, 1e9);
    double bw = measure::bandwidth_3db(result, out);
    double expected = 1.0 / (2.0 * M_PI * 1e3 * 1e-9);  // ~159kHz
    EXPECT_NEAR(bw, expected, expected * 0.1);  // within 10%
}
