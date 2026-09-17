#include <gtest/gtest.h>
#include "devices/vsource.hpp"
#include "core/neo_solver.hpp"
#include "core/circuit.hpp"   // tls_integrator_ctx, IntegratorCtx
#include "core/types.hpp"
#include "devices/isource.hpp"
#include "parser/netlist_parser.hpp"
#include "parser/tokenizer.hpp"

using namespace neospice;

TEST(AMSource, ParsedOffsetsPhasesZeroFrequenciesAndDelay) {
    NetlistParser parser;
    auto circuit = parser.parse("AM parameter semantics\n"
        "V1 v 0 AM(2 3 4 0 0 5u 90 -90)\n"
        "I1 i 0 AM(2 3 4 0 0 5u 90 -90)\n"
        "R1 i 0 1\n.end\n");
    int checked = 0;
    const auto check = [&](auto& source) {
        source.resolve_defaults(1e-9, 1e-3);
        EXPECT_DOUBLE_EQ(source.value_at(0), 0.0);
        // Use the parsed boundary: the token "5u" and C++ literal 5e-6
        // can differ by one ULP after engineering-suffix multiplication.
        const double delay = parse_spice_number("5u");
        EXPECT_DOUBLE_EQ(source.value_at(delay), 0.0);
        EXPECT_NEAR(source.value_at(std::nextafter(delay, 1.0)), -5.0, 1e-14);
        EXPECT_NEAR(source.value_at(6e-6), -5.0, 1e-14);
        EXPECT_NEAR(source.value_at(1e-3), -5.0, 1e-14);
        ++checked;
    };
    for (auto& device : circuit.devices()) {
        if (auto* v = dynamic_cast<VSource*>(device.get())) check(*v);
        if (auto* i = dynamic_cast<ISource*>(device.get())) check(*i);
    }
    EXPECT_EQ(checked, 2);
}

TEST(AMSource, OmittedFrequenciesFollowEachTransientDuration) {
    NetlistParser parser;
    auto circuit = parser.parse("AM defaults\nV1 v 0 AM(0 0)\n"
        "I1 i 0 AM(0 0)\nR1 i 0 1\n.end\n");
    int checked = 0;
    const auto check = [&](auto& source) {
        for (double stop : {1.0, 2.0}) {
            source.resolve_defaults(1e-3, stop);
            // At stop/400 the default carrier is at its positive peak;
            // the default modulator phase is pi/40 and its amplitude is one.
            EXPECT_NEAR(source.value_at(stop / 400), std::sin(M_PI / 40), 1e-14);
        }
        ++checked;
    };
    for (auto& device : circuit.devices()) {
        if (auto* v = dynamic_cast<VSource*>(device.get())) check(*v);
        if (auto* i = dynamic_cast<ISource*>(device.get())) check(*i);
    }
    EXPECT_EQ(checked, 2);
}

TEST(AMSource, RequiresTwoParameters) {
    NetlistParser parser;
    EXPECT_THROW(parser.parse("Invalid AM\nV1 v 0 AM(1)\n.end\n"), ParseError);
    EXPECT_THROW(parser.parse("Invalid AM\nI1 i 0 AM(1)\nR1 i 0 1\n.end\n"), ParseError);
}

// Helper RAII guard for setting up integrator context in unit tests.
namespace {
struct VSrcIntegratorGuard {
    IntegratorCtx ctx;
    explicit VSrcIntegratorGuard(int mode) { ctx.mode = mode; tls_integrator_ctx = &ctx; }
    ~VSrcIntegratorGuard() { tls_integrator_ctx = nullptr; }
};
} // namespace

// ---------------------------------------------------------------------------
// stamp_pattern — with node0 and GROUND, branch=1 → 2x2 MNA
// Only (0,1) and (1,0) are non-ground entries.
// ---------------------------------------------------------------------------
TEST(VSource, StampPattern) {
    VSource vs("V1", 0, GROUND_INTERNAL, 5.0);
    vs.set_branch_index(1);
    SparsityBuilder builder(2);
    vs.stamp_pattern(builder);
    auto pattern = builder.build();
    EXPECT_EQ(pattern.nnz(), 2); // (0,1) and (1,0)
}

