#include <gtest/gtest.h>
#include "core/transient.hpp"
#include "parser/netlist_parser.hpp"
#include <cmath>
#include <algorithm>
#include <limits>
#include <array>

using namespace neospice;

TEST(Transient, AppliesInitialBreakpointReductionWithoutSourceEvents) {
    NetlistParser parser;
    auto ckt = parser.parse("Constant source\nV1 out 0 1\nR1 out 0 1k\n.end\n");
    const auto result = solve_transient(ckt, 0.01, 0.5);
    ASSERT_TRUE(result.status.converged);
    ASSERT_GT(result.time.size(), 2u);
    EXPECT_DOUBLE_EQ(result.time[1], 5e-5);
}

TEST(Transient, SeedsOlderIntervalsWithMaximumStep) {
    class HistoryProbe : public Device {
    public:
        explicit HistoryProbe(int32_t node) : Device("history_probe"), node_(node) {}
        void stamp_pattern(SparsityBuilder& builder) const override { builder.add(node_, node_); }
        void assign_offsets(const SparsityPattern& pattern) override { off_ = pattern.offset(node_, node_); }
        void evaluate(const std::vector<double>&, NumericMatrix& mat, std::span<double> rhs) override {
            const auto& ctx = *tls_integrator_ctx;
            if (ctx.current_time > 0.0 && (times.empty() || times.back() != ctx.current_time)) {
                times.push_back(ctx.current_time);
                intervals.push_back({ctx.delta_old[0], ctx.delta_old[1], ctx.delta_old[2]});
            }
            mat.add(off_, 1.0);
            rhs[node_] += 1.0;
        }
        double compute_trunc(const IntegratorCtx& ctx, const SimOptions&) const override {
            return 2.0 * ctx.delta;
        }
        std::vector<double> times;
        std::vector<std::array<double, 3>> intervals;
    private:
        int32_t node_;
        MatrixOffset off_ = -1;
    };
    Circuit circuit;
    const auto node = static_cast<int32_t>(circuit.node("probe"));
    auto device = std::make_unique<HistoryProbe>(node);
    auto* probe = device.get();
    circuit.add_device(std::move(device));
    circuit.finalize();
    const auto result = solve_transient(circuit, 0.01, 0.5);
    ASSERT_TRUE(result.status.converged);
    ASSERT_GE(probe->intervals.size(), 3u);
    // dctran.c seeds all older intervals with maxStep, then shifts in each
    // attempted step. Startup subdivision affects slot zero only.
    const std::array<std::array<double, 3>, 3> expected{{
        {5e-5, 0.01, 0.01}, {5e-5, 5e-5, 0.01}, {1e-4, 5e-5, 5e-5}}};
    for (size_t step = 0; step < expected.size(); ++step)
        for (size_t history = 0; history < 3; ++history)
            EXPECT_NEAR(probe->intervals[step][history], expected[step][history], 1e-17)
                << "step=" << step << " history=" << history;
}

TEST(Transient, CompletesIntervalsBelowFormerAbsoluteTimeCutoff) {
    for (double stop : {1e-19, 1e-17, 1e-15}) {
        SCOPED_TRACE(stop);
        NetlistParser parser;
        auto circuit = parser.parse("Small time scale\nV1 out 0 1\nR1 out 0 1k\n.end\n");
        const auto result = solve_transient(circuit, stop / 20.0, stop);
        ASSERT_TRUE(result.status.converged);
        ASSERT_GT(result.time.size(), 2u);
        EXPECT_DOUBLE_EQ(result.time.back(), stop);
        for (double value : result.voltage("out")) EXPECT_NEAR(value, 1.0, 1e-12);
        for (double value : result.current("v1")) EXPECT_NEAR(value, -1e-3, 1e-15);
    }
}

