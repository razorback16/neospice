#include <gtest/gtest.h>
#include "devices/tline.hpp"
#include "core/matrix.hpp"
#include "core/circuit.hpp"
#include "parser/netlist_parser.hpp"
#include "core/dc.hpp"
#include "core/transient.hpp"
#include "core/types.hpp"
#include <cmath>
#include <vector>

using namespace neospice;

// ---------------------------------------------------------------------------
// Unit tests: TransmissionLine device construction and basic properties
// ---------------------------------------------------------------------------

TEST(TLine, ConstructionBasic) {
    TransmissionLine tl("T1", 1, 0, 2, 0, 50.0, 1e-9);
    EXPECT_DOUBLE_EQ(tl.z0(), 50.0);
    EXPECT_DOUBLE_EQ(tl.td(), 1e-9);
}

TEST(TLine, StampPattern) {
    // Port1: nodes 1 (pos) and 0 (neg=ground), Port2: nodes 2 (pos) and 0 (neg=ground)
    // System size 3 (0,1,2).  Ground is not stamped.
    // Port1 self: (1,1) only (since node 0 is ground)
    // Port2 self: (2,2) only
    // Cross-port: (1,2) and (2,1) for DC short-circuit coupling
    TransmissionLine tl("T1", 1, GROUND_INTERNAL, 2, GROUND_INTERNAL, 50.0, 1e-9);
    SparsityBuilder builder(3);
    tl.stamp_pattern(builder);
    auto pattern = builder.build();
    // 2 self-port diagonals + 2 cross-port entries = 4
    EXPECT_EQ(pattern.nnz(), 4);
}

TEST(TLine, StampPatternBothPortsFloating) {
    // Port1: nodes 1,2; Port2: nodes 3,4
    // Self-port: 4 entries per port = 8
    // Cross-port: 8 entries (p1p↔p2p, p1p↔p2n, p1n↔p2p, p1n↔p2n, symmetric) = 8
    // Total = 16
    TransmissionLine tl("T1", 1, 2, 3, 4, 50.0, 1e-9);
    SparsityBuilder builder(5);
    tl.stamp_pattern(builder);
    auto pattern = builder.build();
    EXPECT_EQ(pattern.nnz(), 16);
}

TEST(TLine, DCStampShortCircuit) {
    // At DC, the TL is a short circuit. Large conductance ties p1+↔p2+ and p1-↔p2-.
    // With both neg nodes grounded: stamps g_dc on (0,0) and (1,1), -g_dc on (0,1) and (1,0).
    TransmissionLine tl2("T1", 0, GROUND_INTERNAL, 1, GROUND_INTERNAL, 50.0, 1e-9);

    SparsityBuilder builder(2);
    tl2.stamp_pattern(builder);
    auto pattern = builder.build();
    tl2.assign_offsets(pattern);

    NumericMatrix mat(pattern);
    std::vector<double> rhs(2, 0.0);
    std::vector<double> voltages(2, 0.0);
    tl2.evaluate(voltages, mat, rhs);

    double g_dc = 1e9;
    EXPECT_DOUBLE_EQ(mat.value(pattern.offset(0, 0)), g_dc);
    EXPECT_DOUBLE_EQ(mat.value(pattern.offset(1, 1)), g_dc);
    EXPECT_DOUBLE_EQ(mat.value(pattern.offset(0, 1)), -g_dc);
    EXPECT_DOUBLE_EQ(mat.value(pattern.offset(1, 0)), -g_dc);
    // RHS should be zero at DC (no history)
    EXPECT_DOUBLE_EQ(rhs[0], 0.0);
    EXPECT_DOUBLE_EQ(rhs[1], 0.0);
}

// ---------------------------------------------------------------------------
// Parser tests
// ---------------------------------------------------------------------------

TEST(TLineParser, BasicTDParam) {
    const char* netlist = R"(
T element test
V1 in 0 1
T1 in 0 out 0 Z0=50 TD=1n
R1 out 0 50
.op
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);

    TransmissionLine* tl = nullptr;
    for (auto& dev : ckt.devices()) {
        if (auto* t = dynamic_cast<TransmissionLine*>(dev.get())) {
            tl = t;
            break;
        }
    }
    ASSERT_NE(tl, nullptr);
    EXPECT_DOUBLE_EQ(tl->z0(), 50.0);
    EXPECT_DOUBLE_EQ(tl->td(), 1e-9);
}

