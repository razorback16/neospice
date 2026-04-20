#include <gtest/gtest.h>
#include "devices/ltra.hpp"
#include "core/matrix.hpp"
#include "core/circuit.hpp"
#include "parser/netlist_parser.hpp"
#include "core/dc.hpp"
#include "core/transient.hpp"
#include "core/types.hpp"
#include <cmath>
#include <memory>
#include <vector>

using namespace neospice;

// ---------------------------------------------------------------------------
// Unit tests: LTRAModel setup
// ---------------------------------------------------------------------------

TEST(LTRA, ModelSetupRLC) {
    LTRAModel model;
    model.R = 0.1;    model.R_given = true;
    model.L = 0.2e-6; model.L_given = true;
    model.C = 0.2e-12; model.C_given = true;
    model.len = 1.0;  model.len_given = true;

    EXPECT_TRUE(model.setup(1e-3, 1e-12));
    EXPECT_EQ(model.specialCase, LTRA_CASE_RLC);
    EXPECT_GT(model.Z0, 0.0);
    EXPECT_GT(model.Y0, 0.0);
    EXPECT_GT(model.td, 0.0);
    EXPECT_NEAR(model.Z0, std::sqrt(0.2e-6 / 0.2e-12), 1e-6);
}

TEST(LTRA, ModelSetupRC) {
    LTRAModel model;
    model.R = 100.0;  model.R_given = true;
    model.C = 1e-12;  model.C_given = true;
    model.len = 0.01;  model.len_given = true;

    EXPECT_TRUE(model.setup(1e-3, 1e-12));
    EXPECT_EQ(model.specialCase, LTRA_CASE_RC);
    EXPECT_GT(model.cByR, 0.0);
    EXPECT_GT(model.rclsqr, 0.0);
}

TEST(LTRA, ModelSetupLC) {
    LTRAModel model;
    model.L = 0.5e-6; model.L_given = true;
    model.C = 0.2e-12; model.C_given = true;
    model.len = 1.0;   model.len_given = true;

    EXPECT_TRUE(model.setup(1e-3, 1e-12));
    EXPECT_EQ(model.specialCase, LTRA_CASE_LC);
    EXPECT_DOUBLE_EQ(model.attenuation, 1.0);  // lossless
}

TEST(LTRA, ModelSetupRG) {
    LTRAModel model;
    model.R = 50.0; model.R_given = true;
    model.G = 0.01; model.G_given = true;
    model.len = 1.0; model.len_given = true;

    EXPECT_TRUE(model.setup(1e-3, 1e-12));
    EXPECT_EQ(model.specialCase, LTRA_CASE_RG);
}

TEST(LTRA, ModelSetupInvalidSingleParam) {
    LTRAModel model;
    model.R = 50.0; model.R_given = true;
    model.len = 1.0; model.len_given = true;
    // Only R given - need at least 2 params
    EXPECT_FALSE(model.setup(1e-3, 1e-12));
}

TEST(LTRA, ModelSetupNoLength) {
    LTRAModel model;
    model.R = 50.0; model.R_given = true;
    model.C = 1e-12; model.C_given = true;
    // No length
    EXPECT_FALSE(model.setup(1e-3, 1e-12));
}

// ---------------------------------------------------------------------------
// Unit tests: Bessel functions
// ---------------------------------------------------------------------------

TEST(LTRA, BesselI0Zero) {
    EXPECT_NEAR(ltra::bessI0(0.0), 1.0, 1e-10);
}

TEST(LTRA, BesselI1Zero) {
    EXPECT_NEAR(ltra::bessI1(0.0), 0.0, 1e-10);
}

TEST(LTRA, BesselI0Positive) {
    // I0(1) ≈ 1.2660658...
    EXPECT_NEAR(ltra::bessI0(1.0), 1.2660658, 1e-5);
}