TEST(Transient, CannotAdvanceTimeReportsFailureInsteadOfPartialSuccess) {
    for (bool no_throw : {false, true}) {
        NetlistParser parser;
        auto circuit = parser.parse("Underflowing initial interval\nV1 out 0 1\nR1 out 0 1k\n.end\n");
        circuit.options.no_throw = no_throw;
        // A positive requested interval whose startup subdivision underflows.
        // The driver must reject it, never label the DC-only prefix complete.
        const double step = std::numeric_limits<double>::denorm_min();
        if (no_throw) {
            const auto result = solve_transient(circuit, step, 32 * step);
            EXPECT_FALSE(result.status.converged);
            ASSERT_FALSE(result.status.warnings.empty());
            EXPECT_EQ(result.status.warnings.back(), "Transient timestep cannot advance time");
        } else {
            EXPECT_THROW(solve_transient(circuit, step, 32 * step), SimulationError);
        }
    }
}

TEST(Transient, ChecksDeviceLTEOnSecondCandidate) {
    class StepProbe : public Device {
    public:
        explicit StepProbe(int32_t node) : Device("step_probe"), node_(node) {}
        void stamp_pattern(SparsityBuilder& builder) const override { builder.add(node_, node_); }
        void assign_offsets(const SparsityPattern& pattern) override { off_ = pattern.offset(node_, node_); }
        void evaluate(const std::vector<double>&, NumericMatrix& mat, std::span<double> rhs) override {
            mat.add(off_, 1.0);
            rhs[node_] += 1.0;
        }
        double compute_trunc(const IntegratorCtx& ctx, const SimOptions&) const override {
            if (ctx.order == 1 && ctx.current_time > 0.8e-4 && ctx.current_time < 1.2e-4)
                return std::min(2.5e-5, 2.0 * ctx.delta);
            return 2.0 * ctx.delta;
        }
    private:
        int32_t node_;
        MatrixOffset off_ = -1;
    };
    Circuit circuit;
    const auto node = static_cast<int32_t>(circuit.node("probe"));
    circuit.add_device(std::make_unique<StepProbe>(node));
    circuit.finalize();
    const auto result = solve_transient(circuit, 0.01, 0.5);
    ASSERT_TRUE(result.status.converged);
    ASSERT_GE(result.time.size(), 3u);
    EXPECT_NEAR(result.time[1], 5e-5, 1e-18);
    // The initial 1e-4 candidate violates the controlled device constraint;
    // it must be retried at 7.5e-5 instead of silently accepted.
    EXPECT_NEAR(result.time[2], 7.5e-5, 1e-18);
    EXPECT_GE(result.rejected_steps, 1);
}

TEST(Transient, RetainsSourceBreakpointRestartLimitAfterGrowthProposal) {
    NetlistParser parser;
    auto ckt = parser.parse("Source restart limit\n"
                         "V1 in 0 PULSE(0 1 1n 1n 1n 50n 100n)\n"
                         "R1 in 0 1k\n.tran 0.1n 20n\n.end\n");
    const auto result = solve_transient(ckt, 0.1e-9, 20e-9);
    ASSERT_TRUE(result.status.converged);
    // ngspice dctran.c caps the next proposal at 0.1*saveDelta AFTER
    // accepted-step growth. Here maxstep/saveDelta is 0.1 ns, so both pulse
    // corners must be followed by a 10 ps step, not the old doubled 20 ps.
    for (double corner : {1e-9, 2e-9}) {
        const auto point = std::find_if(result.time.begin(), result.time.end(),
            [corner](double t) { return std::abs(t - corner) < 1e-22; });
        ASSERT_NE(point, result.time.end());
        ASSERT_NE(point + 1, result.time.end());
        EXPECT_NEAR(*(point + 1) - *point, 1e-11, 1e-22);
    }
}