// ---------------------------------------------------------------------------
// Solve V1=5V from node0 to ground, R1=1kΩ from node0 to ground
// MNA (2x2): node0, branch
//   [G  +1] [V0  ]   [0]
//   [+1  0] [Ibr ] = [5]
// Expected: V0=5V, Ibr=-5mA
// ---------------------------------------------------------------------------
TEST(VSource, SolveWithResistor) {
    SparsityBuilder builder(2);
    builder.add(0, 0); // resistor conductance
    builder.add(0, 1); // vsource: (np, branch)
    builder.add(1, 0); // vsource: (branch, np)
    auto pattern = builder.build();

    NumericMatrix mat(pattern);
    // Resistor: G = 1/1000 at (0,0)
    mat.add(pattern.offset(0, 0), 1.0 / 1000.0);
    // VSource stamps
    mat.add(pattern.offset(0, 1), 1.0);   // (np, branch) = +1
    mat.add(pattern.offset(1, 0), 1.0);   // (branch, np) = +1

    std::vector<double> rhs = {0.0, 5.0};  // rhs[1] = V_source

    auto solver = std::make_unique<NeoSolver>();
    solver->symbolic(pattern);
    solver->numeric(pattern, mat);
    solver->solve(rhs);

    EXPECT_NEAR(rhs[0],  5.0,    1e-12);   // V0 = 5 V
    EXPECT_NEAR(rhs[1], -0.005,  1e-12);   // Ibranch = -5 mA (current into positive terminal)
}

// ---------------------------------------------------------------------------
// evaluate() should produce the same matrix/rhs as the manual stamps above
// ---------------------------------------------------------------------------
TEST(VSource, Evaluate) {
    VSource vs("V1", 0, GROUND_INTERNAL, 5.0);
    vs.set_branch_index(1);

    SparsityBuilder builder(2);
    vs.stamp_pattern(builder);
    builder.add(0, 0); // make room for resistor entry so matrix is non-singular
    auto pattern = builder.build();

    NumericMatrix mat(pattern);
    // Add resistor conductance manually
    mat.add(pattern.offset(0, 0), 1.0 / 1000.0);

    vs.assign_offsets(pattern);
    std::vector<double> voltages(2, 0.0);
    std::vector<double> rhs(2, 0.0);
    vs.evaluate(voltages, mat, rhs);

    EXPECT_DOUBLE_EQ(mat.value(pattern.offset(0, 1)),  1.0);
    EXPECT_DOUBLE_EQ(mat.value(pattern.offset(1, 0)),  1.0);
    EXPECT_DOUBLE_EQ(rhs[1], 5.0);
}

// ---------------------------------------------------------------------------
// ngspice VSRCdcGiven: in the DC operating point (MODEDCOP) and DC-transfer-
// curve (MODEDCTRANCURVE) solves, a source with an explicit DC value and a
// transient waveform must stamp the DC value, NOT the waveform's time=0 value.
// Repro: "V1 n1 0 DC 0 PULSE 0.68 ..." — OP must use 0, not 0.68.
// ---------------------------------------------------------------------------
namespace {
double op_rhs_for(VSource& vs, int mode) {
    SparsityBuilder builder(2);
    vs.stamp_pattern(builder);
    builder.add(0, 0);
    auto pattern = builder.build();
    NumericMatrix mat(pattern);
    vs.assign_offsets(pattern);
    std::vector<double> voltages(2, 0.0);
    std::vector<double> rhs(2, 0.0);
    VSrcIntegratorGuard guard(mode);
    vs.evaluate(voltages, mat, rhs);
    return rhs[1];
}
} // namespace