TEST(LTRA, BesselI1Positive) {
    // I1(1) ≈ 0.5651591...
    EXPECT_NEAR(ltra::bessI1(1.0), 0.5651591, 1e-5);
}

// ---------------------------------------------------------------------------
// Unit tests: Interpolation
// ---------------------------------------------------------------------------

TEST(LTRA, LinearInterpEndpoints) {
    double c1, c2;
    ltra::linInterp(1.0, 1.0, 2.0, &c1, &c2);
    EXPECT_DOUBLE_EQ(c1, 1.0);
    EXPECT_DOUBLE_EQ(c2, 0.0);

    ltra::linInterp(2.0, 1.0, 2.0, &c1, &c2);
    EXPECT_DOUBLE_EQ(c1, 0.0);
    EXPECT_DOUBLE_EQ(c2, 1.0);
}

TEST(LTRA, LinearInterpMidpoint) {
    double c1, c2;
    ltra::linInterp(1.5, 1.0, 2.0, &c1, &c2);
    EXPECT_NEAR(c1, 0.5, 1e-15);
    EXPECT_NEAR(c2, 0.5, 1e-15);
}

// ---------------------------------------------------------------------------
// Unit tests: Device construction
// ---------------------------------------------------------------------------

TEST(LTRA, DeviceConstructionRLC) {
    auto model = std::make_shared<LTRAModel>();
    model->R = 10.0;  model->R_given = true;
    model->L = 1e-6;  model->L_given = true;
    model->C = 1e-12; model->C_given = true;
    model->len = 0.1;  model->len_given = true;
    model->setup(1e-3, 1e-12);

    LossyTransmissionLine ltra("O1", 0, 1, 2, 3, model);
    EXPECT_EQ(ltra.extra_vars(), 2);
    EXPECT_EQ(ltra.p1_pos(), 0);
    EXPECT_EQ(ltra.p1_neg(), 1);
    EXPECT_EQ(ltra.p2_pos(), 2);
    EXPECT_EQ(ltra.p2_neg(), 3);
}

// ---------------------------------------------------------------------------
// Unit tests: Query parameters
// ---------------------------------------------------------------------------

TEST(LTRA, QueryParams) {
    auto model = std::make_shared<LTRAModel>();
    model->R = 10.0;  model->R_given = true;
    model->L = 1e-6;  model->L_given = true;
    model->C = 1e-12; model->C_given = true;
    model->len = 0.1;  model->len_given = true;
    model->setup(1e-3, 1e-12);

    LossyTransmissionLine ltra("O1", 0, 1, 2, 3, model);
    EXPECT_EQ(ltra.query_param("r").value(), 10.0);
    EXPECT_EQ(ltra.query_param("R").value(), 10.0);
    EXPECT_EQ(ltra.query_param("len").value(), 0.1);
    EXPECT_GT(ltra.query_param("z0").value(), 0.0);
    EXPECT_GT(ltra.query_param("td").value(), 0.0);
    EXPECT_EQ(ltra.query_param("unknown"), std::nullopt);
}

// ---------------------------------------------------------------------------
// Parser integration tests
// ---------------------------------------------------------------------------

TEST(LTRAParser, BasicRCLine) {
    const char* netlist = R"(
LTRA RC Line Test
V1 in 0 1
R1 out 0 1k
O1 in 0 out 0 rcmod
.model rcmod ltra R=50 C=100p LEN=1
.end
)";
    NetlistParser parser;
    Circuit ckt = parser.parse(netlist);
    // Should parse successfully
    EXPECT_GE(ckt.devices().size(), 3u);  // V1, R1, O1

    // Check that an LTRA device was created
    bool found = false;
    for (const auto& dev : ckt.devices()) {
        if (auto* ltl = dynamic_cast<LossyTransmissionLine*>(dev.get())) {
            found = true;
            EXPECT_EQ(ltl->model().specialCase, LTRA_CASE_RC);
        }
    }
    EXPECT_TRUE(found);
}