TEST(Transient, RetainsSecondOrderStepProposalWithoutRequiringPromotion) {
    class StepProbe : public Device {
    public:
        StepProbe(int32_t node, double ratio)
            : Device("step_probe"), node_(node), ratio_(ratio) {}
        void stamp_pattern(SparsityBuilder& builder) const override { builder.add(node_, node_); }
        void assign_offsets(const SparsityPattern& pattern) override { off_ = pattern.offset(node_, node_); }
        void evaluate(const std::vector<double>&, NumericMatrix& mat, std::span<double> rhs) override {
            mat.add(off_, 1.0);
            rhs[node_] += 1.0;
        }
        double compute_trunc(const IntegratorCtx& ctx, const SimOptions&) const override {
            if (ctx.current_time > 0.75e-4 && ctx.current_time < 1.25e-4 && ctx.order == 2)
                return ratio_ * ctx.delta;
            return 2.0 * ctx.delta;
        }
    private:
        int32_t node_;
        double ratio_;
        MatrixOffset off_ = -1;
    };

    for (double ratio : {0.95, 1.5}) {
        SCOPED_TRACE(ratio);
        Circuit circuit;
        const auto node = static_cast<int32_t>(circuit.node("probe"));
        circuit.add_device(std::make_unique<StepProbe>(node, ratio));
        circuit.finalize();
        const auto result = solve_transient(circuit, 0.01, 0.5);
        ASSERT_TRUE(result.status.converged);
        ASSERT_GE(result.time.size(), 4u);
        EXPECT_NEAR(result.time[1], 5e-5, 1e-18);
        EXPECT_NEAR(result.time[2], 1e-4, 1e-18);
        EXPECT_NEAR(result.time[3] - result.time[2], ratio * 5e-5, 1e-18);
    }
}

TEST(Transient, CapacitorCurrentDoesNotRingAfterSourceSlopeBreakpoints) {
    NetlistParser parser;
    auto ckt = parser.parse("Piecewise-linear capacitor drive\n"
        "V1 in 0 PWL(0 0 1 1 2 1 3 0 4 0)\nC1 in 0 1\n.end\n");
    const auto result = solve_transient(ckt, 0.05, 4.0);
    ASSERT_TRUE(result.status.converged);
    const auto& current = result.current("v1");
    ASSERT_EQ(current.size(), result.time.size());
    for (size_t i = 1; i < result.time.size(); ++i) {
        const double t = result.time[i];
        // The point exactly at a slope change retains the incoming slope.
        if (t == 1 || t == 2 || t == 3) continue;
        const double expected = t < 1 ? -1 : (t > 2 && t < 3 ? 1 : 0);
        EXPECT_NEAR(current[i], expected, 1e-8) << "t=" << t;
    }
}

TEST(Transient, InterpolationChangesOutputOnlyAndIncludesFinalPoint) {
    for (double stop : {1e-6, 1.003e-6}) {
        SCOPED_TRACE(stop);
        const std::string netlist = "RC interpolation\nV1 in 0 PULSE(0 5 0 1n 1n 500n 1u)\n"
            "R1 in out 1k\nC1 out 0 1n\n.end\n";
        NetlistParser parser;
        auto raw_ckt = parser.parse(netlist);
        auto interp_ckt = parser.parse(netlist);
        interp_ckt.options.interp = true;
        const auto raw = solve_transient(raw_ckt, 1e-8, stop);
        const auto sampled = solve_transient(interp_ckt, 1e-8, stop);
        ASSERT_TRUE(raw.status.converged);
        ASSERT_TRUE(sampled.status.converged);
        EXPECT_EQ(sampled.status.iterations, raw.status.iterations);
        EXPECT_EQ(sampled.rejected_steps, raw.rejected_steps);
        EXPECT_EQ(sampled.time.back(), stop);
        EXPECT_EQ(interp_ckt.integrator_ctx.integrate_method, 0);
        const auto& y = raw.voltage("out");
        for (size_t i = 0; i < sampled.time.size(); ++i) {
            const double t = sampled.time[i];
            auto found = std::lower_bound(raw.time.begin(), raw.time.end(), t);
            ASSERT_NE(found, raw.time.end());
            size_t hi = found - raw.time.begin();
            double expected = y[hi];
            if (hi > 0) {
                const double fraction = (t - raw.time[hi - 1]) / (raw.time[hi] - raw.time[hi - 1]);
                expected = y[hi - 1] + fraction * (y[hi] - y[hi - 1]);
            }
            EXPECT_NEAR(sampled.voltage("out")[i], expected, 1e-12);
        }
    }
}