TEST(VSource, DcGivenUsesDcValueAtOperatingPoint) {
    constexpr int MODEDCOP = 0x10;
    constexpr int MODETRANOP = 0x20;
    constexpr int MODEDCTRANCURVE = 0x40;

    PulseParams p;
    p.v1 = 0.68; p.v2 = -0.02; p.td = 0.0;
    p.tr = 0.29e-6; p.tf = 0.29e-6; p.pw = 4.5e-6; p.per = 5.08e-6;

    // dc_given = true (explicit "DC 0"): OP/DCTRANCURVE use the DC value (0).
    {
        VSource vs("V1", 0, GROUND_INTERNAL, 0.0);
        vs.set_branch_index(1);
        vs.set_pulse(p);
        vs.set_dc_given(true);
        EXPECT_DOUBLE_EQ(op_rhs_for(vs, MODEDCOP), 0.0);
        EXPECT_DOUBLE_EQ(op_rhs_for(vs, MODEDCTRANCURVE), 0.0);
        // MODETRANOP must still evaluate the waveform at t=0 -> v1 (0.68).
        EXPECT_DOUBLE_EQ(op_rhs_for(vs, MODETRANOP), 0.68);
    }

    // dc_given = false (no DC value): OP uses the waveform t=0 value (0.68).
    {
        VSource vs("V1", 0, GROUND_INTERNAL, 0.0);
        vs.set_branch_index(1);
        vs.set_pulse(p);
        vs.set_dc_given(false);
        EXPECT_DOUBLE_EQ(op_rhs_for(vs, MODEDCOP), 0.68);
    }
}

// ---------------------------------------------------------------------------
// DC value_at
// ---------------------------------------------------------------------------
TEST(VSource, ValueAtDC) {
    VSource vs("V1", 0, GROUND_INTERNAL, 3.3);
    vs.set_branch_index(1);
    EXPECT_DOUBLE_EQ(vs.value_at(0.0), 3.3);
    EXPECT_DOUBLE_EQ(vs.value_at(1e-3), 3.3);
}

// ---------------------------------------------------------------------------
// PULSE waveform
// ---------------------------------------------------------------------------
TEST(VSource, ValueAtPulse) {
    VSource vs("V1", 0, GROUND_INTERNAL, 0.0);
    vs.set_branch_index(1);
    PulseParams p;
    p.v1  = 0.0;
    p.v2  = 5.0;
    p.td  = 1e-9;   // 1 ns delay
    p.tr  = 2e-9;   // 2 ns rise
    p.tf  = 2e-9;   // 2 ns fall
    p.pw  = 5e-9;   // 5 ns pulse width
    p.per = 20e-9;  // 20 ns period
    vs.set_pulse(p);

    // Before delay: v1
    EXPECT_DOUBLE_EQ(vs.value_at(0.0), 0.0);
    // At midpoint of rise: v1 + (v2-v1)*0.5
    EXPECT_DOUBLE_EQ(vs.value_at(p.td + p.tr * 0.5), 2.5);
    // During high plateau
    EXPECT_DOUBLE_EQ(vs.value_at(p.td + p.tr + p.pw * 0.5), 5.0);
    // After fall: back to v1
    EXPECT_DOUBLE_EQ(vs.value_at(p.td + p.tr + p.pw + p.tf + 1e-9), 0.0);
}

// ---------------------------------------------------------------------------
// SIN waveform
// ---------------------------------------------------------------------------
TEST(VSource, ValueAtSin) {
    VSource vs("V1", 0, GROUND_INTERNAL, 0.0);
    vs.set_branch_index(1);
    SinParams s;
    s.v0    = 1.0;
    s.va    = 2.0;
    s.freq  = 1.0;  // 1 Hz
    s.td    = 0.0;
    s.theta = 0.0;
    s.phase = 0.0;
    vs.set_sin(s);

    // At t=0: v0 + va*sin(0) = 1
    EXPECT_NEAR(vs.value_at(0.0), 1.0, 1e-12);
    // At t=0.25: v0 + va*sin(pi/2) = 1+2=3
    EXPECT_NEAR(vs.value_at(0.25), 3.0, 1e-12);
}

