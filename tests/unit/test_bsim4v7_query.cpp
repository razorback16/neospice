// Tests for Task 5.4: BSIM4v7 parameter query (ask/mask).
//
// 1. query_param() on base Device returns nullopt.
// 2. After DC solve, gm/gds/von are non-zero and reasonable.
// 3. Unknown parameter returns nullopt.
// 4. Geometry parameters W/L return netlist values.
// 5. Case-insensitive: "GM", "gm", "Gm" all return the same value.
// 6. Capacitance and charge queries return values.
// 7. Multiple devices can be queried independently.

#include <gtest/gtest.h>
#include "core/dc.hpp"
#include "core/types.hpp"
#include "devices/device.hpp"
#include "devices/bsim4v7/bsim4v7_device.hpp"
#include "parser/netlist_parser.hpp"
#include <cmath>
#include <optional>

using namespace neospice;

// ---------------------------------------------------------------------------
// Helper: find the first BSIM4v7Device in a circuit
// ---------------------------------------------------------------------------
static BSIM4v7Device* find_bsim4(Circuit& ckt) {
    for (auto& d : ckt.devices()) {
        auto* p = dynamic_cast<BSIM4v7Device*>(d.get());
        if (p) return p;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// 1. Base Device query_param returns nullopt
// ---------------------------------------------------------------------------
TEST(BSIM4v7Query, BaseDeviceReturnsNullopt) {
    struct DummyDevice : public Device {
        DummyDevice() : Device("dummy") {}
        void stamp_pattern(SparsityBuilder&) const override {}
        void assign_offsets(const SparsityPattern&) override {}
        void evaluate(const std::vector<double>&,
                      NumericMatrix&, std::vector<double>&) override {}
    };
    DummyDevice d;
    EXPECT_FALSE(d.query_param("gm").has_value());
    EXPECT_FALSE(d.query_param("vth").has_value());
}

// ---------------------------------------------------------------------------
// 2. After DC solve, gm/gds/von are reasonable
// ---------------------------------------------------------------------------
TEST(BSIM4v7Query, DCQueryGmGdsVon) {
    std::string netlist = R"(
NMOS DC bias for query
VDD d 0 1.0
VGS g 0 0.8
M1 d g 0 0 NMOD W=1u L=100n
.model NMOD NMOS LEVEL=14 VTH0=0.4 U0=0.04 TOXE=2e-9
.op
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    auto result = solve_dc(ckt);

    auto* m1 = find_bsim4(ckt);
    ASSERT_NE(m1, nullptr);

    // gm should be positive for an NMOS in saturation
    auto gm = m1->query_param("gm");
    ASSERT_TRUE(gm.has_value());
    EXPECT_GT(*gm, 0.0);

    // gds should be positive (finite output conductance)
    auto gds = m1->query_param("gds");
    ASSERT_TRUE(gds.has_value());
    EXPECT_GT(*gds, 0.0);

    // von/vth should be close to the specified VTH0=0.4
    auto von = m1->query_param("von");
    ASSERT_TRUE(von.has_value());
    EXPECT_GT(*von, 0.1);
    EXPECT_LT(*von, 1.0);

    // vdsat should be positive
    auto vdsat = m1->query_param("vdsat");
    ASSERT_TRUE(vdsat.has_value());
    EXPECT_GT(*vdsat, 0.0);

    // id should be positive (current flowing D->S in NMOS)
    auto id = m1->query_param("id");
    ASSERT_TRUE(id.has_value());
    EXPECT_GT(*id, 0.0);
}

// ---------------------------------------------------------------------------
// 3. Unknown parameter returns nullopt
// ---------------------------------------------------------------------------
TEST(BSIM4v7Query, UnknownParamReturnsNullopt) {
    std::string netlist = R"(
NMOS for nullopt test
VDD d 0 1.0
VGS g 0 0.8
M1 d g 0 0 NMOD W=1u L=100n
.model NMOD NMOS LEVEL=14 VTH0=0.4 U0=0.04 TOXE=2e-9
.op
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    solve_dc(ckt);

    auto* m1 = find_bsim4(ckt);
    ASSERT_NE(m1, nullptr);

    EXPECT_FALSE(m1->query_param("nonexistent_param").has_value());
    EXPECT_FALSE(m1->query_param("foo").has_value());
    EXPECT_FALSE(m1->query_param("").has_value());
}

// ---------------------------------------------------------------------------
// 4. Geometry parameters W, L return netlist values
// ---------------------------------------------------------------------------
TEST(BSIM4v7Query, GeometryParams) {
    std::string netlist = R"(
NMOS geometry query
VDD d 0 1.0
VGS g 0 0.8
M1 d g 0 0 NMOD W=2u L=200n NF=2
.model NMOD NMOS LEVEL=14 VTH0=0.4 U0=0.04 TOXE=2e-9
.op
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    solve_dc(ckt);

    auto* m1 = find_bsim4(ckt);
    ASSERT_NE(m1, nullptr);

    auto w = m1->query_param("w");
    ASSERT_TRUE(w.has_value());
    EXPECT_NEAR(*w, 2e-6, 1e-9);

    auto l = m1->query_param("l");
    ASSERT_TRUE(l.has_value());
    EXPECT_NEAR(*l, 200e-9, 1e-12);

    auto nf = m1->query_param("nf");
    ASSERT_TRUE(nf.has_value());
    EXPECT_NEAR(*nf, 2.0, 1e-12);

    // Default multiplier should be 1.0
    auto m_val = m1->query_param("m");
    ASSERT_TRUE(m_val.has_value());
    EXPECT_NEAR(*m_val, 1.0, 1e-12);
}

// ---------------------------------------------------------------------------
// 5. Case-insensitive query
// ---------------------------------------------------------------------------
TEST(BSIM4v7Query, CaseInsensitive) {
    std::string netlist = R"(
NMOS case test
VDD d 0 1.0
VGS g 0 0.8
M1 d g 0 0 NMOD W=1u L=100n
.model NMOD NMOS LEVEL=14 VTH0=0.4 U0=0.04 TOXE=2e-9
.op
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    solve_dc(ckt);

    auto* m1 = find_bsim4(ckt);
    ASSERT_NE(m1, nullptr);

    auto gm_lower = m1->query_param("gm");
    auto gm_upper = m1->query_param("GM");
    auto gm_mixed = m1->query_param("Gm");

    ASSERT_TRUE(gm_lower.has_value());
    ASSERT_TRUE(gm_upper.has_value());
    ASSERT_TRUE(gm_mixed.has_value());

    EXPECT_EQ(*gm_lower, *gm_upper);
    EXPECT_EQ(*gm_lower, *gm_mixed);

    // Also test VTH / vth / Vth
    auto vth_lower = m1->query_param("vth");
    auto vth_upper = m1->query_param("VTH");
    auto vth_mixed = m1->query_param("Vth");

    ASSERT_TRUE(vth_lower.has_value());
    ASSERT_TRUE(vth_upper.has_value());
    ASSERT_TRUE(vth_mixed.has_value());

    EXPECT_EQ(*vth_lower, *vth_upper);
    EXPECT_EQ(*vth_lower, *vth_mixed);
}

// ---------------------------------------------------------------------------
// 6. Capacitance and charge queries return values after DC solve
// ---------------------------------------------------------------------------
TEST(BSIM4v7Query, CapacitanceAndChargeParams) {
    std::string netlist = R"(
NMOS cap/charge query
VDD d 0 1.0
VGS g 0 0.8
M1 d g 0 0 NMOD W=1u L=100n
.model NMOD NMOS LEVEL=14 VTH0=0.4 U0=0.04 TOXE=2e-9
.op
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    solve_dc(ckt);

    auto* m1 = find_bsim4(ckt);
    ASSERT_NE(m1, nullptr);

    // Gate-gate capacitance should exist (may be positive or negative)
    auto cgg = m1->query_param("cgg");
    ASSERT_TRUE(cgg.has_value());

    // Gate-drain and gate-source caps
    auto cgd = m1->query_param("cgd");
    auto cgs = m1->query_param("cgs");
    ASSERT_TRUE(cgd.has_value());
    ASSERT_TRUE(cgs.has_value());

    // Junction capacitances
    auto capbd = m1->query_param("capbd");
    auto capbs = m1->query_param("capbs");
    ASSERT_TRUE(capbd.has_value());
    ASSERT_TRUE(capbs.has_value());
    EXPECT_GE(*capbd, 0.0);
    EXPECT_GE(*capbs, 0.0);

    // Charges
    auto qg = m1->query_param("qg");
    auto qd = m1->query_param("qd");
    auto qb = m1->query_param("qb");
    auto qs = m1->query_param("qs");
    ASSERT_TRUE(qg.has_value());
    ASSERT_TRUE(qd.has_value());
    ASSERT_TRUE(qb.has_value());
    ASSERT_TRUE(qs.has_value());
}

// ---------------------------------------------------------------------------
// 7. Multiple devices can be queried independently
// ---------------------------------------------------------------------------
TEST(BSIM4v7Query, MultipleDevices) {
    std::string netlist = R"(
CMOS inverter query
VDD vdd 0 1.8
VIN in 0 0.9
M1p out in vdd vdd PMOD W=2u L=100n
M1n out in 0 0 NMOD W=1u L=100n
CL out 0 10f
.model NMOD NMOS LEVEL=14 VTH0=0.4 U0=0.04 TOXE=2e-9
.model PMOD PMOS LEVEL=14 VTH0=-0.4 U0=0.02 TOXE=2e-9
.op
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    solve_dc(ckt);

    // Collect both BSIM4v7 devices
    std::vector<BSIM4v7Device*> mosfets;
    for (auto& d : ckt.devices()) {
        auto* p = dynamic_cast<BSIM4v7Device*>(d.get());
        if (p) mosfets.push_back(p);
    }
    ASSERT_EQ(mosfets.size(), 2u);

    // Both should have valid gm values
    auto gm0 = mosfets[0]->query_param("gm");
    auto gm1 = mosfets[1]->query_param("gm");
    ASSERT_TRUE(gm0.has_value());
    ASSERT_TRUE(gm1.has_value());
    EXPECT_GT(*gm0, 0.0);
    EXPECT_GT(*gm1, 0.0);

    // They should have different widths (2u vs 1u)
    auto w0 = mosfets[0]->query_param("w");
    auto w1 = mosfets[1]->query_param("w");
    ASSERT_TRUE(w0.has_value());
    ASSERT_TRUE(w1.has_value());
    EXPECT_NE(*w0, *w1);

    // Von should differ between NMOS and PMOS
    auto von0 = mosfets[0]->query_param("von");
    auto von1 = mosfets[1]->query_param("von");
    ASSERT_TRUE(von0.has_value());
    ASSERT_TRUE(von1.has_value());
    EXPECT_NE(*von0, *von1);
}