TEST(TLineParser, FAndNLParam) {
    // TD = NL / F = 0.25 / 1e9 = 250ps
    const char* netlist = R"(
T element with F and NL
V1 in 0 1
T1 in 0 out 0 Z0=75 F=1e9 NL=0.25
R1 out 0 75
.op
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);

    TransmissionLine* tl = nullptr;
    for (auto& dev : ckt.devices()) {
        if (auto* t = dynamic_cast<TransmissionLine*>(dev.get())) {
            tl = t;
            break;
        }
    }
    ASSERT_NE(tl, nullptr);
    EXPECT_DOUBLE_EQ(tl->z0(), 75.0);
    EXPECT_NEAR(tl->td(), 0.25e-9, 1e-20);
}

TEST(TLineParser, FOnlyParam) {
    // F only → NL defaults to 0.25 → TD = 0.25/F
    const char* netlist = R"(
T element with F only
V1 in 0 1
T1 in 0 out 0 Z0=50 F=2e9
R1 out 0 50
.op
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);

    TransmissionLine* tl = nullptr;
    for (auto& dev : ckt.devices()) {
        if (auto* t = dynamic_cast<TransmissionLine*>(dev.get())) {
            tl = t;
            break;
        }
    }
    ASSERT_NE(tl, nullptr);
    EXPECT_DOUBLE_EQ(tl->z0(), 50.0);
    EXPECT_NEAR(tl->td(), 0.25 / 2e9, 1e-20);
}

TEST(TLineParser, MissingZ0Throws) {
    const char* netlist = R"(
Missing Z0
V1 in 0 1
T1 in 0 out 0 TD=1n
R1 out 0 50
.op
.end
)";
    NetlistParser parser;
    EXPECT_THROW(parser.parse(netlist), ParseError);
}

TEST(TLineParser, MissingTDAndFThrows) {
    const char* netlist = R"(
Missing TD and F
V1 in 0 1
T1 in 0 out 0 Z0=50
R1 out 0 50
.op
.end
)";
    NetlistParser parser;
    EXPECT_THROW(parser.parse(netlist), ParseError);
}

TEST(TLineParser, CaseInsensitive) {
    const char* netlist = R"(
case insensitive T element
v1 in 0 1
t1 in 0 out 0 z0=50 td=2n
r1 out 0 50
.op
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);

    TransmissionLine* tl = nullptr;
    for (auto& dev : ckt.devices()) {
        if (auto* t = dynamic_cast<TransmissionLine*>(dev.get())) {
            tl = t;
            break;
        }
    }
    ASSERT_NE(tl, nullptr);
    EXPECT_DOUBLE_EQ(tl->z0(), 50.0);
    EXPECT_DOUBLE_EQ(tl->td(), 2e-9);
}

// ---------------------------------------------------------------------------
// DC analysis: TL is a short circuit at DC (passes DC between ports)
// ---------------------------------------------------------------------------

TEST(TLineDC, MatchedLoad) {
    // V1 = 1V, TL with Z0=50, port2 loaded with R=50
    // DC: TL is a short circuit, so v(in) = v(out) = 1V
    // v(out) = 1V because the TL directly connects port1+ to port2+.
    // The load R1=50 to ground draws 20mA from V1, but voltage is fixed.
    const char* netlist = R"(
TLine DC test
V1 in 0 DC 1
T1 in 0 out 0 Z0=50 TD=1n
R1 out 0 50
.op
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    auto result = solve_dc(ckt);
    // v(in) is driven by V1 to 1V
    EXPECT_NEAR(result.node_voltages.at("v(in)"), 1.0, 1e-6);
    // v(out) = 1V — TL passes DC (short circuit at DC)
    EXPECT_NEAR(result.node_voltages.at("v(out)"), 1.0, 1e-6);
}

TEST(TLineDC, VoltageDivider) {
    // V1=10V through R1=50 into TL, TL output loaded with R2=100 to ground.
    // DC: TL is a short circuit, so the circuit is a simple voltage divider:
    // V(b) = V1 * R2/(R1+R2) = 10 * 100/150 = 6.667V
    const char* netlist = R"(
TLine DC voltage divider
V1 in 0 DC 10
R1 in a 50
T1 a 0 b 0 Z0=50 TD=10n
R2 b 0 100
.op
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    auto result = solve_dc(ckt);
    EXPECT_NEAR(result.node_voltages.at("v(a)"), 10.0 * 100.0 / 150.0, 1e-3);
    EXPECT_NEAR(result.node_voltages.at("v(b)"), 10.0 * 100.0 / 150.0, 1e-3);
}

// ---------------------------------------------------------------------------
// Transient analysis: pulse through matched transmission line
// ---------------------------------------------------------------------------

