#include <gtest/gtest.h>
#include "devices/diode.hpp"
#include "core/matrix.hpp"

using namespace cudaspice;

// Default model helper
static DiodeModel default_model() {
    return DiodeModel{};
}

// ---------------------------------------------------------------------------
// StampPattern — 2 non-ground nodes → nnz = 4
// ---------------------------------------------------------------------------
TEST(Diode, StampPattern) {
    Diode d("D1", 0, 1, default_model());
    SparsityBuilder builder(2);
    d.stamp_pattern(builder);
    auto pattern = builder.build();
    EXPECT_EQ(pattern.nnz(), 4);
}

// ---------------------------------------------------------------------------
// ForwardBias — vd = 0.7 V → positive conductance in matrix, nonzero RHS
// ---------------------------------------------------------------------------
TEST(Diode, ForwardBias) {
    // Anode at node 0, cathode at ground (GROUND_INTERNAL)
    Diode d("D1", 0, GROUND_INTERNAL, default_model());

    SparsityBuilder builder(1);
    d.stamp_pattern(builder);
    auto pattern = builder.build();
    NumericMatrix mat(pattern);
    d.assign_offsets(pattern);

    // Set anode voltage to 0.7 V (cathode is ground = 0)
    std::vector<double> voltages = {0.7};
    std::vector<double> rhs(1, 0.0);
    d.evaluate(voltages, mat, rhs);

    // Conductance should be positive
    EXPECT_GT(mat.value(pattern.offset(0, 0)), 0.0);

    // RHS should be nonzero (Norton current source)
    EXPECT_NE(rhs[0], 0.0);
}

// ---------------------------------------------------------------------------
// ReverseBias — vd = -1 V → very small conductance (near Is/nvt)
// ---------------------------------------------------------------------------
TEST(Diode, ReverseBias) {
    DiodeModel model;
    model.Is = 1e-14;
    model.N  = 1.0;

    Diode d("D1", 0, GROUND_INTERNAL, model);
    SparsityBuilder builder(1);
    d.stamp_pattern(builder);
    auto pattern = builder.build();
    NumericMatrix mat(pattern);
    d.assign_offsets(pattern);

    std::vector<double> voltages = {-1.0};
    std::vector<double> rhs(1, 0.0);
    d.evaluate(voltages, mat, rhs);

    // Conductance should be very small (diode is reverse biased)
    const double gd = mat.value(pattern.offset(0, 0));
    EXPECT_GT(gd, 0.0);       // still positive
    EXPECT_LT(gd, 1e-9);      // but very small (essentially Is/nvt * exp(-1/vt))
}

// ---------------------------------------------------------------------------
// VoltageLimiting — very large new voltage should be clamped
// ---------------------------------------------------------------------------
TEST(Diode, VoltageLimiting) {
    DiodeModel model;
    model.Is = 1e-14;
    model.N  = 1.0;

    Diode d("D1", 0, GROUND_INTERNAL, model);

    // old_v: anode = 0.6 V
    std::vector<double> old_v  = {0.6};
    // new_v: anode = 100 V (Newton step gone wild)
    std::vector<double> new_v  = {100.0};

    d.limit_voltages(old_v, new_v);

    // Voltage should be limited to a reasonable value (well below 100 V)
    EXPECT_LT(new_v[0], 2.0);
    // And should still be forward-biased (positive)
    EXPECT_GT(new_v[0], 0.0);
}

// ---------------------------------------------------------------------------
// OutputCurrents — should return {"i(D1)"}
// ---------------------------------------------------------------------------
TEST(Diode, OutputCurrents) {
    Diode d("D1", 0, 1, default_model());
    auto names = d.output_currents();
    ASSERT_EQ(names.size(), 1u);
    EXPECT_EQ(names[0], "i(D1)");
}

// ---------------------------------------------------------------------------
// ACStamp — check that conductance and capacitance are stamped into G and C
// ---------------------------------------------------------------------------
TEST(Diode, AcStamp) {
    DiodeModel model;
    model.Is  = 1e-14;
    model.N   = 1.0;
    model.Cj0 = 1e-12;   // 1 pF junction cap
    model.Vj  = 0.7;
    model.M   = 0.5;
    model.Tt  = 1e-9;    // 1 ns transit time

    // Anode node 0, cathode node 1
    Diode d("D1", 0, 1, model);
    SparsityBuilder builder(2);
    d.stamp_pattern(builder);
    auto pattern = builder.build();
    d.assign_offsets(pattern);

    // Run evaluate first to populate last_gd_
    std::vector<double> voltages = {0.7, 0.0};
    NumericMatrix mat(pattern);
    std::vector<double> rhs(2, 0.0);
    d.evaluate(voltages, mat, rhs);

    // Now ac_stamp
    NumericMatrix G(pattern);
    NumericMatrix C(pattern);
    d.ac_stamp(voltages, G, C);

    // G(0,0) should be positive (forward bias conductance)
    EXPECT_GT(G.value(pattern.offset(0, 0)), 0.0);

    // C(0,0) should be positive (capacitance)
    EXPECT_GT(C.value(pattern.offset(0, 0)), 0.0);

    // Off-diagonal entries should be negative
    EXPECT_LT(G.value(pattern.offset(0, 1)), 0.0);
    EXPECT_LT(C.value(pattern.offset(0, 1)), 0.0);
}

// ---------------------------------------------------------------------------
// GroundCathode — sanity check with cathode at GROUND_INTERNAL
// ---------------------------------------------------------------------------
TEST(Diode, GroundCathode) {
    Diode d("D1", 0, GROUND_INTERNAL, default_model());
    SparsityBuilder builder(1);
    d.stamp_pattern(builder);
    auto pattern = builder.build();
    // Only (0,0) entry since cathode is ground
    EXPECT_EQ(pattern.nnz(), 1);
}