TEST(LTRAParser, BasicRLCLine) {
    const char* netlist = R"(
LTRA RLC Line Test
V1 in 0 1
R1 out 0 1k
O1 in 0 out 0 rlcmod
.model rlcmod ltra R=0.1 L=0.2u C=0.2p LEN=1
.end
)";
    NetlistParser parser;
    Circuit ckt = parser.parse(netlist);
    bool found = false;
    for (const auto& dev : ckt.devices()) {
        if (auto* ltl = dynamic_cast<LossyTransmissionLine*>(dev.get())) {
            found = true;
            EXPECT_EQ(ltl->model().specialCase, LTRA_CASE_RLC);
            EXPECT_NEAR(ltl->model().Z0, std::sqrt(0.2e-6 / 0.2e-12), 1e-3);
        }
    }
    EXPECT_TRUE(found);
}

TEST(LTRAParser, LCLine) {
    const char* netlist = R"(
LTRA LC Line Test
V1 in 0 1
R1 out 0 50
O1 in 0 out 0 lcmod
.model lcmod ltra L=250n C=100p LEN=0.1
.end
)";
    NetlistParser parser;
    Circuit ckt = parser.parse(netlist);
    bool found = false;
    for (const auto& dev : ckt.devices()) {
        if (auto* ltl = dynamic_cast<LossyTransmissionLine*>(dev.get())) {
            found = true;
            EXPECT_EQ(ltl->model().specialCase, LTRA_CASE_LC);
        }
    }
    EXPECT_TRUE(found);
}

TEST(LTRAParser, RGLine) {
    const char* netlist = R"(
LTRA RG Line Test
V1 in 0 1
R1 out 0 1k
O1 in 0 out 0 rgmod
.model rgmod ltra R=100 G=0.01 LEN=0.5
.end
)";
    NetlistParser parser;
    Circuit ckt = parser.parse(netlist);
    bool found = false;
    for (const auto& dev : ckt.devices()) {
        if (auto* ltl = dynamic_cast<LossyTransmissionLine*>(dev.get())) {
            found = true;
            EXPECT_EQ(ltl->model().specialCase, LTRA_CASE_RG);
        }
    }
    EXPECT_TRUE(found);
}

// ---------------------------------------------------------------------------
// DC operating point tests
// ---------------------------------------------------------------------------

TEST(LTRA, DCOperatingPointRC) {
    // Simple RC lossy line: V1=1V driving through the line into R1=1k.
    // At DC, the RC line acts as a zero-resistance wire (R*LEN series resistance).
    const char* netlist = R"(
LTRA DC Test
V1 in 0 DC 1
R1 out 0 1k
O1 in 0 out 0 rcmod
.model rcmod ltra R=50 C=100p LEN=0.01
.end
)";
    NetlistParser parser;
    Circuit ckt = parser.parse(netlist);
    auto result = solve_dc(ckt);

    // At DC, the RC line is a simple R*L resistor.
    // V(in) = 1V, R_line = 50*0.01 = 0.5 ohm
    // V(out) = 1V * 1000/(1000+0.5) ≈ 0.9995V
    double v_out = result.voltage("out");
    EXPECT_NEAR(v_out, 1.0 * 1000.0 / (1000.0 + 0.5), 0.01);
}

TEST(LTRA, DCOperatingPointRG) {
    // RG line test
    const char* netlist = R"(
LTRA DC RG Test
V1 in 0 DC 1
R1 out 0 1k
O1 in 0 out 0 rgmod
.model rgmod ltra R=100 G=0.01 LEN=0.5
.end
)";
    NetlistParser parser;
    Circuit ckt = parser.parse(netlist);
    auto result = solve_dc(ckt);
    // Just verify convergence for RG case
    double v_out = result.voltage("out");
    EXPECT_GT(v_out, 0.0);
    EXPECT_LT(v_out, 1.0);
}