TEST(TLineTransient, MatchedLinePulseDelay) {
    // Circuit: V1 PULSE(0 1 0 0 0 50ns 1us) through Rs=50 into TL Z0=50 TD=10ns,
    // loaded with R_load=50 (matched).
    //
    //   V1 -> Rs=50 -> port1+ (node "in")
    //   port1- = gnd
    //   port2+ = "out"
    //   port2- = gnd
    //   Rload=50 from "out" to gnd
    //
    // With matched load and matched source, the signal at "out" should be a
    // delayed version of the input with amplitude V1/2 = 0.5V (50-Ω voltage
    // divider at port1 between Rs and Z0), appearing TD=10ns after the source
    // transitions.
    //
    // We run for 60ns and check that v(out) ≈ 0 before 10ns and ≈ 0.5V after 20ns.

    const char* netlist = R"(
TLine matched transient
V1 src 0 PULSE(0 1 0 1p 1p 50n 200n)
Rs src in 50
T1 in 0 out 0 Z0=50 TD=10n
Rload out 0 50
.tran 1n 60n
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    auto result = solve_transient(ckt, 1e-9, 60e-9);

    ASSERT_FALSE(result.time.empty());

    // Find v(out) at t ≈ 5ns (before delay) — should be near 0
    double v_out_early = 0.0;
    double v_out_late  = 0.0;
    for (size_t i = 0; i < result.time.size(); ++i) {
        double t = result.time[i];
        double v = result.voltages.at("v(out)")[i];
        if (std::abs(t - 5e-9) < 2e-9) {
            v_out_early = v;
        }
        if (std::abs(t - 25e-9) < 2e-9) {
            v_out_late = v;
        }
    }
    // Before the delay: output should be close to 0
    EXPECT_NEAR(v_out_early, 0.0, 0.1);
    // After the delay + rise: output should be close to 0.5V
    EXPECT_NEAR(v_out_late, 0.5, 0.15);
}

TEST(TLineTransient, OpenTerminatedReflection) {
    // Mismatched load: Z_load → ∞ (open circuit).  The reflected wave has the
    // same sign as the incident wave → full positive reflection.
    //
    // Circuit: V1=1V DC, Rs=50, TL Z0=50 TD=10ns, open output (no load).
    // At t=0+ the step is applied.  After TD, the wave arrives at the open end
    // and reflects fully.  After 2*TD the reflection arrives back at port1.
    //
    //   V(out) at time > TD  should be ≈ 1V (full reflection at open)
    //
    // Actually for this simple analysis:
    //   Incident voltage at port1 = V_s * Z0/(Rs+Z0) = 1 * 50/100 = 0.5V
    //   At open end: V = 2 * V_incident = 1.0V
    //
    // We use a PULSE source so we can observe before and after.
    const char* netlist = R"(
TLine open terminated
V1 src 0 PULSE(0 1 0 1p 1p 200n 400n)
Rs src in 50
T1 in 0 out 0 Z0=50 TD=10n
.tran 1n 50n
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    auto result = solve_transient(ckt, 1e-9, 50e-9);

    ASSERT_FALSE(result.time.empty());

    // Before TD: v(out) ≈ 0
    // After TD: v(out) ≈ 1.0 (full reflection)
    double v_out_early = 0.0;
    double v_out_late  = 0.0;
    for (size_t i = 0; i < result.time.size(); ++i) {
        double t = result.time[i];
        auto it = result.voltages.find("v(out)");
        if (it == result.voltages.end()) continue;
        double v = it->second[i];
        if (std::abs(t - 5e-9) < 2e-9)  v_out_early = v;
        if (std::abs(t - 30e-9) < 2e-9) v_out_late  = v;
    }
    // Before delay: output should be 0
    EXPECT_NEAR(v_out_early, 0.0, 0.1);
    // After delay: output should be close to 1V (open end full reflection)
    EXPECT_NEAR(v_out_late, 1.0, 0.2);
}

TEST(TLineTransient, ShortTDMatchedLineSettles) {
    // With a very short TD relative to tstep, the TL should behave like two
    // resistors in parallel (both ports at the same effective potential after
    // a few reflections settle).  Check that the circuit reaches steady state.
    const char* netlist = R"(
Short TD TLine
V1 in 0 DC 1 PULSE(0 1 0 1p 1p 100n 200n)
Rs in p1 50
T1 p1 0 p2 0 Z0=50 TD=100p
Rload p2 0 50
.tran 1n 20n
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    auto result = solve_transient(ckt, 1e-9, 20e-9);
    ASSERT_FALSE(result.time.empty());

    // After settling (t > 5ns), v(p2) should be approximately 0.5V
    // (voltage divider: Rs=50 driving Z0=50 matched load through the TL)
    double v_late = 0.0;
    for (size_t i = 0; i < result.time.size(); ++i) {
        if (std::abs(result.time[i] - 15e-9) < 2e-9) {
            v_late = result.voltages.at("v(p2)")[i];
        }
    }
    EXPECT_NEAR(v_late, 0.5, 0.1);
}