TEST(Transient, InterpolatedGridDoesNotAccumulateTimestampDrift) {
    for (const double step : {1e-9, 1e-3}) {
        SCOPED_TRACE(step);
        NetlistParser parser;
        auto ckt = parser.parse("Output grid\nB1 out 0 V=time\nR1 out 0 1k\n.end\n");
        ckt.options.interp = true;
        const size_t intervals = 20000;
        const double stop = intervals * step;
        auto result = solve_transient(ckt, step, stop);
        ASSERT_TRUE(result.status.converged);
        ASSERT_EQ(result.time.size(), intervals + 1);
        EXPECT_EQ(result.time.front(), 0.0);
        EXPECT_EQ(result.time.back(), stop);
        const auto& voltage = result.voltage("out");
        ASSERT_EQ(voltage.size(), result.time.size());
        for (size_t i = 0; i < result.time.size(); ++i) {
            EXPECT_EQ(result.time[i], i * step);
            EXPECT_NEAR(voltage[i], result.time[i], 1e-12 * stop);
            if (i > 0) EXPECT_GT(result.time[i], result.time[i - 1]);
        }
    }
}

TEST(Transient, RCStepResponse) {
    // RC circuit: V1=5V DC, R=1k, C=1uF -> tau = 1ms
    // .ic forces v(out)=0 so we see the charging curve
    std::string netlist = R"(
RC Step Response
V1 in 0 5
R1 in out 1k
C1 out 0 1u
.ic v(out)=0
.tran 10u 5m
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    auto result = solve_transient(ckt, 10e-6, 5e-3);

    EXPECT_NEAR(result.voltage("in").front(), 5.0, 0.01);

    // Find index nearest to t=1ms
    int idx_1ms = 0;
    double best_1ms = 1e30;
    for (size_t i = 0; i < result.time.size(); ++i) {
        double d = std::abs(result.time[i] - 1e-3);
        if (d < best_1ms) { best_1ms = d; idx_1ms = static_cast<int>(i); }
    }
    double expected_1tau = 5.0 * (1.0 - std::exp(-1.0));
    EXPECT_NEAR(result.voltage("out")[idx_1ms], expected_1tau, 0.1);

    // At t=5ms (5tau): should be close to 5V
    EXPECT_NEAR(result.voltage("out").back(), 5.0, 0.05);
}

TEST(Transient, PulseSourceCompletes) {
    // PULSE source should use HARD breakpoints — verify simulation completes
    std::string netlist = R"(
PULSE breakpoint test
V1 in 0 PULSE(0 5 0 1n 1n 5u 10u)
R1 in out 1k
C1 out 0 100p
.tran 100n 50u
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    auto result = solve_transient(ckt, 100e-9, 50e-6);
    EXPECT_GT(result.time.size(), 10u);
    EXPECT_NEAR(result.time.back(), 50e-6, 1e-9);
}

TEST(Transient, SinSourceCompletes) {
    // SIN source should use SOFT breakpoints — verify simulation completes
    std::string netlist = R"(
SIN breakpoint test
V1 in 0 SIN(0 5 1MEG)
R1 in out 1k
C1 out 0 100p
.tran 100n 10u
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    auto result = solve_transient(ckt, 100e-9, 10e-6);
    EXPECT_GT(result.time.size(), 10u);
    EXPECT_NEAR(result.time.back(), 10e-6, 1e-9);
}

TEST(Transient, CustomRestartStepScale) {
    // Verify custom restart_step_scale is parsed and simulation completes
    std::string netlist = R"(
Custom restart_step_scale
V1 in 0 PULSE(0 5 0 1n 1n 5u 10u)
R1 in out 1k
C1 out 0 100p
.options restart_step_scale=0.05
.tran 100n 50u
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    EXPECT_DOUBLE_EQ(ckt.options.restart_step_scale, 0.05);
    auto result = solve_transient(ckt, 100e-9, 50e-6);
    EXPECT_GT(result.time.size(), 10u);
    EXPECT_NEAR(result.time.back(), 50e-6, 1e-9);
}

TEST(Transient, ResultHasTimeVector) {
    std::string netlist = R"(
Simple
V1 in 0 5
R1 in 0 1k
.tran 1u 10u
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    auto result = solve_transient(ckt, 1e-6, 10e-6);

    EXPECT_GE(result.time.size(), 10u);
    EXPECT_NEAR(result.time.front(), 0.0, 1e-12);
    EXPECT_NEAR(result.time.back(), 10e-6, 1e-6);
}
