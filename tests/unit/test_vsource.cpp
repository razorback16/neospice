#include <gtest/gtest.h>
#include "devices/vsource.hpp"
#include "core/neo_solver.hpp"
#include "core/circuit.hpp"   // tls_integrator_ctx, IntegratorCtx
#include "core/types.hpp"

using namespace neospice;

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