// ---------------------------------------------------------------------------
// extra_vars and output_currents
// ---------------------------------------------------------------------------
TEST(VSource, ExtraVars) {
    VSource vs("V1", 0, GROUND_INTERNAL, 1.0);
    vs.set_branch_index(1);
    EXPECT_EQ(vs.extra_vars(), 1);
    auto oc = vs.output_currents();
    ASSERT_EQ(oc.size(), 1u);
    EXPECT_EQ(oc[0], "I(V1)");
}

TEST(PulseSource, PeriodBoundaryReturnsReferenceBaseline) {
    NetlistParser parser;
    auto circuit = parser.parse("Pulse roundoff\n"
        "V1 v 0 PULSE(0 5 0 1n 1n 10u 20u)\n"
        "I1 i 0 PULSE(0 5 0 1n 1n 10u 20u)\nR1 i 0 1\n.end\n");
    int checked = 0;
    auto check = [&](auto& source) {
        source.resolve_defaults(1e-6, 5e-4);
        // The paired ngspice RC benchmark returns exactly zero at this stop.
        // fmod leaves a small positive remainder and invents a rising value.
        EXPECT_DOUBLE_EQ(source.value_at(5e-4), 0.0);
        ++checked;
    };
    for (auto& device : circuit.devices()) {
        if (auto* v = dynamic_cast<VSource*>(device.get())) check(*v);
        if (auto* i = dynamic_cast<ISource*>(device.get())) check(*i);
    }
    EXPECT_EQ(checked, 2);
}

TEST(PulseSource, FirstPeriodEndpointDoesNotWrapAnOverlappingPulse) {
    // ngspice wraps only after PER. At exactly PER the first pulse is still
    // high when its width exceeds the period; the next representable time
    // starts the next rise. This distinction is not a tolerance adjustment.
    PulseParams p{2, 7, 0, 1e-9, 1e-9, 20e-9, 10e-9};
    VSource voltage("V1", 0, GROUND_INTERNAL, 0);
    ISource current("I1", 0, GROUND_INTERNAL, 0);
    auto check = [&](auto& source) {
        source.set_pulse(p);
        source.resolve_defaults(1e-10, 1e-6);
        EXPECT_DOUBLE_EQ(source.value_at(p.per), 7.0);
        EXPECT_NEAR(source.value_at(std::nextafter(p.per, 1.0)), 2.0, 1e-12);
    };
    check(voltage);
    check(current);
}

TEST(PulseSource, AcceptedCornerScheduleRespectsSpacingAndResetsForReuse) {
    PulseParams p{0, 1, 2, 1, 1, 3, 10};
    VSource voltage("V1", 0, GROUND_INTERNAL, 0);
    ISource current("I1", 0, GROUND_INTERNAL, 0);
    auto check = [&](auto& source) {
        source.set_pulse(p);
        source.resolve_defaults(0.1, 30);
        EXPECT_EQ(source.accept_pulse_breakpoint(0, 0.01), 2);
        EXPECT_FALSE(source.accept_pulse_breakpoint(1, 0.01));
        EXPECT_EQ(source.accept_pulse_breakpoint(2, 0.01), 3);
        // An accepted point just before the old corner requests the next
        // corner, rather than getting stuck on an ignored nearby request.
        EXPECT_EQ(source.accept_pulse_breakpoint(2.995, 0.01), 6);
        EXPECT_EQ(source.accept_pulse_breakpoint(6, 0.01), 7);
        EXPECT_EQ(source.accept_pulse_breakpoint(7, 0.01), 12);
        EXPECT_EQ(source.accept_pulse_breakpoint(12, 0.01), 13);
        source.resolve_defaults(0.1, 30);
        EXPECT_EQ(source.accept_pulse_breakpoint(0, 0.01), 2);
    };
    check(voltage);
    check(current);
}
